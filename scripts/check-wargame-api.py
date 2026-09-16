"""Live abstract-game checks; no weapon physics, guidance or automatic hit tests.

Requires Unreal/API and the example catalog/Korea map. Refuses to erase existing
shots or hit marks. Uses its own targets, restores catalog bytes, starting pose,
commands and sensor settings, and leaves playback paused. Dynamics/navigation and
clocks are not a restorable simulator snapshot. --capture saves two native JPEGs.
"""
import argparse
import json
from pathlib import Path
import sys
import time
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/wargame-api-check.json")
    parser.add_argument("--capture", action="store_true")
    args = parser.parse_args()
    sim = CheckClient()
    report = {"ok": False, "passed": []}
    catalog = ROOT / "weapons/catalog.json"
    original = None
    initial = None
    started = False
    prefix = "game-check-" + uuid4().hex[:10]
    ids = [prefix + suffix for suffix in ("-hit", "-miss", "-cancel")]

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        report["passed"].append(name)
        print(name, file=sys.stderr, flush=True)

    def stable(state):
        return {**state, "eoir": {k: v for k, v in state["eoir"].items() if k not in ("frame_sequence", "error")},
                "wargame": {k: v for k, v in state["wargame"].items() if k != "error"}}

    def outside_events(state):
        return {k: v for k, v in stable(state).items() if k not in ("wargame", "targets")}

    def target(state, id):
        return next(item for item in state["targets"] if item["id"] == id)

    def shot(state, id):
        return next(item for item in state["wargame"]["shots"] if item["id"] == id)

    def rejected(call, status):
        try:
            call()
        except UAVSimError as error:
            if error.status != status:
                raise RuntimeError(f"Expected HTTP {status}, got {error.status}: {error}") from error
            return True
        return False

    def finish(id):
        state = sim.get_state()
        while shot(state, id)["elapsed_s"] + 1e-9 < shot(state, id)["duration_s"]:
            event = shot(state, id)
            state = sim.step(min(.5, event["duration_s"] - event["elapsed_s"]))
            if state["fault"] or state["ground"]["crashed"]:
                raise RuntimeError(state["fault"] or "Flight fixture crashed")
        return state

    def capture(name):
        path = ROOT / ("artifacts/wargame-" + name + ".jpg")
        image = sim.get_eoir_frame()
        if not image.startswith(b"\xff\xd8") or not image.endswith(b"\xff\xd9"):
            raise RuntimeError("Capture was not JPEG")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(image)
        report.setdefault("captures", {})[name] = str(path)

    try:
        initial = sim.get_state()
        if initial["wargame"]["shots"] or any(t["hit"] or t["hit_count"] or t["last_hit_shot_id"] for t in initial["targets"]):
            raise RuntimeError("Existing game records/hit marks present; refused before changing state")
        original = catalog.read_bytes()
        source = json.loads(original)
        started = True
        sim.pause()
        sim.set_initial_condition(latitude_deg=33.509, longitude_deg=126.490, ellipsoid_height_m=500,
                                  heading_deg=0, pitch_deg=0, roll_deg=0, speed_mps=19)
        sim.set_gimbal(azimuth_deg=0, elevation_deg=-45)
        sim.set_time_scale(1)
        state = sim.reload_wargame_weapons()
        asset = next(item for item in state["wargame"]["weapons"] if item["id"] == source["weapons"][0]["id"])
        weapon = asset["id"]
        check(asset["mesh"] == source["weapons"][0]["mesh"] and asset["triangle_count"] > 0
              and asset["animation_duration_s"] == source["weapons"][0]["animation_duration_s"]
              and (ROOT / "weapons" / asset["mesh"]).is_file(), "separate visual catalog/model metadata loaded")
        before = sim.get_state()
        state = sim.reload_wargame_weapons()
        check(stable(state) == stable(before), "valid paused catalog reload preserves scene and flight")
        invalid = json.loads(original)
        invalid["weapons"][0]["mesh"] = "../aircraft/models/example.stl"
        catalog.write_text(json.dumps(invalid), encoding="utf-8")
        check(rejected(sim.reload_wargame_weapons, 409) and stable(sim.get_state()) == stable(state),
              "invalid catalog path rejected atomically with old catalog retained")
        catalog.write_bytes(original)
        sim.reload_wargame_weapons()
        for index, id in enumerate(ids):
            state = sim.create_target(id=id, kind="tank" if index == 0 else "marker",
                                     latitude_deg=33.5099, longitude_deg=126.490 + .0002 * index)
        bad = [("wargame/fire", {"weapon_id": value, "target_id": ids[0]}) for value in (True, "", "x" * 49, "../bad")]
        bad += [("wargame/resolve", {"shot_id": "shot_1", "outcome": value}) for value in (True, "pending", "unknown")]
        bad += [("wargame/reset", {"extra": 0}), ("wargame/weapons/reload", {"extra": 0}),
                ("wargame/fire", {"weapon_id": weapon}), ("wargame/resolve", {"shot_id": "shot_1", "outcome": "hit", "extra": 0})]
        check(all(rejected(lambda route=route, body=body: sim._request("POST", route, body), 400) for route, body in bad)
              and stable(sim.get_state()) == stable(state), "invalid ID types/lengths and missing/extra/outcome fields rejected")
        check(rejected(lambda: sim.play_wargame_shot(prefix + "-unknown", ids[0]), 409)
              and rejected(lambda: sim.play_wargame_shot(weapon, prefix + "-unknown"), 409)
              and rejected(lambda: sim.resolve_wargame_shot(prefix + "-unknown", "hit"), 409)
              and stable(sim.get_state()) == stable(state), "unknown visual, target or shot IDs rejected without scene mutation")
        if args.capture:
            sim.track_target(ids[0])
            sim.set_eoir(enabled=True, mode="ccd", fov_deg=8)
            capture("normal")
        before = sim.get_state()
        fired = sim.play_wargame_shot(weapon, ids[0])
        id = fired["wargame"]["shots"][-1]["id"]
        event = shot(fired, id)
        check(len(fired["wargame"]["shots"]) == len(before["wargame"]["shots"]) + 1
              and event["phase"] == "animating" and event["outcome"] == "pending" and event["elapsed_s"] == 0
              and outside_events(fired) == outside_events(before) and fired["targets"] == before["targets"],
              "fire adds a visual event without changing aircraft, controls, camera or target marks")
        time.sleep(.25)
        check(stable(sim.get_state()) == stable(fired), "paused wall time does not advance animation or simulation")
        check(rejected(lambda: sim.resolve_wargame_shot(id, "hit"), 409)
              and rejected(sim.reload_wargame_weapons, 409) and stable(sim.get_state()) == stable(fired),
              "early adjudication and active-animation reload are rejected atomically")
        followed = sim.set_view("projectile")
        check(followed["view_mode"] == "projectile" and followed["aircraft"] == fired["aircraft"]
              and stable(followed)["eoir"] == stable(fired)["eoir"],
              "projectile spectator view preserves paused flight and independent EOIR settings")
        dt = min(.5, event["duration_s"] / 2)
        progressed = sim.step(dt)
        check(shot(progressed, id)["phase"] == "animating" and abs(shot(progressed, id)["elapsed_s"] - dt) < 1e-9
              and abs(progressed["wargame"]["time_s"] - fired["wargame"]["time_s"] - dt) < 1e-9,
              "explicit step advances event elapsed time and independent game clock")
        waiting = finish(id)
        check(waiting["view_mode"] == "aircraft" and rejected(lambda: sim.set_view("projectile"), 409),
              "completion restores aircraft view and no active animation refuses projectile view")
        event = shot(waiting, id)
        check(event["phase"] == "awaiting_result" and abs(event["elapsed_s"] - event["duration_s"]) <= 1e-9
              and event["outcome"] == "pending" and not target(waiting, ids[0])["hit"],
              "animation completion waits for operator; never automatically marks a hit")
        hit = sim.resolve_wargame_shot(id, "hit")
        marked = target(hit, ids[0])
        check(shot(hit, id)["phase"] == "resolved" and shot(hit, id)["outcome"] == "hit"
              and marked["hit"] and marked["hit_count"] == 1 and marked["last_hit_shot_id"] == id
              and shot(hit, id)["resolved_time_s"] == hit["wargame"]["time_s"]
              and outside_events(hit) == outside_events(waiting), "explicit hit records one game mark and adjudication time")
        if args.capture:
            capture("hit")
        check(rejected(lambda: sim.resolve_wargame_shot(id, "hit"), 409) and stable(sim.get_state()) == stable(hit),
              "duplicate adjudication rejected without adding another mark")
        for target_id in ids[:2]:
            fired = sim.play_wargame_shot(weapon, target_id)
            miss_id = fired["wargame"]["shots"][-1]["id"]
            finish(miss_id)
            state = sim.resolve_wargame_shot(miss_id, "miss")
        check(target(state, ids[0]) == marked and not target(state, ids[1])["hit"]
              and target(state, ids[1])["hit_count"] == 0 and shot(state, miss_id)["outcome"] == "miss",
              "miss neither marks a normal target nor removes an earlier hit mark")
        state = sim.play_wargame_shot(weapon, ids[2])
        canceled_id = state["wargame"]["shots"][-1]["id"]
        canceled = sim.remove_target(ids[2])
        check(shot(canceled, canceled_id)["phase"] == "canceled" and shot(canceled, canceled_id)["outcome"] == "canceled",
              "deleting a pending target cancels its visual event")
        before = sim.get_state()
        sim.set_running(True)
        refused = rejected(sim.reload_wargame_weapons, 409)
        state = sim.pause()
        check(refused and state["wargame"]["weapons"] == before["wargame"]["weapons"], "running catalog reload rejected with catalog preserved")
        old_ids = {item["id"] for item in state["wargame"]["shots"]}
        clean = sim.reset_wargame()
        target_poses = lambda s: [{k: v for k, v in item.items() if k not in ("hit", "hit_count", "last_hit_shot_id")} for item in s["targets"]]
        check(not clean["wargame"]["shots"] and clean["wargame"]["time_s"] == 0
              and all(not t["hit"] and t["hit_count"] == 0 and not t["last_hit_shot_id"] for t in clean["targets"])
              and outside_events(clean) == outside_events(state) and target_poses(clean) == target_poses(state),
              "game reset clears records/time/marks while preserving flight, target poses and camera")
        replay = sim.play_wargame_shot(weapon, ids[0])
        check(replay["wargame"]["shots"][-1]["id"] not in old_ids, "shot IDs are not reused after game reset")
        report["events_before_reset"] = state["wargame"]["shots"]
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        if started:
            errors = []
            def attempt(call):
                try:
                    return call()
                except Exception as error:
                    errors.append(str(error))
                    return None
            attempt(lambda: catalog.write_bytes(original))
            attempt(sim.pause)
            attempt(sim.reset_wargame)
            attempt(sim.reload_wargame_weapons)
            current = attempt(sim.get_state)
            for id in ids:
                if current and any(t["id"] == id for t in current["targets"]):
                    attempt(lambda id=id: sim.remove_target(id))
            a, sensor = initial["aircraft"], initial["eoir"]
            if initial["ground"]["on_ground"]:
                attempt(lambda: sim.set_ground_initial_condition(latitude_deg=a["latitude_deg"], longitude_deg=a["longitude_deg"],
                    heading_deg=a["heading_deg"], ground_speed_mps=a["ground_speed_mps"]))
            else:
                attempt(lambda: sim.set_initial_condition(**{k: a[k] for k in ("latitude_deg", "longitude_deg", "ellipsoid_height_m",
                    "heading_deg", "pitch_deg", "roll_deg", "speed_mps")}, angle_of_attack_deg=a["angle_of_attack_deg"], sideslip_deg=a["sideslip_deg"]))
            attempt(lambda: sim.set_controls(**initial["controls"]))
            attempt(lambda: sim.set_ground_controls(**{k: initial["ground"][k] for k in ("steering_deg", "brake", "parking_brake")}))
            attempt(lambda: sim.set_time_scale(a["time_scale"]))
            attempt(lambda: sim.set_view(initial["view_mode"]))
            mode = sensor["gimbal_mode"]
            parameters = {"azimuth_deg": sensor["azimuth_deg"], "elevation_deg": sensor["elevation_deg"]} if mode == "body_fixed" else (
                {"target_id": sensor["target_id"]} if mode == "auto" else {k: sensor["point_" + k] for k in
                ("latitude_deg", "longitude_deg", "ellipsoid_height_m")} if mode == "geo" else {})
            attempt(lambda: sim.set_gimbal_mode(mode, **parameters))
            attempt(lambda: sim.set_eoir(enabled=sensor["enabled"], mode=sensor["mode"], fov_deg=sensor["fov_deg"]))
            final = attempt(sim.get_state)
            report["restored"] = final
            if not final or final["targets"] != initial["targets"] or final["aircraft"]["running"] or final["wargame"]["shots"]:
                errors.append("Target preservation, event cleanup or pause verification failed")
            if final and (abs(final["aircraft"]["latitude_deg"] - a["latitude_deg"]) > 1e-7
                    or abs(final["aircraft"]["longitude_deg"] - a["longitude_deg"]) > 1e-7
                    or abs(final["aircraft"]["ellipsoid_height_m"] - a["ellipsoid_height_m"]) > 1e-3
                    or final["controls"] != initial["controls"] or final["view_mode"] != initial["view_mode"]
                    or any(final["eoir"][key] != sensor[key] for key in ("gimbal_mode", "enabled", "mode", "fov_deg", "target_id",
                        "point_latitude_deg", "point_longitude_deg", "point_ellipsoid_height_m"))):
                errors.append("Starting pose, commands or sensor settings were not restored")
            if original != attempt(catalog.read_bytes):
                errors.append("Catalog bytes were not restored")
            report["restore_errors"] = errors
            if errors:
                report["ok"] = False
            else:
                check(True, "finally restores catalog/pose/settings, removes own targets and leaves paused with no events")
    report["api_requests"] = sim.request_count
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"ok": report["ok"], "passed": len(report["passed"]), "api_requests": sim.request_count,
                      "output": str(args.output), "error": report.get("error"), "restore_errors": report.get("restore_errors")}, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
