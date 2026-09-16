"""Prepare one real, offline Jeju terrain patch; GIS tools are not flight dynamics.

Run with .venv/Scripts/python.exe scripts/prepare-terrain.py.
For a local DTED cell covering this patch, add --dem file.dt2 --vertical-datum EGM96.
The source must cover the whole patch; missing elevations fail instead of becoming sea.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image
import pyproj
from pyproj import CRS, Transformer
import rasterio
from rasterio.enums import Resampling
from rasterio.transform import from_bounds, from_origin
from rasterio.warp import reproject, transform_bounds
from rasterio.windows import Window, from_bounds as window_from_bounds


ROOT = Path(__file__).resolve().parents[1]
DEM = ("https://copernicus-dem-30m.s3.amazonaws.com/"
       "Copernicus_DSM_COG_10_N33_00_E126_00_DEM/"
       "Copernicus_DSM_COG_10_N33_00_E126_00_DEM.tif")
IMAGE = ("https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
         "sentinel-s2-l2a-cogs/52/S/BC/2025/4/S2B_52SBC_20250406_0_L2A/TCI.tif")
GEOIDS = {
    "EGM2008": "https://cdn.proj.org/us_nga_egm08_25.tif",
    "EGM96": "https://cdn.proj.org/us_nga_egm96_15.tif",
}


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def cache_crop(source, cache, label, bounds):
    """Preserve native samples in a small window, including a resampling border."""
    identity = source
    if not source.startswith("https://"):
        local = Path(source).resolve(strict=True)
        identity = f"{local}:{local.stat().st_mtime_ns}:{local.stat().st_size}"
    key = hashlib.sha256(json.dumps([identity, bounds]).encode()).hexdigest()[:12]
    path = cache / f"{label}-{key}.tif"
    receipt_path = path.with_suffix(".json")
    if not (path.exists() and receipt_path.exists()):
        print(f"Reading {label} window", flush=True)
        with rasterio.open(source) as src:
            if src.crs is None:
                raise ValueError(f"{label}: missing source CRS")
            source_bounds = transform_bounds("EPSG:32652", src.crs, *bounds,
                                            densify_pts=21)
            window = window_from_bounds(*source_bounds, src.transform)
            left, top = math.floor(window.col_off) - 2, math.floor(window.row_off) - 2
            right = math.ceil(window.col_off + window.width) + 2
            bottom = math.ceil(window.row_off + window.height) + 2
            window = Window(left, top, right - left, bottom - top).intersection(
                Window(0, 0, src.width, src.height))
            pixels = src.read(window=window)
            profile = dict(driver="GTiff", width=pixels.shape[2],
                           height=pixels.shape[1], count=src.count,
                           dtype=src.dtypes[0], crs=src.crs, nodata=src.nodata,
                           transform=src.window_transform(window), compress="deflate")
            with rasterio.open(path, "w", **profile) as dst:
                dst.write(pixels)
                dst.update_tags(**src.tags())
            receipt = dict(source=source, native_crs=src.crs.to_string(),
                           source_size=[src.width, src.height],
                           source_window=[window.col_off, window.row_off,
                                          window.width, window.height],
                           source_tags=src.tags())
        receipt["sha256"] = sha256(path)
        receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if sha256(path) != receipt["sha256"]:
        raise ValueError(f"Cached source checksum mismatch: {path}")
    return path, receipt


def resample(path, transform, size, bands):
    result = np.full((bands, size, size), np.nan, dtype=np.float32)
    with rasterio.open(path) as src:
        if src.count < bands:
            raise ValueError(f"Expected {bands} bands: {path}")
        # Elevation datum correction is explicit below, never delegated implicitly.
        horizontal_crs = CRS.from_user_input(src.crs).to_2d().to_wkt()
        for index in range(bands):
            # RGB zero in one channel is a valid colour; use the combined mask below.
            reproject(src.read(index + 1), result[index],
                      src_transform=src.transform, src_crs=horizontal_crs,
                      src_nodata=src.nodata if bands == 1 else None, dst_transform=transform,
                      dst_crs="EPSG:32652", dst_nodata=np.nan,
                      resampling=Resampling.bilinear)
        if bands > 1:
            valid = np.zeros((size, size), dtype=np.uint8)
            reproject(src.dataset_mask(), valid, src_transform=src.transform,
                      src_crs=horizontal_crs, src_nodata=0, dst_transform=transform,
                      dst_crs="EPSG:32652", dst_nodata=0, resampling=Resampling.nearest)
            if not valid.all():
                raise ValueError(f"Imagery contains missing pixels: {path}")
    if not np.isfinite(result).all():
        raise ValueError(f"Source has missing samples or does not cover the patch: {path}")
    return result


def topocentric(origin):
    return Transformer.from_pipeline(
        "+proj=pipeline +step +proj=unitconvert +xy_in=deg +xy_out=rad "
        "+step +proj=cart +ellps=WGS84 +step +proj=topocentric +ellps=WGS84 "
        f"+lat_0={origin['latitude_deg']} +lon_0={origin['longitude_deg']} "
        f"+h_0={origin['ellipsoid_height_m']}")


def check_geometry(origin, positions, lon, lat, ellipsoid_height, patch_width):
    """One independent ECEF check guards ENU axes, vertical sign, and grid ordering."""
    matrix_transform = topocentric(origin)
    zero = matrix_transform.transform(origin["longitude_deg"], origin["latitude_deg"],
                                      origin["ellipsoid_height_m"])
    if max(abs(value) for value in zero) > 1e-6:
        raise ValueError("ENU origin is not zero")
    ecef = Transformer.from_crs(4979, 4978, always_xy=True)
    xyz0 = np.array(ecef.transform(origin["longitude_deg"], origin["latitude_deg"],
                                   origin["ellipsoid_height_m"]))
    p, l = np.deg2rad([origin["latitude_deg"], origin["longitude_deg"]])
    rotation = np.array([[-np.sin(l), np.cos(l), 0],
                         [-np.sin(p) * np.cos(l), -np.sin(p) * np.sin(l), np.cos(p)],
                         [np.cos(p) * np.cos(l), np.cos(p) * np.sin(l), np.sin(p)]])
    # 2 mm covers float32 rounding over this <=20 km patch, not terrain accuracy.
    for row, col in [(0, 0), (0, -1), (-1, 0), (-1, -1),
                     (len(lat) // 2, len(lat) // 2)]:
        xyz = np.array(ecef.transform(lon[row, col], lat[row, col],
                                      ellipsoid_height[row, col]))
        if np.max(np.abs(rotation @ (xyz - xyz0) - positions[row, col])) > 0.002:
            raise ValueError("ENU/ECEF independent check failed")
    if not (positions[0, 0, 1] > positions[-1, 0, 1]
            and positions[0, 0, 0] < positions[0, -1, 0]):
        raise ValueError("Terrain row/column axes are reversed")
    if np.max(np.abs(positions[:, :, :2])) > patch_width:
        raise ValueError("Unexpected terrain horizontal extent")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dem", default=DEM, help="GeoTIFF or local .dt0/.dt1/.dt2")
    parser.add_argument("--vertical-datum", choices=[*GEOIDS, "WGS84_ELLIPSOID"])
    parser.add_argument("--output", type=Path, default=ROOT / "data/jeju")
    args = parser.parse_args()
    if args.dem != DEM and args.vertical_datum is None:
        parser.error("a custom --dem requires explicit --vertical-datum")
    datum = args.vertical_datum or "EGM2008"
    if args.dem == DEM and datum != "EGM2008":
        parser.error("the bundled Copernicus source uses EGM2008")
    output = args.output.resolve()
    cache = output / "source"
    cache.mkdir(parents=True, exist_ok=True)
    origin = dict(latitude_deg=33.43, longitude_deg=126.52, ellipsoid_height_m=0.0)
    size, image_size, patch_width = 513, 2048, 20000.0
    project = Transformer.from_crs(4326, 32652, always_xy=True)
    cx, cy = project.transform(origin["longitude_deg"], origin["latitude_deg"])
    bounds = [cx - patch_width / 2, cy - patch_width / 2,
              cx + patch_width / 2, cy + patch_width / 2]
    step = patch_width / (size - 1)
    # Raster samples are pixel centres. Put the first/last vertex ON mesh edges.
    grid_transform = from_origin(bounds[0] - step / 2, bounds[3] + step / 2, step, step)
    source_bounds = [bounds[0] - step, bounds[1] - step,
                     bounds[2] + step, bounds[3] + step]
    dem_path, dem_receipt = cache_crop(args.dem, cache, "dem", source_bounds)
    image_path, image_receipt = cache_crop(IMAGE, cache, "satellite", source_bounds)
    heights = resample(dem_path, grid_transform, size, 1)[0]
    geoid_receipt = None
    geoid = np.zeros_like(heights)
    if datum in GEOIDS:
        geoid_path, geoid_receipt = cache_crop(GEOIDS[datum], cache, "geoid", source_bounds)
        geoid = resample(geoid_path, grid_transform, size, 1)[0]
    ellipsoid_height = heights.astype(np.float64) + geoid
    easting, northing = np.meshgrid(np.linspace(bounds[0], bounds[2], size),
                                   np.linspace(bounds[3], bounds[1], size))
    lon, lat = Transformer.from_crs(32652, 4326, always_xy=True).transform(easting, northing)
    positions = np.stack(topocentric(origin).transform(lon, lat, ellipsoid_height), axis=-1)
    positions = positions.astype("<f4")
    check_geometry(origin, positions, lon, lat, ellipsoid_height, patch_width)
    rgb = resample(image_path, from_bounds(*bounds, image_size, image_size), image_size, 3)
    image = Image.fromarray(np.rint(rgb).clip(0, 255).astype(np.uint8).transpose(1, 2, 0))
    positions.tofile(output / "vertices.raw")
    heights.astype("<f4").tofile(output / "heights.raw")
    image.save(output / "satellite.png")
    image.resize((768, 768), Image.Resampling.LANCZOS).save(output / "preview.jpg", quality=90)
    metadata = dict(
        schema_version=1, name="Jeju North and Hallasan - real terrain demonstration",
        width=size, height=size, grid_size=size, vertex_file="vertices.raw",
        height_file="heights.raw", image_file="satellite.png", origin=origin,
        vertex_format="little-endian float32 E,N,U triples in metres, row-major",
        height_format="little-endian float32 metres, row-major",
        height_vertical_datum=datum, vertex_vertical_datum="WGS84 ellipsoid -> local ENU",
        row_direction="north_to_south", column_direction="west_to_east",
        uv="u=column/(width-1), v=row/(height-1); PNG row 0 is north",
        grid_crs="EPSG:32652", grid_bounds_m=bounds, grid_spacing_m=step,
        image_width=image_size, image_height=image_size,
        height_range_m=[float(heights.min()), float(heights.max())],
        geoid_undulation_range_m=[float(geoid.min()), float(geoid.max())],
        vertical_conversion="h_WGS84 = H_orthometric + N_geoid; bilinear geoid sampling"
                            if datum in GEOIDS else "source already WGS84 ellipsoid height",
        sources=dict(dem=dem_receipt, imagery=image_receipt, geoid=geoid_receipt),
        imagery_scene="S2B_52SBC_20250406_0_L2A", imagery_date="2025-04-06",
        tool_versions=dict(rasterio=rasterio.__version__, gdal=rasterio.__gdal_version__,
                           pyproj=pyproj.__version__, proj=pyproj.proj_version_str,
                           numpy=np.__version__),
        validation="finite coverage, axes/order and ECEF/ENU check at five points (<2 mm)",
        limitations=["DEM is a sampled surface including vegetation/buildings, not bare earth.",
                     "Terrain source ~30 m, imagery 10 m; this is not survey-grade geometry.",
                     "One static 20 km patch; terrain outside the patch is not provided.",
                     "No aircraft flight dynamics or qualification tests are part of this data tool."],
        sha256={name: sha256(output / name)
                for name in ["vertices.raw", "heights.raw", "satellite.png"]})
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps({key: metadata[key] for key in
                      ["width", "height", "height_range_m", "geoid_undulation_range_m",
                       "validation", "sha256"]}, indent=2))


if __name__ == "__main__":
    try:
        # No GDAL directory probes or unrelated downloads; only these COG windows.
        with rasterio.Env(GDAL_DISABLE_READDIR_ON_OPEN="EMPTY_DIR", GDAL_HTTP_TIMEOUT="60"):
            main()
    except Exception as error:
        print(f"Terrain preparation failed: {error}", file=sys.stderr)
        sys.exit(1)
