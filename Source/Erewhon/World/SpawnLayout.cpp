#include "World/SpawnLayout.h"

#include "Core/ErewhonSettings.h"
#include "World/SectorLayout.h"
#include "World/SectorTypes.h"
#include "World/VillageNetwork.h"

namespace ErewhonSpawn
{
	namespace
	{
		/** Repère d'une cabane dans celui de son village (origine au centre de la place). */
		FTransform CabinInVillage(EShopKind Kind)
		{
			if (Kind == EShopKind::SectorManager)
			{
				// Au nord de la route de l'est, à une vingtaine de mètres de la place.
				return FTransform(FRotator::ZeroRotator, FVector(1100.0, 2000.0, 0.0));
			}
			if (Kind == EShopKind::Tailor)
			{
				// En face, au sud de la route de l'ouest : la porte regarde vers le nord.
				return FTransform(FRotator(0.0, 180.0, 0.0), FVector(-1100.0, -2000.0, 0.0));
			}
			if (Kind == EShopKind::Breeder)
			{
				// À l'ouest de la route du nord, la porte vers la route (+Y) ; la grange plus au nord.
				return FTransform(FRotator(0.0, -90.0, 0.0), FVector(2100.0, -1550.0, 0.0));
			}
			if (Kind == EShopKind::Workshop)
			{
				// À l'est de la route du sud, la porte vers la route (−Y) ; la cour plus au sud.
				return FTransform(FRotator(0.0, 90.0, 0.0), FVector(-2100.0, 1600.0, 0.0));
			}
			// Le General Store : à côté de la mairie, plus loin sur la route de l'est ; la place reste libre.
			return FTransform(FRotator::ZeroRotator, FVector(1100.0, 3100.0, 0.0));
		}
	}

	FIntPoint VillageAt(const FVector& World)
	{
		return ErewhonVillages::NearestVillage(ErewhonSector::FromWorld(World));
	}

	FBox2D VillageBox(const FIntPoint& Village, const FBox2D& Local)
	{
		const FTransform Frame = ErewhonVillages::VillageFrame(Village);
		const FVector2D A(Frame.TransformPosition(FVector(Local.Min, 0.0)));
		const FVector2D B(Frame.TransformPosition(FVector(Local.Max, 0.0)));
		return FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
	}

	FTransform CabinTransform(const FIntPoint& Village, EShopKind Kind)
	{
		return CabinInVillage(Kind) * ErewhonVillages::VillageFrame(Village);
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

	FVector MerchantLocation(const FIntPoint& Village, EShopKind Kind)
	{
		// Au fond de la cabane, derrière le comptoir (la porte est côté −X).
		return CabinTransform(Village, Kind).TransformPosition(FVector(CabinHalfSize(Kind).X - 230.0, 0.0, 0.0));
	}

	FVector DoorstepLocation(const FIntPoint& Village, EShopKind Kind)
	{
		return CabinTransform(Village, Kind).TransformPosition(FVector(-CabinHalfSize(Kind).X - 190.0, 0.0, 0.0));
	}

	bool RoadCenterline(const FIntPoint& Sector, TArray<FVector2D>& OutPoints)
	{
		OutPoints.Reset();
		FVector2D Controls[4];
		if (!ErewhonVillages::RoadCurve(Sector, Controls))
		{
			return false;
		}
		// Vingt pas de cinq mètres environ : la flèche entre deux points reste de quelques centimètres.
		constexpr int32 Steps = 20;
		const double Size = UErewhonSettings::Get().SectorSize();
		const FVector2D Origin(ErewhonSector::Origin(Sector));
		OutPoints.Reserve(Steps + 1);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			OutPoints.Add(Origin + ErewhonVillages::RoadCurvePoint(Controls, static_cast<double>(Step) / Steps) * Size);
		}
		return true;
	}

