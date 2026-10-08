#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sector.generated.h"

struct FSectorLayout;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;

/**
 * Représentation locale d'un secteur : sol, chemins et plantes instanciées.
 * Non répliqué — tout se déduit du FSectorLayout et de l'état des récoltes.
 */
UCLASS()
class EREWHON_API ASector : public AActor
{
	GENERATED_BODY()

public:
	ASector();

	/** bWithPlants = false pour un serveur dédié ou un secteur lointain : sol seul. */
	void Initialize(const TSharedRef<const FSectorLayout>& InLayout, bool bWithPlants);

	void SetPlantHarvested(int32 PlantIndex, bool bHarvested);

	bool HasPlants() const { return bPlantsBuilt; }
	FIntPoint GetCoord() const;

private:
	struct FPlantKind
	{
		FName ItemId;
		TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Parts;
		TArray<FTransform> PartLocal;   // transformation de chaque partie pour une plante de 1 m
	};

	void BuildGround();
	void BuildPlants();
	FPlantKind& FindOrCreateKind(FName ItemId);
	FTransform PlantTransform(int32 PlantIndex) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	TSharedPtr<const FSectorLayout> Layout;
	TArray<FPlantKind> Kinds;
	TArray<int32> KindOfPlant;      // index dans Kinds
	TArray<int32> InstanceOfPlant;  // index d'instance, identique dans chaque partie
	bool bPlantsBuilt = false;
};
