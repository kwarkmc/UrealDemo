"""Live display-only checks and native Unreal captures; resets flight and leaves ground parked/paused."""
import importlib.util
import json
import math
from pathlib import Path
import shutil
import sys
import time
from urllib.request import Request

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from uavsim_client import UAVSimClient


def main():
    out=ROOT/"artifacts/propeller-visual";out.mkdir(parents=True,exist_ok=True)
    sim=UAVSimClient(timeout=30);report={"ok":False,"passed":[],"captures":[]}
    snapshot=ROOT/"artifacts/terrain-view.png"
    prior=snapshot.read_bytes() if snapshot.exists() else None
    def check(value,message):
        if not value:raise AssertionError(message)
        report["passed"].append(message)
    spec=importlib.util.spec_from_file_location("visual_server",ROOT/"scripts/serve-api.py")
    server=importlib.util.module_from_spec(spec);spec.loader.exec_module(server)
    native=server.Unreal(30010)
    def prop(component,name):
        body={"objectPath":native.object_path()+"."+component,"propertyName":name,"access":"READ_ACCESS"}
        request=Request("http://127.0.0.1:30010/remote/object/property",json.dumps(body).encode(),{"Content-Type":"application/json"},method="PUT")
        with sim.opener.open(request,timeout=15) as response:return json.load(response)[name]
    def camera(x,y,z):
        path=native.object_path()+".ChaseCamera"
        values={"RelativeLocation":{"X":x,"Y":y,"Z":z},"RelativeRotation":{"Pitch":math.degrees(math.atan2(-z,math.hypot(x,y))),
            "Yaw":math.degrees(math.atan2(-y,-x)),"Roll":0}}
        for name,value in values.items():
            body={"objectPath":path,"propertyName":name,"access":"WRITE_ACCESS","propertyValue":{name:value}}
            request=Request("http://127.0.0.1:30010/remote/object/property",json.dumps(body).encode(),{"Content-Type":"application/json"},method="PUT")
            with sim.opener.open(request,timeout=15) as response:response.read()
    def capture(name):
        before=snapshot.stat().st_mtime_ns if snapshot.exists() else 0
        check(native.call(native.object_path(),"CaptureView") is True,"capture accepted: "+name)
        deadline=time.monotonic()+20
        while not snapshot.exists() or snapshot.stat().st_mtime_ns==before:
            if time.monotonic()>deadline:raise RuntimeError("Viewport capture timed out: "+name)
            time.sleep(.05)
        shutil.copyfile(snapshot,out/(name+".png"));report["captures"].append(name+".png")
    try:
        sim.pause();state=sim.reset_on_ground()
        check(state["airframe"]["id"] in ("c208b-research-assumptions","c208b-retractable-research-assumptions"),"C208B research profile selected")
        check(state["visual"]["source"]=="builtin","builtin model selected")
        sim.set_ground_controls(steering_deg=0,brake=1,parking_brake=True)
        for label in ("LeftMain","RightMain"):
            center=prop(label+"WheelPivotStrut","RelativeLocation")
            angle=prop(label+"WheelPivotStrut","RelativeRotation")
            scale=prop(label+"WheelPivotStrut","RelativeScale3D")
            hinge=prop(label+"GearHinge","RelativeLocation")
            # The cube's long local Z axis connects the fuselage to the wheel center.
            roll=math.radians(angle["Roll"]);half=scale["Z"]*50
            root_y=hinge["Y"]+center["Y"]-math.sin(roll)*half
            root_z=hinge["Z"]+center["Z"]-math.cos(roll)*half
            end_y=hinge["Y"]+center["Y"]+math.sin(roll)*half
            end_z=hinge["Z"]+center["Z"]+math.cos(roll)*half
            check(abs(root_y)<65 and abs(root_z)<65,"main strut starts inside fuselage: "+label)
            check(abs(abs(end_y)-180)<.01 and abs(end_z+138)<.01,"main strut ends at physical wheel center: "+label)
        camera(2000,-1700,650);capture("stopped-front")
        camera(0,-2400,450);capture("gear-side")
        sim.set_controls(**(state["controls"]|{"throttle":.08}))
        state=sim.step(.1 if state["airframe"].get("dynamic_propeller") else 1);camera(2000,-1700,650)
        check(prop("PropellerBlades","bVisible") and not prop("PropellerBlur","bVisible"),"low speed shows shaped blades")
        capture("low-speed")
        sim.set_controls(**(state["controls"]|{"throttle":1}))
        for _ in range(3):state=sim.step(1)
        camera(2000,-1700,650)
        check(not prop("PropellerBlades","bVisible") and prop("PropellerBlur","bVisible"),"high speed replaces aliased blades with blur disk")
        capture("high-speed")
        sim.reset();state=sim.step(.1);camera(2000,-1700,650);capture("airborne")
        phase_before=prop("Propeller","RelativeRotation")
        for _ in range(10):state=sim.step(.01)
        phase_after=prop("Propeller","RelativeRotation")
        check(phase_before!=phase_after and not state["fault"],"airborne rotation follows simulation time")
        camera(2000,-1700,650);capture("airborne-next")
        report["ok"]=True
    except Exception as error:report["error"]=str(error)
    finally:
        try:
            sim.pause();sim.reset_on_ground();sim.set_ground_controls(steering_deg=0,brake=1,parking_brake=True);sim.set_view("aircraft")
        except Exception as error:report["restore_error"]=str(error);report["ok"]=False
        if prior is not None:snapshot.write_bytes(prior)
        elif snapshot.exists():snapshot.unlink()
        (out/"result.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps(report,indent=2));return 0 if report["ok"] else 1


if __name__=="__main__":raise SystemExit(main())
