"""Live country-render boundary check; requires the terrain Python environment and running Unreal.

Reads native component transforms against independent pyproj ECEF positions/directions.
Leaves Jeju initialized, paused, in nationwide overview. No desktop/UI automation.
"""
import json
import math
from pathlib import Path
from urllib.request import Request, urlopen

import numpy as np
from pyproj import Transformer
from uavsim_client import UAVSimClient, UAVSimError


ROOT = Path(__file__).resolve().parents[1]
sim = UAVSimClient()
state = sim.pause()
assert state['terrain']['dataset'] == 'korea' and state['terrain']['tile_count'] == 49, state
actor = state['object_path']
ecef = Transformer.from_crs(4979, 4978, always_xy=True)
render_origin = np.array(ecef.transform(128, 36, 0))
latitude, longitude = np.deg2rad([36, 128])
sl, cl, sp, cp = np.sin(longitude), np.cos(longitude), np.sin(latitude), np.cos(latitude)
ecef_to_render = np.array([[-sl, cl, 0], [sp*cl, sp*sl, -cp], [cp*cl, cp*sl, sp]])  # UE E,S,U


def native_property(component, name):
    body = dict(objectPath=actor+'.'+component, propertyName=name, access='READ_ACCESS')
    req = Request('http://127.0.0.1:30010/remote/object/property', json.dumps(body).encode(),
                  {'Content-Type': 'application/json'}, method='PUT')
    with urlopen(req, timeout=15) as response:
        return json.load(response)[name]


def initialize(lat, lon):
    return sim.set_initial_condition(latitude_deg=lat, longitude_deg=lon, ellipsoid_height_m=2600,
                                    heading_deg=0, pitch_deg=1.227578832134544, roll_deg=0, speed_mps=19)


checks = []
for name, lat, lon in [('Jeju',33.43,126.52), ('Seoul',37.5665,126.978),
                       ('Busan',35.1796,129.0756), ('Daegu',35.8714,128.6014),
                       ('Gangneung',37.7519,128.8761), ('Ulleung',37.5,130.87), ('Dokdo',37.241,131.869)]:
    before = initialize(lat, lon)
    assert not before['aircraft']['running'] and before['navigation']['mode'] == 'manual'
    p = native_property('Aircraft', 'RelativeLocation')
    actual = np.array([p['X'], p['Y'], p['Z']]) / 100
    expected = ecef_to_render @ (np.array(ecef.transform(lon,lat,2600)) - render_origin)
    position_error = float(np.max(np.abs(actual-expected)))
    assert position_error < 1e-5, (name, actual, expected)
    r = native_property('Aircraft', 'RelativeRotation')
    pitch,yaw = np.deg2rad([r['Pitch'],r['Yaw']])
    forward = np.array([np.cos(pitch)*np.cos(yaw),np.cos(pitch)*np.sin(yaw),np.sin(pitch)])
    p,l = np.deg2rad([lat,lon])
    north = np.array([-np.sin(p)*np.cos(l),-np.sin(p)*np.sin(l),np.cos(p)])
    up = np.array([np.cos(p)*np.cos(l),np.cos(p)*np.sin(l),np.sin(p)])
    a = math.radians(before['aircraft']['pitch_deg'])
    expected_forward = ecef_to_render @ (north*math.cos(a)+up*math.sin(a))
    attitude_error = float(np.max(np.abs(forward-expected_forward)))
    assert attitude_error < 1e-8, (name,forward,expected_forward)
    after = sim.step(1)
    assert not after['fault'] and after['aircraft']['latitude_deg'] > lat
    assert abs(after['aircraft']['simulation_time_s']-before['aircraft']['simulation_time_s']-1) < 1e-9
    checks.append(dict(region=name,position_error_m=position_error,forward_vector_error=attitude_error))

for mode in ('aircraft','overview'):
    assert sim.set_view(mode)['view_mode'] == mode
try:
    sim.set_view('bad-mode')
    raise AssertionError('Invalid view accepted')
except UAVSimError as error:
    assert error.status == 400
assert sim.get_state()['view_mode'] == 'overview'
before = sim.get_state()
try:
    initialize(0,0)
    raise AssertionError('Out-of-terrain initial condition accepted')
except UAVSimError as error:
    assert error.status == 409
assert sim.get_state()['aircraft'] == before['aircraft']

# Verify the saved starter map did not restore the old reflected left/right component offsets.
assert native_property('LeftAileronHinge','RelativeLocation')['Y'] < 0
assert native_property('RightAileronHinge','RelativeLocation')['Y'] > 0
sim.set_controls(elevator_deg=5,aileron_deg=5,rudder_deg=5,throttle=.5)
state = sim.step(.1)
assert not state['aircraft']['running']
actual = state['actuators']
assert 0 < actual['aileron_deg'] < 5 and 0 < actual['rudder_deg'] < 5
for component,field,expected in [('LeftAileronHinge','Pitch',actual['aileron_deg']),
                                 ('RightAileronHinge','Pitch',-actual['aileron_deg']),
                                 ('RudderHinge','Yaw',-actual['rudder_deg'])]:
    assert abs(native_property(component,'RelativeRotation')[field]-expected) < 1e-6
initialize(33.43,126.52)
state = sim.set_view('overview')
receipt = dict(regions=checks, views_and_invalid_input='passed',
               surface_display_axes='passed: lagged actuators match signed native hinge rotations', final_state=state)
(ROOT/'artifacts/korea-api-check.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps(receipt,indent=2))
