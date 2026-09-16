"""Focused live landing-gear check; requires the running API, Unreal and first runway.

Uses paused explicit steps, changes aircraft state, and finally restores calm air,
cleared failures, gear down, parked ground start and 1x. Does not restart processes.
Optional --impact adds a low-altitude gear-up body-impact fixture for development.
No measured landing-gear performance or qualification claim.
"""
import argparse
import json
from pathlib import Path
import sys
from urllib.request import Request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
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
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/gear-api-check.json")
    parser.add_argument("--impact", action="store_true", help="Also attempt the low-altitude body-impact fixture")
    args = parser.parse_args()
    sim = CheckClient()
    report = {"ok": False, "passed": [], "landing": [], "impact_requested": args.impact}
    native_requests = 0

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        report["passed"].append(name)
        print(name, file=sys.stderr, flush=True)

    def capture(state):
        return {"aircraft": {key: state["aircraft"][key] for key in
                ("simulation_time_s", "latitude_deg", "longitude_deg", "ellipsoid_height_m",
                 "pitch_deg", "speed_mps", "ground_speed_mps", "running")},
                **{key: state[key] for key in ("gear", "ground", "automation", "fault")}}

    def native_property(state, component, name):
        nonlocal native_requests
        native_requests += 1
        body = dict(objectPath=state["object_path"] + "." + component, propertyName=name, access="READ_ACCESS")
        request = Request("http://127.0.0.1:30010/remote/object/property", json.dumps(body).encode("utf-8"),
                          {"Content-Type": "application/json"}, method="PUT")
        with sim.opener.open(request, timeout=15) as response:
            return json.load(response)[name]

    def check_visual(state, label):
        fold = 90 * (1 - state["gear"]["extension"])
        components = {}
        for component, axis, sign in (("NoseGearHinge", "Pitch", -1), ("LeftMainGearHinge", "Roll", -1),
                                       ("RightMainGearHinge", "Roll", 1)):
            rotation = native_property(state, component, "RelativeRotation")
            hidden = native_property(state, component, "bHiddenInGame")
            components[component] = {"rotation": rotation, "hidden": hidden}
            expected = dict(Pitch=0, Yaw=0, Roll=0)
            expected[axis] = sign * fold
            if any(abs(rotation[key] - value) >= 1e-6 for key, value in expected.items()) or hidden != state["gear"]["up_locked"]:
                report[label + "_visual"] = components
                raise RuntimeError(f"{label}: {component} fold or visibility does not match extension")
        report[label + "_visual"] = components
        check(True, label + " native hinge rotations and visibility match physical gear extension")

    def advance(seconds):
        while seconds > 1e-9:
            dt = min(1, seconds)
            state = sim.step(dt)
            if state["fault"] or state["ground"]["crashed"]:
                raise RuntimeError(state["fault"] or "Aircraft crashed during gear cycle")
            seconds -= dt
        return state

    def rejected(call, status):
        try:
            call()
        except UAVSimError as error:
            return error.status == status
        return False

    def normal_environment():
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        sim.set_gust(east_mps=0, north_mps=0, up_mps=0, delay_s=0, duration_s=1)
        return sim.set_failures(engine_power_fraction=1, elevator_jammed=False,
                                aileron_jammed=False, rudder_jammed=False)

    try:
        sim.pause()
        normal_environment()
        sim.set_time_scale(1)
        ground = sim.reset_on_ground()
        check(ground["gear"] == {"command_down": True, "extension": 1,
              "down_locked": True, "up_locked": False, "moving": False},
              "ground initialization starts fully down and locked")
        check_visual(ground, "down")
        bad_bodies = [{"down": value} for value in (0, 1, "false", None, [])]
        bad_bodies += [{}, {"down": True, "extra": 1}]
        check(all(rejected(lambda body=body: sim._request("PUT", "gear", body), 400) for body in bad_bodies)
              and sim.get_state() == ground, "gear rejects non-booleans, missing and unknown fields without mutation")
        check(rejected(lambda: sim.set_landing_gear(False), 409) and sim.get_state() == ground,
              "ground retraction is rejected without changing state")

        initial = sim.reset()
        commanded = sim.set_landing_gear(False)
        paused = sim.get_state()
        report["paused_command"] = capture(paused)
        check(initial["gear"]["down_locked"] and paused == commanded
              and paused["aircraft"] == initial["aircraft"] and paused["gear"]["extension"] == 1
              and not paused["gear"]["command_down"] and not paused["aircraft"]["running"],
              "paused retract command changes target without moving gear or aircraft")
        flight_input = sim.set_controls(elevator_deg=0, aileron_deg=0, rudder_deg=0, throttle=.4)
        check(flight_input["gear"] == paused["gear"], "ordinary manual flight controls preserve gear command and extension")
        half = advance(1.5)
        report["half"] = capture(half)
        check(abs(half["gear"]["extension"] - .5) < 1e-9 and half["gear"]["moving"]
              and not half["gear"]["down_locked"] and not half["gear"]["up_locked"],
              "1.5 simulated seconds reaches half travel with neither lock set")
        check_visual(half, "half")
        up = advance(1.5)
        report["up"] = capture(up)
        check(up["gear"]["extension"] == 0 and up["gear"]["up_locked"] and not up["gear"]["moving"],
              "3 simulated seconds completes retraction and up lock")
        check_visual(up, "up")
        sim.set_landing_gear(True)
        down_half = advance(1.5)
        reverse = sim.set_landing_gear(False)
        check(abs(down_half["gear"]["extension"] - .5) < 1e-9
              and reverse["gear"]["extension"] == down_half["gear"]["extension"],
              "mid-travel reversal preserves current extension")
        reversed_up = advance(1.5)
        check(reversed_up["gear"]["up_locked"] and not reversed_up["gear"]["moving"],
              "reverse command returns to up lock after remaining half travel")
        sim.set_landing_gear()  # Python default is down=True.
        down = advance(3)
        check(down["gear"]["extension"] == 1 and down["gear"]["down_locked"] and not down["gear"]["moving"],
              "default Python command deploys fully in 3 simulated seconds")

        replay = []
        for factor in (.25, 20):
            sim.reset()
            sim.set_time_scale(factor)
            sim.set_landing_gear(False)
            state = advance(1.5)
            replay.append({"gear": state["gear"], "aircraft": {key: value for key, value in state["aircraft"].items()
                           if key not in ("time_scale", "effective_time_scale")}})
        check(replay[0] == replay[1], "explicit gear and flight steps replay identically at .25x and 20x")

        sim.set_time_scale(1)
        sim.prepare_landing_approach()
        sim.set_landing_gear(False)
        approach = advance(3)
        check(approach["gear"]["up_locked"], "landing test approach can start with retracted gear")
        armed = sim.start_automatic_landing()
        check(armed["gear"]["command_down"] and armed["gear"]["extension"] == approach["gear"]["extension"]
              and armed["aircraft"] == approach["aircraft"],
              "automatic landing arms gear deployment without teleporting or instant travel")
        check(rejected(lambda: sim.set_landing_gear(False), 409)
              and rejected(lambda: sim.set_landing_gear(True), 409) and sim.get_state() == armed,
              "active automatic landing rejects both manual gear commands without mutation")
        report["landing"].append(capture(armed))
        locked = False
        for _ in range(120):
            state = sim.step(1)
            if state["fault"] or state["ground"]["crashed"]:
                report["landing"].append(capture(state))
                raise RuntimeError(state["fault"] or "Automatic landing crashed")
            if state["gear"]["down_locked"] and not state["ground"]["on_ground"]:
                locked = True
            last = report["landing"][-1]
            if state["automation"]["phase"] != last["automation"]["phase"] or state["gear"] != last["gear"]:
                report["landing"].append(capture(state))
            if state["automation"]["phase"] == "complete":
                break
        check(locked and state["automation"]["phase"] == "complete" and state["automation"]["completed"]
              and state["gear"]["down_locked"] and state["ground"]["on_ground"]
              and state["ground"]["parking_brake"] and not state["aircraft"]["running"],
              "automatic landing deploys and locks before touchdown, then parks and pauses")

        if args.impact:
            ground = sim.reset_on_ground()
            a = ground["aircraft"]
            sim.set_initial_condition(latitude_deg=a["latitude_deg"], longitude_deg=a["longitude_deg"],
                # Allow more than the 3 s travel time before testing a fully stowed impact.
                ellipsoid_height_m=ground["ground"]["terrain_height_m"] + 80,
                heading_deg=a["heading_deg"], pitch_deg=-60, roll_deg=0, speed_mps=5,
                angle_of_attack_deg=0, sideslip_deg=0)
            sim.set_controls(elevator_deg=0, aileron_deg=0, rudder_deg=0, throttle=0)
            sim.set_landing_gear(False)
            state = advance(3)
            check(state["gear"]["up_locked"], "impact fixture retracts gear before approaching the surface")
            for _ in range(30):
                state = sim.step(.5)
                if state["ground"]["crashed"] or state["fault"]:
                    break
            report["impact"] = capture(state)
            check(state["gear"]["up_locked"] and state["ground"]["crashed"]
                  and "airframe strike" in state["fault"].lower() and not state["aircraft"]["running"],
                  "gear-up descent reaches an explicit body strike and paused crash")
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        try:
            sim.pause()
            normal_environment()
            sim.reset_on_ground()
            final = sim.set_time_scale(1)
            report["restored"] = capture(final)
            check(not final["fault"] and final["gear"]["down_locked"] and not final["gear"]["moving"]
                  and final["ground"]["on_ground"] and final["ground"]["parking_brake"]
                  and not final["aircraft"]["running"] and final["aircraft"]["time_scale"] == 1,
                  "finally restores gear down, parked ground start, pause and 1x")
        except Exception as error:
            report["ok"] = False
            report["restore_error"] = str(error)
    report["api_requests"] = sim.request_count
    report["native_property_requests"] = native_requests
    text = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
