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
	void Initialize(const TSharedRef<const FSectorLayout>& InLayout, bool bWithPlants, bool bInFar = false);

	/** Secteur lointain : sol simplifié seulement ; ses arbres sont dessinés en lot par ASectorManager. */
	bool IsFar() const { return bFar; }

	/** Les masses de feuillage de la canopée d'un secteur de forêt (monde, avant mise à l'échelle du modèle). */
	static void CanopyTransforms(const FSectorLayout& InLayout, TArray<FTransform>& Out);

	void SetPlantHarvested(int32 PlantIndex, bool bHarvested);

	/** Un arbre abattu (sa souche reste) ou repoussé. */
	void SetTreeCut(int32 TreeIndex, bool bCut);
	/** L'arbre dont on vise le tronc (composant et instance touchés), ou INDEX_NONE. */
	int32 TreeAtTrunk(const UPrimitiveComponent* Component, int32 Instance) const;
	const struct FSectorLayout* GetLayout() const { return Layout.Get(); }

	bool HasPlants() const { return bPlantsBuilt; }
	FIntPoint GetCoord() const;

	/** Les champs : la terre (grossière, retournée) et les cultures, selon l'état des cases. */
	void RefreshField();

	/** Géométrie affichée d'une plante (toute sa touffe) : mesh et transformation monde par partie. */
	void GetPlantVisual(int32 PlantIndex, TArray<TPair<UStaticMesh*, FTransform>>& Out) const;
	/** Ajoute à Out les touffes des cultures mûres à moins de Radius de Center (ce que la faux couperait). */
	void GetRipeCropVisuals(const FVector2D& Center, float Radius, TArray<TPair<UStaticMesh*, FTransform>>& Out) const;

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
	/** La place d'un arbre dans ses instances : son modèle (Batches) et son index, la souche. */
	FTransform TreeTransform(int32 TreeIndex, bool bCut) const;
	FTransform TrunkTransform(int32 TreeIndex, bool bCut) const;
	FTransform StumpTransform(int32 TreeIndex, bool bCut) const;
	UPROPERTY(Transient) TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Trunks;
	UPROPERTY(Transient) TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Stumps;
	UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TreeParts;
	TArray<FTransform> TreeFits;
	/** Pour chaque arbre, son instance dans la partie de son modèle. */
	TArray<int32> TreeInstance;
	/** Les souches : créées au premier arbre abattu du secteur seulement. */
	void EnsureStumps();
	bool bTreesVisual = false;
	void BuildSignposts();
	/** Les barrières d'un secteur de champs, et ses portiques. */
	void BuildFences(bool bVisual);
	static UStaticMesh* LoadDecorMesh(const struct FDecorPlant& Plant);
	UTexture2D* BuildDirtMask() const;
	bool IsSoftlyExcluded(const FVector2D& World, float Roll) const;
	void PlantTransforms(int32 PlantIndex, TArray<FTransform>& Out) const;
	UHierarchicalInstancedStaticMeshComponent* MakeInstancedPart(UStaticMesh* Mesh, UMaterialInterface* Material, float CullDistanceMeters);
	int32 FindOrCreateKind(FName ItemId, uint8 Variant);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	/** La terre des champs : section 0 grossière, section 1 retournée. */
	UPROPERTY(Transient)
	TObjectPtr<class UProceduralMeshComponent> FieldSoil;

	/** Pousses et cultures, une instance par touffe, par modèle. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> CropParts;

	/** Les cultures poussent avec les jours : on regarde de temps en temps si l'une a changé d'aspect. */
	void CheckFieldStages();
	int32 FieldStageSignature() const;
	int32 ShownStageSignature = -1;
	FTimerHandle FieldTimer;

	/** Masque terre/herbe du sol, nul pour un secteur sans chemin. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DirtMask;

	TSharedPtr<const FSectorLayout> Layout;
	TArray<FPlantKind> Kinds;
	TArray<int32> KindOfPlant;      // index dans Kinds
	TArray<int32> InstanceOfPlant;  // premier index d'instance de la touffe, identique dans chaque partie
	bool bPlantsBuilt = false;
	bool bFar = false;
};
