"""Focused live EO/IR check; requires the running API, Unreal and Korea terrain.

Changes paused aircraft pose and creates non-firing scene props. On success leaves
a paused CCD view tracking its tank; on failure removes its props, disables capture
and restores a parked start. One explicit 0.5 s flight step checks live pointing;
all captures are paused. Never starts/restarts processes.
IR checks concern luminance display filters, not physical thermal measurements.
"""
import argparse
from io import BytesIO
import json
import math
from pathlib import Path
import sys
from urllib.request import Request
from uuid import uuid4

from PIL import Image, ImageChops, ImageStat

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


def expected_angles(aircraft, target):
    """Independent WGS84 ECEF -> local NED -> aircraft FRD reference.

    The prop aims 1.5 m along its terrain normal, unavailable in public telemetry.
    Approximate that with geodetic up; the flat Jeju fixture allows 0.5 deg error.
    Camera mounting is explicitly 0.20 m forward and 0.12 m below the aircraft CG.
    """
    def ecef(p, extra_height=0):
        lat, lon = map(math.radians, (p["latitude_deg"], p["longitude_deg"]))
        e2 = (1 / 298.257223563) * (2 - 1 / 298.257223563)
        radius = 6378137 / math.sqrt(1 - e2 * math.sin(lat) ** 2)
        height = p["ellipsoid_height_m"] + extra_height
        return ((radius + height) * math.cos(lat) * math.cos(lon),
                (radius + height) * math.cos(lat) * math.sin(lon),
                (radius * (1 - e2) + height) * math.sin(lat))

    x, y, z = (t - a for t, a in zip(ecef(target, 1.5), ecef(aircraft)))
    lat, lon = map(math.radians, (aircraft["latitude_deg"], aircraft["longitude_deg"]))
    north = -math.sin(lat) * math.cos(lon) * x - math.sin(lat) * math.sin(lon) * y + math.cos(lat) * z
    east = -math.sin(lon) * x + math.cos(lon) * y
    down = -(math.cos(lat) * math.cos(lon) * x + math.cos(lat) * math.sin(lon) * y + math.sin(lat) * z)
    h, p, r = map(math.radians, (aircraft["heading_deg"], aircraft["pitch_deg"], aircraft["roll_deg"]))
    ch, sh, cp, sp, cr, sr = math.cos(h), math.sin(h), math.cos(p), math.sin(p), math.cos(r), math.sin(r)
    forward = cp * ch * north + cp * sh * east - sp * down - .20
    right = (sr * sp * ch - cr * sh) * north + (sr * sp * sh + cr * ch) * east + sr * cp * down
    below = (cr * sp * ch + sr * sh) * north + (cr * sp * sh - sr * ch) * east + cr * cp * down - .12
    return math.degrees(math.atan2(right, forward)), math.degrees(math.atan2(-below, math.hypot(forward, right)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/eoir-api-check.json")
    args = parser.parse_args()
    sim = CheckClient()
    report = {"ok": False, "passed": [], "images": {}}
    prefix = "eoir-check-" + uuid4().hex[:8]
    tank_id, marker_id, missile_id = prefix + "-tank", prefix + "-marker", prefix + "-missile"
    created = set()
    latitude, longitude = 33.509, 126.490
    target_latitude = latitude + 100 / 111320
    target_longitude = longitude

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
                raise RuntimeError(f"Expected HTTP {status}, received {error.status}: {error}") from error
            return True
        return False

    def read_url(path):
        sim.request_count += 1
        with sim.opener.open(Request(sim.base_url + path, method="GET"), timeout=sim.timeout) as response:
            return response.headers, response.read()

    def target(state, id=tank_id):
        return next(item for item in state["targets"] if item["id"] == id)

    def settings(state):
        return {key: state["eoir"][key] for key in
                ("enabled", "mode", "fov_deg", "azimuth_deg", "elevation_deg", "target_id", "tracking_status")}

    def geometry_check(state, name):
        expected = expected_angles(state["aircraft"], target(state))
        sensor = state["eoir"]
        actual = sensor["azimuth_deg"], sensor["elevation_deg"]
        az_error = abs((actual[0] - expected[0] + 180) % 360 - 180)
        el_error = abs(actual[1] - expected[1])
        report.setdefault("geometry", []).append(dict(case=name, expected_deg=expected, actual_deg=actual,
                                                      azimuth_error_deg=az_error, elevation_error_deg=el_error))
        check(az_error < .5 and el_error < .5 and sensor["error_deg"] < .01
              and sensor["tracking_status"] == "tracking" and not sensor["limited"]
              and sensor["target_in_fov"], name)

    def place(height, heading=0, pitch=0, roll=0):
        return sim.set_initial_condition(latitude_deg=latitude, longitude_deg=longitude,
            ellipsoid_height_m=height, heading_deg=heading, pitch_deg=pitch, roll_deg=roll,
            speed_mps=19, angle_of_attack_deg=0, sideslip_deg=0)

    def decode_frame(data):
        with Image.open(BytesIO(data)) as source:
            source.load()
            if source.format != "JPEG" or source.size != (1280, 960):
                raise RuntimeError(f"Expected 1280x960 JPEG, got {source.format} {source.size}")
            return source.convert("RGB")

    def frame(mode, path, *, direct=False, label=None):
        state = sim.set_eoir(enabled=True, mode=mode, fov_deg=12)
        if direct:
            headers, data = read_url("/api/v1/eoir/frame.jpg")
            names = ("Content-Type", "Cache-Control", "X-Simulation-Time-S", "X-Sensor-Sequence", "X-Sensor-Mode")
            report["frame_headers"] = {name: headers.get(name) for name in names}
            check(headers.get_content_type() == "image/jpeg" and "no-store" in headers.get("Cache-Control", "")
                  and headers.get("X-Sensor-Mode") == mode
                  and int(headers.get("X-Sensor-Sequence", -1)) > state["eoir"]["frame_sequence"]
                  and abs(float(headers.get("X-Simulation-Time-S", "nan")) - state["aircraft"]["simulation_time_s"]) < 1e-9,
                  "JPEG HTTP response identifies content, disables caching and reports simulation time, sequence and mode")
        else:
            data = sim.get_eoir_frame()
        rgb = decode_frame(data)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        stats = ImageStat.Stat(rgb)
        report["images"][label or mode] = dict(path=str(path), bytes=len(data), size=rgb.size,
                                              mean=stats.mean, variance=stats.var)
        return rgb

    try:
        initial = sim.pause()
        if len(initial["targets"]) > 62:
            raise RuntimeError("This check needs two free target slots; existing targets are preserved")
        existing_targets = initial["targets"]
        main_headers, main_page = read_url("/")
        page_headers, page = read_url("/eoir")
        check(main_headers.get_content_type() == page_headers.get_content_type() == "text/html"
              and b'href="/eoir"' in main_page and b"<title>UAVSim" in page
              and b"/api/v1/eoir/frame.jpg" in page,
              "main page links to the separately served EO/IR page and its JPEG endpoint")
        sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
        sim.set_gust(east_mps=0, north_mps=0, up_mps=0, delay_s=0, duration_s=1)
        sim.set_failures(engine_power_fraction=1, elevator_jammed=False, aileron_jammed=False, rudder_jammed=False)
        sim.set_time_scale(1)
        sim.set_view("aircraft")
        ground = sim.set_ground_initial_condition(latitude_deg=latitude, longitude_deg=longitude,
                                                 heading_deg=0, ground_speed_mps=0)
        height = ground["ground"]["terrain_height_m"] + 150
        place(height)
        tank_body = dict(id=tank_id, kind="tank", latitude_deg=target_latitude,
                         longitude_deg=target_longitude, heading_deg=360)
        state = sim.create_target(**tank_body)
        created.add(tank_id)
        prop = target(state)
        check(prop["kind"] == "tank" and prop["heading_deg"] == 0
              and math.isfinite(prop["ellipsoid_height_m"]) and prop["ellipsoid_height_m"] < height - 100,
              "create tank on sampled ground and normalize 360-degree heading")
        state = sim.create_target(id=marker_id, kind="marker", latitude_deg=target_latitude,
                                  longitude_deg=longitude + .00025, heading_deg=60)
        created.add(marker_id)
        check(target(state, marker_id)["kind"] == "marker" and len(state["targets"]) == len(existing_targets) + 2,
              "create a second independent non-firing marker")
        props = state["targets"]
        check(rejected(lambda: sim.create_target(**tank_body), 409) and sim.get_state()["targets"] == props,
              "duplicate ID is rejected and existing target poses are preserved")
        check(rejected(lambda: sim.create_target(**{**tank_body, "id": prefix + "-absent-ground",
                                                    "latitude_deg": 0, "longitude_deg": 0}), 409)
              and sim.get_state()["targets"] == props,
              "target creation without loaded ground is rejected without a phantom prop")
        invalid = [{"id": "../bad"}, {"id": 7}, {"kind": "unknown"}, {"latitude_deg": 91}, {"heading_deg": 361}]
        check(all(rejected(lambda change=change: sim.create_target(**{**tank_body, **change}), 400)
                  for change in invalid) and sim.get_state()["targets"] == props,
              "target ID type/syntax, kind, geographic and heading bounds are validated")
        check(rejected(lambda: sim.move_target(id=tank_id, latitude_deg=0, longitude_deg=0), 409)
              and sim.get_state()["targets"] == props,
              "failed terrain resampling preserves the previous target pose")
        sim.set_eoir(enabled=False)
        check(rejected(sim.get_eoir_frame, 409), "disabled EO/IR rejects JPEG capture")
        sim.set_eoir(enabled=True, mode="ccd", fov_deg=30)
        state = sim.set_gimbal(azimuth_deg=17, elevation_deg=-23)
        check(settings(state) == dict(enabled=True, mode="ccd", fov_deg=30, azimuth_deg=17,
                                      elevation_deg=-23, target_id="", tracking_status="manual"),
              "manual gimbal reports exact angles and independent camera settings")
        before = settings(state)
        bad_sensor = [dict(enabled=True, mode="unknown", fov_deg=30), dict(enabled=1, mode="ccd", fov_deg=30),
                      dict(enabled=True, mode="ccd", fov_deg=.5), dict(enabled=True, mode="ccd", fov_deg=91)]
        check(all(rejected(lambda body=body: sim.set_eoir(**body), 400) for body in bad_sensor)
              and rejected(lambda: sim.set_gimbal(azimuth_deg=181, elevation_deg=0), 400)
              and rejected(lambda: sim.set_gimbal(azimuth_deg=0, elevation_deg=31), 400)
              and settings(sim.get_state()) == before,
              "sensor modes, boolean, FOV and gimbal travel limits reject invalid input without changing settings")
        zoomed = sim.set_eoir(enabled=True, mode="ccd", fov_deg=1)
        check(zoomed["eoir"]["fov_deg"] == 1 and zoomed["aircraft"] == state["aircraft"],
              "one-degree rendered zoom is accepted without changing aircraft state")
        sim.set_eoir(enabled=True, mode="ccd", fov_deg=30)
        state = sim.track_target(tank_id)
        geometry_check(state, "selected tank pointing agrees with independent WGS84 and body-frame angles")
        before = sim.set_eoir(enabled=False)
        try:
            moved = sim.step(.5)
            a, b = before["aircraft"], moved["aircraft"]
            distance = 111320 * math.hypot(b["latitude_deg"] - a["latitude_deg"],
                (b["longitude_deg"] - a["longitude_deg"]) * math.cos(math.radians(latitude)))
            report["disabled_tracking_step"] = dict(distance_m=distance, before=before["eoir"], after=moved["eoir"])
            check(distance > 5 and abs(b["simulation_time_s"] - a["simulation_time_s"] - .5) < 1e-9
                  and not moved["eoir"]["enabled"] and moved["eoir"]["target_id"] == tank_id
                  and moved["eoir"]["tracking_status"] == "tracking" and moved["eoir"]["error_deg"] < .01
                  and abs(moved["eoir"]["elevation_deg"] - before["eoir"]["elevation_deg"]) > .1
                  and moved["eoir"]["frame_sequence"] == before["eoir"]["frame_sequence"]
                  and not moved["fault"] and not b["running"],
                  "disabled capture preserves tracking and updates pointing during a real 0.5-second flight step")
        finally:
            place(height)
            state = sim.set_eoir(enabled=True, mode="ccd", fov_deg=30)
        azimuth = state["eoir"]["azimuth_deg"]
        state = sim.move_target(id=tank_id, latitude_deg=target_latitude,
                                longitude_deg=longitude + 70 / (111320 * math.cos(math.radians(latitude))),
                                heading_deg=95)
        check(target(state)["heading_deg"] == 95 and state["eoir"]["azimuth_deg"] > azimuth + 20,
              "moving selected target east updates its pose and turns gimbal right while paused")
        geometry_check(state, "moved target retains geometric pointing accuracy")
        state = place(height, heading=20, roll=15)
        geometry_check(state, "aircraft heading and roll changes transform the target into the new body frame")
        sim.set_eoir(enabled=True, mode="ccd", fov_deg=5)
        state = place(height, heading=35, pitch=-89)
        check(state["eoir"]["limited"] and state["eoir"]["tracking_status"] == "limited"
              and state["eoir"]["elevation_deg"] == 30 and state["eoir"]["error_deg"] > 5
              and not state["eoir"]["target_in_fov"],
              "target outside elevation travel is clamped and lies outside narrow rectangular FOV")
        state = place(height)
        stable_aircraft = state["aircraft"]
        sequence = state["eoir"]["frame_sequence"]
        ccd = frame("ccd", ROOT / "artifacts/eoir-ccd.jpg", direct=True)
        r, g, b = ccd.split()
        color = max(ImageStat.Stat(ImageChops.difference(r, g)).mean + ImageStat.Stat(ImageChops.difference(g, b)).mean)
        check(max(ImageStat.Stat(ccd).var) > 4 and color > 1,
              "CCD returns a non-uniform color 1280x960 JPEG scene")
        white = frame("ir_white", ROOT / "artifacts/eoir-ir-white.jpg")
        channels = white.split()
        check(ImageChops.difference(channels[0], channels[1]).getextrema()[1] <= 2
              and ImageChops.difference(channels[1], channels[2]).getextrema()[1] <= 2
              and max(ImageStat.Stat(white).var) > 4,
              "IR white display returns non-uniform grayscale JPEG")
        black = frame("ir_black", ROOT / "artifacts/eoir-ir-black.jpg")
        channels = black.split()
        inversion_error = max(ImageStat.Stat(ImageChops.difference(ImageChops.invert(white), black)).mean)
        report["ir_inversion_mean_error"] = inversion_error
        check(ImageChops.difference(channels[0], channels[1]).getextrema()[1] <= 2
              and ImageChops.difference(channels[1], channels[2]).getextrema()[1] <= 2 and inversion_error < 8,
              "IR black display inverts the paused scene luminance within JPEG tolerance")
        state = sim.get_state()
        check(state["aircraft"] == stable_aircraft and state["view_mode"] == "aircraft"
              and state["eoir"]["frame_sequence"] >= sequence + 3,  # The open viewer can also request frames.
              "camera captures advance only image sequence and preserve paused aircraft and chase view")
        sim.set_eoir(enabled=True, mode="ccd", fov_deg=12)
        sim.set_gimbal(azimuth_deg=0, elevation_deg=30)
        try:
            _, data = read_url("/api/v1/eoir/frame.jpg")
            sky_aircraft = decode_frame(data)
            sim.set_view("overview")
            _, data = read_url("/api/v1/eoir/frame.jpg")
            sky_overview = decode_frame(data)
            sky_error = ImageStat.Stat(ImageChops.difference(sky_aircraft, sky_overview)).mean
            report["overview_sky_comparison"] = dict(mean_error_rgb=sky_error,
                aircraft_mean_rgb=ImageStat.Stat(sky_aircraft).mean,
                overview_mean_rgb=ImageStat.Stat(sky_overview).mean)
            # A blue sky need not contain much red; luminance detects a missing/black sky.
            check(max(sky_error) <= 2 and ImageStat.Stat(sky_aircraft.convert("L")).mean[0] > 20,
                  "switching main camera to overview preserves the independent EO/IR sky image within render tolerance")
        finally:
            sim.set_view("aircraft")
        sim.set_eoir(enabled=True, mode="ir_white", fov_deg=37)
        before = sim.set_gimbal(azimuth_deg=17, elevation_deg=-23)
        state = sim.reset()
        check(settings(state) == settings(before) and state["targets"] == before["targets"],
              "aircraft reset preserves manual sensor settings and all targets")
        place(height)
        state = sim.track_target(tank_id)
        last_angles = state["eoir"]["azimuth_deg"], state["eoir"]["elevation_deg"]
        state = sim.remove_target(tank_id)
        created.remove(tank_id)
        check(not state["eoir"]["target_id"] and state["eoir"]["tracking_status"] == "target_missing"
              and (state["eoir"]["azimuth_deg"], state["eoir"]["elevation_deg"]) == last_angles
              and all(item["id"] != tank_id for item in state["targets"]),
              "deleting selected target clears its ID and holds last angles with missing status")
        check(rejected(lambda: sim.track_target(tank_id), 409)
              and rejected(lambda: sim.remove_target(tank_id), 409)
              and rejected(lambda: sim.move_target(id=tank_id, latitude_deg=target_latitude,
                                                   longitude_deg=target_longitude), 409),
              "tracking, removing and moving missing IDs return explicit conflicts")
        sim.create_target(**tank_body)
        created.add(tank_id)
        sim.remove_target(marker_id)
        created.remove(marker_id)
        sim.create_target(id=missile_id, kind="missile", latitude_deg=target_latitude,
                          longitude_deg=longitude + .0002, heading_deg=60)
        created.add(missile_id)
        state = sim.track_target(missile_id)
        missile = frame("ccd", ROOT / "artifacts/eoir-missile.jpg", label="missile")
        check(target(state, missile_id)["kind"] == "missile" and state["eoir"]["target_in_fov"]
              and max(ImageStat.Stat(missile).var) > 4,
              "non-firing missile prop supports selected-ID pointing and a rendered JPEG artifact")
        sim.remove_target(missile_id)
        created.remove(missile_id)
        sim.set_eoir(enabled=True, mode="ccd", fov_deg=12)
        final = sim.track_target(tank_id)
        report["final"] = final
        check(not final["aircraft"]["running"] and final["aircraft"]["time_scale"] == 1
              and not final["fault"] and final["eoir"]["target_in_fov"]
              and all(item in final["targets"] for item in existing_targets),
              "leave a paused 1x CCD tank demonstration while preserving pre-existing targets")
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        if not report["ok"]:
            cleanup_errors = []
            try:
                created.update(item["id"] for item in sim.get_state()["targets"]
                               if item["id"].startswith(prefix + "-"))
            except Exception as error:
                cleanup_errors.append(str(error))
            actions = [sim.pause, lambda: sim.set_eoir(enabled=False)]
            actions += [lambda id=id: sim.remove_target(id) for id in sorted(created)]
            actions += [sim.reset_on_ground, lambda: sim.set_time_scale(1)]
            for action in actions:
                try:
                    action()
                except Exception as error:
                    cleanup_errors.append(str(error))
            report["failure_cleanup_errors"] = cleanup_errors
    report["api_requests"] = sim.request_count
    text = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
