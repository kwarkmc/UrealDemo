"""Live STL display checks; requires Unreal/API. Restores files and parks paused at 1x.

Only display geometry is tested, not aerodynamic validity. --capture requests a
native render after explicitly setting an illustrative pose; no UI automation.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import time
from urllib.request import Request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from uavsim_client import UAVSimClient, UAVSimError


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/visual-api-check.json")
    parser.add_argument("--capture", action="store_true")
    args = parser.parse_args()
    sim = UAVSimClient()
    report = {"ok": False, "passed": []}
    config = ROOT / "aircraft/visual.json"
    fixture = ROOT / "aircraft/models/runtime-visual-check.stl"
    backups = {path: path.read_bytes() if path.exists() else None for path in (config, fixture)}
    example = json.loads((ROOT / "aircraft/models/example-visual.json").read_text(encoding="utf-8"))
    vertices = [tuple(map(float, line.split()[1:])) for line in
                (ROOT / "aircraft/models/example.stl").read_text(encoding="ascii").splitlines()
                if line.split() and line.split()[0] == "vertex"]

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        report["passed"].append(name)
        print(name, file=sys.stderr, flush=True)

    def native(state, route, **body):
        request = Request("http://127.0.0.1:30010/remote/object/" + route,
                          json.dumps(body).encode(), {"Content-Type": "application/json"}, method="PUT")
        with sim.opener.open(request, timeout=15) as response:
            return json.load(response)

    def prop(state, component, name):
        return native(state, "property", objectPath=state["object_path"] + "." + component,
                      propertyName=name, access="READ_ACCESS")[name]

    def physics(state):
        return {key: value for key, value in state.items() if key != "visual"}

    def identity(state):
        return {key: value for key, value in state["visual"].items() if key != "error"}

    def rejected(body=None):
        try:
            sim._request("POST", "aircraft/visual/reload", {} if body is None else body)
        except UAVSimError as error:
            return error.status
        return None

    def write_config(**changes):
        config.write_text(json.dumps(example | changes, indent=2) + "\n", encoding="utf-8")

    def load(label, **changes):
        before = sim.get_state()
        write_config(**changes)
        state = sim.reload_aircraft_visual()
        check(physics(before) == physics(state) and not state["visual"]["error"], label)
        report[label] = state["visual"]
        return state

    def binary(points):
        return b"solid binary fixture".ljust(80, b"\0") + struct.pack("<I", len(points) // 3) + b"".join(
            struct.pack("<12fH", 0, 0, 0, *points[i], *points[i + 1], *points[i + 2], 0)
            for i in range(0, len(points), 3))

    try:
        sim.pause()
        sim.reset()
        sim.set_time_scale(1)
        builtin = load("builtin reload preserves physics", mesh="")
        check(builtin["visual"]["source"] == "builtin" and builtin["visual"]["triangle_count"] == 0
              and prop(builtin, "AirframeVisual", "bVisible")
              and not prop(builtin, "ImportedAircraftMesh", "bVisible"), "builtin display selected")
        check(rejected({"extra": 1}) == 400 and sim.get_state() == builtin, "unknown request fields rejected without mutation")
        ascii_state = load("paused ASCII reload preserves all nonvisual state")
        check(ascii_state["visual"]["source"] == "stl" and ascii_state["visual"]["triangle_count"] == 108
              and abs(ascii_state["visual"]["span_m"] - 1.8) < 1e-6, "ASCII triangle count and span")
        check(prop(ascii_state, "ImportedAircraftMesh", "bVisible")
              and not prop(ascii_state, "AirframeVisual", "bVisible"), "imported mesh replaces builtin body display")
        position = prop(ascii_state, "Aircraft", "RelativeLocation")
        offset = prop(ascii_state, "ImportedAircraftMesh", "RelativeLocation")
        moved = sim.step(.5)
        check(not moved["fault"] and prop(moved, "Aircraft", "RelativeLocation") != position
              and offset == {"X": 0, "Y": 0, "Z": 0}
              and prop(moved, "ImportedAircraftMesh", "RelativeLocation") == offset,
              "flight moves aircraft while imported local offset stays zero")
        fixture.write_text("solid bad\nfacet normal 0 0 0\nouter loop\n" + "vertex 0 0 0\n" * 3
                           + "endloop\nendfacet\nendsolid bad\n", encoding="ascii")
        for label, changes in (
            ("parent path", {"mesh": "../outside.stl"}),
            ("absolute path", {"mesh": str((ROOT / "aircraft/models/example.stl").resolve())}),
            ("same axes", {"up_axis": "x"}),
            ("missing file", {"mesh": "models/runtime-visual-missing.stl"}),
            ("invalid scale", {"scale_to_m": 0}),
            ("boolean scale", {"scale_to_m": True}),
            ("string gear flag", {"show_builtin_gear": "false"}),
            ("numeric gear flag", {"show_builtin_gear": 0}),
            ("degenerate ASCII", {"mesh": "models/runtime-visual-check.stl"}),
        ):
            before = sim.get_state()
            write_config(**changes)
            status = rejected()
            after = sim.get_state()
            check(status == 409 and after["visual"]["error"] and identity(after) == identity(before)
                  and physics(after) == physics(before), label + " rejected; old mesh and physics preserved")
        fixture.write_bytes(binary(vertices)[:-1])
        before = sim.get_state()
        check(rejected() == 409 and identity(sim.get_state()) == identity(before)
              and physics(sim.get_state()) == physics(before), "truncated binary rejected without replacing mesh or physics")
        fixture.write_bytes(binary(vertices))
        loaded = load("binary solid-header reload preserves physics", mesh="models/runtime-visual-check.stl")
        check(loaded["visual"]["triangle_count"] == 108 and abs(loaded["visual"]["span_m"] - 1.8) < 1e-6,
              "binary solid-header count and span match ASCII")
        fixture.write_bytes(binary([(-y * 1000 + 100, x * 1000 + 200, z * 1000 + 300) for x, y, z in vertices]))
        mm = load("millimetre axes and origin reload preserves physics", mesh="models/runtime-visual-check.stl",
                  scale_to_m=.001, forward_axis="y", origin=[100, 200, 300])
        check(mm["visual"]["triangle_count"] == 108 and abs(mm["visual"]["span_m"] - 1.8) < 1e-6,
              "converted millimetre axis/origin fixture preserves triangle count and span")
        hidden = load("hide builtin gear preserves physical gear", show_builtin_gear=False)
        check(prop(hidden, "NoseGearHinge", "bHiddenInGame"), "optional builtin gear display hidden")
        shown = load("show builtin gear preserves physical gear", show_builtin_gear=True)
        check(prop(shown, "NoseGearHinge", "bHiddenInGame") == shown["gear"]["up_locked"], "builtin gear visibility restored")
        sim.set_running(True)
        status = rejected()
        paused = sim.pause()
        check(status == 409 and identity(paused) == identity(shown), "running reload rejected with current mesh preserved")
        if args.capture:
            pose = {key: paused["aircraft"][key] for key in
                    ("latitude_deg", "longitude_deg", "ellipsoid_height_m", "heading_deg", "pitch_deg", "roll_deg")}
            state = sim.set_pose(**(pose | {"heading_deg": 60, "roll_deg": 15}))
            sim.set_view("aircraft")
            source = ROOT / "artifacts/terrain-view.png"
            previous = source.stat().st_mtime_ns if source.exists() else None
            result = native(state, "call", objectPath=state["object_path"], functionName="CaptureView", parameters={})
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and (not source.exists() or source.stat().st_mtime_ns == previous):
                time.sleep(.1)
            check(result.get("ReturnValue") is True and source.exists() and source.stat().st_mtime_ns != previous,
                  "native STL render captured")
            destination = ROOT / "artifacts/stl-aircraft-view.png"
            destination.write_bytes(source.read_bytes())
            report["capture"] = str(destination)
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        try:
            for path, original in backups.items():
                if original is None:
                    path.unlink(missing_ok=True)
                else:
                    path.write_bytes(original)
            sim.pause()
            sim.reload_aircraft_visual()
            sim.reset_on_ground()
            final = sim.set_time_scale(1)
            check(all((path.read_bytes() if path.exists() else None) == original for path, original in backups.items())
                  and not final["aircraft"]["running"] and final["aircraft"]["time_scale"] == 1
                  and final["ground"]["on_ground"] and final["ground"]["parking_brake"] and not final["fault"],
                  "original files restored and reloaded; parked, paused, 1x")
            report["restored_visual"] = final["visual"]
        except Exception as error:
            report.update(ok=False, restore_error=str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"ok": report["ok"], "passed": len(report["passed"]), "output": str(args.output),
                      "error": report.get("error"), "restore_error": report.get("restore_error")}, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
