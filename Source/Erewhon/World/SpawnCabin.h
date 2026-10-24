#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "World/SectorTypes.h"
#include "SpawnCabin.generated.h"

class UCapsuleComponent;
class UPointLightComponent;

/**
 * Le marchand d'un commerce : au General Store, il achète les récoltes et vend outils et
 * vêtements ; au Sector Manager, il vend et reprend les secteurs.
 * Le dialogue s'ouvre côté client ; le serveur revalide la distance à chaque transaction.
 */
UCLASS()
class EREWHON_API AMerchant : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	AMerchant();

	virtual void BeginPlay() override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	/** À régler avant BeginPlay (SpawnActorDeferred). */
	UPROPERTY(EditAnywhere)
	EShopKind Kind = EShopKind::GeneralStore;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> Capsule;

	/** Rotation du modèle généré pour qu'il regarde dans l'axe de l'acteur. */
	UPROPERTY(EditAnywhere)
	float MerchantMeshYaw = -90.f;

public:
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Le marchand animé (squelette Meshy) : attentes au hasard, un salut quand on approche. */
	UPROPERTY(Transient)
	TObjectPtr<class USkeletalMeshComponent> Animated;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<class UAnimSequence>> Animations;

	void Play(FName Name, bool bLoop);
	FName Playing;
	double PlayingUntil = 0.0;
	double NextFidget = 0.0;
	double LastWave = -100.0;
	/** Le salut lève le bras au-dessus de la tête : le squelette automatique le rend mal, on s'en passe. */
	bool bWaves = false;
};

/**
 * Une cabane de la place du village (General Store ou Sector Manager) : structure procédurale
 * (pour garder un intérieur où se tient le marchand), habillée des textures peintes générées,
 * plus des accessoires Meshy.
 * Acteur local (non répliqué) : chaque machine la construit à l'identique.
 */
UCLASS()
class EREWHON_API ASpawnCabin : public AActor
{
	GENERATED_BODY()

public:
	ASpawnCabin();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** À régler avant BeginPlay (SpawnActorDeferred). */
	UPROPERTY(EditAnywhere)
	EShopKind Kind = EShopKind::GeneralStore;

	/** Commerce de la place : son village (sa cellule, voir ErewhonVillages), à régler avant BeginPlay. */
	UPROPERTY(EditAnywhere)
	FIntPoint Village = FIntPoint::ZeroValue;

	/** Apothicairerie : avec sa remise, à droite (+Y), où l'on passe par une porte à la place de la fenêtre. */
	UPROPERTY(EditAnywhere)
	bool bStoreroom = false;

	/** Maison d'habitant : laquelle (son style, son mobilier). */
	UPROPERTY(EditAnywhere)
	int32 HouseIndex = 0;

private:
	/** Ce que porte une étagère. */
	enum class EShelfGoods : uint8 { Pantry, Office, Fabric, None };

	void BuildCabin();
	void BuildWindow(float Side, float Y0, float Y1);
	void BuildTimberFrame();
	void BuildStoreInterior();
	void BuildManagerInterior();
	void BuildTailorInterior();
	void BuildApothecaryInterior();
	void BuildBreederInterior();
	void BuildWorkshopInterior();
	void BuildHouseInterior();
	/** La grange de l'éleveur (trois box, un bhurak de chaque robe), la cour de l'atelier (ce qu'il vend, exposé). */
	void BuildBarn();
	void BuildYard();
	FVector AddCounter();
	void AddShelf(const FVector& Location, float Yaw, int32 Variant, EShelfGoods Goods);
	class UStaticMeshComponent* AddBox(USceneComponent* Parent, const FVector& Min, const FVector& Max, const FLinearColor& Color,
		const TCHAR* Texture, float Tile, bool bCollision = true);
	void AddFitted(class UStaticMesh* Mesh, const FTransform& Fit, const FVector& Location, float Yaw, bool bCollision);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Lamp;

	/**
	 * Les lampes de la maison (plafond, remise, grange) : leurs ombres ne se calculent que si le joueur est
	 * tout près. Une lampe qui projette des ombres redessine toute la végétation alentour, à chaque image.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> ShadowLamps;
	bool bLampShadows = false;

	/** Le marchand et la chatte de la boutique : des acteurs à part, qui partent avec elle. */
	TArray<TWeakObjectPtr<AActor>> Companions;

	/** Rotation du comptoir généré pour que sa façade regarde la porte. */
	UPROPERTY(EditAnywhere)
	float CounterMeshYaw = 0.f;

	/** Dimensions de la maison (voir ErewhonSpawn::CabinHalfSize). */
	float HalfLength = 450.f;
	float HalfWidth = 350.f;
	float WallHeight = 300.f;

	/** Profondeur du remblai et du perron sous le plancher (plus grande sur un terrain en pente). */
	float FoundationDepth = 20.f;

	/** Rotation de la cheminée générée pour que sa façade regarde la pièce. */
	UPROPERTY(EditAnywhere)
	float FireplaceFrontYaw = 0.f;

	/** Rotation du bureau aux cartes généré, même règle. */
	UPROPERTY(EditAnywhere)
	float DeskMeshYaw = 0.f;
};
