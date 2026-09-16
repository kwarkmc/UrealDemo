"""Local, versioned UAVSim API and web controls. No external Python dependencies."""
import argparse
import base64
import binascii
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import re
import socket
import subprocess
from threading import Lock
from urllib.error import HTTPError, URLError
from urllib.request import ProxyHandler, Request, build_opener


ROOT = Path(__file__).resolve().parents[1]
CONTROL_FIELDS = {
    "elevator_deg": "ElevatorDeg", "aileron_deg": "AileronDeg",
    "rudder_deg": "RudderDeg", "throttle": "Throttle",
}
GROUND_CONTROL_FIELDS = {"steering_deg": "SteeringDeg", "brake": "Brake", "parking_brake": "ParkingBrake"}
GROUND_FIELDS = GROUND_CONTROL_FIELDS | {
    "on_ground": "GroundOnGround", "wheels_on_ground": "WheelsOnGround",
    "height_agl_m": "HeightAglM", "terrain_height_m": "TerrainHeightM",
    "normal_force_n": "GroundNormalForceN", "impact_speed_mps": "ImpactSpeedMps",
    "crashed": "Crashed", "surface": "GroundSurfaceName", "available": "GroundAvailable",
}
GROUND_INITIAL_FIELDS = {"latitude_deg": "LatitudeDeg", "longitude_deg": "LongitudeDeg",
                         "heading_deg": "HeadingDeg", "ground_speed_mps": "GroundSpeedMps"}
GEAR_FIELDS = {"command_down": "GearCommandDown", "extension": "GearExtension",
               "down_locked": "GearDownLocked", "up_locked": "GearUpLocked", "moving": "GearMoving"}
AUTOMATION_FIELDS = {
    "phase": "OperationPhase", "kind": "OperationKind", "runway_index": "OperationRunwayIndex",
    "runway_name": "OperationRunwayName", "completed": "OperationCompleted", "message": "OperationMessage",
    "cross_track_m": "OperationCrossTrackM", "along_track_m": "OperationAlongTrackM",
    "target_height_m": "OperationTargetHeightM",
}
ACTUATOR_FIELDS = {"elevator_deg": "AppliedElevatorDeg", "aileron_deg": "AppliedAileronDeg",
                   "rudder_deg": "AppliedRudderDeg", "throttle": "AppliedThrottle", "thrust_n": "ThrustN"}
AIRFRAME_FIELDS = {"id": "AirframeId", "profile": "AirframeProfile", "fingerprint": "AirframeFingerprint", "error": "AirframeError"}
VISUAL_FIELDS = {"source": "VisualSource", "config": "VisualConfig", "mesh": "VisualMesh",
                 "triangle_count": "VisualTriangleCount", "span_m": "VisualSpanM",
                 "fingerprint": "VisualFingerprint", "error": "VisualError", "show_builtin_gear": "VisualShowBuiltinGear"}
EOIR_FIELDS = {"enabled": "SensorEnabled", "mode": "SensorMode", "fov_deg": "SensorFovDeg",
               "azimuth_deg": "GimbalAzimuthDeg", "elevation_deg": "GimbalElevationDeg",
               "gimbal_mode": "GimbalMode", "point_latitude_deg": "GimbalPointLatitudeDeg",
               "point_longitude_deg": "GimbalPointLongitudeDeg", "point_ellipsoid_height_m": "GimbalPointEllipsoidHeightM",
               "target_id": "GimbalTargetId", "tracking_status": "GimbalTrackingStatus",
               "limited": "GimbalLimited", "target_in_fov": "GimbalTargetInFov",
               "error_deg": "GimbalErrorDeg", "frame_sequence": "SensorFrameSequence", "error": "SensorError"}
TARGET_FIELDS = {"id": "Id", "kind": "Kind", "latitude_deg": "LatitudeDeg", "longitude_deg": "LongitudeDeg",
                 "ellipsoid_height_m": "EllipsoidHeightM", "heading_deg": "HeadingDeg",
                 "hit": "Hit", "hit_count": "HitCount", "last_hit_shot_id": "LastHitShotId"}
WARGAME_WEAPON_FIELDS = {"id": "Id", "label": "Label", "mesh": "Mesh",
                         "animation_duration_s": "AnimationDurationS", "triangle_count": "TriangleCount"}
WARGAME_SHOT_FIELDS = {"id": "Id", "weapon_id": "WeaponId", "target_id": "TargetId", "phase": "Phase",
                       "outcome": "Outcome", "elapsed_s": "ElapsedS", "duration_s": "DurationS", "resolved_time_s": "ResolvedTimeS"}
