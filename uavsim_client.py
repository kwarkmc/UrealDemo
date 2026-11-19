"""Synchronous UAVSim HTTP client; metres, seconds and degrees, no dependencies."""
import json
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener


class UAVSimError(RuntimeError):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class UAVSimClient:
    """Connect to the local API. Requests are never automatically retried."""
    def __init__(self, base_url="http://127.0.0.1:8765", timeout=30):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.opener = build_opener(ProxyHandler({}))

    def _request(self, method, route, body=None, *, timeout=None, binary=False):
        data = None if body is None else json.dumps(body, allow_nan=False).encode("utf-8")
        request = Request(self.base_url + "/api/v1/" + route, data,
                          {"Content-Type": "application/json"}, method=method)
        try:
            with self.opener.open(request, timeout=self.timeout if timeout is None else timeout) as response:
                return response.read() if binary else json.load(response)
        except HTTPError as error:
            try:
                message = json.load(error)["error"]["message"]
            except (ValueError, KeyError, TypeError):
                message = error.reason
            raise UAVSimError(error.code, message) from error

    def get_state(self):
        """Get applied limits/support and physics; file selection may differ until reload.

        airframe.dynamic_propeller enables actuator RPM, shaft power W and torque Nm.
        ground wheel arrays are nose/left/right: reaction N, signed clearance m,
        lateral slip m/s; wheel_surfaces_sampled marks separate terrain queries.
        """
        return self._request("GET", "state")

    def set_eoir(self, *, enabled, mode="ccd", fov_deg=30):
        """Enable the sensor and set ccd/ir_white/ir_black display mode and FOV 1..90 deg.

        IR modes are image filters, not calibrated thermal sensor models.
        Enabling capture in protect mode is rejected; first select another gimbal mode.
        """
        return self._request("PUT", "eoir", dict(enabled=enabled, mode=mode, fov_deg=fov_deg))

    def set_gimbal(self, *, azimuth_deg, elevation_deg):
        """Set manual gimbal angles: azimuth -180..180, elevation -90..30 deg.

        Selects body_fixed mode and cancels tracking/protection; does not enable capture.
        """
        return self._request("PUT", "eoir/gimbal", dict(azimuth_deg=azimuth_deg, elevation_deg=elevation_deg))

    def track_target(self, target_id=""):
        """Geometrically point at a selected target ID; empty cancels tracking.

        Nonempty selects auto mode; empty selects body_fixed at the current angles.
        This exits protection without enabling capture. No recognition or occlusion test.
        """
        return self._request("POST", "eoir/track", {"target_id": target_id})

    def set_gimbal_mode(self, mode, *, azimuth_deg=None, elevation_deg=None, target_id=None,
                        latitude_deg=None, longitude_deg=None, ellipsoid_height_m=None):
        """Select body_fixed, auto, geo or protect with only that mode's parameters.

        body_fixed: azimuth_deg (-180..180), elevation_deg (-90..30).
        auto: nonempty target_id. geo: latitude_deg, longitude_deg and WGS84
        ellipsoid_height_m (-500..20000); no terrain height is inferred.
        protect: no extra parameters; parks at 180/+30 deg and disables capture.
        Leaving protection keeps capture off until set_eoir(enabled=True).
        None parameters are omitted; missing/extra fields are rejected by the API.
        """
        parameters = dict(azimuth_deg=azimuth_deg, elevation_deg=elevation_deg, target_id=target_id,
                          latitude_deg=latitude_deg, longitude_deg=longitude_deg, ellipsoid_height_m=ellipsoid_height_m)
        return self._request("PUT", "eoir/gimbal-mode", {"mode": mode} | {
            key: value for key, value in parameters.items() if value is not None})

    def create_target(self, *, id, kind, latitude_deg, longitude_deg, heading_deg=0):
        """Create tank/missile/marker on sampled terrain; maximum 64 targets.

        ID is 1..48 ASCII letters/digits/_/-. WGS84 coordinates are degrees;
        heading is clockwise from north, 0..360 deg. Height is sampled automatically.
        """
        return self._request("POST", "targets", dict(id=id, kind=kind, latitude_deg=latitude_deg,
                             longitude_deg=longitude_deg, heading_deg=heading_deg))

    def move_target(self, *, id, latitude_deg, longitude_deg, heading_deg=0):
        """Set an existing target's WGS84 position/heading and resample terrain height."""
        return self._request("PUT", "targets/pose", dict(id=id, latitude_deg=latitude_deg,
                             longitude_deg=longitude_deg, heading_deg=heading_deg))

    def remove_target(self, id):
        """Remove one target by ID; returns the complete simulator state."""
        return self._request("POST", "targets/remove", {"id": id})

    def reload_wargame_weapons(self):
        """Reload the visual prop catalog while paused and with no active animations."""
        return self._request("POST", "wargame/weapons/reload", {})

    def play_wargame_shot(self, weapon_id, target_id):
        """Start a scripted visual animation toward the target's current position.

        The endpoint is fixed at creation; there is no homing, weapon physics,
        collision-based hit result or flight-state change. Advance simulation time
        to reach awaiting_result, then explicitly choose hit/miss with resolve.
        """
        return self._request("POST", "wargame/fire", {"weapon_id": weapon_id, "target_id": target_id})

    def resolve_wargame_shot(self, shot_id, outcome):
        """Record the operator's hit/miss outcome only after animation completion.

        Hit adds a target game mark; it does not calculate damage or performance.
        """
        return self._request("POST", "wargame/resolve", {"shot_id": shot_id, "outcome": outcome})

    def reset_wargame(self):
        """Clear visual shots/history and all target hit marks; preserve flight and targets."""
        return self._request("POST", "wargame/reset", {})

    def get_eoir_frame(self):
        """Return one JPEG as bytes; disabled/unavailable capture raises UAVSimError.

        A new capture is requested each call and never retried automatically.
        The HTTP endpoint also supplies simulation time, sequence and mode headers.
        """
        return self._request("GET", "eoir/frame.jpg", binary=True)

    def set_view(self, mode):
        """Select 'aircraft', 'overview' or 'projectile' (latest active visual event).

        Projectile view returns to aircraft when no animations remain; EOIR is unchanged.
        """
        return self._request("PUT", "view", {"mode": mode})

    def set_time_scale(self, factor):
        """Set real-time playback factor .25..20; explicit step(dt_s) remains unscaled."""
        return self._request("PUT", "time-scale", {"factor": factor})

    def reload_terrain(self, *, timeout=60):
        """Read maps/active.json again and pause; failure preserves the previous terrain.

        This request has its own timeout and is never retried automatically.
        """
        return self._request("POST", "terrain/reload", {}, timeout=timeout)

    def get_engine(self):
        return self._request("GET", "engine")

    def start_engine(self):
        """Start the standalone flight window; poll get_engine() until ready."""
        return self._request("POST", "engine/start", {})

    def stop_engine(self):
        """Gracefully stop this API server's ready standalone instance."""
        return self._request("POST", "engine/stop", {})

    def set_controls(self, *, elevator_deg, aileron_deg, rudder_deg, throttle):
        """Full control set: surface angles +/-25 degrees, throttle 0..1.

        Positive elevator: trailing edge down. Positive aileron: right up,
        left down. Positive rudder: trailing edge right. Manual input cancels
        navigation. Throttle is a normalized propulsion command.
        """
        return self._request("PUT", "controls", dict(elevator_deg=elevator_deg,
                             aileron_deg=aileron_deg, rudder_deg=rudder_deg, throttle=throttle))

    def set_ground_controls(self, *, steering_deg, brake, parking_brake):
        """Set ground controls separately from the four flight controls.

        Steering is +/-30 degrees, positive right; brake is 0..1 and parking
        brake is a boolean. Ground AGL is signed CG clearance along the sampled
        surface normal, not vertical height or wheel clearance.
        """
        return self._request("PUT", "ground-controls", dict(steering_deg=steering_deg,
                             brake=brake, parking_brake=parking_brake))

    def reset_on_ground(self):
        """Start paused on the test runway with zero ground speed/thrust and parking brake on."""
        return self._request("POST", "ground/reset", {})

    def set_flaps(self, degrees):
        """Command flaps in degrees; actual angle advances in simulation time.

        Range: 0..airframe.maximum_flap_deg; active automatic operations reject manual commands.
        """
        return self._request("PUT", "flaps", {"degrees": degrees})

    def set_landing_gear(self, down=True):
        """Command gear down/up with a strict bool; full travel takes 3 simulated seconds.

        Paused commands need step() or playback to move. Ground retraction and
        manual commands during an active automatic flight operation are rejected.
        Initial conditions start gear down; ordinary flight controls preserve gear.
        Up requires airframe.retractable_gear. The current default uses a hypothetical
        retractable research derivative; the original C208B research file stays fixed.
        """
        return self._request("PUT", "gear", {"down": down})

    def set_ground_initial_condition(self, *, latitude_deg, longitude_deg, heading_deg, ground_speed_mps):
        """Place on the sampled surface with 0..15 m/s ground speed; height is automatic.

        Heading is clockwise from north in +/-360 degrees. Returned terrain
        height is WGS84 ellipsoid height in metres, distinct from CG AGL.
        """
        return self._request("PUT", "ground/initial-condition", dict(latitude_deg=latitude_deg,
                             longitude_deg=longitude_deg, heading_deg=heading_deg, ground_speed_mps=ground_speed_mps))

    def start_automatic_takeoff(self, *, runway_index=0, climb_height_m=50, speed_mps=19):
        """Arm takeoff on runway index 0..15, climb height 20..150 m, TAS within airframe route limits.

        Preserves pause/running state. Use set_running(True) or paused step(dt_s)
        to advance; this command does not move the aircraft to the runway.
        """
        return self._request("POST", "automation/takeoff", dict(runway_index=runway_index,
                             climb_height_m=climb_height_m, speed_mps=speed_mps))

    def start_automatic_landing(self, *, runway_index=0, approach_speed_mps=18):
        """Arm landing on runway index 0..15 with approach TAS within airframe landing limits.

        Preserves position and pause/running state. Returns through a terrain-surveyed
        approach orbit before deceleration/flap configuration and final approach.
        Inspect state.automation.rejection and desired_* fields; path is WGS84.
        Local mission distance/terrain/trim and development wind limits apply.
        """
        return self._request("POST", "automation/land", dict(runway_index=runway_index,
                             approach_speed_mps=approach_speed_mps))

    def cancel_flight_operation(self):
        """Cancel automatic takeoff/landing while preserving pause/running state."""
        return self._request("POST", "automation/cancel", {})

    def prepare_landing_approach(self, *, runway_index=0):
        """Reset position to a landing test approach for runway 0..15 and pause.

        Explicit test setup 1200 m before the threshold, 70 m above the runway,
        Thor TAS 18 m/s; C208B research TAS 40 m/s with 30-degree flaps.
        Replaces the current initial condition, not flight guidance.
        """
        return self._request("POST", "automation/prepare-landing", {"runway_index": runway_index})

    def reload_aircraft_profile(self):
        """Read aircraft/active.json while paused and reset with the loaded profile.

        Resets to airframe.initial_height_m / initial_speed_mps level trim; clears
        gusts, failures and autopilot, but preserves steady wind. Check
        airframe.error on failure; requests are not automatically retried.
        """
        return self._request("POST", "aircraft/reload", {})

    def reload_aircraft_visual(self, *, timeout=30):
        """Read aircraft/visual.json and its STL while paused; replace display shape only.

        Preserves position, simulation time and physics. Invalid files preserve
        the previous mesh and report visual.error. A single-piece STL does not
        split into animated control surfaces. This request is never retried.
        """
        return self._request("POST", "aircraft/visual/reload", {}, timeout=timeout)

    def set_pose(self, *, latitude_deg, longitude_deg, ellipsoid_height_m,
                 heading_deg, pitch_deg, roll_deg):
        """Set pose and pause. WGS84 ellipsoid height, heading clockwise from north,
        pitch nose-up positive (+/-89 deg), roll right-wing-down positive (+/-180 deg).
        """
        return self._request("PUT", "pose", dict(latitude_deg=latitude_deg, longitude_deg=longitude_deg,
                             ellipsoid_height_m=ellipsoid_height_m, heading_deg=heading_deg,
                             pitch_deg=pitch_deg, roll_deg=roll_deg))

    def set_running(self, running):
        return self._request("PUT", "simulation", {"running": running})

    def set_wind(self, *, wind_east_mps, wind_north_mps, wind_up_mps):
        """Set all three ENU wind velocity components in m/s, each -30..30.

        Positive values blow toward east, north and up, respectively.
        """
        return self._request("PUT", "environment", dict(wind_east_mps=wind_east_mps,
                             wind_north_mps=wind_north_mps, wind_up_mps=wind_up_mps))

    def set_gust(self, *, east_mps, north_mps, up_mps, delay_s=0, duration_s=5):
        """Schedule an ENU gust peak, each component +/-15 m/s, in simulation time.

        Delay is 0..600 s and duration .1..120 s. Zero all three components to
        clear the gust. Positive values blow toward east, north and up.
        """
        return self._request("PUT", "gust", dict(east_mps=east_mps, north_mps=north_mps,
                             up_mps=up_mps, delay_s=delay_s, duration_s=duration_s))

    def set_failures(self, *, engine_power_fraction, elevator_jammed, aileron_jammed, rudder_jammed):
        """Set engine output fraction 0..1 and all three surface jam booleans.

        Fraction 1 and all False clear failures. Compare controls (commands) with
        actuators (actual applied outputs) in the returned state.
        """
        return self._request("PUT", "failures", dict(engine_power_fraction=engine_power_fraction,
                             elevator_jammed=elevator_jammed, aileron_jammed=aileron_jammed, rudder_jammed=rudder_jammed))

    def set_initial_condition(self, *, latitude_deg, longitude_deg, ellipsoid_height_m,
                              heading_deg, pitch_deg, roll_deg, speed_mps,
                              angle_of_attack_deg=None, sideslip_deg=None):
        """Set pose and profile-supported TAS, zero rates; cancel route and pause.

        Supply both aerodynamic angles to specify body-relative airflow: AoA
        -180..180 deg, sideslip -89..89 deg. Omit both for the default trim AoA.
        """
        if (angle_of_attack_deg is None) != (sideslip_deg is None):
            raise ValueError("angle_of_attack_deg and sideslip_deg must be supplied together.")
        body = dict(latitude_deg=latitude_deg, longitude_deg=longitude_deg,
                    ellipsoid_height_m=ellipsoid_height_m, heading_deg=heading_deg,
                    pitch_deg=pitch_deg, roll_deg=roll_deg, speed_mps=speed_mps)
        if angle_of_attack_deg is not None:
            body.update(angle_of_attack_deg=angle_of_attack_deg, sideslip_deg=sideslip_deg)
        return self._request("PUT", "initial-condition", body)

    def set_route(self, waypoints, *, loiter_radius_m, clockwise=True):
        """1..64 points with latitude_deg, longitude_deg, ellipsoid_height_m, speed_mps.

        Fly through intermediate points in order; the last is the orbit center,
        acquired at loiter_radius_m with altitude/speed/course gates. Missed
        intermediate points use a fixed reentry leg (navigation.reacquiring).
        Preserves
        pause/running state; set_running(True) starts movement after a paused setup.
        Speed is true airspeed in m/s; ground speed also depends on wind.
        """
        return self._request("PUT", "route", dict(waypoints=waypoints,
                             loiter_radius_m=loiter_radius_m, clockwise=clockwise))

    def cancel_navigation(self):
        """Return to manual with current applied controls; preserves pause/running."""
        return self._request("POST", "navigation/cancel", {})

    def pause(self):
        return self.set_running(False)

    def step(self, dt_s):
        """Advance paused demo by 0 < dt_s <= 1 second; no timeout retry."""
        return self._request("POST", "step", {"dt_s": dt_s})

    def reset(self):
        return self._request("POST", "reset", {})
