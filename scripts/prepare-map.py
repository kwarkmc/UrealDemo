"""Compile maps/<folder>/map.json and local imagery/elevation into a renderable map.

No network calls. Pass --activate to select the successfully compiled map.
"""

import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re

import numpy as np
from PIL import Image
from pyproj import CRS, Transformer
import rasterio
from rasterio.enums import Resampling
from rasterio.transform import Affine, from_bounds, from_origin
from rasterio.warp import reproject, transform_bounds
from rasterio.windows import Window, from_bounds as window_bounds

ROOT = Path(__file__).resolve().parents[1]
MAPS = ROOT / "maps"
SPEC = importlib.util.spec_from_file_location("map_helpers", ROOT / "scripts/prepare-terrain.py")
HELPERS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPERS)


def save_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding="utf-8")
    os.replace(temporary, path)


def local_file(folder, value):
    if not isinstance(value, str) or not value or Path(value).is_absolute():
        raise ValueError("Source filenames must be relative to the map folder")
    path = (folder / value).resolve(strict=True)
    if not path.is_relative_to(folder) or not path.is_file():
        raise ValueError(f"Source escapes map folder or is not a file: {value}")
    return path


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", value):
        raise ValueError("id must contain 1–64 letters, digits, underscores or hyphens")
    return value