SENSOR_MODES = ("ccd", "ir_white", "ir_black")
GUST_FIELDS = {"east_mps": "GustEastMps", "north_mps": "GustNorthMps", "up_mps": "GustUpMps",
               "delay_s": "DelayS", "duration_s": "DurationS"}
FAILURE_FIELDS = {"engine_power_fraction": "EnginePowerFraction", "elevator_jammed": "ElevatorJammed",
                  "aileron_jammed": "AileronJammed", "rudder_jammed": "RudderJammed"}
DISTURBANCE_FIELDS = FAILURE_FIELDS | {
    "gust_east_mps": "GustEastMps", "gust_north_mps": "GustNorthMps", "gust_up_mps": "GustUpMps",
    "gust_start_s": "GustStartS", "gust_duration_s": "GustDurationS",
    "effective_wind_east_mps": "EffectiveWindEastMps", "effective_wind_north_mps": "EffectiveWindNorthMps",
    "effective_wind_up_mps": "EffectiveWindUpMps",
}
POSE_FIELDS = {
    "latitude_deg": "LatitudeDeg", "longitude_deg": "LongitudeDeg",
    "ellipsoid_height_m": "EllipsoidHeightM", "heading_deg": "HeadingDeg",
    "pitch_deg": "PitchDeg", "roll_deg": "RollDeg",
}
ENVIRONMENT_FIELDS = {"wind_east_mps": "WindEastMps", "wind_north_mps": "WindNorthMps", "wind_up_mps": "WindUpMps"}
AERODYNAMIC_FIELDS = {"angle_of_attack_deg": "AngleOfAttackDeg", "sideslip_deg": "SideslipDeg"}
AIRCRAFT_FIELDS = POSE_FIELDS | {
    "simulation_time_s": "SimulationTimeS", "running": "Running",
    "terrain_loaded": "TerrainLoaded",
    "speed_mps": "SpeedMps", "vertical_speed_mps": "VerticalSpeedMps",
    "time_scale": "TimeScale", "effective_time_scale": "EffectiveTimeScale",
    "distance_travelled_m": "DistanceTravelledM",
    "roll_rate_deg_s": "RollRateDegS", "pitch_rate_deg_s": "PitchRateDegS", "yaw_rate_deg_s": "YawRateDegS",
    "angle_of_attack_deg": "AngleOfAttackDeg", "sideslip_deg": "SideslipDeg",
    "ground_speed_mps": "GroundSpeedMps", "air_density_kg_m3": "AirDensityKgM3",
    "dynamic_pressure_pa": "DynamicPressurePa", "lift_coefficient": "LiftCoefficient",
    "drag_coefficient": "DragCoefficient", "flow_separation": "FlowSeparation",
    "stall_warning": "StallWarning", "stalled": "Stalled", "load_factor": "LoadFactor",
}
NAVIGATION_FIELDS = {"mode": "NavigationMode", "stall_recovery": "StallRecovery", "active_waypoint_index": "ActiveWaypointIndex",
                     "waypoint_count": "WaypointCount", "distance_to_waypoint_m": "DistanceToWaypointM",
                     "loiter_radius_m": "LoiterRadiusM"}
WAYPOINT_FIELDS = {key: POSE_FIELDS[key] for key in ("latitude_deg", "longitude_deg", "ellipsoid_height_m")} | {"speed_mps": "SpeedMps"}


class APIError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def fields(body, expected):
    if type(body) is not dict or set(body) != set(expected):
        raise APIError(400, "Expected exactly these fields: " + ", ".join(expected))


def number(body, name, low, high):
    value = body[name]
    if type(value) not in (int, float) or not low <= value <= high or not math.isfinite(value):
        raise APIError(400, f"{name} must be a finite number in [{low}, {high}].")
    return value


def runway_index(body):
    if type(body["runway_index"]) is not int:
        raise APIError(400, "runway_index must be an integer in [0, 15].")
    return number(body, "runway_index", 0, 15)


def target_id(body, key="id", allow_empty=False):
    value = body[key]
    if type(value) is not str or not (allow_empty and value == "" or re.fullmatch(r"[A-Za-z0-9_-]{1,48}", value)):
        raise APIError(400, f"{key} must contain 1..48 ASCII letters, digits, underscores or hyphens."
                       + (" An empty string cancels tracking." if allow_empty else ""))
    return value


def strict_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate field: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"Invalid JSON number: {value}")


