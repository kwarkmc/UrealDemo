"""Offline scientific plots and comparison for UAVSim flight-test records."""
import argparse
import csv
import html
import hashlib
import json
import math
import os
import statistics
from pathlib import Path


SIGNALS = ("speed_mps", "ellipsoid_height_m", "pitch_deg", "angle_of_attack_deg",
           "roll_deg", "sideslip_deg", "roll_rate_deg_s", "pitch_rate_deg_s", "yaw_rate_deg_s")


def read_run(folder):
    folder = Path(folder)
    metadata = json.loads((folder / "metadata.json").read_text(encoding="utf-8"))
    result = json.loads((folder / "result.json").read_text(encoding="utf-8"))
    with (folder / "samples.csv").open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required = {"time_s", *SIGNALS, "command_elevator_deg", "actual_elevator_deg", "actual_throttle", "thrust_n"}
        if not required <= set(reader.fieldnames or ()):
            raise ValueError("Missing required time-series columns")
        rows = []
        for raw in reader:
            row = {key: value if key in ("navigation_mode", "operation_phase", "fault") else float(value)
                   for key, value in raw.items()}
            if any(not math.isfinite(value) for value in row.values() if isinstance(value, float)):
                raise ValueError("Nonfinite time-series value")
            if rows and row["time_s"] <= rows[-1]["time_s"]:
                raise ValueError("Simulation time must increase strictly")
            rows.append(row)
    if not rows:
        raise ValueError("Empty time series")
    if result["ok"]:
        expected = round(metadata["duration_s"] / metadata["sample_dt_s"]) + 1
        if len(rows) != expected:
            raise ValueError("Completed record has missing samples")
        for index, row in enumerate(rows):
            if abs(row["time_s"] - index * metadata["sample_dt_s"]) > 1e-8:
                raise ValueError("Unexpected simulation sample interval")
    return metadata, result, rows


def metrics(rows):
    return {name: {"minimum": min(row[name] for row in rows),
                   "maximum": max(row[name] for row in rows),
                   "peak_absolute": max(abs(row[name]) for row in rows),
                   "peak_time_s": max(rows, key=lambda row: abs(row[name]))["time_s"],
                   "change": rows[-1][name] - rows[0][name]}
            for name in SIGNALS}


def compare(left, right):
    if len(left) != len(right) or any(abs(a["time_s"] - b["time_s"]) > 1e-8 for a, b in zip(left, right)):
        raise ValueError("Comparison requires identical simulation sample times; no silent interpolation")
    missing_columns=sorted(set(left[0])^set(right[0]))
    common=set(left[0])&set(right[0])
    numerical = [key for key, value in left[0].items() if key in common and isinstance(value, float) and key != "time_s"]
    differences = {}
    first = {"time_s":left[0]["time_s"],"fields":missing_columns} if missing_columns else None
    for name in numerical:
        errors = [abs(a[name] - b[name]) for a, b in zip(left, right)]
        differences[name] = {"maximum_absolute": max(errors), "rms": math.sqrt(sum(e*e for e in errors)/len(errors))}
    for a, b in zip(left, right):
        changed = [key for key in a if key in common and a[key] != b[key]]
        if changed and first is None:
            first = {"time_s": a["time_s"], "fields": changed}
            break
    return {"exact_samples": first is None, "first_difference": first, "differences": differences,"unmatched_columns":missing_columns}


def coordinated_turn(rows):
    """Geometric 3-point curvature versus a steady, level coordinated-turn relation."""
    errors = []
    # One-second point spacing limits cancellation in curvature from geodetic doubles.
    stride = max(1, round(1/(rows[1]["time_s"]-rows[0]["time_s"]))) if len(rows)>1 else 1
    for index in range(stride, len(rows)-stride, stride):
        a, b, c = rows[index-stride], rows[index], rows[index+stride]
        if (b["time_s"] < rows[-1]["time_s"]-60 or b["navigation_mode"] != "loiter"
            or abs(b["roll_rate_deg_s"])>.5 or abs(b["sideslip_deg"])>2
            or abs(b["vertical_speed_mps"])>.5 or not 10<abs(b["roll_deg"])<30.5):
            continue
        latitude = math.radians(b["latitude_deg"])
        eccentricity = 6.69437999014e-3
        n = 6378137/math.sqrt(1-eccentricity*math.sin(latitude)**2)
        m = 6378137*(1-eccentricity)/(1-eccentricity*math.sin(latitude)**2)**1.5
        def xy(point):
            return (math.radians(point["longitude_deg"]-b["longitude_deg"])*(n+b["ellipsoid_height_m"])*math.cos(latitude),
                    math.radians(point["latitude_deg"]-b["latitude_deg"])*(m+b["ellipsoid_height_m"]))
        x, y = xy(a), xy(c)
        cross = abs(x[0]*y[1]-x[1]*y[0])
        if cross < 1e-8:
            continue
        radius = math.hypot(*x)*math.hypot(*y)*math.hypot(x[0]-y[0],x[1]-y[1])/(2*cross)
        ideal = b["speed_mps"]**2/(9.80665*math.tan(math.radians(abs(b["roll_deg"]))))
        errors.append((radius-ideal)/ideal)
    return {"qualified_samples": len(errors), "median_relative_difference": statistics.median(errors) if errors else None,
            "maximum_absolute_relative_difference": max(map(abs,errors)) if errors else None,
            "meaning": "Development comparison under no-wind near-steady level conditions; not measured aircraft turn performance"}


