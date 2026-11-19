"""Unreal commandlet: generate the project's vertex-colour/opacity propeller material."""
import unreal

path="/Game/Materials/M_PropellerVisual"
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
material=unreal.load_asset(path)
if material is None:
    material=unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_PropellerVisual","/Game/Materials",unreal.Material,unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode",unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided",True)
    material.set_editor_property("translucency_lighting_mode",unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    edit=unreal.MaterialEditingLibrary
    vertex=edit.create_material_expression(material,unreal.MaterialExpressionVertexColor,-500,0)
    edit.connect_material_property(vertex,"RGB",unreal.MaterialProperty.MP_BASE_COLOR)
    opacity=edit.create_material_expression(material,unreal.MaterialExpressionScalarParameter,-500,200)
    opacity.set_editor_property("parameter_name","Opacity")
    opacity.set_editor_property("default_value",1.)
    product=edit.create_material_expression(material,unreal.MaterialExpressionMultiply,-200,100)
    edit.connect_material_expressions(vertex,"A",product,"A")
    edit.connect_material_expressions(opacity,"",product,"B")
    edit.connect_material_property(product,"",unreal.MaterialProperty.MP_OPACITY)
    roughness=edit.create_material_expression(material,unreal.MaterialExpressionConstant,-200,300)
    roughness.set_editor_property("r",.55)
    edit.connect_material_property(roughness,"",unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(material)
if not assets.save_loaded_asset(material):raise RuntimeError("Could not save propeller material")
unreal.log("UAVSim propeller material ready: "+path)
