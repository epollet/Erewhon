#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ErewhonMaterials.generated.h"

class UMaterialInstanceDynamic;

/**
 * Matériaux partagés par tous les secteurs d'un monde. L'herbe utilise une seule
 * instance de M_Foliage : le ciel y règle le vent une fois pour toute la prairie.
 */
UCLASS()
class EREWHON_API UErewhonMaterials : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Instance partagée de M_Foliage, nulle si les assets n'ont pas été importés. */
	UMaterialInstanceDynamic* GetFoliage();

	void SetWind(float Intensity, float Speed);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Foliage;

	bool bFoliageLoaded = false;
};