def write_report(folder, candidate=None, require_identical=False):
    # Import only for rendering; record validation and numerical metrics need no plotting dependency.
    cache = Path(__file__).resolve().parents[1] / "artifacts/matplotlib-cache"
    cache.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(cache))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    metadata, result, rows = read_run(folder)
    datasets = [("baseline", metadata, rows)]
    comparison = None
    if candidate:
        other_meta, other_result, other = read_run(candidate)
        if not result["ok"] or not other_result["ok"]:
            raise ValueError("Cannot compare incomplete/failed runs as successful experiments")
        if metadata["case"] != other_meta["case"]:
            raise ValueError("Comparison requires the same experiment case")
        comparison = compare(rows, other)
        comparison["same_profile"] = metadata["profile_sha256"] == other_meta["profile_sha256"]
        comparison["same_configuration"] = all(metadata[key] == other_meta[key] for key in
            ("events", "initial_condition", "source_sha256", "sample_dt_s", "duration_s"))
        if require_identical and not (comparison["same_profile"] and comparison["same_configuration"]):
            raise ValueError("Reproducibility requires identical profile, code, initial condition and input schedule")
        datasets.append(("candidate", other_meta, other))
    panels = [(("command_elevator_deg", "actual_elevator_deg"), "Elevator (deg)"),
              (("command_aileron_deg", "actual_aileron_deg", "command_rudder_deg", "actual_rudder_deg"), "Lateral controls (deg)"),
              (("pitch_deg", "angle_of_attack_deg", "roll_deg", "sideslip_deg"), "Attitude / airflow (deg)"),
              (("roll_rate_deg_s", "pitch_rate_deg_s", "yaw_rate_deg_s"), "Body rates (deg/s)"),
              (("speed_mps",), "TAS (m/s)"),
              (("vertical_speed_mps",), "Vertical speed (m/s)"),
              (("ellipsoid_height_m",), "WGS84 height (m)"),
              (("command_throttle", "actual_throttle", "load_factor"), "Throttle / normal load (1)"),
              (("flap_command_deg", "flap_actual_deg"), "Flaps (deg)"),
              (("thrust_n",), "Thrust (N)")]
    figure, axes = plt.subplots(len(panels), 1, figsize=(12, 3*len(panels)), sharex=True)
    for axis, (names, label) in zip(axes, panels):
        for index, (kind, _, samples) in enumerate(datasets):
            for name in names:
                axis.plot([row["time_s"] for row in samples], [row[name] for row in samples],
                          label=f"{kind}: {name}", linestyle="--" if index else "-", linewidth=1)
        axis.set_ylabel(label)
        axis.grid(alpha=.25)
        axis.legend(fontsize=7, loc="best", ncol=2)
    axes[-1].set_xlabel("Simulation time (s)")
    figure.suptitle(f"{metadata['case']} | RESEARCH ASSUMPTIONS — REAL PERFORMANCE UNVALIDATED")
    figure.tight_layout(rect=(0, 0, 1, .98))
    folder = Path(folder)
    stem = "comparison-"+hashlib.sha256(str(Path(candidate).resolve()).encode()).hexdigest()[:12] if candidate else "response"
    figure.savefig(folder / f"{stem}.svg")
    figure.savefig(folder / f"{stem}.png", dpi=120)
    plt.close(figure)
    summary = {"metrics": metrics(rows), "comparison": comparison,
               "coordinated_turn": coordinated_turn(rows) if metadata["case"] == "loiter" else None}
    (folder / f"{stem}-metrics.json").write_text(json.dumps(summary, indent=2, allow_nan=False), encoding="utf-8")
    details = {"status": "COMPLETE" if result["ok"] else "FAILED / INCOMPLETE", "baseline": str(folder),
               "candidate": str(candidate) if candidate else None, "conditions": metadata,
               "candidate_conditions": other_meta if candidate else None, "result": result, **summary}
    (folder / f"{stem if candidate else 'report'}.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>UAVSim flight test</title>'
        '<style>body{max-width:1200px;margin:2rem auto;font:16px system-ui;padding:1rem}'
        'img{width:100%}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style>'
        '<h1>UAVSim flight test</h1><p>Research assumptions. Real aircraft performance unvalidated.</p>'
        f'<p>{html.escape(details["status"])}</p><img alt="Flight response time histories" src="{stem}.svg">'
        f'<pre>{html.escape(json.dumps(details, indent=2, ensure_ascii=False, allow_nan=False))}</pre>', encoding="utf-8")
    if require_identical and not comparison["exact_samples"]:
        raise ValueError("Reproducibility failed: " + json.dumps(comparison["first_difference"]))
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path, nargs="?")
    parser.add_argument("--require-identical", action="store_true")
    args = parser.parse_args()
    if args.require_identical and not args.candidate:
        parser.error("--require-identical needs two runs")
    try:
        summary = write_report(args.baseline, args.candidate, args.require_identical)
        print(json.dumps({"report": str(args.baseline), "comparison": summary["comparison"]}, indent=2))
        return 0
    except Exception as error:
        print(f"Report failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
