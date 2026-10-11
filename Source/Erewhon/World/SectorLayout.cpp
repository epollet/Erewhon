#include "World/SectorLayout.h"

#include "Core/ErewhonSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Items/ItemDatabase.h"
#include "World/SpawnLayout.h"
#include "World/Terrain.h"

namespace
{
	constexpr float GCellSize = 500.f;          // 5 m
	constexpr int32 GMaxCachedLayouts = 64;
	constexpr int32 GPlacementRetries = 4;

	/** Distance signée au rectangle (cm) : négative à l'intérieur. */
	float SignedDistance(const FBox2D& Box, const FVector2D& Point)
	{
		const float DX = FMath::Max(Box.Min.X - Point.X, Point.X - Box.Max.X);
		const float DY = FMath::Max(Box.Min.Y - Point.Y, Point.Y - Box.Max.Y);
		const float Outside = FVector2D(FMath::Max(DX, 0.f), FMath::Max(DY, 0.f)).Size();
		return Outside + FMath::Min(FMath::Max(DX, DY), 0.f);
	}
}

float FSectorLayout::DirtAmount(const FVector2D& World) const
{
	if (Paths.IsEmpty() && PlazaRadius <= 0.f)
	{
		return 0.f;
	}
	const float Blend = UErewhonSettings::Get().PathBlendMeters * 100.f;
	// Deux octaves de bruit : de larges ondulations du bord, et un liseré plus fin.
	const float Wobble = FMath::PerlinNoise2D(World / 260.f) * Blend * 0.9f + FMath::PerlinNoise2D(World / 55.f) * Blend * 0.3f;
	float Distance = TNumericLimits<float>::Max();
	for (const FBox2D& Path : Paths)
	{
		Distance = FMath::Min(Distance, SignedDistance(Path, World));
	}
	if (PlazaRadius > 0.f)
	{
		// La place n'est pas un cercle parfait : son bord ondule largement.
		const FVector2D Offset = World - PlazaCenter;
		const float Angle = FMath::Atan2(Offset.Y, Offset.X);
		const float Bulge = FMath::PerlinNoise1D(Angle * 1.7f + 11.3f) * PlazaRadius * 0.18f;
		Distance = FMath::Min(Distance, Offset.Size() - (PlazaRadius + Bulge));
	}
	return 1.f - FMath::SmoothStep(-Blend * 0.5f, Blend * 0.5f, Distance + Wobble);
}

bool FSectorLayout::IsBare(const FVector2D& World, float DirtThreshold) const
{
	return DirtAmount(World) > DirtThreshold;
}

bool FSectorLayout::IsInBush(const FVector2D& World) const
{
	return Bushes.ContainsByPredicate([&World](const FBushInstance& Bush)
	{
		return FVector2D::DistSquared(FVector2D(Bush.Location), World) < FMath::Square(Bush.Radius);
	});
}

float FSectorLayout::SpeedFactor(const FVector2D& World) const
{
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	if (DirtAmount(World) > 0.4f)
	{
		return 1.f;   // chemins, place, cabane
	}
	return IsInBush(World) ? Settings.BushSpeedFactor : Settings.GrassSpeedFactor;
}

void FSectorLayout::GatherNear(const FVector& Point, float Radius, TArray<int32>& Out) const
{
	if (CellsPerSide == 0)
	{
		return;
	}
	const FVector Local = Point - ErewhonSector::Origin(Coord);
	const int32 MinX = FMath::Clamp(FMath::FloorToInt32((Local.X - Radius) / CellSize), 0, CellsPerSide - 1);
	const int32 MaxX = FMath::Clamp(FMath::FloorToInt32((Local.X + Radius) / CellSize), 0, CellsPerSide - 1);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt32((Local.Y - Radius) / CellSize), 0, CellsPerSide - 1);
	const int32 MaxY = FMath::Clamp(FMath::FloorToInt32((Local.Y + Radius) / CellSize), 0, CellsPerSide - 1);
	const float RadiusSq = Radius * Radius;

	for (int32 CY = MinY; CY <= MaxY; ++CY)
	{
		for (int32 CX = MinX; CX <= MaxX; ++CX)
		{
			for (const int32 Index : Cells[CY * CellsPerSide + CX])
			{
				if (FVector::DistSquared2D(Plants[Index].Location, Point) <= RadiusSq)
				{
					Out.Add(Index);
				}
			}
		}
	}
}

