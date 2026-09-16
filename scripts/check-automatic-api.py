"""Live Unreal regression for automatic flight, actuators, gusts and profiles.

Requires the nominal Thor profile, configured first runway, running API and engine.
Uses paused explicit steps; does not start/restart processes or retry writes.
Temporarily changes aircraft/active.json and aircraft/runtime-check.json, preserving
their bytes in finally. Restores the original profile, calm air, cleared failures,
paused ground start, parking brake and 1x. Do not edit these files during the run.
This is a development check, not aircraft qualification.
"""
import argparse
import json
import math
from pathlib import Path
import sys

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
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/automatic-api-check.json")
    args = parser.parse_args()
    sim = CheckClient()
    report = {"ok": False, "passed": [], "operations": {}}
    active = ROOT / "aircraft/active.json"
    temporary = ROOT / "aircraft/runtime-check.json"
    original_active = active.read_bytes()
    original_temporary = temporary.read_bytes() if temporary.exists() else None
    original_airframe = None

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        report["passed"].append(name)
        print(name, file=sys.stderr, flush=True)

    def controls(angle=0, throttle=0):
        return sim.set_controls(elevator_deg=angle, aileron_deg=angle, rudder_deg=angle, throttle=throttle)

    def failures(power=1, jam=False):
        return sim.set_failures(engine_power_fraction=power, elevator_jammed=jam,
                                aileron_jammed=jam, rudder_jammed=jam)

    def clear_gust():
        return sim.set_gust(east_mps=0, north_mps=0, up_mps=0, delay_s=0, duration_s=1)

    def capture(state):
        fields = ("simulation_time_s", "latitude_deg", "longitude_deg", "ellipsoid_height_m",
                  "heading_deg", "pitch_deg", "roll_deg", "speed_mps", "ground_speed_mps",
                  "vertical_speed_mps", "running", "distance_travelled_m")
        return {"aircraft": {key: state["aircraft"][key] for key in fields},
                **{key: state[key] for key in ("automation", "ground", "controls", "actuators", "fault")}}

    def rejected(call, status):
        try:
            call()
        except UAVSimError as error:
            return error.status == status
        return False

    def advance_operation(name, state, target, limit, runway):
        samples = report["operations"][name] = [capture(state)]
        touchdown = None
        for _ in range(limit):
            state = sim.step(1)
            op = state["automation"]
            if op["phase"] != samples[-1]["automation"]["phase"] or state["fault"] or state["ground"]["crashed"]:
                samples.append(capture(state))
                print(f"{name}: t={state['aircraft']['simulation_time_s']:.2f} {op['phase']}", file=sys.stderr, flush=True)
            if state["fault"] or state["ground"]["crashed"]:
                raise RuntimeError(f"{name}: {state['fault'] or 'crashed'}")
            if op["phase"] == "rollout" and touchdown is None:
                touchdown = {"along_track_m": op["along_track_m"], "cross_track_m": op["cross_track_m"],
                             "impact_speed_mps": state["ground"]["impact_speed_mps"]}
                report[name + "_touchdown"] = touchdown
                check(state["ground"]["on_ground"] and 0 <= op["along_track_m"] <= runway["length_m"]
                      and abs(op["cross_track_m"]) <= runway["width_m"] / 2,
                      name + " touchdown observation is inside runway length and width")
            if op["phase"] == target:
                if target == "complete":
                    check(touchdown is not None and op["completed"] and state["ground"]["on_ground"]
                          and state["ground"]["parking_brake"] and state["aircraft"]["ground_speed_mps"] < .2
                          and not state["aircraft"]["running"], name + " completes landed, parked and paused")
                return state
            if op["phase"] in ("aborted", "complete"):
                raise RuntimeError(f"{name}: unexpected terminal phase {op['phase']}")
        raise RuntimeError(f"{name}: did not reach {target} within {limit} simulated seconds")

    def restore_files():
        # Only these two explicit workspace files are changed; preserve pre-existing content.
        if original_temporary is None:
            temporary.unlink(missing_ok=True)
        else:
            temporary.write_bytes(original_temporary)
        active.write_bytes(original_active)

    try:
        sim.pause()
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        baseline = sim.reload_aircraft_profile()
        original_airframe = baseline["airframe"]
        profile = json.loads((ROOT / original_airframe["profile"]).read_text(encoding="utf-8-sig"))
        check(profile["parameters"]["engine_tau_s"] == .3 and profile["parameters"]["servo_rate_deg_s"] == 120,
              "nominal profile provides 0.3 s engine lag and 120 deg/s servo rate")
        manifest = ROOT / "maps" / baseline["terrain"]["manifest"]
        runway = json.loads((manifest.parent / "runways.json").read_text(encoding="utf-8-sig"))["runways"][0]
        report["profile"], report["runway"] = original_airframe, runway
        sim.set_time_scale(1)
        failures()
        clear_gust()

        sim.reset_on_ground()
        commanded = controls(throttle=1)
        spool = sim.step(.3)
        report["engine_response"] = {"expected": 1 - math.exp(-1), "state": capture(spool)}
        check(commanded["actuators"]["throttle"] == 0 and spool["controls"]["throttle"] == 1
              and abs(spool["actuators"]["throttle"] - (1 - math.exp(-1))) < 1e-9 and not spool["fault"],
              "engine output follows independent first-order step solution after 0.3 s")
        powerless = failures(power=0)
        check(powerless["actuators"]["throttle"] > .6 and powerless["actuators"]["thrust_n"] == 0,
              "engine output loss gives zero thrust without replacing spool telemetry")
        failures()

        sim.reset_on_ground()
        controls(angle=20)
        surface = sim.step(.05)
        angles = ("elevator_deg", "aileron_deg", "rudder_deg")
        check(all(0 < surface["actuators"][key] <= 6 + 1e-9 for key in angles) and not surface["fault"],
              "all surfaces obey 120 deg/s rate limit during 0.05 s command step")
        failures(jam=True)
        controls(angle=-10)
        jammed = sim.step(.2)
        report["jammed"] = capture(jammed)
        check(all(jammed["controls"][key] == -10 and jammed["actuators"][key] == surface["actuators"][key]
                  for key in angles) and not jammed["fault"],
              "surface jams retain captured angles while requested commands change")
        failures()

        replay = []
        for _ in range(2):
            sim.reset_on_ground()
            sim.set_gust(east_mps=2, north_mps=1, up_mps=.5, delay_s=.2, duration_s=1)
            peak = sim.step(.7)
            end = sim.step(.6)
            replay.append((peak, end))
        wind_keys = ("effective_wind_east_mps", "effective_wind_north_mps", "effective_wind_up_mps")
        check(all(abs(replay[0][0]["disturbance"][key] - expected) < 1e-12
                  for key, expected in zip(wind_keys, (2, 1, .5)))
              and all(replay[0][1]["disturbance"][key] == 0 for key in wind_keys),
              "delayed one-cosine gust reaches its peak and returns to steady wind after 1.2 s")
        check(replay[0] == replay[1] and not replay[0][1]["fault"],
              "reset and repeated explicit inputs replay gust and aircraft state exactly")
        report["gust"] = {"peak": replay[0][0]["disturbance"], "end": replay[0][1]["disturbance"]}
        clear_gust()

        # Invalid and corrected versions use the same temporary file and full parameter set.
        before = sim.reset_on_ground()
        variant = dict(profile, id="runtime-check", parameters=dict(profile["parameters"], mass_kg=-1))
        temporary.write_text(json.dumps(variant, indent=2) + "\n", encoding="utf-8")
        active.write_text('{"profile":"runtime-check.json"}\n', encoding="utf-8")
        check(rejected(sim.reload_aircraft_profile, 409), "negative mass profile reload is rejected")
        invalid = sim.get_state()
        unchanged = dict(invalid, airframe=dict(invalid["airframe"], error=before["airframe"]["error"]))
        check(bool(invalid["airframe"]["error"]) and unchanged == before,
              "invalid profile preserves active identity, fingerprint and complete physical state")
        variant["parameters"]["mass_kg"] = profile["parameters"]["mass_kg"]
        variant["parameters"]["engine_tau_s"] = .6
        temporary.write_text(json.dumps(variant, indent=2) + "\n", encoding="utf-8")
        loaded = sim.reload_aircraft_profile()
        check(loaded["airframe"]["id"] == "runtime-check" and not loaded["airframe"]["error"]
              and loaded["airframe"]["fingerprint"] != before["airframe"]["fingerprint"]
              and not loaded["aircraft"]["running"], "corrected profile loads with new identity and fingerprint")
        sim.reset_on_ground()
        controls(throttle=1)
        changed = sim.step(.3)
        report["profile_response"] = {"profile": loaded["airframe"], "expected": 1 - math.exp(-.5),
                                      "actual_throttle": changed["actuators"]["throttle"]}
        check(abs(changed["actuators"]["throttle"] - (1 - math.exp(-.5))) < 1e-9 and not changed["fault"],
              "loaded 0.6 s engine constant changes response to the independent analytic solution")
        restore_files()
        restored_profile = sim.reload_aircraft_profile()
        check(restored_profile["airframe"] == original_airframe, "original profile bytes and active identity are restored")

        before = sim.reset_on_ground()
        check(rejected(lambda: sim.start_automatic_takeoff(runway_index=-1), 400)
              and rejected(lambda: sim.start_automatic_takeoff(runway_index=True), 400)
              and rejected(lambda: sim.start_automatic_takeoff(climb_height_m=151), 400)
              and sim.get_state() == before, "invalid runway index and climb height are rejected without state changes")
        check(rejected(sim.start_automatic_landing, 409) and sim.get_state() == before,
              "automatic landing rejects a grounded aircraft without state changes")
        armed = sim.start_automatic_takeoff()
        check(armed["aircraft"] == before["aircraft"] and not armed["aircraft"]["running"]
              and armed["automation"]["phase"] == "takeoff_roll", "takeoff arm preserves paused physical state without teleporting")
        manual = controls()
        check(manual["automation"]["kind"] == "none" and manual["automation"]["phase"] == "idle",
              "manual flight controls cancel automatic operation")

        takeoff = advance_operation("takeoff", sim.start_automatic_takeoff(), "hold", 120, runway)
        check(takeoff["automation"]["completed"] and not takeoff["ground"]["on_ground"]
              and takeoff["aircraft"]["distance_travelled_m"] > 50,
              "automatic takeoff reaches airborne completion through physical time steps")
        landing_arm = sim.start_automatic_landing()
        check(landing_arm["aircraft"] == takeoff["aircraft"] and not landing_arm["aircraft"]["running"],
              "landing arm preserves airborne physical state without teleporting")
        advance_operation("takeoff_to_landing", landing_arm, "complete", 600, runway)

        prepared = sim.prepare_landing_approach()
        check(not prepared["aircraft"]["running"] and abs(prepared["aircraft"]["speed_mps"] - 18) < 1e-8
              and not prepared["ground"]["on_ground"] and not prepared["fault"],
              "explicit landing preparation initializes paused airborne 18 m/s test state")
        final_arm = sim.start_automatic_landing()
        check(abs(final_arm["automation"]["along_track_m"] + 600) < .02
              and abs(final_arm["automation"]["cross_track_m"]) < .02,
              "prepared landing is 600 m before the selected runway threshold on its centerline")
        report["prepared_approach"] = capture(final_arm)
        advance_operation("prepared_landing", final_arm, "complete", 600, runway)

        sim.reset_on_ground()
        sim.start_automatic_takeoff()
        sim.step(1)
        powerless = failures(power=0)
        aborted = advance_operation("rejected_takeoff", powerless, "aborted", 10, runway)
        check(aborted["ground"]["on_ground"] and aborted["ground"]["parking_brake"]
              and aborted["aircraft"]["ground_speed_mps"] < .2 and not aborted["aircraft"]["running"],
              "early engine failure rejects takeoff and stops on the ground")
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        try:
            restore_files()
            check(active.read_bytes() == original_active
                  and (temporary.read_bytes() if temporary.exists() else None) == original_temporary,
                  "finally restores original selector and temporary-file bytes")
        except Exception as error:
            report["ok"] = False
            report["file_restore_error"] = str(error)
        try:
            sim.pause()
            sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
            profile_state = sim.reload_aircraft_profile()
            failures()
            clear_gust()
            sim.reset_on_ground()
            final = sim.set_time_scale(1)
            report["restored"] = capture(final)
            check(not final["fault"] and not final["aircraft"]["running"] and final["aircraft"]["time_scale"] == 1
                  and final["ground"]["parking_brake"] and final["ground"]["on_ground"]
                  and final["controls"]["throttle"] == 0 and final["actuators"]["throttle"] == 0
                  and (original_airframe is None or profile_state["airframe"] == original_airframe),
                  "finally restores original profile, paused ground start, parking brake and 1x")
        except Exception as error:
            report["ok"] = False
            report["restore_error"] = str(error)
    report["api_requests"] = sim.request_count
    text = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
