#pragma once

#include "CoreMinimal.h"

/**
 * Les champs du fermier : un secteur à soi se découpe en quatre champs (nord-ouest, nord-est,
 * sud-ouest, sud-est) séparés par une croix de chemins, ou en un seul grand champ une fois fusionnés.
 * Chaque champ se travaille case par case (2 m) : terre grossière → retournée (bêche) → semée
 * (sac de graines) → petites pousses (1 jour) → grandes pousses (2 jours) → mûre (3 jours) → récoltée.
 * Toutes les longueurs sont en cm, dans le repère du secteur (nord : +X, est : +Y).
 */
namespace ErewhonFarm
{
	constexpr float CellSize = 200.f;
	constexpr int32 CellsPerSide = 50;
	/** Bande d'herbe laissée au bord du secteur, et demi-largeur réservée à la croix de chemins. */
	constexpr float EdgeMargin = 200.f;
	constexpr float CrossHalf = 200.f;

	/** Les quatre champs : leurs bits dans FSectorOwnerEntry::Buildings (après ceux de l'apothicairerie). */
	enum EQuarter : int32 { NorthWest, NorthEast, SouthWest, SouthEast, QuarterCount };
	constexpr uint8 QuarterBit(int32 Quarter) { return static_cast<uint8>(1 << (2 + Quarter)); }
	constexpr uint8 AllQuarters = 0x3C;
	/** La fusion des quatre champs en un seul. */
	constexpr uint8 MergedBit = 1 << 6;
	constexpr uint8 AnyField = AllQuarters | MergedBit;
	/** La clôture du secteur et ses portiques : une amélioration à part, une fois qu'il y a un champ. */
	constexpr uint8 FenceBit = 1 << 7;
	/** Which de TryBuyField pour la clôture (0-3 : un champ, 4 : la fusion). */
	constexpr uint8 FenceWhich = 5;

	EREWHON_API FText QuarterLabel(int32 Quarter);

	/** Rectangles (monde) des champs du secteur, selon ses améliorations. */
	EREWHON_API void FieldBoxes(const FIntPoint& Sector, uint8 Buildings, TArray<FBox2D>& Out);

	/** Les bras de la croix à tracer (bits des côtés : nord, sud, est, ouest), tous dès le premier champ acheté (aucun une fois fusionnés). */
	EREWHON_API uint8 CrossArms(uint8 Buildings);

	/** La case sous ce point monde (INDEX_NONE hors du secteur). */
	EREWHON_API int32 CellAt(const FIntPoint& Sector, const FVector2D& World);
	EREWHON_API FVector2D CellCenter(const FIntPoint& Sector, int32 Cell);
	EREWHON_API bool IsFieldCell(const FIntPoint& Sector, uint8 Buildings, int32 Cell);

	/** Les cases autour d'une case (Radius 1 : un carré de 3 × 3), dans le secteur. */
	EREWHON_API void CellsAround(int32 Cell, int32 Radius, TArray<int32>& Out);

	/** Une culture : la graine qu'on sème, ce qu'on récolte, et son allure. */
	struct FCrop
	{
		FName Seed;
		FName Product;
		int32 Yield = 1;             // par case
		const TCHAR* Mesh = nullptr; // SM_<…> une fois mûre
		float Height = 100.f;        // cm, mûre
		int32 PerCell = 4;           // touffes par case
	};
	EREWHON_API const TArray<FCrop>& Crops();
	EREWHON_API int32 CropOfSeed(FName Seed);

	/** 0 semé (rien ne paraît encore), 1 petites pousses, 2 grandes pousses, 3 mûr. */
	EREWHON_API int32 Stage(double SownAt, double Now);
	constexpr int32 RipeStage = 3;
	EREWHON_API double DaySeconds();

	inline const FName Spade = TEXT("spade");
	inline const FName Scythe = TEXT("scythe");
	inline const FName Sickle = TEXT("cerpe");

	/** La faux : tout ce qui est à portée, autour de soi, d'un geste de deux secondes. */
	constexpr float ScytheRadius = 380.f;
	constexpr float ScytheSeconds = 2.f;
	/** Un coup de bêche : une seconde. */
	constexpr float SpadeSeconds = 1.f;
	/** Cueillir : une seconde à la main, une demi à la cerpe. */
	constexpr float HandHarvestSeconds = 1.f;
	constexpr float SickleHarvestSeconds = 0.5f;
}
