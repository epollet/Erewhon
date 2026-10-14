#include "World/Farm.h"

#include "Core/ErewhonSettings.h"
#include "World/SectorTypes.h"

#define LOCTEXT_NAMESPACE "ErewhonFarm"

namespace ErewhonFarm
{
	FText QuarterLabel(int32 Quarter)
	{
		switch (Quarter)
		{
		case NorthWest: return LOCTEXT("NorthWest", "nord-ouest");
		case NorthEast: return LOCTEXT("NorthEast", "nord-est");
		case SouthWest: return LOCTEXT("SouthWest", "sud-ouest");
		default:        return LOCTEXT("SouthEast", "sud-est");
		}
	}

	void FieldBoxes(const FIntPoint& Sector, uint8 Buildings, TArray<FBox2D>& Out)
	{
		const float Size = UErewhonSettings::Get().SectorSize();
		const FVector2D Origin(ErewhonSector::Origin(Sector));
		const float Mid = Size * 0.5f;
		if (Buildings & MergedBit)
		{
			Out.Add(FBox2D(Origin + FVector2D(EdgeMargin), Origin + FVector2D(Size - EdgeMargin)));
			return;
		}
		// Nord : X haut ; ouest : Y bas.
		const FVector2D Low(EdgeMargin, Mid - CrossHalf);
		const FVector2D High(Mid + CrossHalf, Size - EdgeMargin);
		for (int32 Quarter = 0; Quarter < QuarterCount; ++Quarter)
		{
			if (!(Buildings & QuarterBit(Quarter)))
			{
				continue;
			}
			const bool bNorth = Quarter == NorthWest || Quarter == NorthEast;
			const bool bEast = Quarter == NorthEast || Quarter == SouthEast;
			const FVector2D Min(bNorth ? High.X : Low.X, bEast ? High.X : Low.X);
			const FVector2D Max(bNorth ? High.Y : Low.Y, bEast ? High.Y : Low.Y);
			Out.Add(FBox2D(Origin + Min, Origin + Max));
		}
	}

	uint8 CrossArms(uint8 Buildings)
	{
		if (Buildings & MergedBit)
		{
			return 0;
		}
		// Côtés : nord 0, sud 1, est 2, ouest 3. Dès le premier champ, la croix entière : on rejoint chaque bord.
		return (Buildings & AllQuarters) ? 0b1111 : 0;
	}

	int32 CellAt(const FIntPoint& Sector, const FVector2D& World)
	{
		const FVector2D Local = World - FVector2D(ErewhonSector::Origin(Sector));
		const int32 X = FMath::FloorToInt32(Local.X / CellSize);
		const int32 Y = FMath::FloorToInt32(Local.Y / CellSize);
		if (X < 0 || Y < 0 || X >= CellsPerSide || Y >= CellsPerSide)
		{
			return INDEX_NONE;
		}
		return Y * CellsPerSide + X;
	}

	FVector2D CellCenter(const FIntPoint& Sector, int32 Cell)
	{
		const int32 X = Cell % CellsPerSide;
		const int32 Y = Cell / CellsPerSide;
		return FVector2D(ErewhonSector::Origin(Sector)) + FVector2D((X + 0.5f) * CellSize, (Y + 0.5f) * CellSize);
	}

	bool IsFieldCell(const FIntPoint& Sector, uint8 Buildings, int32 Cell)
	{
		if (Cell < 0 || Cell >= CellsPerSide * CellsPerSide || !(Buildings & AnyField))
		{
			return false;
		}
		TArray<FBox2D> Boxes;
		FieldBoxes(Sector, Buildings, Boxes);
		const FVector2D Center = CellCenter(Sector, Cell);
		return Boxes.ContainsByPredicate([&Center](const FBox2D& Box) { return Box.IsInside(Center); });
	}

	void CellsAround(int32 Cell, int32 Radius, TArray<int32>& Out)
	{
		const int32 CX = Cell % CellsPerSide;
		const int32 CY = Cell / CellsPerSide;
		for (int32 Y = CY - Radius; Y <= CY + Radius; ++Y)
		{
			for (int32 X = CX - Radius; X <= CX + Radius; ++X)
			{
				if (X >= 0 && Y >= 0 && X < CellsPerSide && Y < CellsPerSide)
				{
					Out.Add(Y * CellsPerSide + X);
				}
			}
		}
	}

	const TArray<FCrop>& Crops()
	{
		static const TArray<FCrop> List = {
			{ TEXT("seeds_wheat"), TEXT("wheat"), 3, TEXT("SM_crop_wheat"), 105.f, 5 },
			{ TEXT("seeds_flax"), TEXT("flax"), 2, TEXT("SM_crop_flax"), 90.f, 5 },
			{ TEXT("seeds_cabbage"), TEXT("cabbage"), 1, TEXT("SM_crop_cabbage"), 38.f, 2 },
		};
		return List;
	}

	int32 CropOfSeed(FName Seed)
	{
		return Crops().IndexOfByPredicate([Seed](const FCrop& Crop) { return Crop.Seed == Seed; });
	}

	double DaySeconds()
	{
		return FMath::Max(1.0, static_cast<double>(UErewhonSettings::Get().SunPeriodSeconds));
	}

	int32 Stage(double SownAt, double Now)
	{
		return FMath::Clamp(FMath::FloorToInt32((Now - SownAt) / DaySeconds()), 0, RipeStage);
	}
}

#undef LOCTEXT_NAMESPACE
