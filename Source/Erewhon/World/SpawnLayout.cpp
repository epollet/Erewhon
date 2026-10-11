#include "World/SpawnLayout.h"

#include "Core/ErewhonSettings.h"
#include "World/SectorTypes.h"

namespace ErewhonSpawn
{
	void BuildPaths(const FIntPoint& Sector, ESectorType Type, TArray<FBox2D>& OutPaths)
	{
		const float Size = UErewhonSettings::Get().SectorSize();
		const FVector2D Origin(ErewhonSector::Origin(Sector));
		const FVector2D Center(ErewhonSector::Center(Sector));
		const float HalfPath = PathWidth * 0.5f;

		// Au spawn, la croix ; sur une route, le seul bras qui prolonge la croix (le long de son axe).
		const bool bSpawn = Type == ESectorType::Spawn;
		if (bSpawn || (Type == ESectorType::Road && Sector.Y == 0))
		{
			OutPaths.Add(FBox2D(FVector2D(Origin.X, Center.Y - HalfPath), FVector2D(Origin.X + Size, Center.Y + HalfPath)));
		}
		if (bSpawn || (Type == ESectorType::Road && Sector.X == 0))
		{
			OutPaths.Add(FBox2D(FVector2D(Center.X - HalfPath, Origin.Y), FVector2D(Center.X + HalfPath, Origin.Y + Size)));
		}
	}

	bool IsInsideCabin(const FVector& World)
	{
		const FVector Local = World - ErewhonSector::Center(FIntPoint::ZeroValue);
		return FMath::Abs(Local.X) < CabinHalfLength && FMath::Abs(Local.Y) < CabinHalfWidth && Local.Z > CabinFloorTop - 20.f;
	}

	bool IsOnPorch(const FVector& World)
	{
		// Palier et marches : du mur sud jusqu'à 1,8 m devant, sur la largeur de l'escalier.
		const FVector Local = World - ErewhonSector::Center(FIntPoint::ZeroValue);
		return Local.X < -CabinHalfLength + CabinWallThickness && Local.X > -CabinHalfLength - 180.f && FMath::Abs(Local.Y) < 100.f;
	}

	FVector DoorHingeLocation()
	{
		return ErewhonSector::Center(FIntPoint::ZeroValue) + FVector(-CabinHalfLength + CabinWallThickness * 0.5f, -DoorHalfWidth, CabinFloorTop);
	}

	FTransform PlayerSpawnTransform()
	{
		// Une dizaine de mètres au sud de la porte, regard vers le nord (+X).
		const FVector Location = ErewhonSector::Center(FIntPoint::ZeroValue) + FVector(-CabinHalfLength - 900.f, 0.f, 100.f);
		return FTransform(FRotator::ZeroRotator, Location);
	}
}
