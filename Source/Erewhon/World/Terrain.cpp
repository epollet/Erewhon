#include "World/Terrain.h"

#include "World/Villagers.h"
#include "Core/ErewhonSettings.h"
#include "World/SectorTypes.h"
#include "World/SpawnLayout.h"
#include "World/VillageNetwork.h"

namespace ErewhonTerrain
{
	namespace
	{
		/** Décalage du bruit propre à chaque monde. */
		FVector2D SeedOffset(int32 WorldSeed, int32 Octave)
		{
			FRandomStream Rand(HashCombine(GetTypeHash(WorldSeed), GetTypeHash(Octave)));
			return FVector2D(Rand.FRandRange(-500.f, 500.f), Rand.FRandRange(-500.f, 500.f));
		}
	}

	namespace
	{
		/** Combien un type de secteur peut monter ou descendre : la prairie et la lisière restent assez plates, la forêt ondule. */
		float SlopeFactor(ESectorType Type)
		{
			switch (Type)
			{
			case ESectorType::Forest: return 2.2f;
			case ESectorType::Edge:   return 1.2f;
			case ESectorType::Road:   return 0.8f;
			case ESectorType::Spawn:  return 0.f;
			default:                  return 1.f;
			}
		}

		/** Relief propre à l'intérieur d'un secteur (cm), nul sur ses bords. */
		float BumpAmplitude(ESectorType Type)
		{
			switch (Type)
			{
			case ESectorType::Forest: return 380.f;
			case ESectorType::Edge:   return 110.f;
			case ESectorType::Prairie: return 70.f;
			default:                  return 0.f;
			}
		}

		/** Les types des secteurs, mis de côté : la hauteur est demandée des milliers de fois par secteur. */
		ESectorType CachedType(const FIntPoint& Sector, int32 WorldSeed)
		{
			thread_local TMap<FIntPoint, ESectorType> Cache;
			thread_local int32 CacheSeed = 0;
			if (CacheSeed != WorldSeed || Cache.Num() > 8192)
			{
				Cache.Reset();
				CacheSeed = WorldSeed;
			}
			if (const ESectorType* Found = Cache.Find(Sector))
			{
				return *Found;
			}
			return Cache.Add(Sector, ErewhonSector::TypeAt(Sector, WorldSeed));
		}

		/**
		 * L'altitude d'un coin de secteur (cm), partagée par les quatre secteurs qui le touchent : leurs bords
		 * communs passent donc par les mêmes points. Un bruit à grande échelle donne la pente d'un bord à l'autre ;
		 * son ampleur suit le plus accidenté des secteurs voisins.
		 */
		float CornerHeight(const FIntPoint& Corner, int32 WorldSeed)
		{
			thread_local TMap<FIntPoint, float> Cache;
			thread_local int32 CacheSeed = 0;
			if (CacheSeed != WorldSeed || Cache.Num() > 8192)
			{
				Cache.Reset();
				CacheSeed = WorldSeed;
			}
			if (const float* Found = Cache.Find(Corner))
			{
				return *Found;
			}
			const FVector2D P(Corner.X, Corner.Y);
			const float Noise = FMath::PerlinNoise2D(P * 0.23f + SeedOffset(WorldSeed, 10)) + 0.5f * FMath::PerlinNoise2D(P * 0.61f + SeedOffset(WorldSeed, 11));
			float Factor = 0.f;
			for (const FIntPoint& Touching : { Corner, Corner - FIntPoint(1, 0), Corner - FIntPoint(0, 1), Corner - FIntPoint(1, 1) })
			{
				Factor = FMath::Max(Factor, SlopeFactor(CachedType(Touching, WorldSeed)));
			}
			const float Amplitude = UErewhonSettings::Get().TerrainAmplitudeMeters * 100.f * 0.6f;
			return Cache.Add(Corner, Noise * Amplitude * Factor);
		}

		/** Un endroit où le sol s'aplanit : plat jusqu'à Inner, rejoint le relief à Outer (cm). */
		struct FTerrainFlatSpot
		{
			FVector2D At = FVector2D::ZeroVector;
			float Inner = 0.f;
			float Outer = 0.f;
		};

