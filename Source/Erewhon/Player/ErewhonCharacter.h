#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "World/SectorLayout.h"
#include "ErewhonCharacter.generated.h"

class UCameraComponent;
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

	UFUNCTION(Server, Reliable)
	void ServerSetRunning(bool bRunning);

	void ApplyRunning(bool bRunning);
	void UpdateGroundSpeed();
	void UpdateTarget();
	void BindAppearance();
	void ApplyAppearance();
	void BuildBody();
	bool IsMenuOpen() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	/** Objet tenu, vu par soi (attaché à la caméra) et par les autres (attaché au corps). */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HeldFirstPerson;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HeldThirdPerson;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Torso;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Legs;

	FCharacterTarget Target;
	FDelegateHandle AppearanceHandle;
	TWeakObjectPtr<class UInventoryComponent> BoundInventory;

	/** Plantes demandées au serveur, pour ne pas redemander avant sa réponse. */
	TMap<TPair<FIntPoint, int32>, double> PendingHarvests;

	float SwingTime = 0.f;
	bool bIsRunning = false;
};
