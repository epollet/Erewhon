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

	/** Plante un panneau sur Owner, au point Location (relatif à Parent, pied du poteau). */
	EREWHON_API void AddSignpost(AActor* Owner, USceneComponent* Parent, const FVector& Location, const TArray<FArm>& Arms);

	/** Bras d'un commerce vu depuis From : son nom et la distance à pied (m, arrondie à 10 m). */
	EREWHON_API FArm ShopArm(EShopKind Kind, const FVector& From, float Yaw);
}
