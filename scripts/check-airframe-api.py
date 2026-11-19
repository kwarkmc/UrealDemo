"""Live v1/v2 airframe contract check. Synthetic fixture is NOT C208B.

Requires an initialized API/Unreal. Temporarily replaces the selector and one
test profile; finally restores their bytes and reloads the original paused model.
Flight time/position are reset by profile reload, not restored to their prior values.
"""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from uavsim_client import UAVSimClient, UAVSimError


def main():
    sim = UAVSimClient()
    active = ROOT / "aircraft/active.json"
    profile = ROOT / "aircraft/runtime-large-check.json"
    selector_bytes = active.read_bytes()
    prior_profile = profile.read_bytes() if profile.exists() else None
    report = {"ok": False, "passed": [], "restore_errors": [], "model": "synthetic, not C208B"}
    original_wind = None

    def check(condition, name):
        if not condition:
            raise AssertionError(name)
        report["passed"].append(name)

    def rejected(callback, code):
        try:
            callback()
        except UAVSimError as error:
            return error.status == code
        return False

    def write(candidate):
        profile.write_text(json.dumps(candidate), encoding="utf-8")

    def physics(state):
        return {key: state[key] for key in ("aircraft", "controls", "actuators", "disturbance")}

    try:
        sim.pause()
        original = sim.reload_aircraft_profile()
        original_wind = {key: original["environment"][key] for key in ("wind_east_mps", "wind_north_mps", "wind_up_mps")}
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        check(original["airframe"]["schema_version"] == 1, "legacy profile remains v1")
        fixture = json.loads((ROOT / "tests/fixtures/synthetic-large.json").read_text(encoding="utf-8"))
        write(fixture)
        active.write_text('{"profile":"runtime-large-check.json"}\n', encoding="utf-8")
        state = sim.reload_aircraft_profile()
        check(state["airframe"]["schema_version"] == 2 and state["airframe"]["initial_speed_mps"] == 65
              and abs(state["aircraft"]["speed_mps"] - 65) < 1e-7, "v2 initializes declared 65 m/s level trim")
        check(not any(state["airframe"][name] for name in ("ground_supported", "navigation_supported", "automation_supported")),
              "unsupported operating modes are explicit")
        state = sim.step(.1)
        check(abs(state["aircraft"]["speed_mps"] - 65) < 1e-6 and not state["fault"], "live synthetic level trim advances")
        state = sim.reset()
        check(state["aircraft"]["simulation_time_s"] == 0 and abs(state["aircraft"]["speed_mps"] - 65) < 1e-7,
              "reset reuses applied profile defaults")
        a = state["aircraft"]
        pose = {name: a[name] for name in ("latitude_deg", "longitude_deg", "ellipsoid_height_m", "heading_deg", "pitch_deg", "roll_deg")}
        check(rejected(lambda: sim.set_initial_condition(**pose, speed_mps=19), 400), "old 19 m/s rejected for restricted profile")
        before = physics(sim.get_state())
        for callback in (sim.reset_on_ground, lambda: sim.set_landing_gear(down=False), sim.start_automatic_takeoff,
                         sim.start_automatic_landing, sim.prepare_landing_approach,
                         lambda: sim.set_ground_controls(steering_deg=0, brake=0, parking_brake=False),
                         lambda: sim.set_route([dict(latitude_deg=a["latitude_deg"], longitude_deg=a["longitude_deg"],
                                                    ellipsoid_height_m=a["ellipsoid_height_m"], speed_mps=19)], loiter_radius_m=150)):
            check(rejected(callback, 409), "unsupported " + getattr(callback, "__name__", "operation") + " rejected")
        check(physics(sim.get_state()) == before, "unsupported requests preserve physics")
        for change in ("negative_mass", "unknown_operating_field", "unsupported_navigation", "untrimmable", "wrong_type"):
            candidate = json.loads(json.dumps(fixture))
            if change == "negative_mass": candidate["parameters"]["mass_kg"] = -1
            elif change == "unknown_operating_field": candidate["operating"]["unknown"] = 1
            elif change == "unsupported_navigation": candidate["operating"]["automation_supported"] = True
            elif change == "untrimmable": candidate["parameters"]["maximum_thrust_mps2"] = .001
            else: candidate["operating"]["minimum_speed_mps"] = True
            write(candidate)
            snapshot = sim.get_state()
            check(rejected(sim.reload_aircraft_profile, 409), change + " reload rejected")
            after = sim.get_state()
            check(physics(after) == physics(snapshot) and after["airframe"]["fingerprint"] == snapshot["airframe"]["fingerprint"],
                  change + " preserves applied model and physics")
        write(fixture)
        sim.reload_aircraft_profile()
        sim.set_initial_condition(**pose, speed_mps=65)
        check(not sim.step(.01)["fault"], "explicit initial condition above 45 m/s accepted")
        moved = dict(pose, latitude_deg=pose["latitude_deg"] + .005)
        sim.set_initial_condition(**moved, speed_mps=65)
        moved_state = sim.reset()
        check(abs(moved_state["aircraft"]["latitude_deg"]-moved["latitude_deg"])<1e-7
              and abs(moved_state["aircraft"]["speed_mps"]-65)<1e-7, "region-move initialization/reset sequence uses profile TAS")
        check(rejected(lambda: sim.set_initial_condition(**moved, speed_mps=65, angle_of_attack_deg=16, sideslip_deg=0), 400),
              "air-angle limit rejected before mutation")
        sim.set_wind(wind_east_mps=0, wind_north_mps=-30, wind_up_mps=0)
        outside = sim.get_state()
        check(rejected(lambda: sim.step(.01), 409), "runtime out-of-envelope step fails explicitly")
        stopped = sim.get_state()
        check(stopped["fault"] and stopped["aircraft"]["simulation_time_s"] == outside["aircraft"]["simulation_time_s"]
              and stopped["aircraft"]["latitude_deg"] == outside["aircraft"]["latitude_deg"], "runtime failure preserves time/location and publishes fault")
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        try:
            active.write_bytes(selector_bytes)
            if prior_profile is None:
                profile.unlink(missing_ok=True)
            else:
                profile.write_bytes(prior_profile)
            sim.pause()
            if original_wind is not None:
                sim.set_wind(**original_wind)
            restored = sim.reload_aircraft_profile()
            check(active.read_bytes() == selector_bytes and not restored["aircraft"]["running"], "original selector restored; original profile paused")
        except Exception as error:
            report["restore_errors"].append(str(error))
        report["ok"] = report["ok"] and not report["restore_errors"]
        (ROOT / "artifacts/airframe-api-check.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
