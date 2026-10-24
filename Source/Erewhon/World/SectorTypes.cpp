#include "World/SectorTypes.h"

#include "Core/ErewhonSettings.h"

#define LOCTEXT_NAMESPACE "ErewhonSector"

namespace ErewhonSector
{
	FIntPoint FromWorld(const FVector& Location)
	{
		const double Size = UErewhonSettings::Get().SectorSize();
		return FIntPoint(FMath::FloorToInt32(Location.X / Size), FMath::FloorToInt32(Location.Y / Size));
	}

	FVector Origin(const FIntPoint& Sector)
	{
		const double Size = UErewhonSettings::Get().SectorSize();
		return FVector(Sector.X * Size, Sector.Y * Size, 0.0);
	}

	FVector Center(const FIntPoint& Sector)
	{
		const double Half = UErewhonSettings::Get().SectorSize() * 0.5;
		return Origin(Sector) + FVector(Half, Half, 0.0);
	}

	FString ToString(const FIntPoint& Sector)
	{
		return FString::Printf(TEXT("%d:%d"), Sector.X, Sector.Y);
	}

	namespace
	{
		/**
		 * Les bois : un bruit doux à l'échelle de quelques secteurs, propre à chaque monde. Jamais sur
		 * les places, les routes, ni tout près d'un village.
		 */
		bool IsWooded(const FIntPoint& Sector, int32 WorldSeed)
		{
			if (IsRoad(Sector))
			{
				return false;
			}
			const FIntPoint FromPlaza = Sector - ErewhonVillages::CenterOf(ErewhonVillages::NearestVillage(Sector));
			if (FMath::Max(FMath::Abs(FromPlaza.X), FMath::Abs(FromPlaza.Y)) < 2)
			{
				return false;
			}
			FRandomStream Rand(HashCombine(GetTypeHash(WorldSeed), 0x7F0E57u));
			const FVector2D Offset(Rand.FRandRange(-500.f, 500.f), Rand.FRandRange(-500.f, 500.f));
			const FVector2D Offset2(Rand.FRandRange(-500.f, 500.f), Rand.FRandRange(-500.f, 500.f));
			const FVector2D P(Sector.X, Sector.Y);
			const float Noise = FMath::PerlinNoise2D(P * 0.21f + Offset) + 0.35f * FMath::PerlinNoise2D(P * 0.53f + Offset2);
			return Noise > 0.12f;
		}
	}

	ESectorType TypeAt(const FIntPoint& Sector, int32 WorldSeed)
	{
		if (ErewhonVillages::IsCenter(Sector))
		{
			return ESectorType::Spawn;
		}
		if (IsRoad(Sector))
		{
			return ESectorType::Road;
		}
		if (!IsWooded(Sector, WorldSeed))
		{
			return ESectorType::Prairie;
		}
		// Au cœur des bois, la forêt ; au bord (un voisin qui n'est pas boisé), la lisière.
		for (const FIntPoint& Side : Sides)
		{
			if (!IsWooded(Sector + Side, WorldSeed))
			{
				return ESectorType::Edge;
			}
		}
		return ESectorType::Forest;
	}

	bool IsPurchasable(const FIntPoint& Sector)
	{
		return !ErewhonVillages::IsCenter(Sector) && !IsRoad(Sector);
	}

	FText SideLabel(int32 Side)
	{
		switch (Side)
		{
		case 0:  return LOCTEXT("North", "nord");
		case 1:  return LOCTEXT("South", "sud");
		case 2:  return LOCTEXT("East", "est");
		default: return LOCTEXT("West", "ouest");
		}
	}

	FText TypeLabel(ESectorType Type)
	{
		switch (Type)
		{
		case ESectorType::Spawn:   return LOCTEXT("Spawn", "Place du village");
		case ESectorType::Prairie: return LOCTEXT("Prairie", "Prairie");
		case ESectorType::Road:    return LOCTEXT("Road", "Route");
		case ESectorType::Edge:    return LOCTEXT("Edge", "Lisière");
		case ESectorType::Forest:  return LOCTEXT("Forest", "Forêt");
		default:                   return LOCTEXT("Undefined", "Inconnu");
		}
	}
}

#undef LOCTEXT_NAMESPACE
