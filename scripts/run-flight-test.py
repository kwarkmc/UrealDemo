"""Repeatable UAVSim research experiments through the existing public API.

Resets flight; restores selector bytes/wind/profile to paused trim, not prior dynamics.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from uavsim_client import UAVSimClient

CASES = ("trim", "elevator-pulse", "elevator-negative", "aileron-pulse", "rudder-pulse",
         "loiter", "engine-cut", "throttle-cut", "flaps")
AIRCRAFT_COLUMNS = ("latitude_deg", "longitude_deg", "ellipsoid_height_m", "heading_deg", "pitch_deg", "roll_deg",
                    "speed_mps", "ground_speed_mps", "vertical_speed_mps", "angle_of_attack_deg", "sideslip_deg",
                    "roll_rate_deg_s", "pitch_rate_deg_s", "yaw_rate_deg_s", "load_factor", "flow_separation")
GUIDANCE = ("desired_course_deg", "desired_bank_deg", "desired_speed_mps", "desired_height_m", "desired_climb_mps")
GROUND = ("steering_deg", "brake", "applied_steering_deg", "applied_brake")
PROPULSION = ("propeller_rpm","shaft_drive_power_w","shaft_load_power_w","shaft_drive_torque_nm","propeller_load_torque_nm")
WHEELS = ("wheel_normal_force_n","wheel_clearance_m","wheel_lateral_slip_mps")
CONTROLS = ("elevator_deg", "aileron_deg", "rudder_deg", "throttle")
COLUMNS = ("time_s", *AIRCRAFT_COLUMNS, *("command_"+key for key in CONTROLS),
           *("actual_"+key for key in CONTROLS), "thrust_n", "flap_command_deg", "flap_actual_deg",
           "gear_extension", *PROPULSION, *(f"{name}_{wheel}" for name in WHEELS for wheel in ("nose","left","right")), *("guidance_"+name for name in GUIDANCE), *("ground_"+name for name in GROUND), "navigation_mode", "operation_phase", "fault")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False), encoding="utf-8")


def events_for(case):
    if case in ("elevator-pulse", "elevator-negative", "aileron-pulse", "rudder-pulse"):
        axis = case.split("-")[0]
        return [{"time_s": 5, "kind": "surface", "axis": axis, "delta_deg": -1 if case == "elevator-negative" else 1},
                {"time_s": 5.5, "kind": "trim-controls"}]
    if case in ("engine-cut", "throttle-cut"):
        return [{"time_s": 5, "kind": case}]
    if case == "flaps":
        return [{"time_s": 5, "kind": "flaps", "degrees": 10}, {"time_s": 15, "kind": "flaps", "degrees": 0}]
    if case == "loiter":
        return [{"time_s": 0, "kind": "route", "radius_m": 1000, "center_north_m": 3000}]
    return []


def sample(state, start_time, fingerprint, case):
    if state["airframe"]["fingerprint"] != fingerprint:
        raise ValueError("Applied airframe changed during experiment")
    if state["aircraft"]["running"]:
        raise ValueError("Playback enabled during explicit-step experiment")
    if state["fault"] or state["ground"]["crashed"]:
        raise ValueError("Simulator fault/crash: " + state["fault"])
    if state["automation"]["kind"] != "none" or (case != "loiter" and state["navigation"]["waypoint_count"]):
        raise ValueError("Unexpected automatic controller active")
    row = {"time_s": state["aircraft"]["simulation_time_s"] - start_time}
    row.update({name: state["aircraft"][name] for name in AIRCRAFT_COLUMNS})
    for prefix, block in (("command", "controls"), ("actual", "actuators")):
        row.update({prefix+"_"+name: state[block][name] for name in CONTROLS})
    row.update(thrust_n=state["actuators"]["thrust_n"], flap_command_deg=state["flaps"]["command_deg"],
               flap_actual_deg=state["flaps"]["actual_deg"], gear_extension=state["gear"]["extension"],
               navigation_mode=state["navigation"]["mode"], operation_phase=state["automation"]["phase"], fault=state["fault"])
    row.update({"guidance_"+name: state["automation"][name] for name in GUIDANCE})
    row.update({"ground_"+name: state["ground"][name] for name in GROUND})
    row.update({name:state["actuators"][name] for name in PROPULSION})
    row.update({f"{name}_{wheel}":state["ground"][name][index] for name in WHEELS for index,wheel in enumerate(("nose","left","right"))})
    if any(type(value) in (int, float) and not math.isfinite(value) for value in row.values()):
        raise ValueError("Nonfinite sample")
    return row


def apply_event(sim, event, trim, initial):
    kind = event["kind"]
    if kind in ("surface", "trim-controls", "throttle-cut"):
        command = dict(trim)
        if kind == "surface":
            command[event["axis"]+"_deg"] += event["delta_deg"]
        if kind == "throttle-cut":
            command["throttle"] = 0
        return sim.set_controls(**command)
    if kind == "engine-cut":
        return sim.set_failures(engine_power_fraction=0, elevator_jammed=False, aileron_jammed=False, rudder_jammed=False)
    if kind == "flaps":
        return sim.set_flaps(event["degrees"])
    if kind == "route":
        a = initial["aircraft"]
        lat = math.radians(a["latitude_deg"])
        eccentricity = 6.69437999014e-3
        meridional = 6378137*(1-eccentricity)/(1-eccentricity*math.sin(lat)**2)**1.5
        center_lat = a["latitude_deg"] + math.degrees(event["center_north_m"]/(meridional+a["ellipsoid_height_m"]))
        return sim.set_route([dict(latitude_deg=center_lat, longitude_deg=a["longitude_deg"],
                                   ellipsoid_height_m=a["ellipsoid_height_m"], speed_mps=a["speed_mps"])],
                             loiter_radius_m=event["radius_m"], clockwise=True)
    raise ValueError("Unknown event kind")


def run(args, sim=None):
    sim = sim or UAVSimClient(args.api_url)
    if args.case not in CASES or not math.isfinite(args.iy_scale) or not .5 <= args.iy_scale <= 2:
        raise ValueError("Invalid experiment or Iy scale (0.5..2)")
    if not re.fullmatch(r"[A-Za-z0-9_-]+\.json", args.profile):
        raise ValueError("Profile must be a JSON basename")
    duration = 180 if args.case == "loiter" else 30
    dt = .02 if args.case in ("elevator-pulse", "elevator-negative", "aileron-pulse", "rudder-pulse") else .1
    folder = args.output or ROOT / "artifacts/flight-tests" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")+"-"+uuid.uuid4().hex[:8])
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=False)
    start_wall = time.perf_counter()
    metadata = {"schema_version": 2, "case": args.case, "duration_s": duration, "sample_dt_s": dt,
                "created_utc": datetime.now(timezone.utc).isoformat(), "events": events_for(args.case),
                "research_warning": "Development assumptions; real aircraft performance unvalidated",
                "iy_scale": args.iy_scale,
                "units": "SI lengths/speeds/forces; API angles deg, body FRD rates deg/s; WGS84 ellipsoid height; heading clockwise from North"}
    result = {"ok": False, "checks": [], "restore_errors": [], "samples": 0, "status": "incomplete"}
    active = ROOT / "aircraft/active.json"
    prior = None
    temporary = None
    mutated = False
    wind = None
    try:
        prior = active.read_bytes()
        before = sim.get_state()
        if before["aircraft"]["running"] and not args.reset_flight:
            raise ValueError("Flight is running; --reset-flight explicitly replaces the current flight")
        if before["wargame"]["shots"]:
            raise ValueError("Existing wargame records present; experiment would advance their clock")
        wind = {key: before["environment"][key] for key in ("wind_east_mps", "wind_north_mps", "wind_up_mps")}
        profile_bytes = (ROOT / "aircraft" / args.profile).read_bytes()
        profile = json.loads(profile_bytes.decode("utf-8-sig"))
        metadata["original_profile_sha256"] = digest(profile_bytes)
        selected = args.profile
        if args.iy_scale != 1:
            profile["parameters"]["iy"] *= args.iy_scale
            profile["id"] = profile["id"][:55]+"-iy-"+str(args.iy_scale)
            profile.setdefault("provenance", {})["flight_test_variant"] = {
                "assumption": "Iy-only development perturbation, not measured uncertainty", "scale": args.iy_scale,
                "original_sha256": digest(profile_bytes)}
            profile_bytes = (json.dumps(profile, indent=2)+"\n").encode("utf-8")
            temporary = ROOT / "aircraft" / ("runtime-flight-test-"+uuid.uuid4().hex+".json")
            with temporary.open("xb") as stream:
                stream.write(profile_bytes)
            selected = temporary.name
        metadata["profile_sha256"] = digest(profile_bytes)
        (folder / "profile.json").write_bytes(profile_bytes)
        sources = sorted((ROOT / "Source/FlightCore").glob("*.h")) + [ROOT / "uavsim_client.py", ROOT / "scripts/run-flight-test.py",
            ROOT / "scripts/serve-api.py", ROOT / "scripts/report-flight-test.py", ROOT / "Binaries/Win64/UnrealEditor-UAVSim.dll"]
        metadata["source_sha256"] = {str(path.relative_to(ROOT)): digest(path.read_bytes()) if path.exists() else None for path in sources}
        mutated = True  # Any timeout below may have applied a mutation; never retry a write.
        sim.pause()
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        active.write_bytes((json.dumps({"profile": selected})+"\n").encode("utf-8"))
        state = sim.reload_aircraft_profile()
        expected_fingerprint = "sha1:"+hashlib.sha1(profile_bytes.decode("utf-8-sig").encode("utf-8")).hexdigest()
        if state["airframe"]["fingerprint"] != expected_fingerprint:
            raise ValueError("Applied profile fingerprint does not match archived bytes")
        metadata["airframe"] = state["airframe"]
        limits = state["airframe"]
        sim.set_initial_condition(latitude_deg=33.433, longitude_deg=126.52,
            ellipsoid_height_m=limits["initial_height_m"], heading_deg=0, pitch_deg=0, roll_deg=0, speed_mps=limits["initial_speed_mps"])
        initial = sim.reset()
        trim = dict(initial["controls"])
        metadata["initial_condition"] = {key: initial[key] for key in ("aircraft", "controls", "actuators", "flaps", "gear", "environment")}
        metadata["trim_controls"] = trim
        if args.case == "flaps" and limits["maximum_flap_deg"] < 10:
            raise ValueError("Selected profile does not support 10 degree flap experiment")
        fingerprint = initial["airframe"]["fingerprint"]
        start_time = initial["aircraft"]["simulation_time_s"]
        save_json(folder / "metadata.json", metadata)
        samples = []
        event_at = {round(event["time_s"]/dt): event for event in metadata["events"]}
        expected_controls = dict(trim)
        with (folder / "samples.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=COLUMNS)
            writer.writeheader()
            state = initial
            for index in range(round(duration/dt)+1):
                if index in event_at:
                    event = event_at[index]
                    state = apply_event(sim, event, trim, initial)
                    if event["kind"] in ("surface", "trim-controls", "throttle-cut"):
                        expected_controls = dict(trim)
                        if event["kind"] == "surface":
                            expected_controls[event["axis"]+"_deg"] += event["delta_deg"]
                        if event["kind"] == "throttle-cut":
                            expected_controls["throttle"] = 0
                if args.case != "loiter" and state["controls"] != expected_controls:
                    raise ValueError("Control command changed outside the experiment schedule")
                if any(state["environment"][key] != 0 for key in wind):
                    raise ValueError("Wind changed during the no-wind experiment")
                row = sample(state, start_time, fingerprint, args.case)
                if abs(row["time_s"]-index*dt) > 1e-8:
                    raise ValueError("Unexpected simulation time interval")
                writer.writerow(row)
                samples.append(row)
                result["samples"] = len(samples)
                if index % 500 == 0:
                    stream.flush()
                    print(f"{args.case}: {index*dt:.0f}/{duration}s simulation", flush=True)
                if index < round(duration/dt):
                    state = sim.step(dt)
        result["final_state"] = state
        result["checks"].append("finite samples, exact time grid, profile and controller isolation")
        if "elevator" in args.case:
            direction = -1 if args.case == "elevator-pulse" else 1
            response = next(row for row in samples if row["time_s"] >= 5.1)
            if direction*response["pitch_rate_deg_s"] <= 0:
                raise ValueError("Initial elevator response has unexpected pitch-rate sign")
            result["checks"].append("initial elevator pitch-rate sign agrees with declared Cm_delta_e")
        if args.case == "engine-cut" and any(abs(row["shaft_drive_power_w"] if initial["airframe"].get("dynamic_propeller") else row["thrust_n"]) > 1e-12 for row in samples if row["time_s"] >= 5):
            raise ValueError("Engine failure retained thrust")
        if args.case == "loiter" and state["navigation"]["mode"] != "loiter":
            raise ValueError("Route did not reach final loiter")
        result["ok"] = True
        result["status"] = "complete"
    except Exception as error:
        result["error"] = f"{type(error).__name__}: {error}"
        result["status"] = "failed"
        try:
            result["last_observed_state"] = sim.get_state()
        except Exception as observation_error:
            result["observation_error"] = str(observation_error)
    finally:
        if mutated:
            try:
                active.write_bytes(prior)
                sim.pause()
                sim.set_wind(**wind)
                restored = sim.reload_aircraft_profile()
                result["restored_airframe"] = restored["airframe"]
                result["restore_contract"] = "Selector bytes and wind restored; original profile paused at airborne trim. Previous dynamic state/time NOT restored."
            except Exception as error:
                result["restore_errors"].append(str(error))
        if temporary is not None:
            try:
                temporary.unlink()
            except OSError as error:
                result["restore_errors"].append(str(error))
        result["ok"] = result["ok"] and not result["restore_errors"]
        if not result["ok"]:
            result["status"] = "failed"
        metadata["elapsed_wall_s"] = time.perf_counter()-start_wall
        save_json(folder / "metadata.json", metadata)
        save_json(folder / "result.json", result)
    if not args.no_report and (folder / "samples.csv").exists() and result["samples"]:
        rendered = subprocess.run([sys.executable, str(ROOT / "scripts/report-flight-test.py"), str(folder)], check=False)
        if rendered.returncode:
            result["ok"] = False
            result["status"] = "failed"
            result["report_error"] = "Offline report failed; raw samples are preserved"
            save_json(folder / "result.json", result)
    print(json.dumps({"ok": result["ok"], "folder": str(folder), "samples": result["samples"],
                      "error": result.get("error"), "restore_errors": result["restore_errors"]}), flush=True)
    return folder, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=CASES, default="elevator-pulse")
    parser.add_argument("--profile", default="c208b-research.json")
    parser.add_argument("--iy-scale", type=float, default=1)
    parser.add_argument("--reset-flight", action="store_true")
    parser.add_argument("--no-report", action="store_true", help="Record only; render later in an environment with Matplotlib")
    parser.add_argument("--output", type=Path, help="New run directory; existing paths are refused")
    parser.add_argument("--api-url", default="http://127.0.0.1:8765")
    args = parser.parse_args()
    try:
        _, result = run(args)
        return 0 if result["ok"] else 1
    except Exception as error:
        print(f"Experiment refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