		/**
		 * Les replats qui touchent un secteur : la place du village le plus proche, ses cabanes, ses maisons et son
		 * marché (qu'ils soient bâtis ou non : le relief ne dépend pas des achats). Aucun loin des villages.
		 * Mis de côté par secteur, comme les types : la hauteur est demandée des milliers de fois par secteur.
		 */
		const TArray<FTerrainFlatSpot>& FlatSpots(const FIntPoint& Sector, int32 WorldSeed)
		{
			thread_local TMap<FIntPoint, TArray<FTerrainFlatSpot>> Cache;
			thread_local int32 CacheSeed = 0;
			if (CacheSeed != WorldSeed || Cache.Num() > 8192)
			{
				Cache.Reset();
				CacheSeed = WorldSeed;
			}
			if (const TArray<FTerrainFlatSpot>* Found = Cache.Find(Sector))
			{
				return *Found;
			}
			TArray<FTerrainFlatSpot> Spots;
			const FIntPoint Village = ErewhonVillages::NearestVillage(Sector);
			const FIntPoint FromCenter = Sector - ErewhonVillages::CenterOf(Village);
			// Le plus lointain (une maison, à 75 m de la place, aplanie sur 26 m) ne déborde pas de deux secteurs.
			if (FMath::Max(FMath::Abs(FromCenter.X), FMath::Abs(FromCenter.Y)) <= 2)
			{
				// Autour de la place du village, le sol est plat ; il rejoint le relief en pente douce.
				const float FlatRadius = UErewhonSettings::Get().SpawnFlatRadiusMeters * 100.f;
				Spots.Add({ FVector2D(ErewhonSector::Center(ErewhonVillages::CenterOf(Village))), FlatRadius, FlatRadius + 3000.f });
				// Même chose, plus serré, autour des cabanes du bord des routes.
				for (const EShopKind Kind : { EShopKind::SectorManager, EShopKind::GeneralStore, EShopKind::Tailor })
				{
					Spots.Add({ FVector2D(ErewhonSpawn::CabinTransform(Village, Kind).GetLocation()), 1300.f, 3300.f });
				}
				for (const EShopKind Kind : { EShopKind::Breeder, EShopKind::Workshop })
				{
					const FTransform House = ErewhonSpawn::CabinTransform(Village, Kind);
					Spots.Add({ FVector2D(House.GetLocation()), 1500.f, 3500.f });
					Spots.Add({ FVector2D(House.TransformPosition(FVector(ErewhonSpawn::AnnexCenter(Kind), 0.f))), 1500.f, 3500.f });
				}
				// Les maisons des habitants et le marché.
				Spots.Add({ ErewhonVillagers::MarketCenter(Village), 1500.f, 3500.f });
				for (int32 i = 0; i < ErewhonVillagers::HouseCount; ++i)
				{
					Spots.Add({ FVector2D(ErewhonVillagers::HouseTransform(Village, i).GetLocation()), 950.f, 2600.f });
				}
				// Seuls ceux qui atteignent ce secteur comptent.
				const FBox2D Bounds(FVector2D(ErewhonSector::Origin(Sector)), FVector2D(ErewhonSector::Origin(Sector + FIntPoint(1, 1))));
				Spots.RemoveAll([&Bounds](const FTerrainFlatSpot& Spot)
				{
					return Bounds.ComputeSquaredDistanceToPoint(Spot.At) > FMath::Square(Spot.Outer);
				});
			}
			return Cache.Add(Sector, MoveTemp(Spots));
		}
	}

	float Height(const FVector2D& World, int32 WorldSeed)
	{
		const UErewhonSettings& Settings = UErewhonSettings::Get();
		if (Settings.TerrainAmplitudeMeters <= 0.f)
		{
			return 0.f;
		}
		const float Size = Settings.SectorSize();
		const FIntPoint Sector = ErewhonSector::FromWorld(FVector(World, 0.0));
		const float U = FMath::Clamp(static_cast<float>(World.X / Size - Sector.X), 0.f, 1.f);
		const float V = FMath::Clamp(static_cast<float>(World.Y / Size - Sector.Y), 0.f, 1.f);

		// D'un coin à l'autre, en douceur (pas de cassure au passage d'un secteur à son voisin).
		const float SU = FMath::SmoothStep(0.f, 1.f, U), SV = FMath::SmoothStep(0.f, 1.f, V);
		const float H00 = CornerHeight(Sector, WorldSeed);
		const float H10 = CornerHeight(Sector + FIntPoint(1, 0), WorldSeed);
		const float H01 = CornerHeight(Sector + FIntPoint(0, 1), WorldSeed);
		const float H11 = CornerHeight(Sector + FIntPoint(1, 1), WorldSeed);
		float Height = FMath::Lerp(FMath::Lerp(H00, H10, SU), FMath::Lerp(H01, H11, SU), SV);

		// Le relief propre au secteur : des bosses et des creux, qui s'effacent en approchant des bords.
		const float Window = FMath::Sin(UE_PI * U) * FMath::Sin(UE_PI * V);
		const float Bumps = FMath::PerlinNoise2D(World / 2600.f + SeedOffset(WorldSeed, 1)) + 0.4f * FMath::PerlinNoise2D(World / 900.f + SeedOffset(WorldSeed, 2));
		Height += BumpAmplitude(CachedType(Sector, WorldSeed)) * Window * Bumps;

		// Autour de la place des villages, de leurs cabanes, maisons et marchés, le sol est plat ; il rejoint
		// le relief en pente douce.
		float Flatten = 1.f;
		for (const FTerrainFlatSpot& Spot : FlatSpots(Sector, WorldSeed))
		{
			Flatten = FMath::Min(Flatten, FMath::SmoothStep(Spot.Inner, Spot.Outer, static_cast<float>(FVector2D::Distance(World, Spot.At))));
		}
		return Height * Flatten;
	}

	FVector Normal(const FVector2D& World, int32 WorldSeed)
	{
		constexpr float Step = 50.f;
		const float HX = Height(World + FVector2D(Step, 0.f), WorldSeed) - Height(World - FVector2D(Step, 0.f), WorldSeed);
		const float HY = Height(World + FVector2D(0.f, Step), WorldSeed) - Height(World - FVector2D(0.f, Step), WorldSeed);
		return FVector(-HX, -HY, 2.f * Step).GetSafeNormal();
	}
}
