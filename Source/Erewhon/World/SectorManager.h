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

	/** Secteur affiché à ces coordonnées, nul s'il n'est pas chargé. */
	static ASector* FindLoaded(const UObject* WorldContext, const FIntPoint& Coord);

private:
	void Refresh();
	void RebuildAll();
	void OnSectorOwnerChanged(const FIntPoint& Sector);
	void OnPlantHarvestChanged(const FIntPoint& Sector, int32 PlantIndex, bool bHarvested);
	void OnFieldChanged(const FIntPoint& Sector);
	/** Les secteurs dont un champ a changé : redessinés une fois par image, même si cent cases ont bougé. */
	TSet<FIntPoint> DirtyFields;
	FDelegateHandle FieldHandle;

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<ASector>> Loaded;

	/** Les arbres et la canopée de tous les secteurs lointains, en un seul lot par modèle (peu d'appels de dessin). */
	void RebuildFarForest(const TSet<FIntPoint>& Coords);
	UPROPERTY(Transient) TArray<TObjectPtr<class UHierarchicalInstancedStaticMeshComponent>> FarForest;
	TSet<FIntPoint> FarForestCoords;
	/** Les arbres et la canopée de chaque secteur lointain (monde, par modèle), calculés une fois. */
	TMap<FIntPoint, TArray<TArray<FTransform>>> FarTreeCache;
	const TArray<TArray<FTransform>>& FarTreesOf(const FIntPoint& Coord);
	double FarForestBuiltAt = -100.0;

	/** Le fond : des collines puis des montagnes, en couronne, qui suivent le joueur. */
	void BuildBackdrop();
	UPROPERTY(Transient) TArray<TObjectPtr<class UProceduralMeshComponent>> Backdrop;

	/** Prairie lointaine jusqu'à l'horizon, sous le sol des secteurs chargés. */
	UPROPERTY(Transient)
	TObjectPtr<class UStaticMeshComponent> FarGround;
	FIntPoint FarGroundSector = FIntPoint(TNumericLimits<int32>::Max());
	bool bFarGroundPlaced = false;

	FDelegateHandle HarvestHandle;
	FDelegateHandle DetailHandle;
	FDelegateHandle OwnerHandle;
	FDelegateHandle VillageHandle;
};
