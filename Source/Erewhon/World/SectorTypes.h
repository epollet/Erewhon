#pragma once

#include "CoreMinimal.h"
#include "SectorTypes.generated.h"

/** Types de secteur. Seuls Spawn et Prairie existent pour l'instant. */
UENUM(BlueprintType)
enum class ESectorType : uint8
{
	Undefined,
	Spawn,    // secteur 0:0 — cabane du marchand, n'appartient à aucun joueur
	Prairie,
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

	/** Le secteur de spawn appartient au marchand : jamais achetable. */
	inline bool IsPurchasable(const FIntPoint& Sector) { return Sector != FIntPoint::ZeroValue; }

	/** Position du marchand (centre du secteur de spawn). */
	EREWHON_API FVector MerchantLocation();
}
