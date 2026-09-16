"""One integration check of the Python client, HTTP validation and running Unreal actor."""
import argparse
import json
from pathlib import Path
import sys
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from uavsim_client import UAVSimClient, UAVSimError


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8765")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "artifacts/control-api-check.json")
    args = parser.parse_args()
    sim = UAVSimClient(args.url)
    opener = build_opener(ProxyHandler({}))
    report = {"ok": False, "passed": []}
    original = None

    def check(condition, description):
        if not condition:
            raise RuntimeError(description)
        report["passed"].append(description)

    def rejected(body, status=400, origin=None):
        headers = {"Content-Type": "application/json"}
        if origin:
            headers["Origin"] = origin
        data = body.encode() if isinstance(body, str) else json.dumps(body).encode()
        request = Request(args.url + "/api/v1/controls", data, headers, method="PUT")
        try:
            opener.open(request, timeout=30).close()
        except HTTPError as error:
            check(error.code == status and bool(json.load(error)["error"]["message"]),
                  f"invalid request rejected with JSON error {status}: {body}")
            return
        raise RuntimeError("Invalid request was accepted")

    try:
        original = sim.get_state()
        check(original["model"] == "thor_nonlinear_6dof_v2" and original["aircraft"]["terrain_loaded"],
              "runtime discovered and terrain loaded")
        sim.pause()
        controls = dict(elevator_deg=12.5, aileron_deg=-8.0, rudder_deg=6.0, throttle=0.4)
        state = sim.set_controls(**controls)
        check(state["controls"] == controls and sim.get_state() == state,
              "external client controls roundtrip")
        for invalid in (controls | {"elevator_deg": 25.01}, controls | {"aileron_deg": -25.01},
                        controls | {"rudder_deg": 25.01}, controls | {"throttle": 1.01},
                        controls | {"throttle": True}, controls | {"extra": 0},
                        {"elevator_deg": 0}, controls | {"elevator_deg": "0"},
                        controls | {"elevator_deg": float("nan")}):
            rejected(invalid)
        rejected('{"elevator_deg":0,"elevator_deg":1,"aileron_deg":0,"rudder_deg":0,"throttle":0}')
        rejected(controls, status=403, origin="https://example.com")
        check(sim.get_state() == state, "rejected writes do not change aircraft or controls")
        pose = dict(latitude_deg=33.44, longitude_deg=126.53, ellipsoid_height_m=2700.0,
                    heading_deg=135.0, pitch_deg=4.0, roll_deg=-12.0)
        state = sim.set_pose(**pose)
        check(all(abs(state["aircraft"][key] - value) < 1e-6 for key, value in pose.items())
              and not state["aircraft"]["running"], "pose roundtrip pauses simulation")
        sim.reset()
        first = sim.step(0.25)
        sim.reset()
        second = sim.step(0.25)
        check(first == second and second["aircraft"]["simulation_time_s"] == 0.25,
              "explicit step and reset replay reproduce state")
        sim.set_running(True)
        try:
            sim.step(0.25)
        except UAVSimError as error:
            check(error.status == 409, "step while running rejected with conflict")
        else:
            raise RuntimeError("Step while running was accepted")
        report["controls_roundtrip"] = controls
        report["object_path"] = state["object_path"]
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        if original is not None:
            try:
                sim.set_pose(**{key: original["aircraft"][key] for key in (
                    "latitude_deg", "longitude_deg", "ellipsoid_height_m", "heading_deg", "pitch_deg", "roll_deg")})
                sim.set_controls(**original["controls"])
                sim.set_running(original["aircraft"]["running"])
            except Exception as error:
                report["ok"] = False
                report["restore_error"] = str(error)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
