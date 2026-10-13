#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "World/SectorTypes.h"
#include "Village.generated.h"

/**
 * Le village : la place des commerces et les secteurs que gère le Sector Manager.
 * Ses améliorations s'achètent chez le gérant et profitent à tous les joueurs.
 */
namespace ErewhonVillage
{
	struct FUpgradeInfo
	{
		EVillageUpgrade Id = EVillageUpgrade::Count;
		FText Name;
		FText Description;
		int32 Price = 0;
		/** Amélioration à posséder d'abord (Count : aucune). */
		EVillageUpgrade Requires = EVillageUpgrade::Count;
	};

	EREWHON_API const TArray<FUpgradeInfo>& Upgrades();
	EREWHON_API const FUpgradeInfo& Info(EVillageUpgrade Upgrade);

	/** Le secteur fait-il partie du village (dans le rayon de la carte du gérant) ? */
	EREWHON_API bool IsInVillage(const FIntPoint& Sector);

	/** Places assises des bancs de la place (monde) : dessus du siège, regard vers la place. */
	EREWHON_API const TArray<FTransform>& BenchSeats();

	/** Lanterne sur poteau (bWall = false) ou en applique (bWall = true), qui s'allume la nuit. Yaw : direction du bras. */
	EREWHON_API void AddLantern(AActor* Owner, USceneComponent* Parent, const FVector& Location, float Yaw, bool bWall);
}

/**
 * Habillage de la place selon les améliorations achetées : panneaux, lanternes, bordures,
 * fleurs, arbres et bancs. Acteur local, reconstruit quand le village change.
 */
UCLASS()
class EREWHON_API AVillageDecor : public AActor
{
	GENERATED_BODY()

public:
	AVillageDecor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BuildSignposts(bool bTailor, bool bLanterns);
	void BuildLanterns(bool bTailor, bool bSignposts);
	void BuildDecoration();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
};

/** Un banc de la place : on s'y assoit avec E. */
UCLASS()
class EREWHON_API ABench : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	ABench();

	virtual void BeginPlay() override;
	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	/** Index dans ErewhonVillage::BenchSeats(), à régler avant BeginPlay. */
	int32 SeatIndex = INDEX_NONE;

	/** Rotation du modèle généré pour que son assise regarde vers +X. */
	UPROPERTY(EditAnywhere)
	float BenchMeshYaw = 180.f;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
};
