"""Live research-model check. Resets flight; restores selector bytes and wind in finally.

No UI automation. This checks software contracts, not real C208B performance.
"""
import json
import argparse
from pathlib import Path
import sys
from urllib.request import Request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from uavsim_client import UAVSimClient, UAVSimError


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=ROOT/"artifacts/c208b-api-check.json")
    args=parser.parse_args()
    sim = UAVSimClient()
    active = ROOT / "aircraft/active.json"
    prior = active.read_bytes()
    temporary = ROOT / "aircraft/runtime-c208b-check.json"
    prior_temporary = temporary.read_bytes() if temporary.exists() else None
    report = {"ok": False, "passed": [], "phases": [], "restore_errors": []}
    wind = None; sensor=None

    def check(value, name):
        if not value:
            raise AssertionError(name)
        report["passed"].append(name)

    def rejected(callback, status):
        try:
            callback()
        except UAVSimError as error:
            return error.status == status
        return False

    def select(name):
        active.write_text(json.dumps({"profile": name}) + "\n", encoding="utf-8")
        return sim.reload_aircraft_profile()

    def prop(state,component,name):
        request=Request("http://127.0.0.1:30010/remote/object/property",json.dumps({"objectPath":state["object_path"]+"."+component,
            "propertyName":name,"access":"READ_ACCESS"}).encode(),{"Content-Type":"application/json"},method="PUT")
        with sim.opener.open(request,timeout=15) as response:return json.load(response)[name]

    def fly_until(phase, seconds):
        state = sim.get_state()
        previous = None
        for _ in range(seconds):
            state = sim.step(1)
            actual = state["automation"]["phase"]
            if actual != previous:
                report["phases"].append({"phase": actual, "time_s": state["aircraft"]["simulation_time_s"],
                                         "agl_m": state["ground"]["height_agl_m"]})
                previous = actual
            check(not state["fault"] and not state["ground"]["crashed"], "live flight has no fault/crash")
            if actual == phase:
                return state
            if actual in ("aborted", "complete"):
                break
        raise AssertionError("Flight did not reach " + phase + ": " + json.dumps(state["automation"]))

    try:
        sim.pause()
        original = sim.get_state()
        sensor=original["eoir"]
        wind = {key: original["environment"][key] for key in ("wind_east_mps", "wind_north_mps", "wind_up_mps")}
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        state = select("c208b-research.json")
        check(state["airframe"]["id"] == "c208b-research-assumptions", "separate research identity and profile applied")
        check("UNVALIDATED" in state["airframe"]["support_message"], "research warning public")
        check(abs(state["aircraft"]["speed_mps"] - 65) < 1e-7, "research initializes 65 m/s")
        check(not state["airframe"]["retractable_gear"], "fixed gear public")
        for change in ("unknown", "boolean", "geometry", "wheel", "brake_type"):
            candidate = json.loads((ROOT / "aircraft/c208b-research.json").read_text(encoding="utf-8"))
            if change == "unknown":
                candidate["configuration"]["unknown"] = 1
            elif change == "boolean":
                candidate["configuration"]["retractable_gear"] = 0
            elif change == "geometry":
                candidate["configuration"]["cg_height_m"] = .1
            elif change == "wheel":candidate["configuration"]["wheel_radius_m"]=2
            else:candidate["configuration"]["brake_tau_s"]=False
            temporary.write_text(json.dumps(candidate), encoding="utf-8")
            active.write_text('{"profile":"runtime-c208b-check.json"}', encoding="utf-8")
            before_invalid = sim.get_state()
            check(rejected(sim.reload_aircraft_profile, 409), change + " configuration rejected")
            after_invalid = sim.get_state()
            check(after_invalid["aircraft"] == before_invalid["aircraft"]
                  and after_invalid["airframe"]["fingerprint"] == before_invalid["airframe"]["fingerprint"],
                  change + " configuration preserves applied state")
        state = select("c208b-research.json")
        before = state["aircraft"]
        check(rejected(lambda: sim.set_landing_gear(False), 409), "fixed gear retract rejected")
        check(sim.get_state()["aircraft"] == before, "rejected retraction preserves state")
        check(rejected(lambda: sim.set_flaps(31), 400), "flap range rejected")
        commanded = sim.set_flaps(20)
        check(commanded["flaps"] == {"command_deg": 20, "actual_deg": 0}, "flap target independent of paused actual")
        check(9.9 < sim.step(1)["flaps"]["actual_deg"] <= 10, "flaps advance with explicit simulation time")
        sim.reset()
        a = sim.get_state()["aircraft"]
        point = dict(latitude_deg=a["latitude_deg"], longitude_deg=a["longitude_deg"] + .03,
                     ellipsoid_height_m=a["ellipsoid_height_m"], speed_mps=65)
        check(sim.set_route([point], loiter_radius_m=1000)["navigation"]["waypoint_count"] == 1, "research route accepts 65 m/s")
        check(not sim.step(1)["fault"], "research route uses same physics")
        second = dict(point, latitude_deg=point["latitude_deg"] + .03)
        check(sim.set_route([point, second], loiter_radius_m=1000)["navigation"]["waypoint_count"] == 2, "multiple research waypoints share array API")
        sim.set_controls(elevator_deg=0, aileron_deg=0, rudder_deg=0, throttle=.5)
        check(sim.get_state()["navigation"]["waypoint_count"] == 0, "manual control cancels navigation")
        point["speed_mps"] = 19
        check(rejected(lambda: sim.set_route([point], loiter_radius_m=1000), 400), "research route rejects Thor speed")
        sim.set_failures(engine_power_fraction=0, elevator_jammed=True, aileron_jammed=False, rudder_jammed=False)
        sim.set_gust(east_mps=1, north_mps=1, up_mps=1)
        thor = select("thor.json")
        check(thor["airframe"]["schema_version"] == 1 and abs(thor["aircraft"]["speed_mps"] - 19) < 1e-7, "Thor resets to legacy trim")
        check(thor["airframe"]["retractable_gear"] and thor["airframe"]["maximum_flap_deg"] == 0, "Thor configuration isolated")
        check(thor["disturbance"]["engine_power_fraction"] == 1 and not thor["disturbance"]["elevator_jammed"], "fault state cleared across model reload")
        check(rejected(lambda: sim.set_flaps(10), 400), "Thor rejects research flap extension")
        select("c208b-research.json")
        parked = sim.reset_on_ground()
        check(parked["ground"]["on_ground"] and abs(parked["ground"]["height_agl_m"] - 1.7) < .01, "research ground reset uses own gear height")
        check(rejected(lambda: sim.start_automatic_landing(approach_speed_mps=40),409),"ground landing request rejected")
        denied=sim.get_state()
        check(denied["aircraft"]==parked["aircraft"] and "landing_not_ready" in denied["automation"]["rejection"],"rejection reason separate and physics preserved")
        sim.set_ground_controls(steering_deg=30,brake=1,parking_brake=False)
        response=sim.step(.1)["ground"]
        check(0<response["applied_steering_deg"]<=3.00001 and .39<response["applied_brake"]<.40,"live delayed ground outputs")
        parked=sim.reset_on_ground()
        hinge=prop(parked,"NoseGearHinge","RelativeLocation");wheel=prop(parked,"NoseWheel","RelativeLocation");scale=prop(parked,"NoseWheel","RelativeScale3D")
        check(abs(hinge["Z"]+wheel["Z"]-50*scale["Z"]+170)<1e-4,"rendered wheel bottom matches FRD contact point")
        check(rejected(lambda: sim.start_automatic_takeoff(speed_mps=19), 400), "research takeoff rejects Thor target")
        armed = sim.start_automatic_takeoff(climb_height_m=50, speed_mps=55)
        check(not armed["aircraft"]["running"], "arming preserves pause")
        check(not armed["automation"]["rejection"],"accepted takeoff clears old landing rejection")
        check(rejected(lambda: sim.set_flaps(10), 409), "manual flaps refused during automation")
        airborne = fly_until("hold", 160)
        check(airborne["gear"]["down_locked"], "automatic research takeoff keeps fixed gear down")
        sim.cancel_flight_operation()
        prepared = sim.prepare_landing_approach()
        check(abs(prepared["aircraft"]["speed_mps"] - 40) < 1e-7 and prepared["flaps"]["actual_deg"]==30,
              "landing test position uses configured research trim")
        check(abs(prop(prepared,"LeftFlapHinge","RelativeRotation")["Pitch"]-30)<1e-4,"visible flap follows actual output")
        sim.start_automatic_landing(approach_speed_mps=40)
        stopped = fly_until("complete", 150)
        check(stopped["ground"]["parking_brake"] and stopped["ground"]["on_ground"], "live landing parks")
        check(all(stopped["eoir"][key]==original["eoir"][key] for key in ("mode","enabled","gimbal_mode","fov_deg")),"main flight changes preserve EOIR settings")
        if sensor["gimbal_mode"]!="protect":
            sim.set_eoir(enabled=True,mode="ccd",fov_deg=sensor["fov_deg"])
            frame=sim.get_eoir_frame()
            check(len(frame)>1000 and frame.startswith(b"\xff\xd8"),"CCD JPEG capture remains available")
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        active.write_bytes(prior)
        if prior_temporary is None:
            temporary.unlink(missing_ok=True)
        else:
            temporary.write_bytes(prior_temporary)
        try:
            sim.pause()
            if wind is not None:
                sim.set_wind(**wind)
            if sensor is not None and sensor["gimbal_mode"]!="protect":
                sim.set_eoir(enabled=sensor["enabled"],mode=sensor["mode"],fov_deg=sensor["fov_deg"])
            sim.reload_aircraft_profile()
        except Exception as error:
            report["restore_errors"].append(str(error))
        report["ok"] = report["ok"] and not report["restore_errors"]
        report["passed_count"] = len(report["passed"])
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "passed"}, indent=2), flush=True)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
