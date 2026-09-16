"""Focused live check for body_fixed/auto/geo/protect gimbal modes.

Requires the running API/Unreal and Korea terrain. Uses paused initial conditions,
one temporary marker and no simulation steps or process restarts. Finally restores
the starting aircraft pose/speed, control commands, sensor settings/mode and view,
leaving playback paused. Initial-condition calls reset internal dynamics/navigation;
this is not a complete simulator snapshot restore. No image inference or hardware.
"""
import argparse
import json
import math
from pathlib import Path
import sys
from urllib.error import HTTPError
from urllib.request import Request
from uuid import uuid4

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


def expected_angles(aircraft, point):
    """Independent WGS84 ECEF -> local NED -> FRD, without a target-height offset."""
    def ecef(p):
        lat, lon = map(math.radians, (p["latitude_deg"], p["longitude_deg"]))
        e2 = (1 / 298.257223563) * (2 - 1 / 298.257223563)
        radius = 6378137 / math.sqrt(1 - e2 * math.sin(lat) ** 2)
        height = p["ellipsoid_height_m"]
        return ((radius + height) * math.cos(lat) * math.cos(lon),
                (radius + height) * math.cos(lat) * math.sin(lon),
                (radius * (1 - e2) + height) * math.sin(lat))

    x, y, z = (t - a for t, a in zip(ecef(point), ecef(aircraft)))
    lat, lon = map(math.radians, (aircraft["latitude_deg"], aircraft["longitude_deg"]))
    north = -math.sin(lat) * math.cos(lon) * x - math.sin(lat) * math.sin(lon) * y + math.cos(lat) * z
    east = -math.sin(lon) * x + math.cos(lon) * y
    down = -(math.cos(lat) * math.cos(lon) * x + math.cos(lat) * math.sin(lon) * y + math.sin(lat) * z)
    h, p, r = map(math.radians, (aircraft["heading_deg"], aircraft["pitch_deg"], aircraft["roll_deg"]))
    ch, sh, cp, sp, cr, sr = math.cos(h), math.sin(h), math.cos(p), math.sin(p), math.cos(r), math.sin(r)
    # The camera sits 0.20 m forward and 0.12 m below the CG in aircraft FRD.
    forward = cp * ch * north + cp * sh * east - sp * down - .20
    right = (sr * sp * ch - cr * sh) * north + (sr * sp * sh + cr * ch) * east + sr * cp * down
    below = (cr * sp * ch + sr * sh) * north + (cr * sp * sh - sr * ch) * east + cr * cp * down - .12
    return math.degrees(math.atan2(right, forward)), math.degrees(math.atan2(-below, math.hypot(forward, right)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/gimbal-modes-api-check.json")
    args = parser.parse_args()
    sim = CheckClient()
    report = {"ok": False, "passed": [], "geometry": []}
    initial = None
    id = "gimbal-check-" + uuid4().hex[:10]
    latitude, longitude, height = 33.509, 126.490, 500
    marker = dict(id=id, kind="marker", latitude_deg=latitude + .0009,
                  longitude_deg=longitude, heading_deg=0)
    point = dict(latitude_deg=latitude + .0009, longitude_deg=longitude + .0004,
                 ellipsoid_height_m=350)

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        report["passed"].append(name)
        print(name, file=sys.stderr, flush=True)

    def rejected(call, status):
        try:
            call()
        except UAVSimError as error:
            if error.status != status:
                raise RuntimeError(f"Expected HTTP {status}, got {error.status}: {error}") from error
            return True
        return False

    def atomic_state(state):
        # Rejections update diagnostics; a separately open EO/IR page captures frames.
        # Neither the diagnostic nor the JPEG sequence is a gimbal command mutation.
        return {**state, "eoir": {key: value for key, value in state["eoir"].items()
                                  if key not in ("error", "frame_sequence")}}

    def record_atomicity(label, before, after, results):
        old, new = atomic_state(before), atomic_state(after)
        changes = {}
        for section in old.keys() | new.keys():
            a, b = old.get(section), new.get(section)
            if isinstance(a, dict) and isinstance(b, dict):
                changes.update({section + "." + key: {"before": a.get(key), "after": b.get(key)}
                                for key in a.keys() | b.keys() if a.get(key) != b.get(key)})
            elif a != b:
                changes[section] = {"before": a, "after": b}
        report.setdefault("atomicity", {})[label] = {
            **results, "state_unchanged": not changes, "changes": changes,
            "frame_sequence_before": before["eoir"]["frame_sequence"],
            "frame_sequence_after": after["eoir"]["frame_sequence"]}
        return all(results.values()) and not changes

    def angles(state):
        sensor = state["eoir"]
        return sensor["azimuth_deg"], sensor["elevation_deg"]

    def coordinates(state):
        sensor = state["eoir"]
        return {key: sensor["point_" + key] for key in point}

    def non_geo(state, mode):
        return state["eoir"]["gimbal_mode"] == mode and all(value == 0 for value in coordinates(state).values())

    def place(heading=0, pitch=0, roll=0):
        return sim.set_initial_condition(latitude_deg=latitude, longitude_deg=longitude,
            ellipsoid_height_m=height, heading_deg=heading, pitch_deg=pitch, roll_deg=roll,
            speed_mps=19, angle_of_attack_deg=0, sideslip_deg=0)

    def check_geo(state, name):
        sensor = state["eoir"]
        expected = expected_angles(state["aircraft"], point)
        actual = angles(state)
        error = (abs((actual[0] - expected[0] + 180) % 360 - 180), abs(actual[1] - expected[1]))
        report["geometry"].append(dict(case=name, expected_deg=expected, actual_deg=actual, errors_deg=error))
        check(sensor["gimbal_mode"] == "geo" and coordinates(state) == point and not sensor["target_id"]
              and max(error) < .02 and sensor["error_deg"] < .01 and not sensor["limited"]
              and sensor["tracking_status"] == "pointing" and sensor["target_in_fov"], name)

    def restore_sensor(sensor):
        mode = sensor["gimbal_mode"]
        if mode == "body_fixed":
            sim.set_gimbal_mode(mode, azimuth_deg=sensor["azimuth_deg"], elevation_deg=sensor["elevation_deg"])
        elif mode == "auto":
            sim.set_gimbal_mode(mode, target_id=sensor["target_id"])
        elif mode == "geo":
            sim.set_gimbal_mode(mode, **{key: sensor["point_" + key] for key in point})
        else:
            sim.set_gimbal_mode("protect")
        return sim.set_eoir(enabled=sensor["enabled"], mode=sensor["mode"], fov_deg=sensor["fov_deg"])

    try:
        initial = sim.pause()
        if len(initial["targets"]) >= 64:
            raise RuntimeError("This check requires one free target slot; existing targets are preserved")
        place()
        sim.set_gimbal_mode("body_fixed", azimuth_deg=17, elevation_deg=-23)
        sim.set_eoir(enabled=True, mode="ccd", fov_deg=12)
        state = place(heading=45, pitch=10, roll=20)
        check(non_geo(state, "body_fixed") and angles(state) == (17, -23) and not state["eoir"]["target_id"]
              and abs(state["aircraft"]["heading_deg"] - 45) < 1e-8,
              "body_fixed holds its relative angles through aircraft heading, pitch and roll changes")
        place()
        sim.create_target(**marker)
        state = sim.set_gimbal_mode("auto", target_id=id)
        check(non_geo(state, "auto") and state["eoir"]["target_id"] == id
              and state["eoir"]["error_deg"] < .01 and state["eoir"]["target_in_fov"],
              "auto selects an existing object and clears unused geographic coordinates")
        before = angles(state)
        state = sim.move_target(id=id, latitude_deg=marker["latitude_deg"],
                                longitude_deg=longitude + .0008, heading_deg=60)
        check(non_geo(state, "auto") and state["eoir"]["target_id"] == id
              and abs(angles(state)[0] - before[0]) > 20 and state["eoir"]["error_deg"] < .01,
              "auto follows object motion without moving the paused aircraft")
        missing_rejected = rejected(lambda: sim.set_gimbal_mode("auto", target_id=id + "-missing"), 409)
        check(record_atomicity("missing_auto_target", state, sim.get_state(), {"rejected": missing_rejected}),
              "missing auto target is rejected without replacing the active command")
        state = sim.set_gimbal_mode("geo", **point)
        check_geo(state, "geo uses the exact supplied ellipsoid height and independent WGS84 pointing")
        state = place(heading=25, pitch=10, roll=15)
        check_geo(state, "geo retains its world point while aircraft attitude changes its relative angles")
        before = atomic_state(state)
        state = sim.remove_target(id)
        check(atomic_state(state)["eoir"] == before["eoir"]
              and state["aircraft"] == before["aircraft"] and coordinates(state) == point,
              "deleting an unrelated object leaves geographic pointing unchanged")
        state = sim.reset()
        check_geo(state, "aircraft reset preserves geo mode and its exact world point")
        place()
        sim.set_eoir(enabled=True, mode="ccd", fov_deg=5)
        state = sim.set_gimbal_mode("geo", latitude_deg=latitude, longitude_deg=longitude, ellipsoid_height_m=1000)
        check(state["eoir"]["gimbal_mode"] == "geo" and state["eoir"]["limited"]
              and state["eoir"]["elevation_deg"] == 30 and state["eoir"]["error_deg"] > 50
              and not state["eoir"]["target_in_fov"],
              "geo points beyond elevation travel report clamp, pointing error and out-of-FOV state")
        invalid = [{}, {"mode": "unknown"}, {"mode": "body_fixed", "azimuth_deg": 0},
            {"mode": "body_fixed", "azimuth_deg": 181, "elevation_deg": 0},
            {"mode": "auto", "target_id": ""}, {"mode": "auto", "target_id": 7},
            {"mode": "geo", **point, "latitude_deg": 91},
            {"mode": "geo", **point, "longitude_deg": 181},
            {"mode": "geo", **point, "ellipsoid_height_m": 20001},
            {"mode": "protect", "target_id": id}]
        report["invalid_fields"] = []
        for body in invalid:
            result = {"body": body, "rejected": rejected(
                lambda body=body: sim._request("PUT", "eoir/gimbal-mode", body), 400)}
            report["invalid_fields"].append(result)
        bad_fields_rejected = all(result["rejected"] for result in report["invalid_fields"])
        raw = b'{"mode":"geo","latitude_deg":NaN,"longitude_deg":126.49,"ellipsoid_height_m":350}'
        sim.request_count += 1
        request = Request(sim.base_url + "/api/v1/eoir/gimbal-mode", raw,
                          {"Content-Type": "application/json"}, method="PUT")
        nan_rejected = False
        try:
            with sim.opener.open(request, timeout=sim.timeout) as response:
                response.read()
        except HTTPError as error:
            nan_rejected = error.code == 400
        outside_render_bounds = rejected(lambda: sim.set_gimbal_mode("geo", latitude_deg=0,
            longitude_deg=0, ellipsoid_height_m=350), 409)
        antipode_rejected = rejected(lambda: sim.set_gimbal_mode("geo", latitude_deg=-36,
            longitude_deg=-52, ellipsoid_height_m=350), 409)
        check(record_atomicity("invalid_inputs", state, sim.get_state(), {
                  "bad_fields_rejected": bad_fields_rejected, "nan_rejected": nan_rejected,
                  "outside_render_bounds_rejected": outside_render_bounds,
                  "antipode_rejected": antipode_rejected}),
              "missing/extra fields, bad types, NaN and geographic/render bounds reject atomically")
        state = sim.set_gimbal_mode("protect")
        check(non_geo(state, "protect") and angles(state) == (180, 30)
              and state["eoir"]["tracking_status"] == "protected"
              and not state["eoir"]["enabled"] and not state["eoir"]["target_id"],
              "protect parks at 180/+30 degrees, clears point and target, and disables capture")
        enable_rejected = rejected(lambda: sim.set_eoir(enabled=True, mode="ir_black", fov_deg=40), 409)
        frame_rejected = rejected(sim.get_eoir_frame, 409)
        check(record_atomicity("protected_camera", state, sim.get_state(), {
                  "enable_rejected": enable_rejected, "frame_rejected": frame_rejected}),
              "protected camera refuses enable and JPEG capture without changing settings or pose")
        state = sim.reset()
        check(non_geo(state, "protect") and angles(state) == (180, 30) and not state["eoir"]["enabled"],
              "aircraft reset preserves the protected pose and disabled capture")
        state = sim.set_gimbal_mode("body_fixed", azimuth_deg=-10, elevation_deg=-20)
        stayed_off = not state["eoir"]["enabled"] and non_geo(state, "body_fixed")
        state = sim.set_eoir(enabled=True, mode="ccd", fov_deg=12)
        check(stayed_off and state["eoir"]["enabled"] and angles(state) == (-10, -20),
              "leaving protect stays disabled until an explicit enable command")
        sim.set_gimbal_mode("protect")
        manual = sim.set_gimbal(azimuth_deg=11, elevation_deg=-22)
        sim.set_gimbal_mode("protect")
        canceled = sim.track_target()
        place()
        sim.create_target(**marker)
        sim.set_gimbal_mode("protect")
        tracked = sim.track_target(id)
        check(non_geo(manual, "body_fixed") and angles(manual) == (11, -22)
              and non_geo(canceled, "body_fixed") and angles(canceled) == (180, 30)
              and non_geo(tracked, "auto") and tracked["eoir"]["target_id"] == id
              and not any(item["eoir"]["enabled"] for item in (manual, canceled, tracked)),
              "legacy manual, cancel-tracking and track commands exit protection without enabling capture")
        state = sim.remove_target(id)
        check(non_geo(state, "body_fixed") and not state["eoir"]["target_id"]
              and state["eoir"]["tracking_status"] == "target_missing" and angles(state) == angles(tracked),
              "deleting auto target falls back to body_fixed and holds the last angles with missing status")
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        if initial is not None:
            restore_errors = []
            def attempt(call):
                try:
                    return call()
                except Exception as error:
                    restore_errors.append(str(error))
                    return None

            current = attempt(sim.pause)
            if current and any(item["id"] == id for item in current["targets"]):
                attempt(lambda: sim.remove_target(id))
            aircraft = initial["aircraft"]
            if initial["ground"]["on_ground"]:
                attempt(lambda: sim.set_ground_initial_condition(latitude_deg=aircraft["latitude_deg"],
                    longitude_deg=aircraft["longitude_deg"], heading_deg=aircraft["heading_deg"],
                    ground_speed_mps=aircraft["ground_speed_mps"]))
            else:
                attempt(lambda: sim.set_initial_condition(**{key: aircraft[key] for key in
                    ("latitude_deg", "longitude_deg", "ellipsoid_height_m", "heading_deg", "pitch_deg", "roll_deg", "speed_mps")},
                    angle_of_attack_deg=aircraft["angle_of_attack_deg"], sideslip_deg=aircraft["sideslip_deg"]))
            attempt(lambda: sim.set_controls(**initial["controls"]))
            attempt(lambda: sim.set_ground_controls(**{key: initial["ground"][key]
                    for key in ("steering_deg", "brake", "parking_brake")}))
            attempt(lambda: sim.set_time_scale(aircraft["time_scale"]))
            attempt(lambda: sim.set_view(initial["view_mode"]))
            attempt(lambda: restore_sensor(initial["eoir"]))
            final = attempt(sim.get_state)
            report["restored"] = final
            if final:
                sensor_keys = ("gimbal_mode", "enabled", "mode", "fov_deg", "target_id",
                               "point_latitude_deg", "point_longitude_deg", "point_ellipsoid_height_m")
                good = (not final["aircraft"]["running"] and final["targets"] == initial["targets"]
                    and all(final["eoir"][key] == initial["eoir"][key] for key in sensor_keys)
                    and abs(final["aircraft"]["latitude_deg"] - aircraft["latitude_deg"]) < 1e-7
                    and abs(final["aircraft"]["longitude_deg"] - aircraft["longitude_deg"]) < 1e-7
                    and abs(final["aircraft"]["ellipsoid_height_m"] - aircraft["ellipsoid_height_m"]) < 1e-3
                    and all(abs((final["aircraft"][key] - aircraft[key] + 180) % 360 - 180) < 1e-4
                            for key in ("heading_deg", "pitch_deg", "roll_deg"))
                    and abs(final["aircraft"]["speed_mps"] - aircraft["speed_mps"]) < 1e-5
                    and final["controls"] == initial["controls"]
                    and final["aircraft"]["time_scale"] == aircraft["time_scale"]
                    and final["view_mode"] == initial["view_mode"])
                if not good:
                    restore_errors.append("Starting pose, targets or sensor settings were not restored")
            report["restore_errors"] = restore_errors
            if restore_errors:
                report["ok"] = False
            else:
                check(True, "finally restores startup pose, sensor mode/settings, view and pre-existing targets while paused")
    report["api_requests"] = sim.request_count
    text = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