TSharedRef<const FSectorLayout> USectorLayoutSubsystem::GetLayout(const FIntPoint& Sector, int32 WorldSeed)
{
	if (WorldSeed != CachedSeed)
	{
		Cache.Reset();
		CacheOrder.Reset();
		CachedSeed = WorldSeed;
	}
	if (const TSharedRef<FSectorLayout>* Found = Cache.Find(Sector))
	{
		return *Found;
	}

	TSharedRef<FSectorLayout> Layout = Build(Sector, WorldSeed);
	Cache.Add(Sector, Layout);
	CacheOrder.Add(Sector);
	if (CacheOrder.Num() > GMaxCachedLayouts)
	{
		Cache.Remove(CacheOrder[0]);
		CacheOrder.RemoveAt(0);
	}
	return Layout;
}

void USectorLayoutSubsystem::BuildBushes(FSectorLayout& Layout, int32 WorldSeed) const
{
	// Massifs de buissons : un centre tiré au hasard, des exemplaires resserrés autour.
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	const float Size = Settings.SectorSize();
	const FVector2D Origin(ErewhonSector::Origin(Layout.Coord));
	for (int32 Kind = 0; Kind < Settings.DecorPlants.Num(); ++Kind)
	{
		const FDecorPlant& Plant = Settings.DecorPlants[Kind];
		const int32 Count = Layout.Type == ESectorType::Spawn ? Plant.CountSpawn : Plant.CountPrairie;
		if (!Plant.bSolid || Count <= 0)
		{
			continue;
		}
		FRandomStream Rand(static_cast<int32>(HashCombine(HashCombine(GetTypeHash(WorldSeed), GetTypeHash(Layout.Coord)), GetTypeHash(Kind) * 0x85EBCA6Bu)));
		const float PatchRadius = Plant.PatchRadius * 100.f;
		const int32 PatchSize = FMath::Max(1, Plant.PatchSize);
		FVector2D PatchCenter = FVector2D::ZeroVector;
		for (int32 i = 0; i < Count; ++i)
		{
			if (i % PatchSize == 0)
			{
				PatchCenter = Origin + FVector2D(Rand.FRand() * Size, Rand.FRand() * Size);
			}
			const float Distance = PatchRadius * FMath::Sqrt(Rand.FRand());
			const float Angle = Rand.FRandRange(0.f, UE_TWO_PI);
			const FVector2D Point = PatchCenter + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
			const float Height = Rand.FRandRange(Plant.HeightMin, Plant.HeightMax);
			const float Yaw = Rand.FRandRange(0.f, 360.f);
			if (Point.X < Origin.X || Point.Y < Origin.Y || Point.X >= Origin.X + Size || Point.Y >= Origin.Y + Size || Layout.IsBare(Point))
			{
				continue;
			}
			FBushInstance& Bush = Layout.Bushes.AddDefaulted_GetRef();
			Bush.Location = FVector(Point.X, Point.Y, ErewhonTerrain::Height(Point, WorldSeed));
			Bush.Height = Height;
			Bush.Radius = Height * 100.f * Plant.FootprintRatio;
			Bush.Yaw = Yaw;
			Bush.Kind = Kind;
		}
	}
}

