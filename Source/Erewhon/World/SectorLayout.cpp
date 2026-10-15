#include "World/SectorLayout.h"

#include "World/Villagers.h"
#include "Core/ErewhonSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Items/ItemDatabase.h"
#include "World/SpawnLayout.h"
#include "Core/ErewhonGameState.h"
#include "World/Terrain.h"
#include "World/Village.h"
#include "World/Apothecary.h"
#include "World/Farm.h"

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

namespace
{
	/** Distance signée (cm) au plus proche chemin ou place, pavés seulement si demandé ; bord ondulé par le bruit. */
	float DistanceToShapes(const FSectorLayout& Layout, const FVector2D& World, bool bPavedOnly)
	{
		float Distance = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Layout.Paths.Num(); ++Index)
		{
			if (!bPavedOnly || (Layout.PathPaved.IsValidIndex(Index) && Layout.PathPaved[Index]))
			{
				Distance = FMath::Min(Distance, SignedDistance(Layout.Paths[Index], World));
			}
		}
		for (int32 Index = 0; Index < Layout.Clearings.Num(); ++Index)
		{
			// Une place n'est pas un cercle parfait : son bord ondule largement, différemment pour chacune.
			const FClearing& Clearing = Layout.Clearings[Index];
			if (bPavedOnly && !Clearing.bPaved)
			{
				continue;
			}
			const FVector2D Offset = World - Clearing.Center;
			const float Angle = FMath::Atan2(Offset.Y, Offset.X);
			const float Bulge = FMath::PerlinNoise1D(Angle * 1.7f + 11.3f + Index * 5.1f) * Clearing.Radius * 0.18f;
			Distance = FMath::Min(Distance, Offset.Size() - (Clearing.Radius + Bulge));
		}
		return Distance;
	}

	float BlendAmount(float Distance, const FVector2D& World)
	{
		const float Blend = UErewhonSettings::Get().PathBlendMeters * 100.f;
		// Deux octaves de bruit : de larges ondulations du bord, et un liseré plus fin.
		const float Wobble = FMath::PerlinNoise2D(World / 260.f) * Blend * 0.9f + FMath::PerlinNoise2D(World / 55.f) * Blend * 0.3f;
		return 1.f - FMath::SmoothStep(-Blend * 0.5f, Blend * 0.5f, Distance + Wobble);
	}
}

float FSectorLayout::DirtAmount(const FVector2D& World) const
{
	if (Paths.IsEmpty() && Clearings.IsEmpty())
	{
		return 0.f;
	}
	return BlendAmount(DistanceToShapes(*this, World, false), World);
}

