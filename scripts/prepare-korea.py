"""Prepare the offline South Korea terrain base and one small Dokdo detail tile.

Uses existing GIS dependencies; no credentials or runtime map service is needed.
Re-running reuses source windows after verifying their SHA-256 receipts.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import time
import urllib.error
import urllib.parse
import urllib.request

import numpy as np
from PIL import Image
from pyproj import CRS, Transformer
import rasterio
from rasterio.enums import Resampling
from rasterio.transform import Affine, from_bounds, from_origin
from rasterio.warp import reproject, transform_bounds
from rasterio.windows import Window, from_bounds as window_from_bounds


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("terrain_helpers", ROOT / "scripts/prepare-terrain.py")
HELPERS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPERS)
ORIGIN = dict(latitude_deg=36.0, longitude_deg=128.0, ellipsoid_height_m=0.0)
BOUNDS = [124.0, 33.0, 132.0, 39.0]
SIZE, IMAGE_SIZE = 257, 1024
STEP = 1.0 / (SIZE - 1)
TILE_LIST = "https://copernicus-dem-30m.s3.amazonaws.com/tileList.txt"
EOX = "https://tiles.maps.eox.at/wms"
DOKDO_BOUNDS = [131.84375, 37.21875, 131.890625, 37.265625]
DOKDO_IMAGE = ("https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
               "sentinel-s2-l2a-cogs/53/S/KB/2025/6/"
               "S2C_53SKB_20250604_0_L2A/TCI.tif")
GEOID = "https://cdn.proj.org/us_nga_egm08_25.tif"
ENV = dict(GDAL_DISABLE_READDIR_ON_OPEN="EMPTY_DIR",
           CPL_VSIL_CURL_ALLOWED_EXTENSIONS=".tif", GDAL_HTTP_CONNECTTIMEOUT="20",
           GDAL_HTTP_TIMEOUT="90", GDAL_HTTP_MAX_RETRY="2", GDAL_HTTP_RETRY_DELAY="1")
PATCH_SCENES = [
    ("S2C_52SEG_20250727_0_L2A", [128.999784,36.951489,130.249343,37.947581], 0.007867),
    ("S2A_52SDD_20250729_0_L2A", [127.900531,34.248116,129.107264,35.243021], 0.222279),
    ("S2B_52SDE_20250801_0_L2A", [127.888086,35.149921,129.108478,36.144840], 0.240583),
    ("S2A_52SED_20250729_1_L2A", [128.999791,34.247133,130.206496,35.243069], 0.246253),
    ("S2C_52SEF_20250408_0_L2A", [128.999786,36.050537,130.234427,37.046574], 0.282580),
    ("S2A_52SEE_20250729_1_L2A", [128.999789,35.148905,130.220151,36.144889], 0.678392),
    ("S2A_52SDF_20250530_0_L2A", [127.880547,36.051588,129.109748,37.046523], 1.466684),
]
ULLEUNG_SCENE = ("S2C_52SFG_20250823_0_L2A", [130.75,37.4,131.0,37.65], 0.127595)


def json_write(path, value):
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding="utf-8")
    os.replace(temp, path)


def fetch(url):
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "UAVSim-terrain-preparation/1.0"})
            with urllib.request.urlopen(request, timeout=90) as response:
                return response.read(32 * 1024 * 1024)
        except (urllib.error.URLError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def cached_bytes(cache, label, url):
    key = hashlib.sha256(url.encode()).hexdigest()[:12]
    path = cache / f"{label}-{key}"
    receipt_path = path.with_suffix(".json")
    if not path.exists() or not receipt_path.exists():
        data = fetch(url)
        path.write_bytes(data)
        json_write(receipt_path, dict(source=url, sha256=HELPERS.sha256(path)))
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if HELPERS.sha256(path) != receipt["sha256"]:
        raise ValueError(f"Source cache checksum mismatch: {path}")
    return path, receipt


def raster_window(cache, label, source, bounds, max_size, allow_partial=False):
    """Cache only the requested source window, using existing COG overviews."""
    identity = [source, bounds, max_size] + (["partial-mask"] if allow_partial else [])
    key = hashlib.sha256(json.dumps(identity).encode()).hexdigest()[:12]
    path = cache / f"{label}-{key}.tif"
    receipt_path = path.with_suffix(".json")
    if not path.exists() or not receipt_path.exists():
        with rasterio.Env(**ENV), rasterio.open(source) as src:
            if src.crs is None:
                raise ValueError(f"Source has no CRS: {source}")
            b = transform_bounds("EPSG:4326", src.crs, *bounds, densify_pts=21)
            win = window_from_bounds(*b, src.transform)
            left, top = math.floor(win.col_off) - 2, math.floor(win.row_off) - 2
            right, bottom = math.ceil(win.col_off + win.width) + 2, math.ceil(win.row_off + win.height) + 2
            win = Window(left, top, right - left, bottom - top).intersection(Window(0, 0, src.width, src.height))
            scale = min(1.0, max_size / max(win.width, win.height))
            width, height = math.ceil(win.width * scale), math.ceil(win.height * scale)
            pixels = src.read(window=win, out_shape=(src.count, height, width), resampling=Resampling.bilinear)
            mask = src.dataset_mask(window=win, out_shape=(height, width), resampling=Resampling.nearest)
            transform = src.window_transform(win) * Affine.scale(win.width / width, win.height / height)
            profile = dict(driver="GTiff", width=width, height=height, count=src.count,
                           dtype=pixels.dtype, crs=src.crs, transform=transform, compress="deflate")
            if src.count == 1:
                pixels = pixels.astype(np.float32)
                pixels[:, mask == 0] = np.nan
                profile.update(dtype="float32", nodata=np.nan)
            elif not mask.all():
                if not allow_partial:
                    raise ValueError(f"RGB source window contains missing pixels: {source}")
                pixels = pixels.astype(np.float32)
                pixels[:, mask == 0] = np.nan
                profile.update(dtype="float32", nodata=np.nan)
            with rasterio.open(path, "w", **profile) as dst:
                dst.write(pixels)
            receipt = dict(source=source, geographic_request_bounds=bounds,
                           source_crs=str(src.crs), source_window=[win.col_off, win.row_off, win.width, win.height],
                           cached_size=[width, height], resampling="bilinear, COG overview selected by GDAL",
                           sha256=HELPERS.sha256(path))
        json_write(receipt_path, receipt)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if HELPERS.sha256(path) != receipt["sha256"]:
        raise ValueError(f"Source cache checksum mismatch: {path}")
    return path, receipt


def sample(path, bounds, size, vertices=False):
    west, south, east, north = bounds
    if vertices:
        dx, dy = (east - west) / (size[1] - 1), (north - south) / (size[0] - 1)
        transform = from_origin(west - dx / 2, north + dy / 2, dx, dy)
    else:
        transform = from_bounds(*bounds, size[1], size[0])
    with rasterio.open(path) as src:
        result = np.full((src.count, *size), np.nan, dtype=np.float32)
        for band in range(src.count):
            reproject(src.read(band + 1), result[band], src_transform=src.transform,
                      src_crs=CRS.from_user_input(src.crs).to_2d().to_wkt(), src_nodata=src.nodata,
                      dst_transform=transform, dst_crs="EPSG:4326", dst_nodata=np.nan,
                      resampling=Resampling.bilinear)
    return result


def satellite(cache, tile_id, bounds, size=IMAGE_SIZE):
    params = dict(service="WMS", version="1.1.1", request="GetMap", layers="s2cloudless",
                  styles="", srs="EPSG:4326", bbox=",".join(map(str, bounds)),
                  width=size, height=size, format="image/jpeg")
    path, receipt = cached_bytes(cache, f"eox-{tile_id}", EOX + "?" + urllib.parse.urlencode(params))
    with Image.open(path) as image:
        if image.size != (size, size):
            raise ValueError(f"Unexpected WMS image dimensions for {tile_id}: {image.size}")
        rgb = np.array(image.convert("RGB"))
    return rgb, receipt


def dem_url(lat, lon):
    name = f"Copernicus_DSM_COG_10_N{lat:02d}_00_E{lon:03d}_00_DEM"
    return name, f"https://copernicus-dem-30m.s3.amazonaws.com/{name}/{name}.tif"


def patch_imagery(cache, tile_id, bounds, rgb, dem_path):
    scenes = [ULLEUNG_SCENE] if tile_id == "N37E130" else PATCH_SCENES
    land = sample(dem_path, bounds, (IMAGE_SIZE,IMAGE_SIZE))[0] > 0.1
    filled = np.zeros(land.shape, dtype=bool)
    receipts = []
    for scene_id, scene_bounds, cloud in scenes:
        clipped = [max(bounds[0],scene_bounds[0]), max(bounds[1],scene_bounds[1]),
                   min(bounds[2],scene_bounds[2]), min(bounds[3],scene_bounds[3])]
        if clipped[0] >= clipped[2] or clipped[1] >= clipped[3]:
            continue
        _,mgrs,date,*_ = scene_id.split("_")
        url = ("https://sentinel-cogs.s3.us-west-2.amazonaws.com/sentinel-s2-l2a-cogs/"
               f"{mgrs[:2]}/{mgrs[2]}/{mgrs[3:]}/{date[:4]}/{int(date[4:6])}/{scene_id}/TCI.tif")
        path,receipt = raster_window(cache, "sentinel-"+tile_id+"-"+mgrs, url, clipped, 1536, allow_partial=True)
        pixels = sample(path, bounds, (IMAGE_SIZE,IMAGE_SIZE))
        mask = land & np.isfinite(pixels).all(axis=0) & ~filled
        rgb[mask] = np.rint(pixels[:,mask]).clip(0,255).astype(np.uint8).T
        filled |= mask
        receipts.append(dict(**receipt, scene_id=scene_id, scene_cloud_cover_percent=cloud,
                             replaced_pixels=int(np.count_nonzero(mask))))
    if not filled.any():
        raise ValueError(f"Sentinel imagery did not cover any land in {tile_id}")
    return dict(scenes=receipts, replaced_land_pixels=int(np.count_nonzero(filled)),
                total_land_pixels=int(np.count_nonzero(land)),
                compositing="Best cloud-score scene first; finite Sentinel RGB only on DEM > 0.1 m; EOX elsewhere")


def prepare_base(cache, latlon, available):
    lat, lon = latlon
    tile_id = f"N{lat:02d}E{lon:03d}"
    bounds = [lon, lat, lon + 1, lat + 1]
    name, url = dem_url(lat, lon)
    receipt = dict(source=TILE_LIST, ocean_tile=True, orthometric_height_m=0)
    if name in available:
        path, receipt = raster_window(cache, "dem-" + tile_id, url, bounds, 513)
        heights = sample(path, bounds, (SIZE, SIZE), vertices=True)[0]
        if not np.isfinite(heights[1:-1, 1:-1]).all():
            raise ValueError(f"Missing interior elevation in listed land tile {tile_id}")
    else:
        heights = np.zeros((SIZE, SIZE), dtype=np.float32)
    rgb, rgb_receipt = satellite(cache, tile_id, bounds)
    if tile_id in {"N35E128","N35E129","N36E128","N36E129","N37E130"}:
        rgb_receipt["sentinel_patches"] = patch_imagery(cache, tile_id, bounds, rgb, path)
    print(f"Source ready {tile_id} ({'land' if name in available else 'ocean'})", flush=True)
    return dict(id=tile_id, bounds=bounds, heights=heights, rgb=rgb,
                sources=dict(elevation=receipt, imagery=rgb_receipt))


def write_tile(staging, tile, heights, geoid, checks, detail=False):
    west, south, east, north = tile["bounds"]
    size = heights.shape[0]
    lon, lat = np.meshgrid(np.linspace(west, east, size), np.linspace(north, south, size))
    geo = HELPERS.topocentric(ORIGIN)
    center = np.array(geo.transform((west + east) / 2, (south + north) / 2, 0.0))
    global_positions = np.stack(geo.transform(lon, lat, heights.astype(np.float64) + geoid), axis=-1)
    positions = (global_positions - center).astype("<f4")
    # Independent ECEF rotation checks units/axes and bounds float32 tile error.
    ecef = Transformer.from_crs(4979, 4978, always_xy=True)
    base = np.array(ecef.transform(128.0, 36.0, 0.0))
    p, l = np.deg2rad([36.0, 128.0])
    rotation = np.array([[-np.sin(l), np.cos(l), 0], [-np.sin(p)*np.cos(l), -np.sin(p)*np.sin(l), np.cos(p)],
                         [np.cos(p)*np.cos(l), np.cos(p)*np.sin(l), np.sin(p)]])
    error = 0.0
    for row, col in [(0,0), (0,size-1), (size-1,0), (size-1,size-1), (size//2,size//2)]:
        xyz = np.array(ecef.transform(lon[row,col], lat[row,col], float(heights[row,col]) + float(geoid[row,col])))
        error = max(error, float(np.max(np.abs(rotation @ (xyz-base) - (positions[row,col].astype(float) + center)))))
    if error > 0.006 or not np.isfinite(positions).all():
        raise ValueError(f"Independent coordinate check failed {tile['id']}: {error} m")
    directory = staging / "tiles" / tile["id"]
    directory.mkdir(parents=True, exist_ok=True)
    positions.tofile(directory / "vertices.raw")
    Image.fromarray(tile["rgb"]).save(directory / "satellite.png")
    entry = dict(id=tile["id"], directory=f"tiles/{tile['id']}", width=size, height=size,
                 center_enu_m=center.tolist(), west_deg=west, south_deg=south, east_deg=east, north_deg=north,
                 vertex_file="vertices.raw", image_file="satellite.png",
                 orthometric_height_range_m=[float(heights.min()), float(heights.max())],
                 geoid_range_m=[float(geoid.min()), float(geoid.max())],
                 sources=tile["sources"], files={name: HELPERS.sha256(directory/name) for name in ("vertices.raw", "satellite.png")})
    if tile["id"] == "N37E131":
        entry["skip_cells"] = [216, 188, 228, 200]
    if detail:
        entry["detail"] = True
    checks["maximum_coordinate_error_m"] = max(checks["maximum_coordinate_error_m"], error)
    return entry, positions.astype(np.float64) + center


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "maps/korea")
    args = parser.parse_args()
    started = time.monotonic()
    output = args.output.resolve()
    cache = ROOT / "data/korea/source" if output == (ROOT / "maps/korea").resolve() else output / "source"
    cache.mkdir(parents=True, exist_ok=True)
    staging = output / ".staging"
    staging.mkdir(exist_ok=True)
    list_path, list_receipt = cached_bytes(cache, "tile-list", TILE_LIST)
    available = set(list_path.read_text(encoding="utf-8").splitlines())
    geoid_path, geoid_receipt = raster_window(cache, "geoid", GEOID, [123.9,32.9,132.1,39.1], 1024)
    latlons = [(lat,lon) for lat in range(33,39) for lon in range(124,132)]
    # ponytail: two source readers keep this bounded; runtime streaming is separate.
    with ThreadPoolExecutor(max_workers=2) as pool:
        bases = list(pool.map(lambda item: prepare_base(cache, item, available), latlons))
    shape = (6*(SIZE-1)+1, 8*(SIZE-1)+1)
    sums, counts = np.zeros(shape, dtype=np.float64), np.zeros(shape, dtype=np.uint8)
    for tile in bases:
        west,south,_,_ = tile["bounds"]
        row, col = (38-south)*(SIZE-1), (west-124)*(SIZE-1)
        valid = np.isfinite(tile["heights"])
        sums[row:row+SIZE,col:col+SIZE] += np.where(valid, tile["heights"], 0)
        counts[row:row+SIZE,col:col+SIZE] += valid
    if not counts[1:-1,1:-1].all():
        raise ValueError("Missing interior global elevation samples")
    # COGs omit their final east/south sample. Outside the complete national box,
    # extend only the outermost half-cell from its nearest valid interior sample.
    missing_y,missing_x = np.where(counts == 0)
    source_y,source_x = np.clip(missing_y,1,shape[0]-2),np.clip(missing_x,1,shape[1]-2)
    sums[missing_y,missing_x] = sums[source_y,source_x] / counts[source_y,source_x]
    counts[missing_y,missing_x] = 1
    heights = (sums/counts).astype(np.float32)
    geoid = sample(geoid_path, BOUNDS, shape, vertices=True)[0]
    if not np.isfinite(geoid).all():
        raise ValueError("Incomplete EGM2008 geoid coverage")
    checks = dict(base_tile_count=48, detail_tile_count=1, maximum_coordinate_error_m=0.0,
                  maximum_shared_edge_error_m=0.0, imagery_size=[IMAGE_SIZE,IMAGE_SIZE],
                  global_height_lattice=list(shape), finite_elevations=True,
                  outer_boundary_extrapolated_samples=len(missing_y))
    entries, edges = [], {}
    overview = Image.new("RGB", (8*256, 6*256))
    for tile in bases:
        west,south,_,_ = tile["bounds"]
        row,col = (38-south)*(SIZE-1), (west-124)*(SIZE-1)
        tile_h, tile_g = heights[row:row+SIZE,col:col+SIZE], geoid[row:row+SIZE,col:col+SIZE]
        entry, positions = write_tile(staging, tile, tile_h, tile_g, checks)
        entries.append(entry)
        edges[(south,west)] = dict(top=positions[0].copy(), bottom=positions[-1].copy(), left=positions[:,0].copy(), right=positions[:,-1].copy())
        overview.paste(Image.fromarray(tile["rgb"]).resize((256,256), Image.Resampling.LANCZOS), ((west-124)*256, (38-south)*256))
    for (lat,lon), edge in edges.items():
        for neighbor, side, opposite in [((lat+1,lon),"top","bottom"), ((lat,lon+1),"right","left")]:
            if neighbor in edges:
                error = float(np.max(np.abs(edge[side]-edges[neighbor][opposite])))
                checks["maximum_shared_edge_error_m"] = max(checks["maximum_shared_edge_error_m"],error)
    if checks["maximum_shared_edge_error_m"] > 0.006:
        raise ValueError("Base tile seam continuity failed")
    # EOX 2016 contains no Dokdo pixels; supplement with a real, low-cloud Sentinel acquisition.
    _, url = dem_url(37,131)
    dem_path, dem_receipt = raster_window(cache, "dem-Dokdo", url, DOKDO_BOUNDS, 1024)
    detail_h = sample(dem_path, DOKDO_BOUNDS, (SIZE,SIZE), vertices=True)[0]
    detail_g = sample(geoid_path, DOKDO_BOUNDS, (SIZE,SIZE), vertices=True)[0]
    image_path, image_receipt = raster_window(cache, "sentinel-Dokdo", DOKDO_IMAGE, DOKDO_BOUNDS, 1536)
    detail_rgb = sample(image_path, DOKDO_BOUNDS, (IMAGE_SIZE,IMAGE_SIZE))
    if not np.isfinite(detail_h).all() or not np.isfinite(detail_rgb).all():
        raise ValueError("Dokdo detail has missing pixels")
    # Preserve the surrounding basemap's sea colour; no synthetic land texture is used.
    water, water_receipt = satellite(cache, "Dokdo-water", DOKDO_BOUNDS)
    mask = sample(dem_path, DOKDO_BOUNDS, (IMAGE_SIZE,IMAGE_SIZE))[0] > 0.1
    rgb = np.rint(detail_rgb).clip(0,255).astype(np.uint8).transpose(1,2,0)
    rgb[~mask] = water[~mask]
    detail = dict(id="Dokdo", bounds=DOKDO_BOUNDS, rgb=rgb,
                  sources=dict(elevation=dem_receipt, imagery=image_receipt, water=water_receipt,
                               imagery_date="2025-06-04", imagery_cloud_cover_percent=0.00142,
                               water_blend="EOX water where Copernicus orthometric DEM <= 0.1 m"))
    # Match coarse sea-edge heights; this patch's perimeter is entirely offshore.
    br,bc = int((39-DOKDO_BOUNDS[3])/STEP), int((DOKDO_BOUNDS[0]-124)/STEP)
    u = np.linspace(0,12,SIZE)
    detail_h[0] = np.interp(u,np.arange(13),heights[br,bc:bc+13])
    detail_h[-1] = np.interp(u,np.arange(13),heights[br+12,bc:bc+13])
    detail_h[:,0] = np.interp(u,np.arange(13),heights[br:br+13,bc])
    detail_h[:,-1] = np.interp(u,np.arange(13),heights[br:br+13,bc+12])
    entry,_ = write_tile(staging, detail, detail_h, detail_g, checks, detail=True)
    entries.append(entry)
    checks["land_tile_count"] = sum(not tile["sources"]["elevation"].get("ocean_tile",False) for tile in bases)
    checks["ocean_tile_count"] = 48-checks["land_tile_count"]
    checks["dokdo_positive_height_vertices"] = int(np.count_nonzero(detail_h > 0.1))
    checks["elapsed_seconds"] = round(time.monotonic()-started,2)
    manifest = dict(schema_version=2, id="korea", name="대한민국 지형·위성 지도", origin=ORIGIN,
                    attribution='EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016 & 2017)\nContains modified Copernicus Sentinel data 2025; Copernicus DEM GLO-30.',
                    bounds_deg=BOUNDS, tiles=entries, grid_size=SIZE,
                    vertex_format="little-endian float32 local E,N,U metres; add tile center_enu_m; north-to-south rows",
                    height_vertical_datum="EGM2008", vertex_vertical_datum="WGS84 ellipsoid -> ENU",
                    source=dict(elevation="Copernicus GLO-30 DSM, EGM2008 orthometric heights", geoid=geoid_receipt,
                                tile_list=list_receipt, imagery="EOX Sentinel-2 cloudless 2016; five southeast/Ulleung tiles supplemented by 2025 Sentinel; Dokdo 2025-06-04"),
                    licenses=[dict(name="EOX 2016 CC BY 4.0", url="https://cloudless.eox.at/license-non-commercial",
                                   attribution="EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016 & 2017)"),
                              dict(name="Copernicus DEM public licence", url="https://copernicus-dem-30m.s3.amazonaws.com/readme.html"),
                              dict(name="Copernicus Sentinel data", url="https://sentinels.copernicus.eu/documents/247904/690755/Sentinel_Data_Legal_Notice")],
                    limitations=["Base mesh spacing about 340-435 m; base imagery about 84-109 m per pixel.",
                                 "DSM includes vegetation and buildings; not DTED or survey-verified terrain.",
                                 "Rectangular bounds include surrounding sea and neighbouring land; no political-boundary clipping.",
                                 "Missing samples only on the outside bbox edge use the adjacent interior sample; missing interior elevations fail.",
                                 "2016 base is not completely cloud-free. Five named tiles use real 2025 land imagery; dates and colour differ across sources.",
                                 "Dokdo source heights and its small coastline are limited by Copernicus DEM resolution."],
                    checks=checks)
    for entry in entries:
        dest = output / entry["directory"]
        dest.mkdir(parents=True, exist_ok=True)
        for filename in entry["files"]:
            os.replace(staging/entry["directory"]/filename, dest/filename)
    json_write(output/"terrain.json", manifest)
    artifacts = ROOT / "artifacts"
    artifacts.mkdir(exist_ok=True)
    overview.save(artifacts/"korea-overview.jpg", quality=90)
    Image.fromarray(rgb).save(artifacts/"korea-dokdo.png")
    checks["output_bytes"] = sum((output/entry["directory"]/name).stat().st_size for entry in entries for name in entry["files"])
    json_write(artifacts/"korea-preparation.json", checks)
    print(json.dumps(checks, indent=2), flush=True)


if __name__ == "__main__":
    main()
