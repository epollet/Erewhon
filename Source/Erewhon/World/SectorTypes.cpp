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
		default:                   return LOCTEXT("Undefined", "Inconnu");
		}
	}
}

#undef LOCTEXT_NAMESPACE
