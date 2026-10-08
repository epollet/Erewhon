#include "World/SpawnLayout.h"

#include "Core/ErewhonSettings.h"
#include "World/SectorTypes.h"

namespace ErewhonSpawn
{
	void BuildExclusions(const FIntPoint& Sector, TArray<FBox2D>& OutPaths, TArray<FBox2D>& OutExclusions)
	{
		const float Size = UErewhonSettings::Get().SectorSize();
		const FVector2D Origin(ErewhonSector::Origin(Sector));
		const FVector2D Center(ErewhonSector::Center(Sector));
		const float HalfPath = PathWidth * 0.5f;

		// Bras nord-sud et est-ouest, d'un bord à l'autre du secteur.
		OutPaths.Add(FBox2D(FVector2D(Origin.X, Center.Y - HalfPath), FVector2D(Origin.X + Size, Center.Y + HalfPath)));
		OutPaths.Add(FBox2D(FVector2D(Center.X - HalfPath, Origin.Y), FVector2D(Center.X + HalfPath, Origin.Y + Size)));

		OutExclusions.Append(OutPaths);
		OutExclusions.Add(FBox2D(Center - FVector2D(PlotHalfSize), Center + FVector2D(PlotHalfSize)));
	}

	FTransform PlayerSpawnTransform()
	{
		// Une dizaine de mètres au sud de la porte, regard vers le nord (+X).
		const FVector Location = ErewhonSector::Center(FIntPoint::ZeroValue) + FVector(-CabinHalfLength - 900.f, 0.f, 100.f);
		return FTransform(FRotator::ZeroRotator, Location);
	}
}
