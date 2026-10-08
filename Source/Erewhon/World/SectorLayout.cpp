#include "World/SectorLayout.h"

#include "Core/ErewhonSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Items/ItemDatabase.h"
#include "World/SpawnLayout.h"

namespace
{
	constexpr float GCellSize = 500.f;          // 5 m
	constexpr int32 GMaxCachedLayouts = 64;
	constexpr int32 GPlacementRetries = 4;

	bool IsExcluded(const TArray<FBox2D>& Exclusions, const FVector2D& Point)
	{
		for (const FBox2D& Box : Exclusions)
		{
			if (Box.IsInside(Point))
			{
				return true;
			}
		}
		return false;
	}
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

TSharedRef<FSectorLayout> USectorLayoutSubsystem::Build(const FIntPoint& Sector, int32 WorldSeed) const
{
	TSharedRef<FSectorLayout> Layout = MakeShared<FSectorLayout>();
	Layout->Coord = Sector;
	Layout->Type = ErewhonSector::TypeAt(Sector, WorldSeed);

	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector Origin = ErewhonSector::Origin(Sector);

	if (Layout->Type == ESectorType::Spawn)
	{
		ErewhonSpawn::BuildExclusions(Sector, Layout->Paths, Layout->Exclusions);
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
				bPlaced = !IsExcluded(Layout->Exclusions, Point);
			}
			// Tirages consommés même en cas d'échec : la suite reste stable.
			const float Height = Rand.FRandRange(Item->Plant.HeightMin, Item->Plant.HeightMax);
			const float Yaw = Rand.FRandRange(0.f, 360.f);
			if (!bPlaced)
			{
				continue;
			}

			FPlantInstance& Plant = Layout->Plants.AddDefaulted_GetRef();
			Plant.ItemId = Item->Id;
			Plant.Location = FVector(Point.X, Point.Y, 0.0);
			Plant.Height = Height;
			Plant.Yaw = Yaw;
		}
	}

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
