"""One real Unreal integration run: manual dynamics, route validation, sequential points and final orbit."""
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from uavsim_client import UAVSimClient, UAVSimError


def main():
    sim = UAVSimClient()
    report = {"ok": False, "passed": []}

    def check(ok, message):
        if not ok:
            raise RuntimeError(message)
        report["passed"].append(message)
        print(message, flush=True)

    def point(north, east, height=2600, speed=19):
        return dict(latitude_deg=33.43 + math.degrees(north / 6378137),
                    longitude_deg=126.52 + math.degrees(east / (6378137 * math.cos(math.radians(33.43)))),
                    ellipsoid_height_m=height, speed_mps=speed)

    def fly(points, seconds, clockwise=True):
        sim.reset()
        sim.set_route(points, loiter_radius_m=150, clockwise=clockwise)
        indexes = set()
        samples = []
        for tick in range(seconds):
            state = sim.step(1)
            n = state["navigation"]
            indexes.add(n["active_waypoint_index"])
            if n["mode"] == "loiter" and tick >= seconds - 30:
                a = state["aircraft"]
                radial = math.atan2((a["longitude_deg"] - points[-1]["longitude_deg"]) * math.cos(math.radians(a["latitude_deg"])),
                                    a["latitude_deg"] - points[-1]["latitude_deg"])
                samples.append((n["distance_to_waypoint_m"], a["ellipsoid_height_m"], a["speed_mps"], radial))
            if tick % 40 == 0:
                print(f"t={tick+1}s {n['mode']} point={n['active_waypoint_index']} distance={n['distance_to_waypoint_m']:.1f}m", flush=True)
        check(indexes == set(range(len(points))) and len(samples) == 30,
              f"{len(points)} waypoint(s) reached sequentially and final orbit maintained")
        error = dict(radius_m=max(abs(s[0] - 150) for s in samples),
                     height_m=max(abs(s[1] - points[-1]["ellipsoid_height_m"]) for s in samples),
                     speed_mps=max(abs(s[2] - points[-1]["speed_mps"]) for s in samples),
                     turn_rad=sum(math.remainder(b[3] - a[3], 2 * math.pi) for a, b in zip(samples, samples[1:])))
        check(error["radius_m"] < 10 and error["height_m"] < 5 and error["speed_mps"] < 1
              and error["turn_rad"] * (1 if clockwise else -1) > 1,
              f"{len(points)} waypoint final orbit radius/height/speed/direction within development tolerances")
        report[f"route_{len(points)}_errors"] = error

    try:
        sim.pause()
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        sim.set_initial_condition(latitude_deg=33.43, longitude_deg=126.52,
            ellipsoid_height_m=2600, speed_mps=19, heading_deg=0, pitch_deg=0, roll_deg=0)
        state = sim.reset()
        check(state["model"] == "thor_nonlinear_6dof_v2" and not state["fault"], "live own nonlinear 6DOF model ready")
        check(sim.step(1e-12)["aircraft"]["simulation_time_s"] > 0, "very small explicit step advances time")
        sim.reset()
        sim.set_controls(elevator_deg=2, aileron_deg=2, rudder_deg=2, throttle=.7)
        a = sim.step(.1)["aircraft"]
        check(a["pitch_rate_deg_s"] < 0 and a["roll_rate_deg_s"] > 0
              and a["yaw_rate_deg_s"] > 0 and a["speed_mps"] > 19,
              "manual surfaces and throttle change real angular rates and speed")
        before = sim.get_state()
        try:
            sim.set_initial_condition(latitude_deg=33.431, longitude_deg=126.521,
                ellipsoid_height_m=-500, speed_mps=19, heading_deg=0, pitch_deg=0, roll_deg=0)
        except UAVSimError as error:
            check(error.status == 409 and sim.get_state() == before,
                  "initial condition below terrain rejected atomically with HTTP 409")
        else:
            raise RuntimeError("Initial condition below terrain was accepted")
        state = sim.set_initial_condition(latitude_deg=33.431, longitude_deg=126.521,
            ellipsoid_height_m=20000, speed_mps=19, heading_deg=0, pitch_deg=0, roll_deg=0)
        check(abs(state["aircraft"]["ellipsoid_height_m"] - 20000) < 1e-6,
              "initial condition at 20000m boundary accepted after ENU roundtrip")
        sim.reset()
        points = [point(180, 0, speed=18), point(180, 250, 2610, speed=20)]
        state = sim.set_route(points, loiter_radius_m=150)
        for bad_points, radius, expected in (([points[0], points[1] | {"latitude_deg":91}], 150, 400),
                                             ([point(180, 0, speed=24)], 100, 400),
                                             ([points[0], points[1] | {"latitude_deg":0}], 150, 409)):
            try:
                sim.set_route(bad_points, loiter_radius_m=radius)
            except UAVSimError as error:
                check(error.status == expected and sim.get_state() == state,
                      f"invalid route rejected atomically with HTTP {expected}")
            else:
                raise RuntimeError("Invalid route was accepted")
        state = sim.set_controls(elevator_deg=0, aileron_deg=0, rudder_deg=0, throttle=.5)
        check(state["navigation"]["mode"] == "manual" and state["navigation"]["waypoint_count"] == 0,
              "manual input cancels navigation")
        fly([point(180, 0)], 180)
        fly(points, 240, clockwise=False)
        check(sim.cancel_navigation()["navigation"]["mode"] == "manual", "explicit navigation cancel")
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        try:
            sim.reset()
        except Exception as error:
            report["reset_error"] = str(error)
            report["ok"] = False
        output = Path(__file__).resolve().parents[1] / "artifacts/navigation-api-check.json"
        output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
