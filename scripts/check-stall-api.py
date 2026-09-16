"""Focused live Unreal check: nonlinear stall, recovery, wind and explicit time.

Changes flight state. Finally restores still air, 19 m/s trim, 1x and pause.
Development regression only; no aircraft qualification claim.
"""
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from uavsim_client import UAVSimClient, UAVSimError


def main():
    sim = UAVSimClient()
    report = {"ok": False, "passed": [], "manual_recovery": []}

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        report["passed"].append(name)
        print(name, flush=True)

    def initialize(alpha=18, speed=9, height=2600, **extra):
        return sim.set_initial_condition(latitude_deg=33.43, longitude_deg=126.52,
            ellipsoid_height_m=height, heading_deg=0, pitch_deg=alpha, roll_deg=0,
            speed_mps=speed, angle_of_attack_deg=alpha, sideslip_deg=0, **extra)

    def controls(elevator=0, throttle=0):
        return sim.set_controls(elevator_deg=elevator, aileron_deg=0, rudder_deg=0, throttle=throttle)

    def rejected(method, path, body):
        before = sim.get_state()
        try:
            sim._request(method, path, body)
        except UAVSimError as error:
            return error.status == 400 and sim.get_state() == before
        return False

    try:
        sim.pause()
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        initialize(0, 19)
        start = sim.reset()
        check(start["model"] == "thor_nonlinear_6dof_v2" and not start["fault"], "nonlinear native model and API schema ready")
        for _ in range(10):
            state = sim.step(1)
        a = state["aircraft"]
        check(abs(a["speed_mps"] - 19) < 1e-7 and abs(a["ellipsoid_height_m"] - 2600) < .01
              and abs(a["distance_travelled_m"] - 190) < 1e-6,
              "computed trim sustains 10 s level flight at scene altitude")
        check(abs(a["air_density_kg_m3"] - .947264) < 1e-6
              and abs(a["dynamic_pressure_pa"] - .5*a["air_density_kg_m3"]*19**2) < 1e-6,
              "ISA density and dynamic pressure reach native telemetry")

        low = initialize(12, 19)["aircraft"]
        high = initialize(20, 19)["aircraft"]
        check(high["lift_coefficient"] < .6*low["lift_coefficient"]
              and high["drag_coefficient"] > 1.5*low["drag_coefficient"] and high["stalled"],
              "same-speed post-stall lift falls and drag increases")

        initialize()
        before = controls()["aircraft"]
        sinking = sim.step(.2)["aircraft"]
        check(sinking["stalled"] and sinking["vertical_speed_mps"] < -.5
              and sinking["flow_separation"] > before["flow_separation"],
              "9 m/s power-off stall loses lift, separates further and sinks")
        controls(3, 1)
        for _ in range(7):
            state = sim.step(.2)
            report["manual_recovery"].append(state["aircraft"])
        after = state["aircraft"]
        check(not state["fault"] and not after["stalled"] and after["flow_separation"] < .15
              and abs(after["angle_of_attack_deg"]) < 4 and after["speed_mps"] > 19
              and after["ellipsoid_height_m"] < sinking["ellipsoid_height_m"],
              "manual nose-down and thrust reattach airflow with speed gain and altitude loss")

        replay = []
        for factor in (.25, 20):
            sim.reset()
            initialize()
            controls(3, 1)
            sim.set_time_scale(factor)
            replay.append(sim.step(.6)["aircraft"])
        check(all(replay[0][key] == replay[1][key] for key in replay[0]
                  if key not in ("time_scale", "effective_time_scale")),
              "stall state and explicit time replay identically at .25x and 20x")

        base = sim.reset()["aircraft"]
        wind = sim.set_wind(wind_east_mps=3, wind_north_mps=0, wind_up_mps=0)["aircraft"]
        check(wind["latitude_deg"] == base["latitude_deg"] and wind["longitude_deg"] == base["longitude_deg"]
              and wind["ground_speed_mps"] == base["ground_speed_mps"]
              and abs(wind["speed_mps"] - math.hypot(19, 3)) < 1e-7,
              "wind change preserves inertial state and changes relative airspeed")
        base = sim.reset()["aircraft"]
        drift = sim.step(1)["aircraft"]
        east = math.radians(drift["longitude_deg"] - base["longitude_deg"]) * 6378137 * math.cos(math.radians(33.43))
        check(abs(east - 3) < .02 and abs(drift["speed_mps"] - 19) < 1e-6,
              "steady wind moves ground track while true airspeed stays trimmed")
        check(rejected("PUT", "environment", {"wind_east_mps":31,"wind_north_mps":0,"wind_up_mps":0})
              and rejected("PUT", "initial-condition", {"angle_of_attack_deg":18}),
              "invalid wind and incomplete airflow initialization reject atomically")

        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        for speed in (5, 45):
            check(abs(initialize(0, speed)["aircraft"]["speed_mps"] - speed) < 1e-8,
                  f"explicit {speed} m/s initial condition accepted independently of level trim")
        before = sim.get_state()
        try:
            initialize(0, 19, -500)
        except UAVSimError as error:
            check(error.status == 409 and sim.get_state() == before,
                  "initial condition below terrain rejected atomically with HTTP 409")
        else:
            raise RuntimeError("Initial condition below terrain was accepted")
        check(abs(initialize(0, 19, 20000)["aircraft"]["ellipsoid_height_m"] - 20000) < 1e-6,
              "geodetic 20000 m boundary survives atmosphere initialization")

        sim.reset()
        initialize()
        target = dict(latitude_deg=33.443, longitude_deg=126.52, ellipsoid_height_m=2600, speed_mps=19)
        sim.set_route([target], loiter_radius_m=150)
        state = sim.step(.01)
        check(state["navigation"]["stall_recovery"] and state["controls"]["throttle"] == 1,
              "autopilot exposes stall recovery priority through API")
        lowest = 2600
        for _ in range(40):
            state = sim.step(1)
            lowest = min(lowest, state["aircraft"]["ellipsoid_height_m"])
        a = state["aircraft"]
        check(not state["navigation"]["stall_recovery"] and not a["stall_warning"]
              and abs(a["speed_mps"] - 19) < .5 and not state["fault"],
              "autopilot recovers through physics then resumes route tracking")
        report["autopilot_recovery"] = {"height_loss_m":2600-lowest,"final":state}
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        try:
            sim.pause()
            sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
            sim.reset()
            sim.set_time_scale(1)
        except Exception as error:
            report["restore_error"] = str(error)
            report["ok"] = False
        output = Path(__file__).resolve().parents[1] / "artifacts/stall-api-check.json"
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({key:value for key,value in report.items() if key not in ("manual_recovery","autopilot_recovery")}, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
