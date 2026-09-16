"""Live map replacement and fixed-step playback check. Restores the selected map and pauses.

Uses the prepared maps/example and a running Unreal/API. No browser/desktop automation.
"""
import json
from pathlib import Path
import time

from PIL import Image
from uavsim_client import UAVSimClient, UAVSimError


ROOT = Path(__file__).resolve().parents[1]
sim = UAVSimClient()
active = ROOT / 'maps/active.json'
example = ROOT / 'maps/example/terrain.json'
original_active, original_example = active.read_bytes(), example.read_bytes()
report = {'ok': False, 'rates': [], 'passed': []}


def check(condition, description):
    if not condition:
        raise AssertionError(description)
    report['passed'].append(description)
    print(description, flush=True)


def rejected(action, status):
    try:
        action()
    except UAVSimError as error:
        check(error.status == status, f'Rejected request with HTTP {status}: {error}')
    else:
        raise AssertionError('Invalid request was accepted')


try:
    sim.pause()
    check(sim.get_state()['terrain']['dataset'] == 'korea', 'Start on the migrated country map')
    for factor in (.25, 1, 5, 20):
        sim.reset()
        sim.set_time_scale(factor)
        started_wall = time.monotonic()
        start = sim.set_running(True)['aircraft']
        time.sleep(1.6)
        sample = sim.get_state()['aircraft']
        elapsed_wall = time.monotonic() - started_wall
        sim.pause()
        elapsed_sim = sample['simulation_time_s'] - start['simulation_time_s']
        measured = elapsed_sim / elapsed_wall
        check(.8 * factor < measured < 1.2 * factor, f'{factor}x real-time playback measured {measured:.3f}x')
        check(.8 * factor < sample['effective_time_scale'] < 1.2 * factor,
              f'{factor}x native effective-rate telemetry agrees')
        check(sample['latitude_deg'] > start['latitude_deg'] and sample['distance_travelled_m'] > 0,
              f'{factor}x changes position and travelled distance')
        check(abs(sample['distance_travelled_m'] - 19 * sample['simulation_time_s']) < .01,
              f'{factor}x trimmed trajectory travels 19 metres per simulation second')
        report['rates'].append(dict(requested=factor, measured=measured, native=sample['effective_time_scale'],
                                    simulation_seconds=elapsed_sim, distance_m=sample['distance_travelled_m']))
    results = []
    for factor in (.25, 20):
        sim.reset()
        sim.set_time_scale(factor)
        results.append(sim.step(.5)['aircraft'])
    keys = ('latitude_deg','longitude_deg','ellipsoid_height_m','simulation_time_s','distance_travelled_m','speed_mps')
    check(all(results[0][key] == results[1][key] for key in keys), 'Explicit .5 s step is identical at .25x and 20x')
    for factor in (0, 21, True):
        rejected(lambda: sim.set_time_scale(factor), 400)
    check(sim.get_state()['aircraft']['time_scale'] == 20, 'Invalid factor preserves the current setting')

    old = sim.get_state()
    active.write_text(json.dumps({'manifest':'../data/korea/metadata.json'}), encoding='utf-8')
    rejected(sim.reload_terrain, 409)
    bad = sim.get_state()
    check(bad['terrain']['dataset'] == old['terrain']['dataset']
          and bad['terrain']['tile_count'] == old['terrain']['tile_count']
          and bad['aircraft']['latitude_deg'] == old['aircraft']['latitude_deg'],
          'Invalid manifest path preserves the loaded map and aircraft')
    active.write_text(json.dumps({'manifest':'example/terrain.json'}), encoding='utf-8')
    custom = sim.reload_terrain()
    check(custom['terrain']['dataset'] == 'example' and custom['terrain']['tile_count'] == 1
          and not custom['terrain']['error'] and not custom['aircraft']['running'],
          'Imported local PNG/DEM map loads without an Unreal rebuild and pauses flight')
    check(sim.set_view('overview')['view_mode'] == 'overview', 'Custom map overview is available')
    manifest = json.loads(original_example)
    tile = manifest['tiles'][0]
    directory = example.parent / tile['directory']
    with Image.open(directory / tile['image_file']) as pixels:
        pixels.convert('RGB').save(directory / 'replacement.jpg', quality=92)
    tile['image_file'] = 'replacement.jpg'
    example.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    check(sim.reload_terrain()['terrain']['dataset'] == 'example', 'Alternative JPEG filename renders through the same loader')
    tile['image_file'] = 'missing-file.png'
    example.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    rejected(sim.reload_terrain, 409)
    check(sim.get_state()['terrain']['dataset'] == 'example' and sim.get_state()['aircraft']['terrain_loaded'],
          'Missing replacement image preserves the previous valid map')
    report['ok'] = True
finally:
    example.write_bytes(original_example)
    active.write_bytes(original_active)
    sim.reload_terrain()
    sim.set_time_scale(1)
    sim.set_view('aircraft')
    report['final_state'] = sim.get_state()
    (ROOT / 'artifacts/map-playback-api-check.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

print(json.dumps(report, indent=2))
