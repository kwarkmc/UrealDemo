"""Check one real start/control/stop/restart cycle. API must be running, Unreal stopped."""
import json
from pathlib import Path
import runpy
import sys
import time
from urllib.request import ProxyHandler, build_opener

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from uavsim_client import UAVSimClient


def check_owned_error_guard():
    api = runpy.run_path(str(Path(__file__).with_name("serve-api.py")))
    engine = api["Engine"](None, "unused", 30010)
    engine.status = lambda: dict(status="error", managed=True, ready=False, pid=123)
    try:
        engine.execute("POST", "/api/v1/engine/start", {})
    except api["APIError"] as error:
        if error.status == 409:
            return True
        raise
    raise RuntimeError("An alive engine with a terrain error must be stopped before restart")


def main():
    sim = UAVSimClient()
    report = {"ok": False, "passed": []}
    started = False

    def check(condition, description):
        if not condition:
            raise RuntimeError(description)
        report["passed"].append(description)
        print(description, flush=True)

    def wait_for(expected):
        deadline = time.monotonic() + 180
        previous = None
        while time.monotonic() < deadline:
            state = sim.get_engine()
            if state["status"] != previous:
                print(json.dumps(state, ensure_ascii=False), flush=True)
                previous = state["status"]
            if state["status"] == expected:
                return state
            if state["status"] in ("error", "external"):
                raise RuntimeError(state["message"])
            time.sleep(1)
        raise RuntimeError(f"Unreal did not reach {expected} within 180 seconds")

    try:
        check(check_owned_error_guard(), "owned engine error cannot launch a duplicate")
        check(sim.get_engine()["status"] == "stopped", "initial engine stopped")
        first = sim.start_engine()
        started = True
        check(first["managed"] and first["pid"] == sim.start_engine()["pid"],
              "repeated start owns one process")
        active = wait_for("running")
        check(active["ready"] and sim.get_state()["aircraft"]["terrain_loaded"],
              "standalone starts with terrain and control API, no manual Play")
        sim.pause()
        controls = dict(elevator_deg=8, aileron_deg=-6, rudder_deg=4, throttle=0.25)
        check(sim.set_controls(**controls)["controls"] == controls, "standalone controls roundtrip")
        sim.stop_engine()
        check(not wait_for("stopped")["managed"], "graceful stop completes")
        started = False
        with build_opener(ProxyHandler({})).open("http://127.0.0.1:8765/", timeout=5) as response:
            check(response.status == 200 and b'engine-start' in response.read(), "Web UI remains available after engine stop")
        second = sim.start_engine()
        started = True
        check(second["pid"] != first["pid"] and wait_for("running")["ready"], "engine can restart")
        sim.stop_engine()
        wait_for("stopped")
        started = False
        check(sim.stop_engine()["status"] == "stopped", "repeated stop is harmless")
        report["pids"] = [first["pid"], second["pid"]]
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        if started:
            try:
                if sim.get_engine()["ready"]:
                    sim.stop_engine()
                    wait_for("stopped")
            except Exception as error:
                report["cleanup_error"] = str(error)
        output = Path(__file__).resolve().parents[1] / "artifacts/engine-api-check.json"
        output.parent.mkdir(exist_ok=True)
        output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
