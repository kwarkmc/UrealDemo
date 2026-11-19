"""Offline comparison of preserved navigation/physics probes and live round trips."""
import csv
import json
import math
import os
from pathlib import Path
import statistics

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"artifacts/navigation-physics"
os.environ.setdefault("MPLCONFIGDIR",str(ROOT/"artifacts/matplotlib-cache"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def rows(path):
    with path.open(encoding="utf-8-sig",newline="") as stream:
        return [{k:float(v) for k,v in r.items()} for r in csv.DictReader(stream)]


def main():
    summary={"probes":{},"roundtrips":{}};fig,axes=plt.subplots(3,2,figsize=(13,11),layout="constrained")
    for name in ("baseline","navigation-final","integrated"):
        data=rows(OUT/(name+".csv"));selected=[r for r in data if r["case"]==3 and r["time_s"]>120 and r["loiter"]==1 and abs(r["bank_deg"])>5]
        if not selected:raise ValueError("No settled turn records")
        geometric=[]
        series=[r for r in data if r["case"]==3]
        for prev,r,nxt in zip(series,series[1:],series[2:]):
            if r["time_s"]<=120 or not r["loiter"]:continue
            dt=(nxt["time_s"]-prev["time_s"])*.5
            vx=(nxt["x_m"]-prev["x_m"])/(2*dt);vy=(nxt["y_m"]-prev["y_m"])/(2*dt)
            ax=(nxt["x_m"]-2*r["x_m"]+prev["x_m"])/(dt*dt);ay=(nxt["y_m"]-2*r["y_m"]+prev["y_m"])/(dt*dt)
            acceleration=(vy*ax-vx*ay)/math.hypot(vx,vy)
            geometric.append((acceleration-r["radial_accel_mps2"])/r["radial_accel_mps2"])
        if max(map(abs,geometric))>.002:raise ValueError("Trajectory curvature and force projection disagree")
        result={"settled_samples":len(selected),"median_beta_deg":statistics.median(r["beta_deg"] for r in selected),
            "median_radius_residual_percent":statistics.median(100*(r["radius_m"]/r["ideal_radius_m"]-1) for r in selected),
            "geometric_vs_force_acceleration_max_relative":max(map(abs,geometric)),
            "median_side_radial_mps2":statistics.median(r["side_radial_mps2"] for r in selected),
            "capture_after_s":{str(case):next((r["time_s"] for r in data if r["case"]==case and r["loiter"]==1),None) for case in range(4)}}
        summary["probes"][name]=result
        lateral=[r for r in data if r["case"]==0]
        axes[0,0].plot([r["x_m"] for r in lateral],[r["y_m"] for r in lateral],label=name)
        axes[0,1].plot([r["time_s"] for r in series],[r["beta_deg"] for r in series],label=name)
        axes[1,0].plot([r["time_s"] for r in selected],[100*(r["radius_m"]/r["ideal_radius_m"]-1) for r in selected],label=name)
    for name in ("roundtrip","roundtrip-wind"):
        path=OUT/name/"result.json"
        if not path.exists():continue
        result=json.loads(path.read_text(encoding="utf-8"))
        if not result["ok"]:raise ValueError("Failed live round trip: "+name)
        state=result["final_state"];a=state["aircraft"];g=state["ground"]
        summary["roundtrips"][name]={"initializations":result["initializations"],"return_distance_m":result["return_requested_at"]["distance_m"],
            "return_height_wgs84_m":result["return_requested_at"]["aircraft"]["ellipsoid_height_m"],"parked_time_s":a["simulation_time_s"],
            "touchdown_normal_speed_mps":g["impact_speed_mps"],"parked_ground_speed_mps":a["ground_speed_mps"],
            "wheel_normal_force_n":g["wheel_normal_force_n"],"wheel_clearance_m":g["wheel_clearance_m"],"wind":result["environment"]}
    path=OUT/"roundtrip-wind/samples.csv"
    if path.exists():
        with path.open(encoding="utf-8-sig",newline="") as f:data=list(csv.DictReader(f))
        t=[float(r["time_s"]) for r in data]
        axes[1,1].plot(t,[float(r["propeller_rpm"]) for r in data],label="shaft RPM")
        axes[2,0].plot(t,[float(r["gear_extension"]) for r in data],label="gear extension")
        for wheel in ("nose","left","right"):
            tail=[r for r in data if float(r["time_s"])>t[-1]-20]
            axes[2,1].plot([float(r["time_s"])-t[-1] for r in tail],[float(r[wheel+"_normal_n"]) for r in tail],label=wheel)
    labels=[("East (m), lateral target 1 km","North (m)"),("Time (s), straight-in turn","Sideslip (deg)"),
        ("Time (s)","Radius vs ideal level-turn relation (%)"),("Simulation time (s)","Propeller speed (RPM)"),
        ("Simulation time (s)","Gear extension (0..1)"),("Time relative to parking (s)","Wheel reaction (N)")]
    for ax,(x,y) in zip(axes.flat,labels):ax.set(xlabel=x,ylabel=y);ax.grid(alpha=.25);ax.legend(fontsize=8)
    fig.suptitle("Navigation + physics research integration - real aircraft performance unvalidated")
    fig.savefig(OUT/"comparison.png",dpi=150);fig.savefig(OUT/"comparison.svg");plt.close(fig)
    (OUT/"summary.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
    print(json.dumps(summary,indent=2))


if __name__=="__main__":main()
