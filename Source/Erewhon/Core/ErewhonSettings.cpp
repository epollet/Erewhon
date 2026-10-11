#include "Core/ErewhonSettings.h"

UErewhonSettings::UErewhonSettings()
{
	// Végétation décorative (l'herbe, elle, est entièrement récoltable : voir Items.json).
	// Fougères et feuillages en bosquets ; buissons en massifs, jusqu'à 1,5× la hauteur de l'herbe.
	const auto Plant = [this](const TCHAR* Mesh, int32 Prairie, int32 Spawn, float HMin, float HMax,
		float PatchRadius, int32 PatchSize, float Cull, bool bSolid) -> FDecorPlant&
	{
		FDecorPlant& Entry = DecorPlants.AddDefaulted_GetRef();
		Entry.Mesh = Mesh;
		Entry.CountPrairie = Prairie;
		Entry.CountSpawn = Spawn;
		Entry.HeightMin = HMin;
		Entry.HeightMax = HMax;
		Entry.PatchRadius = PatchRadius;
		Entry.PatchSize = PatchSize;
		Entry.CullMeters = Cull;
		Entry.bSolid = bSolid;
		return Entry;
	};
	Plant(TEXT("Meshes/SM_fern"), 520, 200, 0.6f, 1.1f, 4.f, 7, 90.f, false);
	Plant(TEXT("Meshes/SM_broadleaf"), 420, 160, 0.4f, 0.8f, 3.f, 6, 80.f, false);
	Plant(TEXT("Meshes/SM_weed_dock"), 300, 120, 0.8f, 1.4f, 3.f, 4, 90.f, false);
	Plant(TEXT("Meshes/SM_weed_nettle"), 380, 150, 0.5f, 0.9f, 3.5f, 7, 80.f, false);
	Plant(TEXT("Meshes/SM_bush"), 60, 18, 1.1f, 1.8f, 7.f, 5, 160.f, true);
	Plant(TEXT("Meshes/SM_bush_hazel"), 56, 16, 1.5f, 2.25f, 8.f, 7, 180.f, true).FootprintRatio = 0.3f;
	Plant(TEXT("Meshes/SM_bush_rose"), 48, 14, 1.2f, 1.9f, 7.f, 6, 160.f, true);
	Plant(TEXT("Meshes/SM_bush_box"), 56, 16, 1.0f, 1.7f, 6.f, 7, 160.f, true);
}