def finite(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("Coordinates and heights must be finite numbers")
    return float(value)


def raster_samples(path, bounds, size, vertices=False, rgb=False):
    """Read a bounded native window; keep coverage/nodata separate from RGB zeros."""
    west,south,east,north = bounds
    step = [(east-west)/(size-1), (north-south)/(size-1)]
    destination = (from_origin(west-step[0]/2,north+step[1]/2,*step) if vertices
                   else from_bounds(*bounds,size,size))
    bands = [1,2,3] if rgb else [1]
    with rasterio.open(path) as src:
        if src.crs is None or src.count < len(bands):
            raise ValueError(f"Missing georeference or required bands: {path.name}")
        if rgb and any(src.dtypes[i] != "uint8" for i in range(3)):
            raise ValueError("GeoTIFF imagery must contain display-ready 8-bit RGB bands")
        projected = transform_bounds("EPSG:4326",src.crs,*bounds,densify_pts=21)
        win = window_bounds(*projected,src.transform)
        left,top = math.floor(win.col_off)-2,math.floor(win.row_off)-2
        win = Window(left,top,math.ceil(win.col_off+win.width)+2-left,
                     math.ceil(win.row_off+win.height)+2-top).intersection(Window(0,0,src.width,src.height))
        scale = min(1.0,4096/max(win.width,win.height))
        width,height = math.ceil(win.width*scale),math.ceil(win.height*scale)
        pixels = src.read(bands,window=win,out_shape=(len(bands),height,width),resampling=Resampling.bilinear)
        mask = src.dataset_mask(window=win,out_shape=(height,width),resampling=Resampling.nearest)
        transform = src.window_transform(win)*Affine.scale(win.width/width,win.height/height)
        crs = CRS.from_user_input(src.crs).to_2d().to_wkt()
        result = np.full((len(bands),size,size),np.nan,dtype=np.float32)
        for band in range(len(bands)):
            reproject(pixels[band],result[band],src_transform=transform,src_crs=crs,
                      src_nodata=None if rgb else src.nodata,dst_transform=destination,
                      dst_crs="EPSG:4326",dst_nodata=np.nan,resampling=Resampling.bilinear)
        valid = np.zeros((size,size),dtype=np.uint8)
        reproject(mask,valid,src_transform=transform,src_crs=crs,src_nodata=0,
                  dst_transform=destination,dst_crs="EPSG:4326",dst_nodata=0,resampling=Resampling.nearest)
    if not valid.all() or not np.isfinite(result).all():
        raise ValueError(f"Source does not completely cover tile or contains nodata: {path.name}")
    return result


def geoid_samples(folder, tile, bounds, size):
    datum = tile.get("vertical_datum")
    if datum == "WGS84":
        return np.zeros((size,size)), {"datum":"WGS84 ellipsoid; no geoid correction"}
    if datum not in ("EGM2008","EGM96"):
        raise ValueError("vertical_datum must explicitly be WGS84, EGM2008 or EGM96")
    if "geoid_file" in tile:
        paths = [local_file(folder,tile["geoid_file"])]
    else:
        paths = []
        for receipt in (ROOT/"data/korea/source").glob("geoid-*.json"):
            if json.loads(receipt.read_text())["source"] == HELPERS.GEOIDS[datum]:
                paths.append(receipt.with_suffix(".tif"))
    for path in paths:
        try:
            values = raster_samples(path,bounds,size,vertices=True)[0]
        except (ValueError,rasterio.errors.RasterioError):
            if "geoid_file" in tile:
                raise
            continue
        if np.max(np.abs(values)) > 200:
            raise ValueError("Geoid undulation exceeds the supported +/-200 m range")
        return values, {"datum":datum,"source_file":str(path),"sha256":HELPERS.sha256(path)}
    raise ValueError(f"No local {datum} geoid covers this tile; provide geoid_file inside the map folder")


def compile_map(config, activate=False):
    config = config.resolve(strict=True)
    folder = config.parent
    if not folder.is_relative_to(MAPS.resolve()) or folder == MAPS.resolve():
        raise ValueError("Configuration must be inside maps/<folder>/")
    if any(not re.fullmatch(r"[A-Za-z0-9_-]+",part) for part in folder.relative_to(MAPS.resolve()).parts):
        raise ValueError("Map folder names must use letters, digits, underscores or hyphens")
    spec = json.loads(config.read_text(encoding="utf-8"))
    map_id = identifier(spec["id"])
    if any(not isinstance(spec.get(key),str) or not spec[key].strip() for key in ("name","attribution")):
        raise ValueError("name and attribution are required nonempty strings")
    if len(spec["name"]) > 200 or len(spec["attribution"]) > 4096:
        raise ValueError("name is limited to 200 characters and attribution to 4096")
    origin = {key:finite(spec["origin"][key]) for key in ("latitude_deg","longitude_deg","ellipsoid_height_m")}
    if not (-90 < origin["latitude_deg"] < 90 and -180 <= origin["longitude_deg"] <= 180
            and -1000 <= origin["ellipsoid_height_m"] <= 100000):
        raise ValueError("Invalid map origin")
    tiles = spec["tiles"]
    if not isinstance(tiles,list) or not 1 <= len(tiles) <= 64:
        raise ValueError("A map requires 1–64 tiles")
    staging = folder/".build"
    entries,ids,bounds_list = [],set(),[]
    maximum_error = 0.0
    geo = HELPERS.topocentric(origin)
    for tile in tiles:
        tile_id = identifier(tile["id"])
        if tile_id.casefold() in ids:
            raise ValueError("Tile ids must be unique")
        ids.add(tile_id.casefold())
        bounds = [finite(value) for value in tile["bounds_deg"]]
        if len(bounds) != 4 or not (-180 <= bounds[0] < bounds[2] <= 180 and -90 < bounds[1] < bounds[3] < 90):
            raise ValueError("bounds_deg must be [west,south,east,north], without antimeridian crossing")
        size = tile.get("grid_size",129)
        if isinstance(size,bool) or not isinstance(size,int) or not 2 <= size <= 513:
            raise ValueError("grid_size must be an integer from 2 to 513")
        dem,image = local_file(folder,tile["dem"]),local_file(folder,tile["image"])
        heights = raster_samples(dem,bounds,size,vertices=True)[0]
        if heights.min() < -12000 or heights.max() > 10000:
            raise ValueError("DEM heights are outside the supported -12000..10000 m range")
        geoid,datum_receipt = geoid_samples(folder,tile,bounds,size)
        lon,lat = np.meshgrid(np.linspace(bounds[0],bounds[2],size),np.linspace(bounds[3],bounds[1],size))
        center = np.array(geo.transform((bounds[0]+bounds[2])/2,(bounds[1]+bounds[3])/2,0))
        positions = np.stack(geo.transform(lon,lat,heights+geoid),axis=-1)
        if np.max(np.hypot(positions[...,0],positions[...,1])) > 1e6:
            raise ValueError("Tile exceeds the supported 1000 km radius from the map origin")
        local = (positions-center).astype("<f4")
        if np.max(np.abs(center)) > 2e6 or np.max(np.abs(local)) > 200000 or not np.isfinite(local).all():
            raise ValueError("Tile exceeds supported local coordinates; split it into smaller tiles")
        recovered = geo.transform(*(local.astype(float)+center).reshape(-1,3)[[0,size-1,size*size//2,-size,-1]].T,
                                  direction="INVERSE")
        sample_indices = [0,size-1,size*size//2,-size,-1]
        ecef = Transformer.from_crs(4979,4978,always_xy=True)
        actual = np.array(ecef.transform(*recovered))
        expected = np.array(ecef.transform(lon.ravel()[sample_indices],lat.ravel()[sample_indices],(heights+geoid).ravel()[sample_indices]))
        error = float(np.max(np.abs(actual-expected)))
        maximum_error = max(maximum_error,error)
        if error > 0.025:
            raise ValueError("Coordinate roundtrip exceeds 25 mm float32 storage tolerance")
        if image.suffix.lower() in (".tif",".tiff"):
            rgb = Image.fromarray(np.rint(raster_samples(image,bounds,1024,rgb=True)).clip(0,255).astype(np.uint8).transpose(1,2,0))
        elif image.suffix.lower() in (".png",".jpg",".jpeg"):
            with Image.open(image) as source:
                rgb = source.convert("RGB")
                rgb.thumbnail((2048,2048),Image.Resampling.LANCZOS)
        else:
            raise ValueError("image must be PNG, JPEG or a georeferenced RGB GeoTIFF")
        directory = (staging/"tiles"/tile_id).resolve()
        if not directory.is_relative_to(folder) or not (folder/"tiles"/tile_id).resolve().is_relative_to(folder):
            raise ValueError("Tile output escapes map folder")
        directory.mkdir(parents=True,exist_ok=True)
        local.tofile(directory/"vertices.raw")
        rgb.save(directory/"satellite.png")
        entries.append(dict(id=tile_id,directory=f"tiles/{tile_id}",width=size,height=size,
                            west_deg=bounds[0],south_deg=bounds[1],east_deg=bounds[2],north_deg=bounds[3],
                            center_enu_m=center.tolist(),vertex_file="vertices.raw",image_file="satellite.png",
                            sources={"dem":tile["dem"],"dem_sha256":HELPERS.sha256(dem),"image":tile["image"],
                                     "image_sha256":HELPERS.sha256(image),"vertical":datum_receipt}))
        bounds_list.append(bounds)
    union = [min(b[0] for b in bounds_list),min(b[1] for b in bounds_list),max(b[2] for b in bounds_list),max(b[3] for b in bounds_list)]
    if not (union[0] <= origin["longitude_deg"] <= union[2] and union[1] <= origin["latitude_deg"] <= union[3]):
        raise ValueError("Map origin must be inside the map bounds")
    manifest = dict(schema_version=2,id=map_id,name=spec["name"],attribution=spec["attribution"],origin=origin,
                    bounds_deg=union,tiles=entries,vertex_format="float32 little-endian ENU metres relative to tile center; rows north to south",
                    checks={"finite_coordinates":True,"maximum_roundtrip_error_m":maximum_error})
    for tile in entries:
        destination = (folder/tile["directory"]).resolve()
        if not destination.is_relative_to(folder):
            raise ValueError("Tile output escapes map folder")
        destination.mkdir(parents=True,exist_ok=True)
        for filename in ("vertices.raw","satellite.png"):
            os.replace(staging/tile["directory"]/filename,destination/filename)
    save_json(folder/"terrain.json",manifest)
    if activate:
        save_json(MAPS/"active.json",{"manifest":(folder/"terrain.json").relative_to(MAPS).as_posix()})
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map_json",type=Path)
    parser.add_argument("--activate",action="store_true")
    args = parser.parse_args()
    try:
        manifest = compile_map(args.map_json,args.activate)
    except (ValueError,KeyError,TypeError,OSError,rasterio.errors.RasterioError) as error:
        parser.exit(1,f"Map preparation failed: {error}\n")
    print(json.dumps({"id":manifest["id"],"tiles":len(manifest["tiles"]),**manifest["checks"]},indent=2))


if __name__ == "__main__":
    main()
