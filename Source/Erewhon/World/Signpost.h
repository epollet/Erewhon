#pragma once

#include "CoreMinimal.h"
#include "World/SectorTypes.h"

class AActor;
class USceneComponent;

/**
 * Panneaux indicateurs en bois : un poteau, des planches en flèche gravées des deux côtés.
 * Purement visuels, construits localement sur chaque machine.
 */
namespace ErewhonSigns
{
	struct FArm
	{
		FString Text;
		float Yaw = 0.f;   // direction pointée, en degrés (0 = +X, nord ; 90 = +Y, est)
	};

	/**
	 * Plante un panneau sur Owner, au point Location (relatif à Parent, pied du poteau).
	 * bLantern : le poteau, plus haut, porte aussi une lanterne (pas de second poteau à côté).
	 */
	EREWHON_API void AddSignpost(AActor* Owner, USceneComponent* Parent, const FVector& Location, const TArray<FArm>& Arms, bool bLantern = false);

	/** Bras d'un commerce vu depuis From (celui du village le plus proche) : son nom et la distance à pied (m, arrondie à 10 m). */
	EREWHON_API FArm ShopArm(EShopKind Kind, const FVector& From, float Yaw);

	/** Bras d'un village (ErewhonVillages) vu depuis From, sur sa route : son nom et la distance (km, au dixième). */
	EREWHON_API FArm VillageArm(const FIntPoint& Village, const FVector& From, float Yaw);
}