class Unreal:
    def __init__(self, port, object_path=None):
        self.url = f"http://127.0.0.1:{port}/remote/object/call"
        self.override = object_path
        self.opener = build_opener(ProxyHandler({}))

    def object_path(self):
        try:
            receipt = {} if self.override else json.loads(
                (ROOT / "Saved/uavsim-runtime.json").read_text(encoding="utf-8-sig"))
            path = self.override or receipt["object_path"]
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise APIError(503, "No active Unreal runtime. Start Play in the UAVSim editor.") from error
        is_pie = isinstance(path, str) and re.match(r"^/Game/(?:.*/)?UEDPIE_\d+_.*:PersistentLevel\.", path)
        is_game = (isinstance(path, str) and path.startswith("/Game/")
                   and ":PersistentLevel." in path and receipt.get("world_kind") == "Game")
        if not (is_pie or is_game):
            raise APIError(503, "Runtime locator must identify a PIE or Game actor; editor actors are not accepted.")
        return path

    def call(self, path, function, parameters=None, timeout=10):
        body = {"objectPath": path, "functionName": function, "parameters": parameters or {}}
        request = Request(self.url, json.dumps(body, allow_nan=False).encode("utf-8"),
                          {"Content-Type": "application/json"}, method="PUT")
        try:
            with self.opener.open(request, timeout=timeout) as response:
                result = json.load(response)
            return result.get("ReturnValue")
        except (HTTPError, URLError, OSError, ValueError, AttributeError) as error:
            # Never replay mutations: a timeout can occur after Unreal applied the command.
            raise APIError(503, "Unreal did not return a valid response. The command was not retried; "
                           "a write may already have completed. Check state before continuing.") from error

    def state(self, path, timeout=10):
        native = self.call(path, "GetAircraftState", timeout=timeout)
        try:
            return {"aircraft": {key: native[value] for key, value in AIRCRAFT_FIELDS.items()},
                    "controls": {key: native[value] for key, value in CONTROL_FIELDS.items()},
                    "ground": {key: native[value] for key, value in GROUND_FIELDS.items()},
                    "gear": {key: native[value] for key, value in GEAR_FIELDS.items()},
                    "automation": {key: native[value] for key, value in AUTOMATION_FIELDS.items()},
                    "actuators": {key: native[value] for key, value in ACTUATOR_FIELDS.items()},
                    "airframe": {key: native[value] for key, value in AIRFRAME_FIELDS.items()},
                    "visual": {key: native[value] for key, value in VISUAL_FIELDS.items()},
                    "eoir": {key: native[value] for key, value in EOIR_FIELDS.items()},
                    "targets": [{key: target[value] for key, value in TARGET_FIELDS.items()} for target in native["Targets"]],
                    "wargame": {"time_s": native["WargameTimeS"], "error": native["WargameError"],
                                "weapons": [{key: weapon[value] for key, value in WARGAME_WEAPON_FIELDS.items()} for weapon in native["WargameWeapons"]],
                                "shots": [{key: shot[value] for key, value in WARGAME_SHOT_FIELDS.items()} for shot in native["WargameShots"]]},
                    "disturbance": {key: native[value] for key, value in DISTURBANCE_FIELDS.items()},
                    "navigation": {key: native[value] for key, value in NAVIGATION_FIELDS.items()},
                    "environment": {key: native[value] for key, value in ENVIRONMENT_FIELDS.items()},
                    "view_mode": native["ViewMode"],
                    "terrain": {"dataset": native["TerrainDataset"], "tile_count": native["TerrainTileCount"],
                                "manifest": native["TerrainManifest"], "attribution": native["TerrainAttribution"],
                                "error": native["TerrainError"]},
                    "object_path": path, "model": native["Model"], "fault": native["Fault"]}
        except (TypeError, KeyError) as error:
            raise APIError(503, "Unreal state schema is unavailable. Build the current UAVSim project.") from error

    def sensor_frame(self):
        native = self.call(self.object_path(), "CaptureSensorFrame")
        try:
            if native["Error"]:
                raise APIError(409, str(native["Error"]))
            payload = base64.b64decode(native["ImageBase64"], validate=True)
            mode, sequence, seconds = native["Mode"], native["Sequence"], native["SimulationTimeS"]
            if (mode not in SENSOR_MODES or type(sequence) is not int or sequence < 0
                    or type(seconds) not in (int, float) or not math.isfinite(seconds) or seconds < 0
                    or any(type(native[key]) is not int or native[key] <= 0 for key in ("Width", "Height"))
                    or not payload.startswith(b"\xff\xd8") or not payload.endswith(b"\xff\xd9")):
                raise ValueError("Invalid JPEG frame or metadata")
            return payload, {"X-Simulation-Time-S": str(seconds), "X-Sensor-Sequence": str(sequence), "X-Sensor-Mode": mode}
        except (TypeError, KeyError, ValueError, binascii.Error) as error:
            raise APIError(503, "Unreal returned an invalid sensor frame.") from error

    def execute(self, method, route, body):
        if method == "GET" and route == "/api/v1/state":
            path = self.object_path()
            return self.state(path)
        if method == "PUT" and route == "/api/v1/controls":
            fields(body, CONTROL_FIELDS)
            parameters = {native: number(body, key, 0 if key == "throttle" else -25,
                                         1 if key == "throttle" else 25)
                          for key, native in CONTROL_FIELDS.items()}
            function = "SetControls"
        elif method == "PUT" and route == "/api/v1/ground-controls":
            fields(body, GROUND_CONTROL_FIELDS)
            if type(body["parking_brake"]) is not bool:
                raise APIError(400, "parking_brake must be a boolean.")
            function = "SetGroundControls"
            parameters = {"SteeringDeg": number(body, "steering_deg", -30, 30),
                          "Brake": number(body, "brake", 0, 1), "ParkingBrake": body["parking_brake"]}
        elif method == "POST" and route == "/api/v1/ground/reset":
            fields(body, ())
            function, parameters = "ResetOnGround", {}
        elif method == "PUT" and route == "/api/v1/ground/initial-condition":
            fields(body, GROUND_INITIAL_FIELDS)
            ranges = [(-90, 90), (-180, 180), (-360, 360), (0, 15)]
            parameters = {native: number(body, key, *limits)
                          for (key, native), limits in zip(GROUND_INITIAL_FIELDS.items(), ranges)}
            function = "SetGroundInitialCondition"
        elif method == "PUT" and route == "/api/v1/gear":
            fields(body, ("down",))
            if type(body["down"]) is not bool:
                raise APIError(400, "down must be a boolean.")
            function, parameters = "SetLandingGear", {"Down": body["down"]}
        elif method == "POST" and route == "/api/v1/automation/takeoff":
            fields(body, ("runway_index", "climb_height_m", "speed_mps"))
            function = "StartAutomaticTakeoff"
            parameters = {"RunwayIndex": runway_index(body), "ClimbHeightM": number(body, "climb_height_m", 20, 150),
                          "SpeedMps": number(body, "speed_mps", 16, 24)}
        elif method == "POST" and route == "/api/v1/automation/land":
            fields(body, ("runway_index", "approach_speed_mps"))
            function = "StartAutomaticLanding"
            parameters = {"RunwayIndex": runway_index(body), "ApproachSpeedMps": number(body, "approach_speed_mps", 16, 22)}
        elif method == "POST" and route == "/api/v1/automation/prepare-landing":
            fields(body, ("runway_index",))
            function, parameters = "PrepareLandingApproach", {"RunwayIndex": runway_index(body)}
        elif method == "POST" and route == "/api/v1/automation/cancel":
            fields(body, ())
            function, parameters = "CancelFlightOperation", {}
        elif method == "POST" and route == "/api/v1/aircraft/reload":
            fields(body, ())
            function, parameters = "ReloadAircraftProfile", {}
        elif method == "POST" and route == "/api/v1/aircraft/visual/reload":
            fields(body, ())
            function, parameters = "ReloadAircraftVisual", {}
        elif method == "PUT" and route == "/api/v1/eoir":
            fields(body, ("enabled", "mode", "fov_deg"))
            if type(body["enabled"]) is not bool or body["mode"] not in SENSOR_MODES:
                raise APIError(400, "enabled must be a boolean; mode must be ccd, ir_white or ir_black.")
            function = "SetSensorSettings"
            parameters = {"Enabled": body["enabled"], "Mode": body["mode"], "FovDeg": number(body, "fov_deg", 1, 90)}
        elif method == "PUT" and route == "/api/v1/eoir/gimbal":
            fields(body, ("azimuth_deg", "elevation_deg"))
            function = "SetGimbal"
            parameters = {"AzimuthDeg": number(body, "azimuth_deg", -180, 180),
                          "ElevationDeg": number(body, "elevation_deg", -90, 30)}
        elif method == "PUT" and route == "/api/v1/eoir/gimbal-mode":
            if type(body) is not dict or body.get("mode") not in ("body_fixed", "auto", "geo", "protect"):
                raise APIError(400, "mode must be body_fixed, auto, geo or protect.")
            mode = body["mode"]
            if mode == "body_fixed":
                fields(body, ("mode", "azimuth_deg", "elevation_deg"))
                function = "SetGimbal"
                parameters = {"AzimuthDeg": number(body, "azimuth_deg", -180, 180),
                              "ElevationDeg": number(body, "elevation_deg", -90, 30)}
            elif mode == "auto":
                fields(body, ("mode", "target_id"))
                function, parameters = "TrackSensorTarget", {"TargetId": target_id(body, "target_id")}
            elif mode == "geo":
                fields(body, ("mode", "latitude_deg", "longitude_deg", "ellipsoid_height_m"))
                function = "PointSensorGeodetic"
                parameters = {"LatitudeDeg": number(body, "latitude_deg", -90, 90),
                              "LongitudeDeg": number(body, "longitude_deg", -180, 180),
                              "EllipsoidHeightM": number(body, "ellipsoid_height_m", -500, 20000)}
            else:
                fields(body, ("mode",))
                function, parameters = "ProtectSensor", {}
        elif method == "POST" and route == "/api/v1/eoir/track":
            fields(body, ("target_id",))
            function, parameters = "TrackSensorTarget", {"TargetId": target_id(body, "target_id", allow_empty=True)}
        elif (method, route) in (("POST", "/api/v1/targets"), ("PUT", "/api/v1/targets/pose")):
            create = method == "POST"
            fields(body, ("id", "latitude_deg", "longitude_deg", "heading_deg") + (("kind",) if create else ()))
            parameters = {"Id": target_id(body), "LatitudeDeg": number(body, "latitude_deg", -90, 90),
                          "LongitudeDeg": number(body, "longitude_deg", -180, 180), "HeadingDeg": number(body, "heading_deg", 0, 360)}
            if create:
                if body["kind"] not in ("tank", "missile", "marker"):
                    raise APIError(400, "kind must be tank, missile or marker.")
                parameters["Kind"] = body["kind"]
            function = "CreateGroundTarget" if create else "MoveGroundTarget"
        elif method == "POST" and route == "/api/v1/targets/remove":
            fields(body, ("id",))
            function, parameters = "RemoveGroundTarget", {"Id": target_id(body)}
        elif method == "POST" and route == "/api/v1/wargame/weapons/reload":
            fields(body, ())
            function, parameters = "ReloadWargameWeapons", {}
        elif method == "POST" and route == "/api/v1/wargame/fire":
            fields(body, ("weapon_id", "target_id"))
            function, parameters = "PlayWargameShot", {"WeaponId": target_id(body, "weapon_id"), "TargetId": target_id(body, "target_id")}
        elif method == "POST" and route == "/api/v1/wargame/resolve":
            fields(body, ("shot_id", "outcome"))
            if body["outcome"] not in ("hit", "miss"):
                raise APIError(400, "outcome must be hit or miss; the operator chooses after the visual animation.")
            function, parameters = "ResolveWargameShot", {"ShotId": target_id(body, "shot_id"), "Outcome": body["outcome"]}
        elif method == "POST" and route == "/api/v1/wargame/reset":
            fields(body, ())
            function, parameters = "ResetWargame", {}
        elif method == "PUT" and route == "/api/v1/gust":
            fields(body, GUST_FIELDS)
            ranges = [(-15, 15), (-15, 15), (-15, 15), (0, 600), (.1, 120)]
            parameters = {native: number(body, key, *limits)
                          for (key, native), limits in zip(GUST_FIELDS.items(), ranges)}
            function = "SetGust"
        elif method == "PUT" and route == "/api/v1/failures":
            fields(body, FAILURE_FIELDS)
            parameters = {"EnginePowerFraction": number(body, "engine_power_fraction", 0, 1)}
            for key, native in FAILURE_FIELDS.items():
                if key == "engine_power_fraction":
                    continue
                if type(body[key]) is not bool:
                    raise APIError(400, f"{key} must be a boolean.")
                parameters[native] = body[key]
            function = "SetFailures"
        elif method == "PUT" and route == "/api/v1/view":
            fields(body, ("mode",))
            if body["mode"] not in ("aircraft", "overview", "projectile"):
                raise APIError(400, "mode must be aircraft, overview or projectile.")
            function, parameters = "SetViewMode", {"ViewMode": body["mode"]}
        elif method == "PUT" and route == "/api/v1/time-scale":
            fields(body, ("factor",))
            function, parameters = "SetTimeScale", {"TimeScale": number(body, "factor", .25, 20)}
        elif method == "POST" and route == "/api/v1/terrain/reload":
            fields(body, ())
            function, parameters = "LoadTerrain", {}
        elif method == "PUT" and route == "/api/v1/environment":
            fields(body, ENVIRONMENT_FIELDS)
            function = "SetWind"
            parameters = {native: number(body, key, -30, 30) for key, native in ENVIRONMENT_FIELDS.items()}
        elif method == "PUT" and route in ("/api/v1/pose", "/api/v1/initial-condition"):
            initial = route.endswith("/initial-condition")
            aerodynamic = initial and type(body) is dict and bool(set(body) & AERODYNAMIC_FIELDS.keys())
            expected = POSE_FIELDS | ({"speed_mps": "SpeedMps"} if initial else {})
            fields(body, expected | (AERODYNAMIC_FIELDS if aerodynamic else {}))
            ranges = [(-90, 90), (-180, 180), (-500, 20000), (-360, 360), (-89, 89), (-180, 180)]
            parameters = {native: number(body, key, *limits)
                          for (key, native), limits in zip(POSE_FIELDS.items(), ranges)}
            function = "SetAircraftGeodetic"
            if initial:
                parameters["SpeedMps"] = number(body, "speed_mps", 5, 45)
                function = "SetInitialCondition"
                if aerodynamic:
                    parameters["AngleOfAttackDeg"] = number(body, "angle_of_attack_deg", -180, 180)
                    parameters["SideslipDeg"] = number(body, "sideslip_deg", -89, 89)
                    function = "SetAerodynamicInitialCondition"
        elif method == "PUT" and route == "/api/v1/route":
            fields(body, ("waypoints", "loiter_radius_m", "clockwise"))
            if type(body["waypoints"]) is not list or not 1 <= len(body["waypoints"]) <= 64:
                raise APIError(400, "waypoints must be an array of 1..64 waypoint objects.")
            if type(body["clockwise"]) is not bool:
                raise APIError(400, "clockwise must be a boolean.")
            points = []
            for point in body["waypoints"]:
                fields(point, WAYPOINT_FIELDS)
                limits = [(-90, 90), (-180, 180), (-500, 20000), (16, 24)]
                points.append({native: number(point, key, *bounds)
                               for (key, native), bounds in zip(WAYPOINT_FIELDS.items(), limits)})
            minimum = max(100, points[-1]["SpeedMps"] ** 2 / (9.80665 * math.tan(math.radians(30))))
            radius = number(body, "loiter_radius_m", minimum, 5000)
            parameters = {"Waypoints": points, "LoiterRadiusM": radius, "Clockwise": body["clockwise"]}
            function = "SetRoute"
        elif method == "POST" and route == "/api/v1/navigation/cancel":
            fields(body, ())
            function, parameters = "CancelNavigation", {}
        elif method == "PUT" and route == "/api/v1/simulation":
            fields(body, ("running",))
            if type(body["running"]) is not bool:
                raise APIError(400, "running must be a boolean.")
            function, parameters = "SetRunning", {"Running": body["running"]}
        elif method == "POST" and route == "/api/v1/reset":
            fields(body, ())
            function, parameters = "ResetFlight", {}
        elif method == "POST" and route == "/api/v1/step":
            fields(body, ("dt_s",))
            dt = number(body, "dt_s", 0, 1)
            if dt == 0:
                raise APIError(400, "dt_s must satisfy 0 < dt_s <= 1 second.")
            function, parameters = "AdvanceSimulation", {"DeltaSeconds": dt}
        else:
            raise APIError(404, "Unknown API route or method.")
        path = self.object_path()
        self.state(path)  # Resolve the current game actor before any mutation.
        timeout = {"LoadTerrain": 60, "ReloadAircraftVisual": 30}.get(function, 10)
        if self.call(path, function, parameters, timeout=timeout) is False:
            failed = self.state(path)
            if function == "SetViewMode" and parameters["ViewMode"] == "projectile":
                raise APIError(409, "Projectile view requires an active visual animation. Start a shot first.")
            if function == "LoadTerrain":
                raise APIError(409, failed["terrain"]["error"] or "Terrain reload failed. The previous map is preserved and flight is paused.")
            if function == "ReloadAircraftProfile":
                raise APIError(409, failed["airframe"]["error"] or failed["fault"]
                               or "Aircraft reload requires pause and a valid aircraft/active.json profile.")
            if function == "ReloadAircraftVisual":
                if failed["aircraft"]["running"]:
                    raise APIError(409, "Aircraft visual reload requires pause; the current mesh and flight state are preserved.")
                raise APIError(409, failed["visual"]["error"]
                               or "Aircraft visual reload failed. The previous mesh and flight state are preserved.")
            if function in ("StartAutomaticTakeoff", "StartAutomaticLanding", "PrepareLandingApproach", "CancelFlightOperation"):
                raise APIError(409, failed["fault"] or failed["automation"]["message"]
                               or "Unreal rejected the flight operation. Check the runway and aircraft state.")
            if function in ("SetGust", "SetFailures"):
                raise APIError(409, failed["fault"] or "Unreal rejected the disturbance command. Check the simulation state.")
            if function == "SetLandingGear":
                raise APIError(409, failed["fault"] or "Manual gear commands are unavailable during automatic flight operations; "
                               "gear cannot retract while on the ground.")
            if function in ("SetSensorSettings", "SetGimbal", "TrackSensorTarget", "PointSensorGeodetic", "ProtectSensor",
                            "CreateGroundTarget", "MoveGroundTarget", "RemoveGroundTarget"):
                raise APIError(409, failed["eoir"]["error"] or failed["fault"] or "Unreal rejected the sensor or target command.")
            if function in ("ReloadWargameWeapons", "PlayWargameShot", "ResolveWargameShot", "ResetWargame"):
                raise APIError(409, failed["wargame"]["error"] or "Unreal rejected the wargame command.")
            if function in ("SetGroundControls", "ResetOnGround", "SetGroundInitialCondition"):
                raise APIError(409, failed["fault"] or "Unreal rejected the ground command. Check ground availability and simulation state.")
            raise APIError(409, failed["fault"] or "Unreal rejected the command. Step requires pause; "
                           "the route and final circle must stay within 50 km of the scene origin.")
        return self.state(path)


