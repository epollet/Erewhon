#include "Core/ErewhonSettings.h"

UErewhonSettings::UErewhonSettings()
{
	// Végétation décorative : les buissons, en massifs jusqu'à 1,5× la hauteur de l'herbe.
	// Tout ce qui pousse au ras du sol (herbe, fleurs, fougères, orties…) est récoltable : voir Items.json.
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
	Plant(TEXT("Meshes/SM_bush"), 60, 18, 1.1f, 1.8f, 7.f, 5, 160.f, true);
	Plant(TEXT("Meshes/SM_bush_hazel"), 56, 16, 1.5f, 2.25f, 8.f, 7, 180.f, true).FootprintRatio = 0.3f;
	Plant(TEXT("Meshes/SM_bush_rose"), 48, 14, 1.2f, 1.9f, 7.f, 6, 160.f, true);
	Plant(TEXT("Meshes/SM_bush_box"), 56, 16, 1.0f, 1.7f, 6.f, 7, 160.f, true);
}
