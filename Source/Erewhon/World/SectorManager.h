#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SectorManager.generated.h"

class ASector;

/**
 * Charge et décharge les secteurs autour des joueurs, sur chaque machine.
 * - Autour du joueur local : secteurs complets jusqu'à SectorRenderDistance.
 * - Autour des autres joueurs (serveur) : sol seul, pour les collisions.
 */
UCLASS()
class EREWHON_API ASectorManager : public AActor
{
	GENERATED_BODY()

public:
	ASectorManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void Refresh();
	void OnPlantHarvestChanged(const FIntPoint& Sector, int32 PlantIndex, bool bHarvested);

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<ASector>> Loaded;

	FDelegateHandle HarvestHandle;
};
