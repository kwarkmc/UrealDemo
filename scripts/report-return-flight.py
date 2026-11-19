"""Summarize saved continuous-return development runs; never contacts Unreal."""
import argparse
import csv
import json
import math
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folders",nargs="+",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    summary={};fig,axes=plt.subplots(3,2,figsize=(13,11),layout="constrained")
    for folder in args.folders:
        result=json.loads((folder/"result.json").read_text(encoding="utf-8"))
        if not result["ok"]:raise ValueError("Cannot label failed run as successful: "+str(folder))
        with (folder/"samples.csv").open(encoding="utf-8-sig",newline="") as stream:
            rows=[{key:value if key=="phase" else float(value) for key,value in row.items()} for row in csv.DictReader(stream)]
        if not rows or any(b["time_s"]<=a["time_s"] for a,b in zip(rows,rows[1:])):raise ValueError("Invalid sample time grid")
        if any(not math.isfinite(value) for row in rows for value in row.values() if isinstance(value,float)):raise ValueError("Nonfinite record")
        final=[row for row in rows if row["phase"]=="return:final"]
        if not final:raise ValueError("Final approach samples missing")
        signs=[math.copysign(1,row["aileron_command_deg"]) for row in final if abs(row["aileron_command_deg"])>1]
        ground=result["final_state"]["ground"];a=result["final_state"]["aircraft"]
        summary[folder.name]={"return_distance_m":result["return_requested_at"]["distance_m"],
            "return_height_wgs84_m":result["return_requested_at"]["aircraft"]["ellipsoid_height_m"],
            "completed_time_s":a["simulation_time_s"],"distance_travelled_m":a["distance_travelled_m"],
            "final_max_abs_bank_deg":max(abs(r["roll_deg"]) for r in final),
            "final_max_abs_cross_track_m":max(abs(r["cross_track_m"]) for r in final),
            "final_aileron_reversals_above_1deg":sum(x!=y for x,y in zip(signs,signs[1:])),
            "final_aileron_saturation_samples":sum(abs(r["aileron_command_deg"])>=24.9 for r in final),
            "touchdown_normal_speed_mps":ground["impact_speed_mps"],"parked_ground_speed_mps":a["ground_speed_mps"],
            "parked_reaction_n":ground["normal_force_n"],"initializations":result["initializations"],
            "go_arounds":sum(r["phase"]=="return:go_around" for r in result["phases"])}
        name=folder.name.removeprefix("return-flight-")
        t=[r["time_s"] for r in rows]
        axes[0,0].plot([r["longitude_deg"] for r in rows],[r["latitude_deg"] for r in rows],label=name)
        axes[0,1].plot(t,[r["height_m"] for r in rows],label=name)
        axes[1,0].plot(t,[r["speed_mps"] for r in rows],label=name)
        axes[1,1].plot(t,[r["flap_actual_deg"] for r in rows],label=name)
        offset=final[0]["time_s"];ft=[r["time_s"]-offset for r in final]
        axes[2,0].plot(ft,[r["roll_deg"] for r in final],label=name)
        axes[2,1].plot(ft,[r["aileron_command_deg"] for r in final],label=name)
    labels=[("Longitude (deg)","Latitude (deg)"),("Simulation time (s)","WGS84 height (m)"),
            ("Simulation time (s)","TAS (m/s)"),("Simulation time (s)","Actual flap (deg)"),
            ("Time since final entry (s)","Actual bank (deg)"),("Time since final entry (s)","Aileron command (deg)")]
    for ax,(xlabel,ylabel) in zip(axes.flat,labels):
        ax.set(xlabel=xlabel,ylabel=ylabel);ax.grid(alpha=.25);ax.legend(fontsize=8)
    fig.suptitle("Continuous real-terrain return checks - research model, performance unvalidated")
    fig.savefig(args.output/"comparison.png",dpi=150);plt.close(fig)
    (args.output/"summary.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
    print(json.dumps(summary,indent=2))


if __name__=="__main__":main()
