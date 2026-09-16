"""Focused live Unreal ground check; requires an already running API and engine.

Changes simulation state. Always attempts to restore paused ground start,
parking brake, zero wind and 1x. Development regression, not qualification.
Prints JSON; --output artifacts/ground-api-check.json also saves the report.
"""
import argparse
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from uavsim_client import UAVSimClient, UAVSimError


class CheckClient(UAVSimClient):
    def __init__(self):
        super().__init__()
        self.request_count = 0

    def _request(self, *args, **kwargs):
        self.request_count += 1
        return super()._request(*args, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Optional JSON report path")
    args = parser.parse_args()
    sim = CheckClient()
    report = {"ok": False, "passed": [], "takeoff": []}

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        report["passed"].append(name)

    def controls(elevator=0, throttle=0):
        return sim.set_controls(elevator_deg=elevator, aileron_deg=0, rudder_deg=0, throttle=throttle)

    def wheels(steering=0, brake=0, parking=False):
        return sim.set_ground_controls(steering_deg=steering, brake=brake, parking_brake=parking)

    def capture(state):
        return {key: state[key] for key in ("aircraft", "ground", "fault")}

    def advance(seconds):
        # Paused explicit time only; no playback delays or request retries.
        for _ in range(seconds):
            state = sim.step(1)
            if state["fault"]:
                break
        return state

    def rejected(call, status):
        try:
            call()
        except UAVSimError as error:
            return error.status == status
        return False

    try:
        sim.pause()
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        start = sim.reset_on_ground()
        a = start["aircraft"]
        position = {key: a[key] for key in ("latitude_deg", "longitude_deg", "heading_deg")}
        terrain_height = start["ground"]["terrain_height_m"]
        report["start"] = capture(start)
        check(not start["fault"] and start["ground"]["available"]
              and "virtual" in start["ground"]["surface"].lower()
              and abs(a["heading_deg"] - 60) < .1,
              "reset locates the virtual 60-degree development strip")

        rest = sim.step(1)
        g, a = rest["ground"], rest["aircraft"]
        report["rest"] = capture(rest)
        check(not rest["fault"] and g["parking_brake"] and g["on_ground"]
              and g["wheels_on_ground"] == 3 and a["ground_speed_mps"] < .02
              and abs(g["height_agl_m"] - .18) < .01
              and abs(g["normal_force_n"] - 1.959 * 9.80665) < .1
              and a["distance_travelled_m"] < .01 and not a["running"]
              and a["angle_of_attack_deg"] == 0 and a["sideslip_deg"] == 0
              and a["flow_separation"] < .01 and not a["stall_warning"] and not a["stalled"],
              "parked wheel reaction balances weight without drift or sinking")

        sim.set_ground_initial_condition(**position, ground_speed_mps=6)
        coast = advance(2)
        sim.set_ground_initial_condition(**position, ground_speed_mps=6)
        wheels(brake=1)
        stopped = advance(2)
        report["coast"], report["brake"] = capture(coast), capture(stopped)
        check(not coast["fault"] and not stopped["fault"] and stopped["ground"]["on_ground"]
              and stopped["aircraft"]["ground_speed_mps"] < .1
              and stopped["aircraft"]["distance_travelled_m"] < coast["aircraft"]["distance_travelled_m"]
              and coast["aircraft"]["ground_speed_mps"] > 1,
              "wheel brake stops the same 6 m/s start sooner than coasting")

        sim.reset_on_ground()
        wheels(steering=20)
        controls(throttle=.1)
        taxi = advance(3)
        turn = (taxi["aircraft"]["heading_deg"] - position["heading_deg"] + 180) % 360 - 180
        report["taxi"] = capture(taxi)
        check(not taxi["fault"] and taxi["ground"]["on_ground"] and turn > 5
              and taxi["aircraft"]["distance_travelled_m"] > .5,
              "positive nose-wheel steering turns right during powered taxi")

        sim.reset_on_ground()
        wheels()
        state = controls(throttle=1)
        rotating = False
        for _ in range(60):
            if not rotating and state["aircraft"]["speed_mps"] > 11:
                controls(elevator=-6, throttle=1)
                rotating = True
            state = sim.step(.1)
            report["takeoff"].append(capture(state))
            if state["fault"] or state["ground"]["height_agl_m"] > 2:
                break
        check(not state["fault"] and rotating and not state["ground"]["on_ground"]
              and state["ground"]["height_agl_m"] > 2,
              "thrust and elevator produce liftoff through the same flight dynamics")

        sim.set_initial_condition(**position, ellipsoid_height_m=terrain_height + 1,
            pitch_deg=0, roll_deg=0, speed_mps=10,
            angle_of_attack_deg=math.degrees(math.atan(.6 / 10)), sideslip_deg=0)
        wheels(brake=.7)
        controls(elevator=2)
        landed = advance(6)
        report["landing"] = capture(landed)
        check(not landed["fault"] and landed["ground"]["on_ground"]
              and landed["ground"]["height_agl_m"] > .15
              and landed["ground"]["impact_speed_mps"] > .1
              and landed["aircraft"]["ground_speed_mps"] < .2,
              "gentle touchdown transitions to a braked ground roll and stop")

        sim.set_initial_condition(**position, ellipsoid_height_m=terrain_height + .6,
            pitch_deg=0, roll_deg=0, speed_mps=12, angle_of_attack_deg=89, sideslip_deg=0)
        wheels()
        controls()
        crash = sim.step(.1)
        report["hard_touchdown"] = capture(crash)
        check(crash["ground"]["crashed"] and "Ground collision" in crash["fault"]
              and not crash["aircraft"]["running"] and crash["ground"]["impact_speed_mps"] > 4
              and crash["ground"]["height_agl_m"] > .1,
              "hard touchdown reports impact and pauses without passing through the surface")
        check(rejected(lambda: sim.set_running(True), 409) and sim.get_state() == crash,
              "crashed aircraft cannot resume until explicitly initialized")

        before = sim.reset_on_ground()
        invalid = [
            rejected(lambda: wheels(steering=31), 400),
            rejected(lambda: wheels(brake=1.1), 400),
            rejected(lambda: wheels(parking=1), 400),
            rejected(lambda: sim.set_ground_initial_condition(**position, ground_speed_mps=-1), 400),
            rejected(lambda: sim.set_ground_initial_condition(**position, ground_speed_mps=16), 400),
            rejected(lambda: sim.set_ground_initial_condition(**position, ground_speed_mps=16), 400),
        ]
        check(all(invalid) and sim.get_state() == before,
              "invalid ground controls and initial speed are rejected without mutation")
        waypoint = {key: position[key] for key in ("latitude_deg", "longitude_deg")}
        waypoint.update(ellipsoid_height_m=terrain_height + 100, speed_mps=19)
        check(rejected(lambda: sim.set_route([waypoint], loiter_radius_m=200), 409)
              and sim.get_state() == before,
              "airborne waypoint autopilot is rejected while on the ground")
        check(rejected(lambda: sim.set_initial_condition(**position, ellipsoid_height_m=terrain_height - 1,
                  pitch_deg=0, roll_deg=0, speed_mps=19), 409) and sim.get_state() == before,
              "airborne initialization below the actual terrain is rejected atomically")

        report["terrain_contact"] = []
        for lat, lon in ((33.45, 126.55), (37.45, 127.05)):
            sim.set_ground_initial_condition(latitude_deg=lat, longitude_deg=lon,
                heading_deg=60, ground_speed_mps=0)
            ground = sim.step(.1)
            report["terrain_contact"].append(capture(ground))
            check(ground["ground"]["available"] and ground["ground"]["surface"] == "terrain"
                  and ground["ground"]["on_ground"] and not ground["fault"]
                  and abs(ground["ground"]["height_agl_m"] - .18) < .01,
                  f"actual terrain mesh contact works at {lat}, {lon}")

        replay = []
        for factor in (.25, 20):
            sim.reset_on_ground()
            sim.set_time_scale(factor)
            wheels(steering=20)
            controls(throttle=.1)
            replay.append(sim.step(.6))
        report["replay"] = [capture(state) for state in replay]
        check(all(replay[0]["aircraft"][key] == replay[1]["aircraft"][key]
                  for key in replay[0]["aircraft"] if key not in ("time_scale", "effective_time_scale"))
              and replay[0]["ground"] == replay[1]["ground"] and not replay[1]["fault"],
              "explicit ground steps replay identically at .25x and 20x")
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        try:
            sim.pause()
            sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
            sim.reset_on_ground()
            final = sim.set_time_scale(1)
            report["restored"] = capture(final)
            check(not final["fault"] and not final["aircraft"]["running"]
                  and final["aircraft"]["time_scale"] == 1 and final["ground"]["parking_brake"]
                  and final["ground"]["on_ground"] and final["controls"]["throttle"] == 0,
                  "finally restores paused ground start with parking brake and 1x")
        except Exception as error:
            report["ok"] = False
            report["restore_error"] = str(error)
    report["api_requests"] = sim.request_count
    text = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
