import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("lock_analysis", Path(__file__).resolve().parents[1] / "scripts/analyze_lock_trace.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def fixture():
    records = []
    for frame in range(1, 4):
        for kind in ("acquire", "marker_add", "sound", "remove"):
            records.append(dict(event=kind, seq=len(records) + 1, frame=frame, pc=10, pc_index=1, target=20, target_index=2,
                                result=1, accepted=1, sound=30, cue="SE_UI_lockon1_Cue", angular_reached=1,
                                angle_valid=1, camera_valid=1, angle=45, limit=37.5, fov=70))
    records.append(dict(event="end", reason="time_limit", dropped=0))
    return records


class AnalysisTests(unittest.TestCase):
    def test_repair_gate_rejects_empty_capture_despite_complete_timer(self):
        rows = [dict(event="session", stable_cone=1, shooting_degrees=45),
                dict(event="end", reason="time_limit", active_ms=30000, dropped=0)]
        validation = analysis.analyze(rows)["repair_validation"]
        self.assertFalse(validation["trace_checks_pass"])
        self.assertFalse(validation["checks"]["ordinary_locks_and_ui_sounds_present"])
        self.assertFalse(validation["checks"]["repair_exercised"])

    def test_repair_gate_requires_positive_evidence_and_complete_capture(self):
        rows = [dict(event="session", stable_cone=1, shooting_degrees=45)]
        rows += [r.copy() for r in fixture() if r["event"] != "end"]
        for row in rows:
            if row["event"] == "remove":
                row["result"] = 0
                row["angle"] = 20
            if row["event"] == "acquire":
                row.update(position_verified=1, native_min=3.6, effective_min=45,
                           target_angle=20, angle=20)
        rows.append(dict(event="acquire", seq=13, frame=4, pc=10, pc_index=1,
                         target=99, target_index=3, result=0, accepted=0, view_gated=1,
                         position_verified=1, native_min=3.6, effective_min=3.6,
                         angle_valid=1, target_angle=60, angle=60))
        rows.append(dict(event="end", reason="time_limit", active_ms=30000, dropped=0))
        self.assertTrue(analysis.analyze(rows)["repair_validation"]["trace_checks_pass"])
        for key, value in (("reason", "byte_limit"), ("active_ms", 20000), ("dropped", 1)):
            incomplete = [r.copy() for r in rows]
            incomplete[-1][key] = value
            self.assertFalse(analysis.analyze(incomplete)["repair_validation"]["trace_checks_pass"])
        for key, value in (("stable_cone", 0), ("shooting_degrees", 0)):
            disabled = [r.copy() for r in rows]
            disabled[0][key] = value
            self.assertFalse(analysis.analyze(disabled)["repair_validation"]["trace_checks_pass"])
        mismatch = [r.copy() for r in rows]
        mismatch[1]["target_angle"] = 5
        self.assertFalse(analysis.analyze(mismatch)["repair_validation"]["trace_checks_pass"])

    def test_repair_gate_rejects_a_confirmed_loop(self):
        rows = [dict(event="session", stable_cone=1, shooting_degrees=45)] + fixture()
        rows[-1]["active_ms"] = 30000
        validation = analysis.analyze(rows)["repair_validation"]
        self.assertFalse(validation["trace_checks_pass"])
        self.assertFalse(validation["checks"]["repeating_sequences_classified"])

    def test_native_loop_is_reported_without_failing_mod_loop_check(self):
        rows = fixture()
        for row in rows:
            if row["event"] == "acquire":
                row.update(angle=3.15, native_min=3.6, native_max=50,
                           effective_min=3.6, effective_max=50, limit=3.6)
        report = analysis.analyze(rows)
        self.assertEqual(report["confirmed_sequences"][0]["acquisition_origins"], {"native": 3})
        self.assertTrue(report["repair_validation"]["checks"]["no_assist_only_repeating_sequence"])
        self.assertTrue(report["repair_validation"]["checks"]["repeating_sequences_classified"])

    def test_widened_loop_still_fails_mod_loop_check(self):
        rows = fixture()
        for row in rows:
            if row["event"] == "acquire":
                row.update(angle=20, native_min=3.6, native_max=50,
                           effective_min=45, effective_max=50, limit=45)
        report = analysis.analyze(rows)
        self.assertEqual(report["confirmed_sequences"][0]["acquisition_origins"], {"assist_only": 3})
        self.assertFalse(report["repair_validation"]["checks"]["no_assist_only_repeating_sequence"])

    def test_exact_repeating_sequence(self):
        report = analysis.analyze(fixture())
        self.assertEqual(report["confirmed_sequences"][0]["cycles"], 3)
        self.assertTrue(report["lossless"])

    def test_eligibility_drop_is_not_a_camera_drop(self):
        rows = fixture()
        for row in rows:
            if row["event"] == "remove":
                row["angular_reached"] = 0
        self.assertFalse(analysis.analyze(rows)["confirmed_sequences"])

    def test_no_sound_or_retained_target_is_not_the_loop(self):
        for key, value in (("cue", ""), ("sound", 0), ("result", 0), ("angle", 10)):
            rows = fixture()
            for row in rows:
                if row["event"] in ("sound", "remove"):
                    row[key] = value
            self.assertFalse(analysis.analyze(rows)["confirmed_sequences"])

    def test_object_reuse_breaks_the_sequence(self):
        rows = fixture()
        for row in rows:
            if row.get("frame") == 2:
                row["target_index"] = 99
        self.assertFalse(analysis.analyze(rows)["confirmed_sequences"])

    def test_missing_footer_and_drops_are_not_lossless(self):
        rows = fixture()
        self.assertFalse(analysis.analyze(rows[:-1])["lossless"])
        rows[-1]["dropped"] = 7
        self.assertFalse(analysis.analyze(rows)["lossless"])

    def test_level_cue_path_is_not_an_observed_ui_sound_request(self):
        rows = [dict(event="level_change", seq=1, frame=1, pc=10, pc_index=1,
                     level_before=0, level_after=1, cue="LevelUp_Cue", sound=30)]
        report = analysis.analyze(rows)
        self.assertEqual(report["level_changes"][0]["configured_cue"], "LevelUp_Cue")
        self.assertFalse(report["ui_sound_requests"])
        self.assertFalse(report["confirmed_sequences"])

    def test_invalid_order_rejected(self):
        rows = fixture()
        rows[2]["seq"] = 1
        with self.assertRaises(ValueError):
            analysis.analyze(rows)


if __name__ == "__main__":
    unittest.main()
