"""Check a running PIE FlightScene through the product HTTP API; resume afterward."""
import argparse
import json
import math
from pathlib import Path
from urllib.request import ProxyHandler, Request, build_opener


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object-path", required=True, help="Actual PIE FlightScene object path")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "artifacts/flight-api-check.json")
    args = parser.parse_args()
    opener = build_opener(ProxyHandler({}))
    report = {"object_path": args.object_path, "passed": [], "ok": False}

    def call(function, **parameters):
        body = {"objectPath": args.object_path, "functionName": function, "parameters": parameters}
        request = Request("http://127.0.0.1:30010/remote/object/call", json.dumps(body).encode(),
                          {"Content-Type": "application/json"}, method="PUT")
        with opener.open(request, timeout=15) as response:
            return json.load(response).get("ReturnValue")

    def check(condition, description):
        if not condition:
            raise RuntimeError(description)
        report["passed"].append(description)

    try:
        check(call("GetAircraftState")["TerrainLoaded"], "terrain loaded")
        position = dict(LatitudeDeg=33.44, LongitudeDeg=126.53, EllipsoidHeightM=2700.0,
                        HeadingDeg=135.0, PitchDeg=4.0, RollDeg=-12.0)
        check(call("SetAircraftGeodetic", **position) is True, "position accepted")
        state = call("GetAircraftState")
        check(not state["Running"] and all(math.isclose(state[k], v, rel_tol=0, abs_tol=1e-9)
                                          for k, v in position.items()), "position and attitude roundtrip; paused")
        report["position_roundtrip"] = state
        check(call("SetAircraftGeodetic", **(position | {"LatitudeDeg": 91.0})) is False
              and call("GetAircraftState") == state, "invalid latitude rejected without state change")
        call("ResetFlight")
        state = call("GetAircraftState")
        check(state["SimulationTimeS"] == 0 and not state["Running"], "reset to paused time zero")
        check(call("AdvanceSimulation", DeltaSeconds=0.5) is True, "explicit half-second accepted")
        first = call("GetAircraftState")
        check(first["SimulationTimeS"] == 0.5 and not first["Running"], "explicit simulation time exact")
        call("ResetFlight")
        check(call("AdvanceSimulation", DeltaSeconds=0.5) is True
              and call("GetAircraftState") == first, "reset and replay reproduce state")
        check(call("AdvanceSimulation", DeltaSeconds=0.0) is False
              and call("GetAircraftState") == first, "invalid time step rejected without state change")
        report["stepped_state"] = first
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        try:
            call("SetRunning", Running=True)
            check(call("GetAircraftState")["Running"], "demo resumed")
        except Exception as error:
            report["ok"] = False
            report["resume_error"] = str(error)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
