#pragma once

#include "CoreMinimal.h"
#include "SectorTypes.generated.h"

/** Types de secteur. */
UENUM(BlueprintType)
enum class ESectorType : uint8
{
	Undefined,
	Spawn,    // secteur 0:0 — cabane du marchand, n'appartient à aucun joueur
	Prairie,
	Road,     // prolonge les chemins du marchand vers les quatre horizons ; jamais à vendre
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

	/** Le spawn appartient au marchand et les routes à tous : ni l'un ni les autres ne se vendent. */
	EREWHON_API bool IsPurchasable(const FIntPoint& Sector);

	/** Secteur traversé par une route : sur l'un des deux axes du marchand, hors spawn. */
	inline bool IsRoad(const FIntPoint& Sector) { return (Sector.X == 0) != (Sector.Y == 0); }

	/** Position du marchand (centre du secteur de spawn). */
	EREWHON_API FVector MerchantLocation();
}
