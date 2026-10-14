#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "ShopCat.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;

/**
 * La chatte de la boutique : tigrée grise aux yeux jaunes. Elle change de place de temps
 * en temps — l'endroit se déduit de l'horloge du monde, donc tous les joueurs la voient
 * au même endroit sans rien répliquer. On peut la caresser.
 */
UCLASS()
class EREWHON_API AShopCat : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	AShopCat();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	/** Endroits où elle aime s'installer (repère monde) et son orientation (lacet). */
	void SetSpots(const TArray<FTransform>& InSpots) { Spots = InSpots; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;

	TArray<FTransform> Spots;
	int32 CurrentSpot = INDEX_NONE;
	FVector BaseScale = FVector::OneVector;
	FTransform BaseTransform;

	/** Son attente du moment : regarder à gauche ou à droite, s'étirer, se mettre en boule, frissonner. */
	enum class EIdle : uint8 { Sit, LookLeft, LookRight, Stretch, Loaf, Twitch };
	EIdle Idle = EIdle::Sit;
	double IdleStart = 0.0;
	double IdleEnd = 0.0;
	/** Pose courante (adoucie) : lacet, tangage, roulis, et écrasement vertical. */
	FVector4 Pose = FVector4(0.f, 0.f, 0.f, 1.f);
};