float FSectorLayout::PavedAmount(const FVector2D& World) const
{
	if (!PathPaved.Contains(true) && !Clearings.ContainsByPredicate([](const FClearing& C) { return C.bPaved; }))
	{
		return 0.f;
	}
	// Les pavés s'arrêtent un peu avant le bord de la terre : un liseré de terre les borde.
	return BlendAmount(DistanceToShapes(*this, World, true) + 25.f, World);
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
	if (PavedAmount(World) > 0.4f)
	{
		return Settings.PavedSpeedFactor;   // pavés : on y marche plus vite
	}
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

TSharedRef<const FSectorLayout> USectorLayoutSubsystem::GetFarLayout(const FIntPoint& Sector, int32 WorldSeed)
{
	if (WorldSeed != CachedSeed)
	{
		InvalidateAll();
		CachedSeed = WorldSeed;
	}
	// Le plan complet, s'il est déjà là, fait l'affaire.
	if (const TSharedRef<FSectorLayout>* Full = Cache.Find(Sector))
	{
		return *Full;
	}
	if (const TSharedRef<FSectorLayout>* Found = FarCache.Find(Sector))
	{
		return *Found;
	}
	TSharedRef<FSectorLayout> Layout = Build(Sector, WorldSeed, false);
	FarCache.Add(Sector, Layout);
	FarCacheOrder.Add(Sector);
	if (FarCacheOrder.Num() > 400)
	{
		FarCache.Remove(FarCacheOrder[0]);
		FarCacheOrder.RemoveAt(0);
	}
	return Layout;
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
		// Plus d'arbustes en lisière, un peu plus en forêt qu'en prairie.
		const float Woods = Layout.Type == ESectorType::Edge ? 2.2f : Layout.Type == ESectorType::Forest ? 1.4f : 1.f;
		const int32 Count = Layout.Type == ESectorType::Spawn ? Plant.CountSpawn : FMath::RoundToInt32(Plant.CountPrairie * Woods);
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

void USectorLayoutSubsystem::BuildTrees(FSectorLayout& Layout, int32 WorldSeed) const
{
	// Quelques arbres en lisière, beaucoup en forêt ; jamais sur un chemin, une clairière, ni trop serrés.
	if (Layout.Type != ESectorType::Edge && Layout.Type != ESectorType::Forest)
	{
		return;
	}
	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector2D Origin(ErewhonSector::Origin(Layout.Coord));
	FRandomStream Rand(static_cast<int32>(HashCombine(HashCombine(GetTypeHash(WorldSeed), GetTypeHash(Layout.Coord)), 0x7EE5u)));
	const bool bForest = Layout.Type == ESectorType::Forest;
	const int32 Wanted = bForest ? Rand.RandRange(85, 110) : Rand.RandRange(10, 16);
	const float MinSpacing = bForest ? 560.f : 1100.f;
	for (int32 Attempt = 0; Attempt < Wanted * 6 && Layout.Trees.Num() < Wanted; ++Attempt)
	{
		const FVector2D Point = Origin + FVector2D(Rand.FRandRange(150.f, Size - 150.f), Rand.FRandRange(150.f, Size - 150.f));
		// En forêt, de vieux arbres : jusqu'à 1,75 fois plus grands, à la couronne plus large, qui se rejoignent au-dessus de soi.
		const float Grow = bForest ? Rand.FRandRange(1.f, 1.75f) : 1.f;
		const float Height = Rand.FRandRange(900.f, 1350.f) * Grow;
		const float Girth = bForest ? Rand.FRandRange(1.1f, 1.3f) : 1.f;
		const float Yaw = Rand.FRandRange(0.f, 360.f);
		// La forêt : surtout des pins et des hêtres ; la lisière : des feuillus surtout.
		const float Pick = Rand.FRand();
		const int32 Model = bForest ? (Pick < 0.45f ? 0 : Pick < 0.85f ? 1 : 2) : (Pick < 0.2f ? 0 : Pick < 0.45f ? 1 : Pick < 0.75f ? 2 : 3);
		if (Layout.DirtAmount(Point) > 0.f || Layout.Trees.ContainsByPredicate([&](const FTreeInstance& T) { return FVector2D::Distance(FVector2D(T.Location), Point) < MinSpacing; }))
		{
			continue;
		}
		Layout.Trees.Add({ FVector(Point, ErewhonTerrain::Height(Point, WorldSeed)), Height, Yaw, Model, Girth });
	}
}

void USectorLayoutSubsystem::InvalidateAround(const FIntPoint& Sector)
{
	for (const FIntPoint& Offset : { FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
	{
		Cache.Remove(Sector + Offset);
		CacheOrder.Remove(Sector + Offset);
		FarCache.Remove(Sector + Offset);
		FarCacheOrder.Remove(Sector + Offset);
	}
}

void USectorLayoutSubsystem::BuildAccessPaths(FSectorLayout& Layout) const
{
	// Amélioration « chemin d'accès » : un chemin plus étroit part de la route et rejoint
	// le bord du secteur privé voisin.
	const AErewhonGameState* State = AErewhonGameState::Get(GetWorld());
	if (!State)
	{
		return;
	}
	const float Size = UErewhonSettings::Get().SectorSize();
	const float Half = ErewhonSpawn::AccessPathWidth * 0.5f;
	const FVector2D Center(ErewhonSector::Center(Layout.Coord));
	const auto Corridor = [&](const FIntPoint& Toward, bool bPaved)
	{
		Layout.PathPaved.Add(bPaved);
		// Du centre du secteur jusqu'à son bord, dans la direction du voisin.
		const FVector2D Rim = Center + FVector2D(Toward) * (Size * 0.5f);
		const FVector2D Min(FMath::Min(Center.X, Rim.X), FMath::Min(Center.Y, Rim.Y));
		const FVector2D Max(FMath::Max(Center.X, Rim.X), FMath::Max(Center.Y, Rim.Y));
		Layout.Paths.Add(FBox2D(Min - FVector2D(Toward.Y != 0 ? Half : 0.f, Toward.X != 0 ? Half : 0.f),
			Max + FVector2D(Toward.Y != 0 ? Half : 0.f, Toward.X != 0 ? Half : 0.f)));
	};
	// L'apothicairerie : une clairière autour de la maison, et un chemin jusqu'à elle depuis
	// chaque route reliée (il prolonge celui de la route, jusqu'au centre du secteur).
	if (ErewhonSector::IsPurchasable(Layout.Coord) && ErewhonApothecary::HasHouse(State, Layout.Coord))
	{
		Layout.Clearings.Add({ Center, ErewhonApothecary::ClearingRadius, false });
		const uint8 Links = State->GetRoadLinks(Layout.Coord);
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (Links & (1 << Side))
			{
				Corridor(ErewhonSector::Sides[Side], State->IsVillagePaved() && (State->GetPavedLinks(Layout.Coord) & (1 << Side)));
			}
		}
		return;
	}
	// Les champs du fermier : de la terre, et la croix de chemins qui les sépare, du centre jusqu'aux
	// bords (là où arrivent les chemins d'accès depuis la route).
	if (ErewhonSector::IsPurchasable(Layout.Coord))
	{
		const uint8 Buildings = State->GetBuildings(Layout.Coord);
		if (Buildings & ErewhonFarm::AnyField)
		{
			ErewhonFarm::FieldBoxes(Layout.Coord, Buildings, Layout.Fields);
			for (const FBox2D& Field : Layout.Fields)
			{
				Layout.Paths.Add(Field);
				Layout.PathPaved.Add(false);
			}
			const uint8 Arms = ErewhonFarm::CrossArms(Buildings);
			Layout.bFenced = (Buildings & ErewhonFarm::FenceBit) != 0;   // la clôture s'achète à part
			Layout.FenceGates = Arms | State->GetRoadLinks(Layout.Coord);
			for (int32 Side = 0; Side < 4; ++Side)
			{
				if (Arms & (1 << Side))
				{
					Corridor(ErewhonSector::Sides[Side], false);
				}
			}
		}
		return;
	}
	if (Layout.Type != ESectorType::Road)
	{
		return;
	}
	// Seule la route porte le chemin : il part de la route et s'arrête à la limite du secteur
	// privé, quand son propriétaire a acheté cette liaison au Sector Manager.
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FIntPoint Neighbor = Layout.Coord + ErewhonSector::Sides[Side];
		const int32 Back = Side ^ 1;   // côté du voisin qui regarde vers nous (nord ↔ sud, est ↔ ouest)
		if (!ErewhonSector::IsPurchasable(Neighbor) || !(State->GetRoadLinks(Neighbor) & (1 << Back)))
		{
			continue;
		}
		// Pavé si le propriétaire a acheté cette amélioration (une fois le village pavé).
		Corridor(ErewhonSector::Sides[Side], State->IsVillagePaved() && (State->GetPavedLinks(Neighbor) & (1 << Back)));
		// Un panneau au coin du carrefour, côté place du village ; une lanterne au coin d'en face.
		const FVector2D Road = FVector2D(-FMath::Sign(Layout.Coord.X), -FMath::Sign(Layout.Coord.Y));
		const FVector2D Out = FVector2D(ErewhonSector::Sides[Side]) * (ErewhonSpawn::PathWidth * 0.5f + 60.f);
		const FVector2D Foot = Center + Out + Road * (Half + 60.f);
		const FVector2D Across = Center + Out - Road * (Half + 60.f);
		Layout.Signposts.Add({ FVector(Foot, ErewhonTerrain::Height(Foot, Layout.WorldSeed)), Road,
			FVector(Across, ErewhonTerrain::Height(Across, Layout.WorldSeed)) });
	}
}

TSharedRef<FSectorLayout> USectorLayoutSubsystem::Build(const FIntPoint& Sector, int32 WorldSeed, bool bPlants) const
{
	TSharedRef<FSectorLayout> Layout = MakeShared<FSectorLayout>();
	Layout->Coord = Sector;
	Layout->WorldSeed = WorldSeed;
	Layout->Type = ErewhonSector::TypeAt(Sector, WorldSeed);

	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector Origin = ErewhonSector::Origin(Sector);

	ErewhonSpawn::BuildPaths(Sector, Layout->Type, Layout->Paths);
	// Pavés : la place et ses chemins (centre du village), puis les routes du village.
	const AErewhonGameState* State = AErewhonGameState::Get(GetWorld());
	const bool bPavedCenter = State && State->HasVillageUpgrade(EVillageUpgrade::PavedCenter);
	const bool bPavedRoad = Layout->Type == ESectorType::Road && State && State->HasVillageUpgrade(EVillageUpgrade::PavedVillage)
		&& ErewhonVillage::IsInVillage(Sector);
	Layout->PathPaved.Init(Layout->Type == ESectorType::Spawn ? bPavedCenter : bPavedRoad, Layout->Paths.Num());
	if (Layout->Type == ESectorType::Spawn)
	{
		// La place ronde du General Store, puis la clairière de chaque cabane du bord des routes
		// (celle du tailleur une fois sa boutique construite).
		Layout->Clearings.Add({ FVector2D(ErewhonSector::Center(Sector)), UErewhonSettings::Get().PlazaRadiusMeters * 100.f, bPavedCenter });
		for (const EShopKind Kind : { EShopKind::SectorManager, EShopKind::Tailor })
		{
			if (Kind == EShopKind::Tailor && !(State && State->HasVillageUpgrade(EVillageUpgrade::Tailor)))
			{
				continue;
			}
			Layout->Clearings.Add({ FVector2D(ErewhonSpawn::CabinTransform(Kind).GetLocation()), ErewhonSpawn::ManagerClearingRadius, bPavedCenter });
		}
		// L'éleveur et l'atelier : de la terre autour de la maison et de son annexe, et jusqu'à la route.
		for (const EShopKind Kind : { EShopKind::Breeder, EShopKind::Workshop })
		{
			const FTransform House = ErewhonSpawn::CabinTransform(Kind);
			const FVector2D Half = ErewhonSpawn::CabinHalfSize(Kind);
			Layout->Clearings.Add({ FVector2D(House.GetLocation()), static_cast<float>(Half.X + 350.0), bPavedCenter });
			const FVector2D AnnexAt(House.TransformPosition(FVector(ErewhonSpawn::AnnexCenter(Kind), 0.f)));
			const FVector2D AnnexHalf = ErewhonSpawn::AnnexHalfSize(Kind);
			Layout->Clearings.Add({ AnnexAt, static_cast<float>(FMath::Max(AnnexHalf.X, AnnexHalf.Y) + 250.0), false });
			// Du perron à la route, et de la façade de l'annexe à la route.
			for (const FVector& LocalFrom : { FVector(-Half.X - 100.f, 0.f, 0.f), FVector(ErewhonSpawn::AnnexCenter(Kind).X - AnnexHalf.X - 100.f, ErewhonSpawn::AnnexCenter(Kind).Y, 0.f) })
			{
				const FVector2D From(House.TransformPosition(LocalFrom));
				const FVector2D Road(ErewhonSector::Center(Sector).X, ErewhonSector::Center(Sector).Y);
				// La route du nord ou du sud court le long de X, à Y = centre.
				const FVector2D To(From.X, Road.Y);
				Layout->Paths.Add(FBox2D(FVector2D(From.X - 160.f, FMath::Min(From.Y, To.Y)), FVector2D(From.X + 160.f, FMath::Max(From.Y, To.Y))));
				Layout->PathPaved.Add(false);
			}
		}
		// La fontaine : un peu de terre autour, et un chemin jusqu'à l'allée.
		{
			const FTransform Fountain = ErewhonVillage::FountainTransform();
			const FVector2D Foot(Fountain.GetLocation());
			Layout->Clearings.Add({ Foot + FVector2D(Fountain.GetRotation().GetForwardVector()) * 40.f, 300.f, false });
			const FVector2D Spot(ErewhonVillage::FountainBarrelSpot());
			const float AlleyEdge = ErewhonSector::Center(Sector).X - ErewhonSpawn::PathWidth * 0.5f;
			Layout->Paths.Add(FBox2D(FVector2D(FMath::Min(Spot.X, AlleyEdge), Spot.Y - 110.f), FVector2D(FMath::Max(Spot.X, AlleyEdge) + 60.f, Spot.Y + 110.f)));
			Layout->PathPaved.Add(false);
		}
		if (State && State->HasVillageUpgrade(EVillageUpgrade::Decoration))
		{
			// Sous chaque banc, la terre battue, et un petit sentier qui y mène depuis la place.
			const FVector2D Plaza(ErewhonSector::Center(Sector));
			for (const FTransform& Seat : ErewhonVillage::BenchSeats())
			{
				const FVector2D Bench(Seat.GetLocation());
				Layout->Clearings.Add({ Bench, 150.f, false });
				const FVector2D Toward = (Plaza - Bench).GetSafeNormal();
				for (float Step = 120.f; Step < 420.f; Step += 60.f)
				{
					Layout->Clearings.Add({ Bench + Toward * Step, 70.f, false });
				}
			}
		}
	}

	// Les maisons des habitants et le marché : de la terre, des sentiers.
	ErewhonVillagers::AddGrounds(Sector, Layout->Clearings, Layout->Paths, Layout->PathPaved);

	Layout->CellSize = GCellSize;
	Layout->CellsPerSide = FMath::CeilToInt32(Size / GCellSize);
	Layout->Cells.SetNum(Layout->CellsPerSide * Layout->CellsPerSide);

	const UItemDatabase* Items = UItemDatabase::Get(GetWorld());
	if (!Items)
	{
		return Layout;
	}

	for (const FItemDef* Item : bPlants ? Items->GetPlants() : TArray<const FItemDef*>())
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

		// Semis uniforme, ou par bosquets (fougères, orties…) : les exemplaires se resserrent autour d'un centre.
		const int32 PatchSize = FMath::Max(1, Item->Plant.PatchSize);
		const float PatchRadius = Item->Plant.PatchRadiusMeters * 100.f;
		FVector2D PatchCenter = FVector2D::ZeroVector;

		Layout->Plants.Reserve(Layout->Plants.Num() + Count);
		for (int32 i = 0; i < Count; ++i)
		{
			if (PatchSize > 1 && i % PatchSize == 0)
			{
				PatchCenter = FVector2D(Origin.X + Rand.FRand() * Size, Origin.Y + Rand.FRand() * Size);
			}
			FVector2D Point = FVector2D::ZeroVector;
			bool bPlaced = false;
			for (int32 Attempt = 0; Attempt < GPlacementRetries && !bPlaced; ++Attempt)
			{
				if (PatchSize > 1)
				{
					const float Distance = PatchRadius * FMath::Sqrt(Rand.FRand());
					const float Angle = Rand.FRandRange(0.f, UE_TWO_PI);
					Point = PatchCenter + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
					bPlaced = Point.X >= Origin.X && Point.Y >= Origin.Y && Point.X < Origin.X + Size && Point.Y < Origin.Y + Size
						&& !Layout->IsBare(Point);
					continue;
				}
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

	// Les chemins d'accès viennent après les plantes : ils n'en décalent pas les index.
	BuildAccessPaths(*Layout);
	if (Layout->Paths.Num() > 0 || Layout->Clearings.Num() > 0)
	{
		for (int32 Index = 0; Index < Layout->Plants.Num(); ++Index)
		{
			if (Layout->IsBare(FVector2D(Layout->Plants[Index].Location)))
			{
				Layout->CoveredPlants.Add(Index);
			}
		}
	}
	if (bPlants)
	{
		BuildBushes(*Layout, WorldSeed);
	}
	BuildTrees(*Layout, WorldSeed);

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
				if (Layout->CoveredPlants.Contains(Index) || !Filter(Sector, Index))
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
