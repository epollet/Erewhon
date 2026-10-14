#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sector.generated.h"

struct FSectorLayout;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

/**
 * Représentation locale d'un secteur : sol (herbe et terre mêlées), plantes
 * récoltables et végétation décorative, tout en instances.
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

	/** Géométrie affichée d'une plante (toute sa touffe) : mesh et transformation monde par partie. */
	void GetPlantVisual(int32 PlantIndex, TArray<TPair<UStaticMesh*, FTransform>>& Out) const;

	/** Taille d'une répétition des textures de sol (cm). */
	static constexpr float GroundTileSize = 450.f;
	static constexpr float DirtTileSize = 260.f;
	static constexpr float PavedTileSize = 300.f;

private:
	struct FPlantKind
	{
		FName ItemId;
		uint8 Variant = 0;
		TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Parts;
		TArray<FTransform> PartLocal;   // transformation de chaque partie pour une plante de 1 m
	};

	void BuildGround(bool bVisual);
	void BuildPlants();
	void BuildDecor();
	void BuildBushes();
	void BuildTrees(bool bVisual);
	void BuildSignposts();
	static UStaticMesh* LoadDecorMesh(const struct FDecorPlant& Plant);
	UTexture2D* BuildDirtMask() const;
	bool IsSoftlyExcluded(const FVector2D& World, float Roll) const;
	void PlantTransforms(int32 PlantIndex, TArray<FTransform>& Out) const;
	UHierarchicalInstancedStaticMeshComponent* MakeInstancedPart(UStaticMesh* Mesh, UMaterialInterface* Material, float CullDistanceMeters);
	int32 FindOrCreateKind(FName ItemId, uint8 Variant);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	/** Masque terre/herbe du sol, nul pour un secteur sans chemin. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DirtMask;

	TSharedPtr<const FSectorLayout> Layout;
	TArray<FPlantKind> Kinds;
	TArray<int32> KindOfPlant;      // index dans Kinds
	TArray<int32> InstanceOfPlant;  // premier index d'instance de la touffe, identique dans chaque partie
	bool bPlantsBuilt = false;
};
