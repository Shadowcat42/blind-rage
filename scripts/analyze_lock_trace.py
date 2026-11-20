"""Summarize a bounded Blind Rage trace without modifying it or the game."""
import argparse
import collections
import json
import math
from pathlib import Path


def analyze(records):
    counts = collections.Counter()
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
                    cycles[identity][frame] = {"frame": frame, "cue": sound["cue"], "angle": angle, "limit": limit, "fov": removed.get("fov")}
    loops = []
    for identity, frames in cycles.items():
        runs = []
        for frame in sorted(frames):
            if not runs or frame != runs[-1][-1] + 1:
                runs.append([])
            runs[-1].append(frame)
        for run in runs:
            if len(run) >= 3:
                loops.append({"identity": identity, "first_frame": run[0], "last_frame": run[-1], "cycles": len(run), "example": frames[run[0]]})
    return {
        "session": session,
        "events": dict(counts),
        "stop_reason": footer.get("reason", "missing_footer"),
        "dropped": footer.get("dropped"),
        "lossless": bool(footer) and footer.get("dropped") == 0,
        "confirmed_sequences": loops,
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
    args = parser.parse_args()
    try:
        result = {"capture": read_report(args.trace)}
        if args.baseline:
            result["baseline"] = read_report(args.baseline)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f"Cannot analyze trace: {error}\n")


if __name__ == "__main__":
    main()
