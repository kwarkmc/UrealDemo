"""Add cached 10 m Jeju imagery to the active Korea map, entirely offline.

One coarse-cell-aligned hole and one detail tile use the existing runtime format.
The outer coarse-cell band joins both the original mesh and its image colors;
rendering and ground queries use the same vertices. Reload Unreal explicitly.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil

import numpy as np
from PIL import Image
from pyproj import Transformer

ROOT = Path(__file__).resolve().parents[1]
MAP = ROOT / "maps/korea"
SPEC = importlib.util.spec_from_file_location("map_import", ROOT / "scripts/prepare-map.py")
HELP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELP)
DETAIL = "JejuDetail"
SKIP = [108, 124, 152, 168]  # N33E126 half-open column/row cells; 44 x 44 cells.
SUBDIVISIONS = 11
SIZE, IMAGE_SIZE = 44 * SUBDIVISIONS + 1, 2048


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    manifest_path = MAP / "terrain.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    legacy_path = ROOT / "data/jeju/metadata.json"
    legacy = json.loads(legacy_path.read_text(encoding="utf-8"))
    base = next(tile for tile in manifest["tiles"] if tile["id"] == "N33E126")
    assert manifest["schema_version"] == 2 and manifest["id"] == "korea"
    assert (base["width"], base["height"]) == (257, 257)
    assert [base[k] for k in ("west_deg", "south_deg", "east_deg", "north_deg")] == [126, 33, 127, 34]
    assert base.get("skip_cells", SKIP) == SKIP, "Existing base exclusion must not be overwritten"
    tiles = [tile for tile in manifest["tiles"] if tile["id"] != DETAIL]
    assert len(tiles) < 64 and SIZE <= 513
    source_files = {}
    for key, prefix in (("dem", "dem"), ("imagery", "satellite"), ("geoid", "geoid")):
        expected = legacy["sources"][key]["sha256"]
        source_files[key] = next(path for path in (ROOT / "data/jeju/source").glob(prefix + "-*.tif")
                                 if digest(path) == expected)
    west, south, east, north = 126 + SKIP[0] / 256, 34 - SKIP[3] / 256, 126 + SKIP[2] / 256, 34 - SKIP[1] / 256
    bounds = [west, south, east, north]
    heights = HELP.raster_samples(source_files["dem"], bounds, SIZE, vertices=True)[0]
    geoid = HELP.raster_samples(source_files["geoid"], bounds, SIZE, vertices=True)[0]
    rgb = HELP.raster_samples(source_files["imagery"], bounds, IMAGE_SIZE, rgb=True)
    # Sample the base texture at the same WGS84 UVs, including its half-texel
    # convention. Feather only the geometry's outer one-coarse-cell band.
    base_image_path = MAP / base["directory"] / base["image_file"]
    with Image.open(base_image_path) as image:
        base_rgb = np.asarray(image.convert("RGB"), dtype=np.float32)
    image_lon = west + (np.arange(IMAGE_SIZE) + .5) * (east-west) / IMAGE_SIZE
    image_lat = north - (np.arange(IMAGE_SIZE) + .5) * (north-south) / IMAGE_SIZE
    # Clamp sampling at the exact mesh edge, where the runtime clamps edge texels.
    image_lon[[0, -1]], image_lat[[0, -1]] = [west, east], [north, south]
    x = np.clip((image_lon-126) * base_rgb.shape[1] - .5, 0, base_rgb.shape[1]-1)
    y = np.clip((34-image_lat) * base_rgb.shape[0] - .5, 0, base_rgb.shape[0]-1)
    ix, iy = x.astype(int), y.astype(int)
    jx, jy = np.minimum(ix+1, base_rgb.shape[1]-1), np.minimum(iy+1, base_rgb.shape[0]-1)
    fx, fy = (x-ix).astype(np.float32)[None, :, None], (y-iy).astype(np.float32)[:, None, None]
    coarse_rgb = ((1-fx)*(1-fy)*base_rgb[iy[:, None], ix[None, :]]
                  + fx*(1-fy)*base_rgb[iy[:, None], jx[None, :]]
                  + (1-fx)*fy*base_rgb[jy[:, None], ix[None, :]]
                  + fx*fy*base_rgb[jy[:, None], jx[None, :]])
    image_columns, image_rows = (image_lon-126)*256, (34-image_lat)*256
    color_distance = np.minimum(np.minimum(image_columns-SKIP[0], SKIP[2]-image_columns)[None, :],
                                np.minimum(image_rows-SKIP[1], SKIP[3]-image_rows)[:, None])
    color_weight = np.clip(color_distance, 0, 1).astype(np.float32)
    original_rgb = np.rint(rgb).clip(0, 255).astype(np.uint8).transpose(1, 2, 0)
    blended_rgb = np.rint(rgb.transpose(1, 2, 0)*color_weight[..., None]
                         + coarse_rgb*(1-color_weight[..., None])).clip(0, 255).astype(np.uint8)
    assert np.array_equal(blended_rgb[color_weight == 1], original_rgb[color_weight == 1])
    color_edge_error = float(np.max(np.abs(blended_rgb[color_weight == 0].astype(float)
                                           - coarse_rgb[color_weight == 0])))
    assert color_edge_error <= .5001
    color_feather = dict(base_tile=base["id"], base_image_sha256=digest(base_image_path),
        band_coarse_cells=1, method="Bilinear base WGS84 UV sampling; linear feather in encoded RGB",
        interior_pixels_unchanged=True, maximum_edge_rgb_quantization_error=color_edge_error)
    longitude, latitude = np.meshgrid(np.linspace(west, east, SIZE), np.linspace(north, south, SIZE))
    transform = HELP.HELPERS.topocentric(manifest["origin"])
    center = np.array(transform.transform((west + east) / 2, (south + north) / 2, 0))
    detail = np.stack(transform.transform(longitude, latitude, heights.astype(float) + geoid), axis=-1)

    # The base's boundary segments are the exact coarse triangle edges. Match
    # them in 3D, blending only the outer one-cell strip to avoid skirts/overlap.
    base_path = MAP / base["directory"] / base["vertex_file"]
    coarse = np.fromfile(base_path, dtype="<f4").reshape(257, 257, 3).astype(float) + base["center_enu_m"]
    columns, rows = np.meshgrid(np.linspace(SKIP[0], SKIP[2], SIZE), np.linspace(SKIP[1], SKIP[3], SIZE))
    col, row = np.floor(columns).astype(int), np.floor(rows).astype(int)
    u, v = (columns - col)[..., None], (rows - row)[..., None]
    interpolated = ((1-u)*(1-v)*coarse[row, col] + u*(1-v)*coarse[row, col+1]
                    + (1-u)*v*coarse[row+1, col] + u*v*coarse[row+1, col+1])
    distance = np.minimum.reduce((columns-SKIP[0], SKIP[2]-columns, rows-SKIP[1], SKIP[3]-rows))
    weight = np.clip(distance, 0, 1)[..., None]
    joined = interpolated * (1-weight) + detail * weight
    raw = (joined - center).astype("<f4")
    reconstructed = raw.astype(float) + center
    border = distance == 0
    edge_error = float(np.max(np.linalg.norm(reconstructed[border] - interpolated[border], axis=-1)))
    storage_error = float(np.max(np.linalg.norm(reconstructed - joined, axis=-1)))
    assert edge_error < .002 and storage_error < .002 and np.isfinite(raw).all()
    # Ascending columns and descending rows must remain unflipped after joining.
    east_vectors = reconstructed[:-1, 1:] - reconstructed[:-1, :-1]
    south_vectors = reconstructed[1:, :-1] - reconstructed[:-1, :-1]
    assert (np.cross(east_vectors, south_vectors)[..., 2] < 0).all()
    assert (np.abs(raw) <= 200000).all()

    # Separate WGS84 ECEF rotation checks an untouched interior position.
    origin = manifest["origin"]
    ecef = Transformer.from_crs(4979, 4978, always_xy=True)
    anchor = np.array(ecef.transform(origin["longitude_deg"], origin["latitude_deg"], origin["ellipsoid_height_m"]))
    p, l = np.deg2rad([origin["latitude_deg"], origin["longitude_deg"]])
    rotation = np.array([[-np.sin(l), np.cos(l), 0], [-np.sin(p)*np.cos(l), -np.sin(p)*np.sin(l), np.cos(p)],
                         [np.cos(p)*np.cos(l), np.cos(p)*np.sin(l), np.sin(p)]])
    mid = SIZE // 2
    xyz = np.array(ecef.transform(longitude[mid, mid], latitude[mid, mid], float(heights[mid, mid]+geoid[mid, mid])))
    coordinate_error = float(np.linalg.norm(rotation @ (xyz-anchor) - reconstructed[mid, mid]))
    assert coordinate_error < .002
    test_col, test_row = (126.49-126)*256, (34-33.509)*256
    assert min(test_col-SKIP[0], SKIP[2]-test_col, test_row-SKIP[1], SKIP[3]-test_row) > 1

    # Preserve every pre-existing file before replacing it. Runtime remains on
    # its already loaded assets until an explicit reload after this tool returns.
    backup = ROOT / "artifacts/jeju-detail-backup" / digest(manifest_path)[:12]
    directory = MAP / "tiles" / DETAIL
    for old in (manifest_path, directory / "vertices.raw", directory / "satellite.png"):
        if old.exists():
            saved = backup / old.relative_to(MAP)
            saved.parent.mkdir(parents=True, exist_ok=True)
            if not saved.exists():
                shutil.copy2(old, saved)
    directory.mkdir(parents=True, exist_ok=True)
    vertex_tmp, image_tmp = directory / "vertices.raw.tmp", directory / "satellite.png.tmp"
    raw.tofile(vertex_tmp)
    Image.fromarray(blended_rgb).save(image_tmp, format="PNG")
    assert vertex_tmp.stat().st_size == SIZE * SIZE * 12
    assert image_tmp.stat().st_size <= 20 * 1024 * 1024
    os.replace(vertex_tmp, directory / "vertices.raw")
    os.replace(image_tmp, directory / "satellite.png")
    entry = dict(id=DETAIL, directory="tiles/"+DETAIL, width=SIZE, height=SIZE,
        center_enu_m=center.tolist(), west_deg=west, south_deg=south, east_deg=east, north_deg=north,
        vertex_file="vertices.raw", image_file="satellite.png", detail=True,
        imagery_scene=legacy["imagery_scene"], imagery_date=legacy["imagery_date"],
        image_size=[IMAGE_SIZE, IMAGE_SIZE], native_image_resolution_m=10,
        height_vertical_datum="EGM2008", vertex_vertical_datum="WGS84 ellipsoid -> ENU",
        boundary_join="Outer one base-cell band blends to existing coarse mesh; border vertices lie on its exact edges",
        imagery_feather=color_feather,
        replaces=dict(tile="N33E126", skip_cells=SKIP), sources=legacy["sources"],
        source_metadata_sha256=digest(legacy_path),
        files={name: digest(directory/name) for name in ("vertices.raw", "satellite.png")})
    base["skip_cells"] = SKIP
    manifest["tiles"] = tiles + [entry]
    manifest["checks"]["detail_tile_count"] = sum(bool(tile.get("detail")) for tile in manifest["tiles"])
    limitation = "JejuDetail reuses 2025-04-06 Sentinel-2 10 m imagery; outer one coarse-cell band joins the original terrain. No sub-10 m source detail is created."
    if limitation not in manifest["limitations"]:
        manifest["limitations"].append(limitation)
    feather_note = "JejuDetail image colors feather to the existing coarse image only within the same outer one-coarse-cell band; interior 10 m imagery is unchanged."
    if feather_note not in manifest["limitations"]:
        manifest["limitations"].append(feather_note)
    note = "Jeju detail: modified Copernicus Sentinel-2 data 2025-04-06."
    if note not in manifest["attribution"]:
        manifest["attribution"] += "\n" + note
    spacing = np.linalg.norm(detail[mid, 1] - detail[mid, 0]), np.linalg.norm(detail[1, mid] - detail[0, mid])
    receipt = dict(tile_count=len(manifest["tiles"]), detail_id=DETAIL, bounds_deg=bounds,
        grid=[SIZE, SIZE], mesh_spacing_m=list(spacing), texture=[IMAGE_SIZE, IMAGE_SIZE],
        texture_ground_sampling_m=[float(s*(SIZE-1)/IMAGE_SIZE) for s in spacing], native_image_resolution_m=10,
        maximum_edge_join_error_m=edge_error, maximum_float32_error_m=storage_error,
        imagery_feather=color_feather,
        independent_coordinate_error_m=coordinate_error, test_point_inside_unblended_detail=True,
        base_skipped_cells=(SKIP[2]-SKIP[0])*(SKIP[3]-SKIP[1]),
        tile_file_bytes=sum((directory/name).stat().st_size for name in ("vertices.raw", "satellite.png")),
        backup_directory=str(backup.relative_to(ROOT)), sources=entry["sources"], files=entry["files"])
    manifest["checks"]["jeju_detail"] = {key: value for key, value in receipt.items() if key not in ("sources", "files")}
    HELP.save_json(manifest_path, manifest)
    HELP.save_json(ROOT / "artifacts/jeju-detail-preparation.json", receipt)
    print(json.dumps(receipt, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
