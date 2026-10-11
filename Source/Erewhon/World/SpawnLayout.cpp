#include "World/SpawnLayout.h"

#include "Core/ErewhonSettings.h"
#include "World/SectorTypes.h"

namespace ErewhonSpawn
{
	namespace
	{
		/** Point monde exprimé dans le repère d'une cabane. */
		FVector ToCabin(EShopKind Kind, const FVector& World)
		{
			return CabinTransform(Kind).InverseTransformPosition(World);
		}
	}

	FTransform CabinTransform(EShopKind Kind)
	{
		const FVector Plaza = ErewhonSector::Center(FIntPoint::ZeroValue);
		if (Kind == EShopKind::SectorManager)
		{
			// Au nord de la route de l'est, à une vingtaine de mètres de la place.
			return FTransform(FRotator::ZeroRotator, Plaza + FVector(1100.0, 2000.0, 0.0));
		}
		if (Kind == EShopKind::Tailor)
		{
			// En face, au sud de la route de l'ouest : la porte regarde vers le nord.
			return FTransform(FRotator(0.0, 180.0, 0.0), Plaza + FVector(-1100.0, -2000.0, 0.0));
		}
		return FTransform(FRotator::ZeroRotator, Plaza);
	}

	FString ShopName(EShopKind Kind)
	{
		switch (Kind)
		{
		case EShopKind::SectorManager: return TEXT("Sector Manager");
		case EShopKind::Tailor:        return TEXT("Tailor");
		default:                       return TEXT("General Store");
		}
	}

	FVector MerchantLocation(EShopKind Kind)
	{
		// Au fond de la cabane, derrière le comptoir (la porte est côté −X).
		return CabinTransform(Kind).TransformPosition(FVector(220.0, 0.0, 0.0));
	}

	FVector DoorstepLocation(EShopKind Kind)
	{
		return CabinTransform(Kind).TransformPosition(FVector(-CabinHalfLength - 190.0, 0.0, 0.0));
	}

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
		for (const EShopKind Kind : ShopKinds)
		{
			const FVector Local = ToCabin(Kind, World);
			if (FMath::Abs(Local.X) < CabinHalfLength && FMath::Abs(Local.Y) < CabinHalfWidth && Local.Z > CabinFloorTop - 20.f)
			{
				return true;
			}
		}
		return false;
	}

	bool IsOnPorch(const FVector& World)
	{
		// Palier et marches : du mur de la porte jusqu'à 1,8 m devant, sur la largeur de l'escalier.
		for (const EShopKind Kind : ShopKinds)
		{
			const FVector Local = ToCabin(Kind, World);
			if (Local.X < -CabinHalfLength + CabinWallThickness && Local.X > -CabinHalfLength - 180.f && FMath::Abs(Local.Y) < 100.f)
			{
				return true;
			}
		}
		return false;
	}

	FTransform DoorHingeTransform(EShopKind Kind)
	{
		return FTransform(FVector(-CabinHalfLength + CabinWallThickness * 0.5f, -DoorHalfWidth, CabinFloorTop)) * CabinTransform(Kind);
	}

	FTransform PlayerSpawnTransform()
	{
		// Une dizaine de mètres au sud de la porte, regard vers le nord (+X).
		const FVector Location = ErewhonSector::Center(FIntPoint::ZeroValue) + FVector(-CabinHalfLength - 900.f, 0.f, 100.f);
		return FTransform(FRotator::ZeroRotator, Location);
	}
}
