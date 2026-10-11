#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "World/SectorLayout.h"
#include "ErewhonCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/** Ce que vise le joueur local, pour le HUD et le clic. */
struct FCharacterTarget
{
	TWeakObjectPtr<AActor> Interactable;
	FPlantTarget Plant;
	/** Faux si la plante est sur un secteur qui n'appartient pas au joueur. */
	bool bPlantHarvestable = false;
	FText Hint;
};

UCLASS()
class EREWHON_API AErewhonCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AErewhonCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_PlayerState() override;
	virtual void PossessedBy(AController* NewController) override;

	const FCharacterTarget& GetTarget() const { return Target; }

	/** V : première personne → troisième personne (de dos) → troisième personne (de face). */
	void CycleView();

private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartRun();
	void StopRun();
	void Primary();
	void Use();
	void StartCrouch();
	void StopCrouch();
	void ToggleInventory();
	void BagAction();
	void StashAction();
	void ApplyView();
	void BuildWornBackpack();
	void ApplyBackpack();

	UFUNCTION(Server, Reliable)
	void ServerSetRunning(bool bRunning);

	void ApplyRunning(bool bRunning);
	void UpdateGroundSpeed();
	void UpdateTarget();
	void UpdateHighlight();
	void UpdateFootsteps(float DeltaSeconds);
	void BindAppearance();
	void ApplyAppearance();
	void BuildBody();
	bool IsMenuOpen() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	/** Caméra de la vue à la troisième personne, au bout d'un bras qui évite les obstacles. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> CameraArm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> ThirdCamera;

	/** Objet tenu, vu par soi (attaché à la caméra) et par les autres (attaché au corps). */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HeldFirstPerson;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HeldThirdPerson;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Torso;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Legs;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Head;

	/** Le sac à dos porté, et ses deux outils accrochés. */
	UPROPERTY(Transient) TObjectPtr<USceneComponent> WornBackpack;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> WornTools;

	/** Halo posé sur la plante visée (une copie par partie de la touffe). */
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Highlights;
	UPROPERTY(Transient) TObjectPtr<class UMaterialInterface> HighlightMaterial;

	FCharacterTarget Target;
	FDelegateHandle AppearanceHandle;
	TWeakObjectPtr<class UInventoryComponent> BoundInventory;
	FDelegateHandle BackpackHandle;
	TWeakObjectPtr<class UBackpackComponent> BoundBackpack;

	enum class EViewMode : uint8 { FirstPerson, Behind, Front };
	EViewMode ViewMode = EViewMode::FirstPerson;

	/** Plantes demandées au serveur, pour ne pas redemander avant sa réponse. */
	TMap<TPair<FIntPoint, int32>, double> PendingHarvests;

	float SwingTime = 0.f;
	bool bIsRunning = false;
	float StrideDistance = 0.f;
};
