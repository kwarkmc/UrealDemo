"""Continuous real-terrain departure/mission/return/parking development check.

Only one ground initialization at the beginning; no pose/reset/prepare command in flight.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from uavsim_client import UAVSimClient


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--distance-km",type=float,default=10)
    parser.add_argument("--height-m",type=float,default=500,help="Mission height above departure CG, metres")
    parser.add_argument("--waypoint-km",type=float,default=30,help="Distant mission endpoint; return is commanded before capture")
    parser.add_argument("--wind-east",type=float,default=0)
    parser.add_argument("--no-capture",action="store_true",help="Flight-only check; no viewport capture requests")
    args=parser.parse_args()
    if not 5<=args.distance_km<=20 or not 100<=args.height_m<=2600 or not math.isfinite(args.wind_east) or abs(args.wind_east)>1:
        parser.error("Development cases require distance 5..20 km, height 100..2600 m, east wind +/-1 m/s")
    if not args.distance_km+2<=args.waypoint_km<=35:parser.error("Mission endpoint must be at least 2 km beyond return trigger and at most 35 km")
    args.output.mkdir(parents=True,exist_ok=False)
    sim=UAVSimClient(timeout=60)
    report={"ok":False,"phases":[],"initializations":0,"restore_errors":[]}
    report["viewport_capture_enabled"]=not args.no_capture
    snapshot=ROOT/"artifacts/terrain-view.png"
    original_image=snapshot.read_bytes() if snapshot.exists() else None
    initial_sensor=None;initial_wind=None
    start_wall=time.perf_counter()
    fields=("time_s","phase","latitude_deg","longitude_deg","height_m","speed_mps","roll_deg","bank_target_deg",
            "course_target_deg","speed_target_mps","climb_target_mps","aileron_command_deg","aileron_actual_deg",
            "flap_actual_deg","cross_track_m","agl_m","normal_force_n","gear_extension","rudder_actual_deg",
            "propeller_rpm","shaft_drive_power_w","shaft_load_power_w",
            "nose_normal_n","left_normal_n","right_normal_n","nose_slip_mps","left_slip_mps","right_slip_mps")
    try:
        spec=importlib.util.spec_from_file_location("return_server",ROOT/"scripts/serve-api.py")
        server=importlib.util.module_from_spec(spec);spec.loader.exec_module(server)
        native=server.Unreal(30010)
        def capture(name):
            if args.no_capture:return
            previous=snapshot.stat().st_mtime_ns if snapshot.exists() else 0
            if native.call(native.object_path(),"CaptureView") is not True:
                raise RuntimeError("Scene capture request rejected")
            deadline=time.monotonic()+60
            while not snapshot.exists() or snapshot.stat().st_mtime_ns==previous:
                if time.monotonic()>deadline:raise RuntimeError("Scene capture timed out")
                time.sleep(.05)
            shutil.copyfile(snapshot,args.output/(name+".png"))
            if initial_sensor["enabled"]:
                (args.output/(name+"-ccd.jpg")).write_bytes(sim.get_eoir_frame())
        sim.pause()
        before=sim.get_state();initial_sensor=before["eoir"]
        initial_wind=before["environment"]
        sim.set_wind(wind_east_mps=args.wind_east,wind_north_mps=0,wind_up_mps=0)
        report["initial_airframe"]=before["airframe"]
        report["environment"]=sim.get_state()["environment"]
        report["case"]={"distance_km":args.distance_km,"height_above_departure_m":args.height_m}
        shutil.copyfile(ROOT/before["airframe"]["profile"],args.output/"profile.json")
        report["source_sha256"]={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [*sorted((ROOT/"Source/FlightCore").glob("*.h")),ROOT/"Binaries/Win64/UnrealEditor-UAVSim.dll"]}
        if before["airframe"]["id"] not in ("c208b-research-assumptions","c208b-retractable-research-assumptions"):raise RuntimeError("C208B research profile required")
        state=sim.reset_on_ground();report["initializations"]+=1
        start=state["aircraft"]
        heading=math.radians(start["heading_deg"])
        latitude=math.radians(start["latitude_deg"]);height=start["ellipsoid_height_m"]
        e2=6.69437999014e-3;n=6378137/math.sqrt(1-e2*math.sin(latitude)**2)
        m=6378137*(1-e2)/(1-e2*math.sin(latitude)**2)**1.5
        mission_height=height+args.height_m
        waypoint={"latitude_deg":start["latitude_deg"]+math.degrees(args.waypoint_km*1000*math.cos(heading)/(m+height)),
                  "longitude_deg":start["longitude_deg"]+math.degrees(args.waypoint_km*1000*math.sin(heading)/((n+height)*math.cos(latitude))),
                  "ellipsoid_height_m":mission_height,"speed_mps":65}
        report["mission_waypoint"]=waypoint
        sim.start_automatic_takeoff(climb_height_m=80,speed_mps=55)
        stage="departure";last_phase=None
        with (args.output/"samples.csv").open("w",newline="",encoding="utf-8") as stream:
            writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader()
            for step in range(4500):
                state=sim.step(1)
                a=state["aircraft"];op=state["automation"];ground=state["ground"]
                if state["fault"] or ground["crashed"]:raise RuntimeError(state["fault"] or "Aircraft crashed")
                phase=stage+":"+op["phase"]
                if phase!=last_phase:
                    entry={"phase":phase,"time_s":a["simulation_time_s"],"height_m":a["ellipsoid_height_m"],"speed_mps":a["speed_mps"]}
                    report["phases"].append(entry);print(entry,flush=True);stream.flush();last_phase=phase
                    if phase.endswith(("hold","aligning","flare","complete")):capture(phase.replace(":","-"))
                writer.writerow(dict(time_s=a["simulation_time_s"],phase=phase,latitude_deg=a["latitude_deg"],longitude_deg=a["longitude_deg"],
                    height_m=a["ellipsoid_height_m"],speed_mps=a["speed_mps"],roll_deg=a["roll_deg"],bank_target_deg=op["desired_bank_deg"],
                    course_target_deg=op["desired_course_deg"],speed_target_mps=op["desired_speed_mps"],climb_target_mps=op["desired_climb_mps"],
                    aileron_command_deg=state["controls"]["aileron_deg"],aileron_actual_deg=state["actuators"]["aileron_deg"],
                    flap_actual_deg=state["flaps"]["actual_deg"],cross_track_m=op["cross_track_m"],agl_m=ground["height_agl_m"],normal_force_n=ground["normal_force_n"],
                    gear_extension=state["gear"]["extension"],rudder_actual_deg=state["actuators"]["rudder_deg"],
                    **{k:state["actuators"][k] for k in ("propeller_rpm","shaft_drive_power_w","shaft_load_power_w")},
                    **{f"{wheel}_normal_n":ground["wheel_normal_force_n"][i] for i,wheel in enumerate(("nose","left","right"))},
                    **{f"{wheel}_slip_mps":ground["wheel_lateral_slip_mps"][i] for i,wheel in enumerate(("nose","left","right"))}))
                if stage=="departure" and op["phase"]=="hold":
                    sim.set_route([waypoint],loiter_radius_m=1200)
                    stage="mission"
                elif stage=="mission":
                    east=math.radians(a["longitude_deg"]-start["longitude_deg"])*(n+height)*math.cos(latitude)
                    north=math.radians(a["latitude_deg"]-start["latitude_deg"])*(m+height)
                    if math.hypot(east,north)>=args.distance_km*1000 and a["ellipsoid_height_m"]>=mission_height-30:
                        report["return_requested_at"]={"distance_m":math.hypot(east,north),"aircraft":a}
                        armed=sim.start_automatic_landing(approach_speed_mps=40)
                        report["accepted_return"]=armed["automation"]
                        stage="return";capture("return-armed")
                elif stage=="return" and op["phase"]=="complete":
                    if not ground["on_ground"] or not ground["parking_brake"]:raise RuntimeError("Completion without parked contact")
                    report["final_state"]=state;report["ok"]=True;break
                if op["phase"]=="aborted":raise RuntimeError(op["message"])
            else:raise RuntimeError("Continuous round trip did not finish within test duration")
    except Exception as error:
        report["error"]=f"{type(error).__name__}: {error}"
        try:report["last_state"]=sim.get_state()
        except Exception as second:report["observation_error"]=str(second)
    finally:
        try:sim.pause()
        except Exception as error:report["restore_errors"].append(str(error))
        if initial_wind is not None:
            try:sim.set_wind(**initial_wind)
            except Exception as error:report["restore_errors"].append(str(error))
        if original_image is not None:snapshot.write_bytes(original_image)
        elif snapshot.exists():snapshot.unlink()
        report["elapsed_wall_s"]=time.perf_counter()-start_wall
        report["ok"]=report["ok"] and not report["restore_errors"]
        (args.output/"result.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({key:report.get(key) for key in ("ok","error","elapsed_wall_s","restore_errors")}),flush=True)
    return 0 if report["ok"] else 1


if __name__=="__main__":raise SystemExit(main())