	FVector2D ClosestOnLine(const TArray<FVector2D>& Points, const FVector2D& World, FVector2D* OutTangent)
	{
		FVector2D Best = Points.IsEmpty() ? World : Points[0];
		FVector2D Tangent(1.0, 0.0);
		double BestDistance = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector2D Start = Points[Index - 1];
			const FVector2D Span = Points[Index] - Start;
			const double T = FMath::Clamp(FVector2D::DotProduct(World - Start, Span) / FMath::Max(Span.SizeSquared(), UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
			const FVector2D Point = Start + Span * T;
			const double Distance = FVector2D::DistSquared(World, Point);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Point;
				Tangent = Span.GetSafeNormal();
			}
		}
		if (OutTangent)
		{
			*OutTangent = Tangent;
		}
		return Best;
	}

	void BuildPaths(const FIntPoint& Sector, ESectorType Type, TArray<FBox2D>& OutPaths, TArray<FPathCurve>& OutCurves)
	{
		const float Size = UErewhonSettings::Get().SectorSize();
		const FVector2D Origin(ErewhonSector::Origin(Sector));
		const FVector2D Center(ErewhonSector::Center(Sector));
		const float HalfPath = PathWidth * 0.5f;

		// Sur une route, le chemin suit son tracé : il serpente d'un côté à l'autre, droit près des places.
		if (Type == ESectorType::Road)
		{
			TArray<FVector2D> Line;
			if (RoadCenterline(Sector, Line))
			{
				OutCurves.Emplace(MoveTemp(Line), HalfPath);
				return;
			}
		}
		// Sur une place, la croix vers ses quatre sorties (un quart de tour la laisse telle quelle) ;
		// sinon, un bras du centre vers chaque côté où la route continue : tout droit, ou en coude
		// là où elle oblique.
		const uint8 Exits = Type == ESectorType::Spawn ? 0xF : Type == ESectorType::Road ? ErewhonVillages::RoadSides(Sector) : 0;
		const bool bNorth = (Exits & 1) != 0, bSouth = (Exits & 2) != 0, bEast = (Exits & 4) != 0, bWest = (Exits & 8) != 0;
		if (bNorth || bSouth)
		{
			// D'un bord à l'autre, ou du centre (une demi-largeur au-delà, pour fermer le coude) à un bord.
			OutPaths.Add(FBox2D(FVector2D(bSouth ? Origin.X : Center.X - HalfPath, Center.Y - HalfPath),
				FVector2D(bNorth ? Origin.X + Size : Center.X + HalfPath, Center.Y + HalfPath)));
		}
		if (bEast || bWest)
		{
			OutPaths.Add(FBox2D(FVector2D(Center.X - HalfPath, bWest ? Origin.Y : Center.Y - HalfPath),
				FVector2D(Center.X + HalfPath, bEast ? Origin.Y + Size : Center.Y + HalfPath)));
		}
	}

	bool IsInsideCabin(const FVector& World)
	{
		// Dans le repère du village le plus proche, puis dans celui de chaque cabane.
		const FVector InVillage = ErewhonVillages::VillageFrame(VillageAt(World)).InverseTransformPosition(World);
		for (const EShopKind Kind : ShopKinds)
		{
			const FVector Local = CabinInVillage(Kind).InverseTransformPosition(InVillage);
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
		const FVector InVillage = ErewhonVillages::VillageFrame(VillageAt(World)).InverseTransformPosition(World);
		for (const EShopKind Kind : ShopKinds)
		{
			const FVector Local = CabinInVillage(Kind).InverseTransformPosition(InVillage);
			const float HalfLength = CabinHalfSize(Kind).X;
			if (Local.X < -HalfLength + CabinWallThickness && Local.X > -HalfLength - 180.f && FMath::Abs(Local.Y) < 100.f)
			{
				return true;
			}
		}
		return false;
	}

	FTransform DoorHingeTransform(const FIntPoint& Village, EShopKind Kind)
	{
		return FTransform(FVector(-CabinHalfSize(Kind).X + CabinWallThickness * 0.5f, -DoorHalfWidth, CabinFloorTop)) * CabinTransform(Village, Kind);
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
		// Au village de départ, sur le chemin du sud, à l'entrée de la place, regard vers le nord (+X de son repère).
		return FTransform(FRotator::ZeroRotator, FVector(-1350.f, 0.f, 100.f)) * ErewhonVillages::VillageFrame(ErewhonVillages::StartVillage);
	}
}