TSharedRef<FSectorLayout> USectorLayoutSubsystem::Build(const FIntPoint& Sector, int32 WorldSeed) const
{
	TSharedRef<FSectorLayout> Layout = MakeShared<FSectorLayout>();
	Layout->Coord = Sector;
	Layout->WorldSeed = WorldSeed;
	Layout->Type = ErewhonSector::TypeAt(Sector, WorldSeed);

	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector Origin = ErewhonSector::Origin(Sector);

	if (Layout->Type == ESectorType::Spawn)
	{
		ErewhonSpawn::BuildPaths(Sector, Layout->Paths);
		Layout->PlazaCenter = FVector2D(ErewhonSector::Center(Sector));
		Layout->PlazaRadius = UErewhonSettings::Get().PlazaRadiusMeters * 100.f;
	}

	Layout->CellSize = GCellSize;
	Layout->CellsPerSide = FMath::CeilToInt32(Size / GCellSize);
	Layout->Cells.SetNum(Layout->CellsPerSide * Layout->CellsPerSide);

	const UItemDatabase* Items = UItemDatabase::Get(GetWorld());
	if (!Items)
	{
		return Layout;
	}

	for (const FItemDef* Item : Items->GetPlants())
	{
		const int32 Count = Item->Plant.CountFor(Layout->Type);
		if (Count <= 0)
		{
			continue;
		}

		// Un flux par (seed, secteur, plante) : ajouter une espèce ne déplace pas les autres.
		uint32 Hash = HashCombine(GetTypeHash(WorldSeed), GetTypeHash(Sector));
		Hash = HashCombine(Hash, GetTypeHash(Item->Plant.SeedSalt));
		FRandomStream Rand(static_cast<int32>(Hash));

		Layout->Plants.Reserve(Layout->Plants.Num() + Count);
		for (int32 i = 0; i < Count; ++i)
		{
			FVector2D Point = FVector2D::ZeroVector;
			bool bPlaced = false;
			for (int32 Attempt = 0; Attempt < GPlacementRetries && !bPlaced; ++Attempt)
			{
				Point = FVector2D(Origin.X + Rand.FRand() * Size, Origin.Y + Rand.FRand() * Size);
				bPlaced = !Layout->IsBare(Point);
			}
			// Tirages consommés même en cas d'échec : la suite reste stable.
			const float Height = Rand.FRandRange(Item->Plant.HeightMin, Item->Plant.HeightMax);
			const float Yaw = Rand.FRandRange(0.f, 360.f);
			const int32 Variant = Rand.RandHelper(FMath::Max(1, Item->MeshVariants.Num()));
			if (!bPlaced)
			{
				continue;
			}

			FPlantInstance& Plant = Layout->Plants.AddDefaulted_GetRef();
			Plant.ItemId = Item->Id;
			Plant.Location = FVector(Point.X, Point.Y, ErewhonTerrain::Height(Point, WorldSeed));
			Plant.Height = Height;
			Plant.Yaw = Yaw;
			Plant.Variant = static_cast<uint8>(Variant);
		}
	}

	BuildBushes(*Layout, WorldSeed);

	for (int32 Index = 0; Index < Layout->Plants.Num(); ++Index)
	{
		const FVector Local = Layout->Plants[Index].Location - Origin;
		const int32 CX = FMath::Clamp(FMath::FloorToInt32(Local.X / GCellSize), 0, Layout->CellsPerSide - 1);
		const int32 CY = FMath::Clamp(FMath::FloorToInt32(Local.Y / GCellSize), 0, Layout->CellsPerSide - 1);
		Layout->Cells[CY * Layout->CellsPerSide + CX].Add(Index);
	}
	return Layout;
}

FPlantTarget USectorLayoutSubsystem::FindTarget(const FVector& Eye, const FVector& Look, float Reach, float ConeDegrees,
	int32 WorldSeed, TFunctionRef<bool(const FIntPoint& Sector, int32 PlantIndex)> Filter)
{
	FPlantTarget Best;
	float BestScore = TNumericLimits<float>::Max();

	const bool bCone = ConeDegrees > 0.f;
	const FVector Flat = FVector(Look.X, Look.Y, 0.0).GetSafeNormal();
	const float CosHalfCone = FMath::Cos(FMath::DegreesToRadians(ConeDegrees * 0.5f));

	const FIntPoint EyeSector = ErewhonSector::FromWorld(Eye);
	TArray<int32> Candidates;
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			const FIntPoint Sector = EyeSector + FIntPoint(DX, DY);
			const TSharedRef<const FSectorLayout> Layout = GetLayout(Sector, WorldSeed);

			Candidates.Reset();
			Layout->GatherNear(Eye, Reach, Candidates);
			for (const int32 Index : Candidates)
			{
				if (!Filter(Sector, Index))
				{
					continue;
				}
				const FPlantInstance& Plant = Layout->Plants[Index];
				const FVector Point = Plant.Location + FVector(0.0, 0.0, Plant.Height * 50.0);   // mi-hauteur
				const FVector ToPlant = Point - Eye;

				float Score;
				if (bCone)
				{
					const FVector ToFlat = FVector(ToPlant.X, ToPlant.Y, 0.0);
					const float Dist = ToFlat.Size();
					if (Dist < KINDA_SMALL_NUMBER || FVector::DotProduct(ToFlat / Dist, Flat) < CosHalfCone)
					{
						continue;
					}
					Score = Dist;
				}
				else
				{
					const float Along = FVector::DotProduct(ToPlant, Look);
					if (Along <= 0.f || Along > Reach)
					{
						continue;
					}
					const float Perp = (ToPlant - Along * Look).Size();
					// Tolérance qui s'élargit avec la distance : les petites fleurs restent visables.
					const float Tolerance = 20.f + Along * 0.04f;
					if (Perp > Tolerance)
					{
						continue;
					}
					Score = Perp / Tolerance + Along / Reach * 0.25f;
				}

				if (Score < BestScore)
				{
					BestScore = Score;
					Best.Sector = Sector;
					Best.PlantIndex = Index;
					Best.ItemId = Plant.ItemId;
					Best.Location = Plant.Location;
				}
			}
		}
	}
	return Best;
}
