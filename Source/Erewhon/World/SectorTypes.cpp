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

	ESectorType TypeAt(const FIntPoint& Sector, int32 /*WorldSeed*/)
	{
		if (Sector == FIntPoint::ZeroValue)
		{
			return ESectorType::Spawn;
		}
		return IsRoad(Sector) ? ESectorType::Road : ESectorType::Prairie;
	}

	bool IsPurchasable(const FIntPoint& Sector)
	{
		return TypeAt(Sector, 0) == ESectorType::Prairie;
	}

	FText TypeLabel(ESectorType Type)
	{
		switch (Type)
		{
		case ESectorType::Spawn:   return LOCTEXT("Spawn", "Cabane du marchand");
		case ESectorType::Prairie: return LOCTEXT("Prairie", "Prairie");
		case ESectorType::Road:    return LOCTEXT("Road", "Route");
		default:                   return LOCTEXT("Undefined", "Inconnu");
		}
	}

	FVector MerchantLocation()
	{
		// Au fond de la cabane, derrière le comptoir (la porte est au sud, côté −X).
		return Center(FIntPoint::ZeroValue) + FVector(220.0, 0.0, 0.0);
	}
}

#undef LOCTEXT_NAMESPACE
