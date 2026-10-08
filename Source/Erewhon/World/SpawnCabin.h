#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "SpawnCabin.generated.h"

class UCapsuleComponent;
class UPointLightComponent;

/**
 * Le marchand de la cabane : achète les récoltes, vend outils, vêtements et secteurs.
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

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> Capsule;
};

/**
 * Cabane du secteur 0:0, construite en primitives en attendant le modèle Meshy.
 * Acteur local (non répliqué) : chaque machine la construit à l'identique.
 */
UCLASS()
class EREWHON_API ASpawnCabin : public AActor
{
	GENERATED_BODY()

public:
	ASpawnCabin();

	virtual void BeginPlay() override;

private:
	void BuildCabin();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Lamp;
};
