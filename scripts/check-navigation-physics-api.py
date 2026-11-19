"""Live integrated navigation/physics contracts. Resets flight; restores files/wind, parks paused."""
import json
import math
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from uavsim_client import UAVSimClient,UAVSimError


def main():
    sim=UAVSimClient(timeout=60);out=ROOT/"artifacts/navigation-physics/api-check.json"
    active=ROOT/"aircraft/active.json";temporary=ROOT/"aircraft/runtime-navigation-physics.json"
    backups={p:p.read_bytes() if p.exists() else None for p in (active,temporary)}
    report={"ok":False,"passed":[],"navigation":[],"restore_errors":[]};wind=None;sensor=None
    def check(value,name):
        if not value:raise AssertionError(name)
        report["passed"].append(name);print(name,flush=True)
    try:
        sim.pause();initial=sim.get_state();wind=initial["environment"];sensor=initial["eoir"]
        check(initial["airframe"]["dynamic_propeller"],"dynamic research propulsion publicly enabled")
        sim.set_wind(wind_east_mps=0,wind_north_mps=0,wind_up_mps=0)
        ground=sim.reset_on_ground();ground=sim.step(.1)["ground"]
        check(ground["wheel_surfaces_sampled"] and len(ground["wheel_normal_force_n"])==3,"three wheel surfaces sampled")
        check(abs(sum(ground["wheel_normal_force_n"])-ground["normal_force_n"])<1e-6
            and all(n>=0 for n in ground["wheel_normal_force_n"]),"per-wheel reaction sum and unilateral load")
        check(min(ground["wheel_clearance_m"])>-.01,"parked wheel penetration bounded")
        report["parked_ground"]=ground
        trim=sim.reset();act=trim["actuators"]
        check(act["propeller_rpm"]>0 and abs(act["shaft_drive_torque_nm"]-act["propeller_load_torque_nm"])<1e-6,"live shaft equilibrium")
        cut=sim.set_failures(engine_power_fraction=0,elevator_jammed=False,aileron_jammed=False,rudder_jammed=False)
        check(cut["actuators"]["shaft_drive_power_w"]==0 and cut["actuators"]["thrust_n"]>0,"drive loss with coasting thrust")
        after=sim.step(.2)
        check(after["actuators"]["propeller_rpm"]<act["propeller_rpm"] and not after["fault"],"live rotor rundown")
        report["engine_cut"]={"initial":act,"after":after["actuators"]}
        for key,value in (("propeller_spin_direction",0),("propeller_inertia_kg_m2",False)):
            candidate=json.loads((ROOT/initial["airframe"]["profile"]).read_text())
            candidate["configuration"][key]=value;temporary.write_text(json.dumps(candidate))
            active.write_text(json.dumps({"profile":temporary.name}))
            before=sim.get_state()
            try:sim.reload_aircraft_profile();rejected=False
            except UAVSimError as error:rejected=error.status==409
            state=sim.get_state()
            check(rejected and state["aircraft"]==before["aircraft"] and state["airframe"]["fingerprint"]==before["airframe"]["fingerprint"],key+" invalid input preserves model and flight")
        active.write_bytes(backups[active]);sim.reload_aircraft_profile()
        for which in ("east","west","reverse","multi"):
            state=sim.reset();a=state["aircraft"];lat=math.radians(a["latitude_deg"])
            n=6378137/math.sqrt(1-6.69437999014e-3*math.sin(lat)**2)
            m=6378137*(1-6.69437999014e-3)/(1-6.69437999014e-3*math.sin(lat)**2)**1.5
            def waypoint(east,north):return {"latitude_deg":a["latitude_deg"]+math.degrees(north/(m+a["ellipsoid_height_m"])),
                "longitude_deg":a["longitude_deg"]+math.degrees(east/((n+a["ellipsoid_height_m"])*math.cos(lat))),
                "ellipsoid_height_m":a["ellipsoid_height_m"],"speed_mps":65}
            points=[waypoint(1000,0),waypoint(1000,4000),waypoint(1000,7000)] if which=="multi" else [waypoint(1000 if which=="east" else -1000 if which=="west" else 0,-1000 if which=="reverse" else 0)]
            sim.set_route(points,loiter_radius_m=1000)
            entry=None;reacquired=False;max_beta=0
            for t in range(800):
                state=sim.step(1)
                if state["fault"] or state["ground"]["crashed"]:raise RuntimeError(state["fault"] or "crash")
                reacquired=reacquired or state["navigation"]["reacquiring"]
                if state["navigation"]["mode"]=="loiter" and entry is None:entry=t
                if entry is not None and t>entry+60:max_beta=max(max_beta,abs(state["aircraft"]["sideslip_deg"]))
                if entry is not None and t>=entry+90:break
            check(entry is not None and state["navigation"]["active_waypoint_index"]==len(points)-1,which+" route reaches gated final orbit")
            check(max_beta<1,which+" coordinated orbit sideslip below 1 degree development threshold")
            if which=="multi":check(reacquired,"missed intermediate waypoint reentry is public")
            report["navigation"].append({"case":which,"entry_after_s":entry+1,"settled_max_beta_deg":max_beta,"reacquiring_observed":reacquired,"final":state["aircraft"]})
        check(all(state["eoir"][k]==sensor[k] for k in ("enabled","mode","gimbal_mode","fov_deg")),"EOIR settings preserved")
        report["ok"]=True
    except Exception as error:report["error"]=f"{type(error).__name__}: {error}"
    finally:
        for path,data in backups.items():
            if data is None:path.unlink(missing_ok=True)
            else:path.write_bytes(data)
        try:
            sim.pause()
            if wind is not None:sim.set_wind(**wind)
            sim.reload_aircraft_profile();sim.reset_on_ground()
        except Exception as error:report["restore_errors"].append(str(error))
        report["ok"]=report["ok"] and not report["restore_errors"]
        out.write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({"ok":report["ok"],"passed":len(report["passed"]),"error":report.get("error"),"restore_errors":report["restore_errors"]}));return 0 if report["ok"] else 1

if __name__=="__main__":raise SystemExit(main())
