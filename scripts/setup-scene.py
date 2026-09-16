"""Run once inside Unreal Editor via scripts/dev.ps1 setup-scene."""
from pathlib import Path
import json
import unreal


root = Path(unreal.Paths.project_dir()).resolve()
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
material_path = "/Game/Materials/M_Satellite"
material = unreal.load_asset(material_path)
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_Satellite", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    sample = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -300, 0)
    sample.set_editor_property("parameter_name", "Satellite")
    sample.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/DefaultTexture"))
    unreal.MaterialEditingLibrary.connect_material_property(
        sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -300, 200)
    roughness.set_editor_property("r", 0.95)
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError("Could not save satellite material")

map_path = "/Game/Maps/JejuDemo"
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if assets.does_asset_exist(map_path):
    if not levels.load_level(map_path):
        raise RuntimeError("Could not open Jeju level")
elif not levels.new_level(map_path):
    raise RuntimeError("Could not create Jeju level")

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
scene = next((actor for actor in actors.get_all_level_actors()
              if isinstance(actor, unreal.FlightScene)), None)
if scene is None:
    scene = actors.spawn_actor_from_class(unreal.FlightScene, unreal.Vector(0, 0, 0))
    scene.set_actor_label("UAVSim Flight Scene")
if not scene.load_terrain():
    raise RuntimeError("Terrain load failed; inspect the Output Log")
if not levels.save_current_level():
    raise RuntimeError("Could not save Jeju level")

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "ModelContextProtocol.StartServer 8000")
unreal.SystemLibrary.execute_console_command(world, "ModelContextProtocol.GenerateClientConfig Codex")
unreal.SystemLibrary.execute_console_command(world, "WebControl.StartServer")

# Keep the actual reflected object path for API clients; never assume an actor suffix.
receipt = root / "artifacts" / "scene-setup.json"
receipt.parent.mkdir(exist_ok=True)
receipt.write_text(json.dumps({"map": map_path, "editor_object_path": scene.get_path_name(),
                               "mcp_url": "http://127.0.0.1:8000/mcp",
                               "remote_control_url": "http://127.0.0.1:30010"}, indent=2))
unreal.log("UAVSim scene setup complete. Press Play for the kinematic flight demonstration.")
