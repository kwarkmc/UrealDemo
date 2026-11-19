"""Offline checks for record integrity, report mathematics and mutation failures."""
import argparse
import copy
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / (name.replace("_", "-")+".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load("run_flight_test")
report = load("report_flight_test")


def state():
    a = dict.fromkeys(runner.AIRCRAFT_COLUMNS, 0.)
    a.update(simulation_time_s=0., running=False, speed_mps=65, time_scale=1)
    return {"aircraft": a, "airframe": {"fingerprint": "test"}, "ground": {"crashed": False, **dict.fromkeys(runner.GROUND,0.),**{k:[0.,0.,0.] for k in runner.WHEELS}},
            "fault": "", "automation": {"kind": "none", "phase": "idle", **dict.fromkeys(runner.GUIDANCE, 0.)},
            "navigation": {"waypoint_count": 0, "mode": "manual"},
            "controls": dict.fromkeys(runner.CONTROLS, 0.),
            "actuators": dict.fromkeys((*runner.CONTROLS, "thrust_n",*runner.PROPULSION), 0.),
            "flaps": {"command_deg": 0, "actual_deg": 0}, "gear": {"extension": 1},
            "environment": dict.fromkeys(("wind_east_mps", "wind_north_mps", "wind_up_mps"), 0.),
            "wargame": {"shots": []}}


class Checks(unittest.TestCase):
    def test_added_columns_are_explicit_in_comparison(self):
        old=[{"time_s":0.,"speed_mps":65.},{"time_s":1.,"speed_mps":64.}]
        new=[r|{"propeller_rpm":1200.} for r in old]
        result=report.compare(old,new)
        self.assertFalse(result["exact_samples"])
        self.assertEqual(result["unmatched_columns"],["propeller_rpm"])
        self.assertEqual(result["differences"]["speed_mps"]["maximum_absolute"],0)
    def fixture(self, folder):
        s = state()
        rows = [runner.sample(s, 0, "test", "trim")]
        s["aircraft"].update(simulation_time_s=.02, pitch_deg=2)
        rows.append(runner.sample(s, 0, "test", "trim"))
        with (folder / "samples.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=runner.COLUMNS)
            writer.writeheader()
            writer.writerows(rows)
        metadata = {"case": "<script>", "duration_s": .02, "sample_dt_s": .02, "events": [],
                    "initial_condition": {}, "source_sha256": {}, "profile_sha256": "test"}
        runner.save_json(folder / "metadata.json", metadata)
        runner.save_json(folder / "result.json", {"ok": True})
        return rows

    def test_metrics_and_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            rows = self.fixture(Path(directory))
            self.assertEqual(report.metrics(rows)["pitch_deg"]["peak_time_s"], .02)
            self.assertTrue(report.compare(rows, rows)["exact_samples"])
            altered = copy.deepcopy(rows)
            altered[-1]["pitch_deg"] += 2
            differences = report.compare(rows, altered)
            self.assertEqual(differences["first_difference"]["time_s"], .02)
            self.assertAlmostEqual(differences["differences"]["pitch_deg"]["rms"], 2**.5)

    def test_record_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            self.fixture(folder)
            report.read_run(folder)
            text = (folder / "samples.csv").read_text()
            (folder / "samples.csv").write_text(text.replace("0.02,", "0.0,", 1))
            with self.assertRaises(ValueError):
                report.read_run(folder)
            self.fixture(folder)
            (folder / "samples.csv").write_text((folder / "samples.csv").read_text().replace("65,", "nan,", 1))
            with self.assertRaises(ValueError):
                report.read_run(folder)
            self.fixture(folder)
            lines = (folder / "samples.csv").read_text().splitlines()
            (folder / "samples.csv").write_text("\n".join(lines[:-1])+"\n")
            with self.assertRaises(ValueError):
                report.read_run(folder)

    def test_html_escape_and_incomplete_label(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            self.fixture(folder)
            runner.save_json(folder / "result.json", {"ok": False, "error": "<script>"})
            report.write_report(folder)
            text = (folder / "report.html").read_text()
            self.assertNotIn("<script>", text)
            self.assertIn("&lt;script&gt;", text)
            self.assertIn("FAILED / INCOMPLETE", text)

    def test_reproducibility_rejects_changed_conditions(self):
        with tempfile.TemporaryDirectory() as directory:
            left, right = Path(directory)/"left", Path(directory)/"right"
            left.mkdir()
            right.mkdir()
            self.fixture(left)
            self.fixture(right)
            metadata = json.loads((right/"metadata.json").read_text())
            metadata["profile_sha256"] = "different-profile"
            runner.save_json(right/"metadata.json", metadata)
            with self.assertRaisesRegex(ValueError, "identical profile"):
                report.write_report(left, right, require_identical=True)

    def test_sample_guard_and_event_times(self):
        s = state()
        s["aircraft"]["speed_mps"] = float("nan")
        with self.assertRaises(ValueError):
            runner.sample(s, 0, "test", "trim")
        s = state()
        with self.assertRaises(ValueError):
            runner.sample(s, 0, "changed", "trim")
        self.assertEqual([round(event["time_s"]/.02) for event in runner.events_for("elevator-pulse")], [250, 275])

    def test_known_circle_curvature(self):
        latitude = math.radians(33.433)
        e2 = 6.69437999014e-3
        n = 6378137/math.sqrt(1-e2*math.sin(latitude)**2)
        m = 6378137*(1-e2)/(1-e2*math.sin(latitude)**2)**1.5
        rows = []
        for t in range(61):
            angle = 65/1000*t
            rows.append(dict(time_s=t, latitude_deg=33.433+math.degrees(1000*math.sin(angle)/(m+2600)),
                longitude_deg=126.52+math.degrees(1000*math.cos(angle)/((n+2600)*math.cos(latitude))),
                ellipsoid_height_m=2600, speed_mps=65, roll_deg=math.degrees(math.atan(65**2/(9.80665*1000))),
                roll_rate_deg_s=0, sideslip_deg=0, vertical_speed_mps=0, navigation_mode="loiter"))
        result = report.coordinated_turn(rows)
        self.assertGreater(result["qualified_samples"], 50)
        self.assertLess(result["maximum_absolute_relative_difference"], .001)

    def test_lost_response_is_not_retried_and_files_restore(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "aircraft").mkdir()
            original = b'{"profile":"original.json"}\n'
            (root / "aircraft/active.json").write_bytes(original)
            profile = b'{"id":"test","parameters":{"iy":100}}'
            (root / "aircraft/test.json").write_bytes(profile)
            s = state()
            s["airframe"].update(initial_speed_mps=65, initial_height_m=2600,
                fingerprint="sha1:"+hashlib.sha1(profile).hexdigest())
            fake = MagicMock()
            fake.get_state.return_value = s
            fake.reload_aircraft_profile.return_value = s
            fake.reset.return_value = s
            fake.step.side_effect = TimeoutError("lost write response")
            args = argparse.Namespace(case="trim", iy_scale=1, profile="test.json", output=root/"run",
                                      reset_flight=True, no_report=True, api_url="unused")
            with patch.object(runner, "ROOT", root):
                _, result = runner.run(args, fake)
            self.assertFalse(result["ok"])
            self.assertEqual(fake.step.call_count, 1)
            self.assertEqual((root / "aircraft/active.json").read_bytes(), original)
            self.assertEqual(result["samples"], 1)
            self.assertIn("TimeoutError", result["error"])
            self.assertTrue((root/"run/samples.csv").exists())
            with self.assertRaises(FileExistsError):
                runner.run(args, fake)


if __name__ == "__main__":
    unittest.main()
