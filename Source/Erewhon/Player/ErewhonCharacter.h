#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "World/SectorLayout.h"
#include "Items/ItemTypes.h"
#include "ErewhonCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class USkeletalMeshComponent;
class UAnimSequence;
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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** S'asseoir sur un banc de la place (index dans ErewhonVillage::BenchSeats), se relever. */
	UFUNCTION(Server, Reliable)
	void ServerSit(int32 Seat);

	UFUNCTION(Server, Reliable)
	void ServerStand();

	bool IsSeated() const { return SeatIndex != INDEX_NONE; }

	/** Le conteneur qu'on porte dans ses bras, la charrette qu'on conduit (le serveur les fixe). */
	class AStorageActor* GetCarried() const { return Carried; }
	class ACart* GetDrivenCart() const { return DrivenCart; }
	void SetCarried(class AStorageActor* Storage) { Carried = Storage; }
	void SetDrivenCart(class ACart* Cart) { DrivenCart = Cart; }

	// ── Potions ──

	/** Serveur : boire une potion, son effet pour ErewhonApothecary::PotionSeconds. */
	void DrinkPotion(EPotionEffect Effect);
	bool HasPotion(EPotionEffect Effect) const { return PotionRemaining(Effect) > 0.f; }
	/** Secondes d'effet restantes (0 s'il est passé). */
	float PotionRemaining(EPotionEffect Effect) const;
	/** Serveur : un effet venu d'ailleurs (la sérénité d'un pot-pourri), prolongé s'il dure déjà. */
	void GrantEffect(EPotionEffect Effect, double Seconds);

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
	void AttachBackpackToSpine();
	bool bBackpackOnSpine = false;
	void ApplySeat();

	/** Le personnage animé (modèle Meshy et ses animations), s'il a été importé. */
	void BuildAnimatedBody();
	void UpdateAnimation(float DeltaSeconds);
	void PlayBodyAnimation(FName Name, bool bLoop, float PlayRate = 1.f);
	/** Certaines animations font avancer le bassin : on recentre le corps pour qu'il reste sur place. */
	void KeepBodyInPlace();

	FName HipsBone;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> AnimatedBody;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAnimSequence>> BodyAnimations;

	FName CurrentAnimation;
	/** Animation jouée une fois (ramasser) : l'heure jusqu'à laquelle elle a la main. */
	double OneShotUntil = 0.0;
	/** Base de l'objet tenu, vu par les autres : décalage dans la main ou devant le corps. */
	FVector HeldThirdPersonBase = FVector::ZeroVector;

	/** Ce qu'on s'apprête à poser (objet en main, conteneur porté) : où, et sur quoi. */
	struct FPlacement
	{
		bool bValid = false;
		/** Visé, mais interdit ici (pas son secteur). */
		bool bForbidden = false;
		uint8 Type = 0;   // 0 : sol, 1 : sur une cagette, 2 : dans une charrette
		FVector Location = FVector::ZeroVector;
		float Yaw = 0.f;
		TWeakObjectPtr<AActor> Target;
		int32 Slot = INDEX_NONE;
	};
	bool IsPlacing() const;
	void UpdatePlacement();

	UPROPERTY(Replicated)
	TObjectPtr<class AStorageActor> Carried;

	UPROPERTY(Replicated)
	TObjectPtr<class ACart> DrivenCart;

	/** Pour chaque effet de potion (index EPotionEffect), l'horloge du monde jusqu'à laquelle il dure. */
	UPROPERTY(Replicated)
	TArray<double> PotionUntil;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPotionDrunk();

	/** Taille, saut et vision selon les potions actives (sur chaque machine, à partir de l'état répliqué). */
	void UpdatePotions(float DeltaSeconds);
	float BodyScale = 1.f;
	float NightVision = 0.f;

	/** Aperçu translucide de ce qu'on va poser. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PlacementGhost;

	FPlacement Placement;

	UFUNCTION()
	void OnRep_Seat();

	UPROPERTY(ReplicatedUsing = OnRep_Seat)
	int32 SeatIndex = INDEX_NONE;

	bool bStandRequested = false;

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
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> ForbiddenMaterial;
	/** Le bruit de l'outil, relancé à chaque coup. */
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> ToolAudio;

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