class Engine:
    """Own only the standalone process launched by this API server."""
    def __init__(self, unreal, engine_root, port):
        self.unreal = unreal
        self.executable = Path(engine_root) / "Engine/Binaries/Win64/UnrealEditor.exe"
        self.port = port
        self.process = None
        self.stopping = False

    def status(self):
        alive = self.process is not None and self.process.poll() is None
        pid = self.process.pid if alive else None
        if alive and self.stopping:
            return dict(status="stopping", pid=pid, managed=True, ready=False, message="Unreal 종료 중")
        listening = False
        try:
            with socket.create_connection(("127.0.0.1", self.port), timeout=0.25):
                listening = True
        except OSError:
            pass
        ready = False
        terrain_failed = False
        if listening:
            try:
                receipt = json.loads((ROOT / "Saved/uavsim-runtime.json").read_text(encoding="utf-8-sig"))
                if not alive or receipt.get("process_id") == pid:
                    native = self.unreal.call(self.unreal.object_path(), "GetAircraftState", timeout=1)
                    if isinstance(native, dict) and (not alive or native.get("ProcessId") == pid):
                        ready = native.get("TerrainLoaded") is True
                        terrain_failed = not ready
            except (OSError, ValueError, TypeError, AttributeError, APIError):
                pass
        if alive:
            status, message = ("running", "Unreal 실행 중") if ready else ("starting", "Unreal 시작 중 · 지형 로딩을 기다립니다")
            if terrain_failed:
                status, message = "error", "지형을 불러오지 못했습니다. Unreal 종료 후 Saved/Logs/UAVSim.log를 확인하세요."
        elif listening:
            status, message = "external", "별도로 실행된 Unreal입니다. 해당 창에서 종료하세요."
        elif self.process is not None and self.process.returncode != 0:
            status, message = "error", f"Unreal이 종료됐습니다 (코드 {self.process.returncode}). Saved/Logs/UAVSim.log를 확인하세요."
        else:
            status, message = "stopped", "Unreal 꺼짐 · 실행 버튼으로 비행 창을 여세요"
        return dict(status=status, pid=pid, managed=alive, ready=bool(ready), message=message)

    def execute(self, method, route, body):
        if method == "GET" and route == "/api/v1/engine":
            return self.status()
        if method != "POST" or route not in ("/api/v1/engine/start", "/api/v1/engine/stop"):
            raise APIError(404, "Unknown engine route or method.")
        fields(body, ())
        status = self.status()
        if route.endswith("/start"):
            if status["status"] in ("starting", "running"):
                return status  # A repeated click must not create another engine.
            if status["managed"] or status["status"] not in ("stopped", "error"):
                raise APIError(409, "Unreal is already active or still shutting down.")
            if not self.executable.is_file():
                raise APIError(503, "UnrealEditor.exe was not found. Set UAVSIM_UNREAL_ROOT before starting the API.")
            # Normalize inherited PATH/Path once, as in dev.ps1. No shell or caller-supplied launch arguments.
            environment = {key: value for key, value in os.environ.items() if key.lower() != "path"}
            environment["Path"] = os.environ.get("PATH", "")
            self.process = subprocess.Popen(
                [str(self.executable), str(ROOT / "UAVSim.uproject"), "/Game/Maps/JejuDemo",
                 "-game", "-windowed", "-ResX=1600", "-ResY=900", "-RCWebControlEnable",
                 f"-ini:RemoteControl:[/Script/RemoteControlCommon.RemoteControlSettings]:RemoteControlHttpServerPort={self.port}"],
                cwd=ROOT, env=environment, stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW)
            self.stopping = False
            return dict(status="starting", pid=self.process.pid, managed=True, ready=False,
                        message="Unreal 시작 중 · 지형 로딩을 기다립니다")
        if status["status"] == "stopped" or self.stopping and status["managed"]:
            return status
        if not status["managed"] or status["status"] not in ("running", "error"):
            raise APIError(409, "Only an initialized Unreal instance started by this API can be stopped.")
        # Receipt PID + owned Popen handle prevent shutdown of another editor/runtime.
        receipt = json.loads((ROOT / "Saved/uavsim-runtime.json").read_text(encoding="utf-8-sig"))
        if (type(receipt) is not dict or receipt.get("process_id") != self.process.pid
                or receipt.get("world_kind") != "Game" or not isinstance(receipt.get("object_path"), str)):
            raise APIError(409, "The active runtime does not belong to this API server.")
        if self.unreal.call(receipt["object_path"], "RequestSimulationExit", {"ExpectedProcessId": self.process.pid}) is not True:
            raise APIError(409, "Unreal refused the shutdown request.")
        self.stopping = True
        return self.status()


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def respond(self, status, data, content_type="application/json; charset=utf-8", headers=None):
        payload = data if isinstance(data, bytes) else json.dumps(data, allow_nan=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(payload)

    def handle_request(self):
        try:
            port = self.server.server_port
            hosts = {f"127.0.0.1:{port}", f"localhost:{port}"}
            if self.headers.get("Host") not in hosts:
                raise APIError(403, "Use the local UAVSim address.")
            origin = self.headers.get("Origin")
            if origin is not None and origin != "http://" + self.headers.get("Host", ""):
                raise APIError(403, "Cross-origin requests are not enabled.")
            if self.command == "GET" and self.path in ("/", "/eoir", "/eoir.html"):
                self.respond(200, (ROOT / "web" / ("index.html" if self.path == "/" else "eoir.html")).read_bytes(), "text/html; charset=utf-8")
                return
            body = None
            if self.command != "GET":
                if self.headers.get_content_type() != "application/json":
                    raise APIError(400, "Content-Type must be application/json.")
                if self.headers.get("Transfer-Encoding"):
                    raise APIError(400, "Chunked requests are not supported.")
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                except ValueError as error:
                    raise APIError(400, "Invalid Content-Length.") from error
                if not 0 < length <= 32768:
                    raise APIError(400, "JSON body must be between 1 and 32768 bytes.")
                try:
                    body = json.loads(self.rfile.read(length), object_pairs_hook=strict_object,
                                      parse_constant=reject_constant)
                except (ValueError, UnicodeError, RecursionError) as error:
                    raise APIError(400, "Invalid JSON: " + str(error)) from error
            with self.server.unreal_lock:
                frame = self.command == "GET" and self.path == "/api/v1/eoir/frame.jpg"
                if frame:
                    result = self.server.unreal.sensor_frame()
                else:
                    target = self.server.engine if self.path.startswith("/api/v1/engine") else self.server.unreal
                    result = target.execute(self.command, self.path, body)
            if frame:
                self.respond(200, result[0], "image/jpeg", result[1])
            else:
                self.respond(200, result)
        except APIError as error:
            self.respond(error.status, {"error": {"message": str(error)}})
        except (OSError, ValueError) as error:
            self.respond(503, {"error": {"message": str(error)}})

    do_GET = handle_request
    do_PUT = handle_request
    do_POST = handle_request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--unreal-port", type=int, default=30010)
    parser.add_argument("--unreal-root", default=os.environ.get("UAVSIM_UNREAL_ROOT", r"C:\Program Files\Epic Games\UE_5.8"))
    parser.add_argument("--object-path", help="Explicit PIE FlightScene path; otherwise read runtime locator")
    args = parser.parse_args()
    # Browsers pre-open idle sockets; threads keep those from blocking other requests.
    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        server.unreal = Unreal(args.unreal_port, args.object_path)
        server.engine = Engine(server.unreal, args.unreal_root, args.unreal_port)
        # ponytail: one actor lock; separate locks only when multiple aircraft are supported.
        server.unreal_lock = Lock()
        print(f"UAVSim API and controls: http://127.0.0.1:{args.port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
