"""Summarize a bounded Blind Rage trace without modifying it or the game."""
import argparse
import collections
import json
import math
from pathlib import Path


def acquisition_origin(record):
    """Classify the angular decision, not eligibility or hypothetical gameplay."""
    angle = record.get("angle")
    values = [record.get(key) for key in
              ("native_min", "native_max", "effective_min", "effective_max", "limit")]
    if not record.get("angle_valid") or not all(
            isinstance(v, (int, float)) and math.isfinite(v) for v in [angle] + values):
        return "unknown"
    low, high, effective_low, effective_high, limit = values
    if not 0 <= low <= high <= 180 or not 0 <= effective_low <= effective_high <= 180:
        return "unknown"
    if angle < low:
        return "native"
    if effective_high - effective_low > .000001:
        fraction = (limit - effective_low) / (effective_high - effective_low)
        if not -.00001 <= fraction <= 1.00001:
            return "unknown"
        native_limit = low + (high - low) * min(1, max(0, fraction))
    elif low == high:
        native_limit = low
    elif angle >= high:
        native_limit = high
    else:
        return "unknown"
    if angle < native_limit - .0001:
        return "native"
    if angle >= native_limit + .0001:
        return "assist_only"
    return "unknown"


def analyze(records):
    counts = collections.Counter()
    sound_requests = collections.Counter()
    level_changes = collections.Counter()
    acquisition = collections.Counter()
    grouped = collections.defaultdict(list)
    session, footer = {}, {}
    previous = 0
    for record in records:
        kind = record.get("event")
        counts[kind] += 1
        if kind == "session":
            session = record
            continue
        if kind == "end":
            footer = record
            continue
        if kind == "sound" and record.get("cue"):
            sound_requests[record["cue"]] += 1
        if kind == "level_change":
            level_changes[(record.get("level_before"), record.get("level_after"), record.get("cue", ""))] += 1
        if kind == "acquire":
            acquisition["new_locks"] += bool(record.get("accepted") and record.get("result"))
            for key in ("view_gated", "cap_applied", "position_verified"):
                acquisition[key] += bool(record.get(key))
            if any(isinstance(record.get("effective_" + suffix), (int, float)) and
                   isinstance(record.get("native_" + suffix), (int, float)) and
                   record["effective_" + suffix] > record["native_" + suffix]
                   for suffix in ("min", "max")):
                acquisition["widened"] += 1
            predicted, native = record.get("target_angle"), record.get("angle")
            if record.get("angle_valid") and all(isinstance(v, (int, float)) and math.isfinite(v)
                                                  for v in (predicted, native)):
                acquisition["position_mismatches"] += abs(predicted - native) >= .25
        seq = record.get("seq", 0)
        if not isinstance(seq, int) or seq <= previous:
            raise ValueError("Trace sequence is missing, duplicated or unordered")
        previous = seq
        identity = tuple(record.get(key, 0) for key in
                         ("pc", "pc_index", "target", "target_index"))
        frame = record.get("frame", 0)
        if all(isinstance(value, int) and value > 0 for value in identity) and frame > 0:
            grouped[(identity, frame)].append(record)
    cycles = collections.defaultdict(dict)
    for (identity, frame), events in grouped.items():
        for acquired in events:
            if acquired["event"] != "acquire" or not acquired.get("result") or not acquired.get("accepted"):
                continue
            marker = next((e for e in events if e["event"] == "marker_add" and e["seq"] > acquired["seq"]), None)
            sound = next((e for e in events if marker and e["event"] == "sound" and e["seq"] > marker["seq"] and e.get("sound") and e.get("cue")), None)
            removed = next((e for e in events if sound and e["event"] == "remove" and e["seq"] > sound["seq"] and e.get("result") and e.get("angular_reached") and e.get("angle_valid") and e.get("camera_valid")), None)
            if removed:
                angle, limit = removed.get("angle"), removed.get("limit")
                if isinstance(angle, (int, float)) and isinstance(limit, (int, float)) and math.isfinite(angle) and math.isfinite(limit) and angle > limit:
                    cycles[identity][frame] = {"frame": frame, "cue": sound["cue"], "angle": angle, "limit": limit, "fov": removed.get("fov"), "acquisition_origin": acquisition_origin(acquired)}
    loops = []
    for identity, frames in cycles.items():
        runs = []
        for frame in sorted(frames):
            if not runs or frame != runs[-1][-1] + 1:
                runs.append([])
            runs[-1].append(frame)
        for run in runs:
            if len(run) >= 3:
                loops.append({"identity": identity, "first_frame": run[0], "last_frame": run[-1], "cycles": len(run), "example": frames[run[0]],
                              "acquisition_origins": dict(collections.Counter(
                                  frames[f]["acquisition_origin"] for f in run))})
    active = footer.get("active_ms", 0)
    checks = {
        "repair_enabled_at_45_degrees": session.get("stable_cone") == 1 and
                                        session.get("shooting_degrees") == 45,
        "complete_active_window": footer.get("reason") == "time_limit" and
                                  isinstance(active, (int, float)) and active >= 30000,
        "no_dropped_records": bool(footer) and footer.get("dropped") == 0,
        "repair_exercised": acquisition["view_gated"] + acquisition["cap_applied"] > 0,
        "assisted_acquisition_exercised": acquisition["widened"] > 0,
        "position_model_verified": acquisition["position_verified"] > 0,
        "no_observed_position_mismatch": acquisition["position_mismatches"] == 0,
        "ordinary_locks_and_ui_sounds_present": acquisition["new_locks"] > 0 and
                                               sum(sound_requests.values()) > 0,
        "no_assist_only_repeating_sequence": not any(
            loop["acquisition_origins"].get("assist_only", 0) for loop in loops),
        "repeating_sequences_classified": not any(
            loop["acquisition_origins"].get("unknown", 0) for loop in loops),
    }
    return {
        "session": session,
        "events": dict(counts),
        "ui_sound_requests": dict(sound_requests),
        "level_changes": [dict(before=key[0], after=key[1], configured_cue=key[2], count=value)
                          for key, value in level_changes.items()],
        "stop_reason": footer.get("reason", "missing_footer"),
        "dropped": footer.get("dropped"),
        "lossless": bool(footer) and footer.get("dropped") == 0,
        "confirmed_sequences": loops,
        "repair_validation": {
            "checks": checks,
            "trace_checks_pass": all(checks.values()),
            "acquisition_evidence": dict(acquisition),
            "active_ms": active,
            "user_listening_check": "Still required: normal lock progression, full-lock feedback, firing and absence of mod-induced intrusive repetition; native-cone behavior is preserved.",
        },
        "interpretation": "Repeated acquire/marker/sound/angular-remove sequences observed." if loops else "No repeated complete sequence confirmed in this capture; this does not rule out the issue.",
    }


def read_report(path):
    path = Path(path)
    if path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("Trace exceeds its two-MiB contract")
    with path.open(encoding="utf-8") as stream:
        return analyze([json.loads(line) for line in stream if line.strip()])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace")
    parser.add_argument("--baseline", help="Optional assistance-off capture; reported separately without assuming identical encounters")
    parser.add_argument("--verify-repair", action="store_true",
                        help="Exit with status 2 unless the capture passes the repair trace checks; user listening verification remains separate")
    args = parser.parse_args()
    try:
        result = {"capture": read_report(args.trace)}
        if args.baseline:
            result["baseline"] = read_report(args.baseline)
        print(json.dumps(result, indent=2))
        if args.verify_repair and not result["capture"]["repair_validation"]["trace_checks_pass"]:
            parser.exit(2, "Repair trace checks did not pass; inspect repair_validation.checks.\n")
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f"Cannot analyze trace: {error}\n")


if __name__ == "__main__":
    main()
