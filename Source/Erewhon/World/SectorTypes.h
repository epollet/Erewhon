#pragma once

#include "CoreMinimal.h"
#include "SectorTypes.generated.h"

/** Types de secteur. */
UENUM(BlueprintType)
enum class ESectorType : uint8
{
	Undefined,
	Spawn,    // secteur 0:0 — place du village et ses commerces, n'appartient à aucun joueur
	Prairie,
	Road,     // prolonge les chemins du marchand vers les quatre horizons ; jamais à vendre
	Edge,     // lisière de forêt : quelques arbres, plus d'arbustes ; borde la forêt (prairie d'un côté)
	Forest,   // forêt : beaucoup d'arbres, des champignons ; entourée de lisière
};

/** Améliorations du village, achetées chez le Sector Manager et partagées par tous. */
UENUM()
enum class EVillageUpgrade : uint8
{
	Tailor,        // la boutique du tailleur
	Signposts,     // panneaux de direction
	Lanterns,      // lanternes des boutiques, de la place et des carrefours
	Decoration,    // bordures, fleurs, arbres et bancs de la place
	PavedCenter,   // pavés sur la place et ses chemins
	PavedVillage,  // pavés sur les routes du village (après le centre)
	Count UMETA(Hidden)
};

/** Les commerces de la place du village. */
UENUM()
enum class EShopKind : uint8
{
	GeneralStore,    // vend outils et vêtements, rachète les récoltes
	SectorManager,   // vend et reprend les secteurs, les aménage
	Tailor,          // vend et rachète les vêtements
	Apothecary,      // pas un commerce : la maison de l'apothicaire, sur un secteur acheté (voir AApothecary)
};

/**
 * Repère des secteurs : le secteur (x, y) couvre [x·S, (x+1)·S[ × [y·S, (y+1)·S[
 * sur les axes X/Y d'Unreal, le sol étant à Z = 0.
 */
namespace ErewhonSector
{
	EREWHON_API FIntPoint FromWorld(const FVector& Location);
	EREWHON_API FVector Origin(const FIntPoint& Sector);
	EREWHON_API FVector Center(const FIntPoint& Sector);
	EREWHON_API FString ToString(const FIntPoint& Sector);

	/** Type déterministe d'un secteur. Le seed servira aux futurs biomes. */
	EREWHON_API ESectorType TypeAt(const FIntPoint& Sector, int32 WorldSeed);

	EREWHON_API FText TypeLabel(ESectorType Type);

	/** Le spawn appartient au marchand et les routes à tous : ni l'un ni les autres ne se vendent (prairie, lisière et forêt, si). */
	EREWHON_API bool IsPurchasable(const FIntPoint& Sector);

	/** Secteur traversé par une route : sur l'un des deux axes du marchand, hors spawn. */
	inline bool IsRoad(const FIntPoint& Sector) { return (Sector.X == 0) != (Sector.Y == 0); }

	/** Les quatre côtés d'un secteur, dans l'ordre des bits des améliorations : nord, sud, est, ouest. */
	inline const FIntPoint Sides[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

	/** Nom du côté, ex. « nord ». */
	EREWHON_API FText SideLabel(int32 Side);
}
