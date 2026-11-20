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

    def test_invalid_order_rejected(self):
        rows = fixture()
        rows[2]["seq"] = 1
        with self.assertRaises(ValueError):
            analysis.analyze(rows)


if __name__ == "__main__":
    unittest.main()
