"""Crée la carte vide /Game/Maps/Erewhon : tout le contenu est instancié par le code."""
import unreal

PATH = "/Game/Maps/Erewhon"

if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    unreal.log("Carte déjà présente : " + PATH)
else:
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, PATH)
    unreal.log("Carte créée : %s (%s)" % (PATH, ok))
