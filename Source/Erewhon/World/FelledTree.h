#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "FelledTree.generated.h"

struct FTreeInstance;
class UStaticMeshComponent;

/**
 * Un arbre abattu : il bascule (deux secondes et demie), sa cime éclate en feuilles et il reste son tronc,
 * couché au sol. La hache en main, chaque coup en détache une bûche ; vide, il disparaît.
 * Répliqué : le serveur décide, chaque machine anime la chute d'après l'horloge du monde.
 */
UCLASS()
class EREWHON_API AFelledTree : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	AFelledTree();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(class AErewhonPlayerController* Controller) override;

	bool IsOwnedBy(const FString& Key) const { return !Key.IsEmpty() && Key == OwnerKey; }
	const FString& GetOwnerKey() const { return OwnerKey; }
	const FString& GetOwnerName() const { return OwnerName; }
	int32 GetLogsLeft() const { return LogsLeft; }
	int32 GetLogsTotal() const { return LogsTotal; }
	int32 GetModel() const { return Model; }
	float GetTreeHeight() const { return TreeHeight; }
	float GetGirth() const { return Girth; }
	float GetTreeYaw() const { return TreeYaw; }
	float GetFallYaw() const { return FallYaw; }
	/** Le tronc est-il au sol (la chute finie) ? */
	bool HasLanded() const;
	/** Le milieu de la cime une fois l'arbre tombé (monde), et sa largeur : là où tombent les branches. */
	FVector CrownCenter() const;
	float CrownRadius() const;

	// ── Serveur ──

	void Setup(const FTreeInstance& Tree, float InFallYaw, double InFellAt, const FString& InOwnerKey, const FString& InOwnerName, int32 InLogsLeft = -1);
	void SetLogsLeft(int32 Count);

private:
	UFUNCTION()
	void OnRep_Logs();

	/** Le tronc couché : sa longueur suit ce qu'il reste de bûches. */
	void ApplyTrunk();
	void StartBurst();
	void UpdateBurst(float DeltaSeconds);
	float PitchAt(double Now) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	/** Le pied de l'arbre : il pivote autour, dans la direction de la chute. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> TreeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Trunk;

	struct FLeaf
	{
		TWeakObjectPtr<UStaticMeshComponent> Part;
		FVector Velocity;
		FRotator Spin;
		FVector Scale;
	};
	TArray<FLeaf> Leaves;
	float BurstTime = -1.f;
	bool bLanded = false;
	bool bSoundPlayed = false;

	/** Basculé de combien (degrés) une fois au sol : un peu moins de 90 si la pente descend. */
	float LandPitch = 88.f;

	UPROPERTY(Replicated) int32 Model = 0;
	UPROPERTY(Replicated) float TreeHeight = 1000.f;
	UPROPERTY(Replicated) float Girth = 1.f;
	UPROPERTY(Replicated) float TreeYaw = 0.f;
	UPROPERTY(Replicated) float FallYaw = 0.f;
	UPROPERTY(Replicated) double FellAt = 0.0;
	UPROPERTY(Replicated) int32 LogsTotal = 6;
	UPROPERTY(ReplicatedUsing = OnRep_Logs) int32 LogsLeft = 6;
	UPROPERTY(Replicated) FString OwnerKey;
	UPROPERTY(Replicated) FString OwnerName;
};
