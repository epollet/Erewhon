#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "Door.generated.h"

class UStaticMeshComponent;

/**
 * Porte qui pivote sur ses gonds. L'état ouvert/fermé est tenu par le serveur et
 * répliqué ; chaque machine anime la rotation localement.
 * L'acteur est posé sur l'axe des gonds, le battant s'étend vers +Y.
 */
UCLASS()
class EREWHON_API ADoor : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	ADoor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	/** Serveur : ouvre ou ferme. */
	void Toggle();

	/** Dimensions du battant (cm). */
	UPROPERTY(EditAnywhere, Category = "Door")
	FVector PanelSize = FVector(6.f, 140.f, 220.f);

	/** Angle d'ouverture (degrés) ; négatif pour ouvrir vers l'intérieur (+X). */
	UPROPERTY(EditAnywhere, Category = "Door")
	float OpenYaw = -100.f;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Hinge;

	UPROPERTY(Replicated)
	bool bOpen = false;

	float CurrentYaw = 0.f;
	bool bLastOpen = false;
};
