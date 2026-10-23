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
		if (Kind == EShopKind::Breeder)
		{
			// À l'ouest de la route du nord, la porte vers la route (+Y) ; la grange plus au nord.
			return FTransform(FRotator(0.0, -90.0, 0.0), Plaza + FVector(2100.0, -1550.0, 0.0));
		}
		if (Kind == EShopKind::Workshop)
		{
			// À l'est de la route du sud, la porte vers la route (−Y) ; la cour plus au sud.
			return FTransform(FRotator(0.0, 90.0, 0.0), Plaza + FVector(-2100.0, 1600.0, 0.0));
		}
		// Le General Store : à côté de la mairie, plus loin sur la route de l'est ; la place reste libre.
		return FTransform(FRotator::ZeroRotator, Plaza + FVector(1100.0, 3100.0, 0.0));
	}

	FString ShopName(EShopKind Kind)
	{
		switch (Kind)
		{
		case EShopKind::SectorManager: return TEXT("Mairie");
		case EShopKind::Tailor:        return TEXT("Tailor");
		case EShopKind::Breeder:       return TEXT("Éleveur");
		case EShopKind::Workshop:      return TEXT("Atelier");
		default:                       return TEXT("General Store");
		}
	}

	FVector MerchantLocation(EShopKind Kind)
	{
		// Au fond de la cabane, derrière le comptoir (la porte est côté −X).
		return CabinTransform(Kind).TransformPosition(FVector(CabinHalfSize(Kind).X - 230.0, 0.0, 0.0));
	}

	FVector DoorstepLocation(EShopKind Kind)
	{
		return CabinTransform(Kind).TransformPosition(FVector(-CabinHalfSize(Kind).X - 190.0, 0.0, 0.0));
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
			const FVector2D Half = CabinHalfSize(Kind);
			if (FMath::Abs(Local.X) < Half.X && FMath::Abs(Local.Y) < Half.Y && Local.Z > CabinFloorTop - 20.f)
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
			const float HalfLength = CabinHalfSize(Kind).X;
			if (Local.X < -HalfLength + CabinWallThickness && Local.X > -HalfLength - 180.f && FMath::Abs(Local.Y) < 100.f)
			{
				return true;
			}
		}
		return false;
	}

	FTransform DoorHingeTransform(EShopKind Kind)
	{
		return FTransform(FVector(-CabinHalfSize(Kind).X + CabinWallThickness * 0.5f, -DoorHalfWidth, CabinFloorTop)) * CabinTransform(Kind);
	}

	FVector2D CabinHalfSize(EShopKind Kind)
	{
		switch (Kind)
		{
		case EShopKind::Breeder:  return FVector2D(560.f, 420.f);
		case EShopKind::Workshop: return FVector2D(620.f, 440.f);
		default:                  return FVector2D(CabinHalfLength, CabinHalfWidth);
		}
	}

	float CabinWallHeightFor(EShopKind Kind)
	{
		return Kind == EShopKind::Breeder || Kind == EShopKind::Workshop ? 340.f : CabinWallHeight;
	}

	bool HasAnnex(EShopKind Kind)
	{
		return Kind == EShopKind::Breeder || Kind == EShopKind::Workshop;
	}

	FVector2D AnnexCenter(EShopKind Kind)
	{
		// À droite de la maison (+Y de son repère), un peu en retrait de la façade.
		const FVector2D Half = CabinHalfSize(Kind);
		return Kind == EShopKind::Breeder ? FVector2D(120.f, Half.Y + 350.f + 620.f) : FVector2D(-60.f, Half.Y + 300.f + 700.f);
	}

	FVector2D AnnexHalfSize(EShopKind Kind)
	{
		return Kind == EShopKind::Breeder ? FVector2D(460.f, 620.f) : FVector2D(650.f, 700.f);
	}

	FTransform PlayerSpawnTransform()
	{
		// Sur le chemin du sud, à l'entrée de la place, regard vers le nord (+X).
		const FVector Location = ErewhonSector::Center(FIntPoint::ZeroValue) + FVector(-1350.f, 0.f, 100.f);
		return FTransform(FRotator::ZeroRotator, Location);
	}
}
