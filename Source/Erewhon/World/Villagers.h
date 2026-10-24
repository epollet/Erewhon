#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "Villagers.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UAnimSequence;
class ADoor;
struct FClearing;

/**
 * Les habitants du village : trois maisons au bord des routes, juste après la place, et leurs propriétaires,
 * qui vont et viennent chez eux puis sortent se promener (la place, le marché, les routes du village).
 * Et le marché : une grande place de terre au nord-est, avec ses étals.
 * Chaque village a les siens, au même plan (dans son repère, voir ErewhonVillages::VillageFrame). Tout est rendu
 * en coordonnées monde, déduit de la place : le serveur et chaque client le calculent à l'identique.
 */
namespace ErewhonVillagers
{
	constexpr int32 HouseCount = 3;

	struct FResident
	{
		const TCHAR* Character;   // Characters/<nom>/SK_<nom>
		const TCHAR* Name;
		const TCHAR* Trade;
	};
	EREWHON_API const FResident& Resident(int32 Index);

	/** La maison (la porte côté −X du repère, vers la route), son perron, et où son sentier rejoint la route. */
	EREWHON_API FTransform HouseTransform(const FIntPoint& Village, int32 Index);
	EREWHON_API FVector2D Doorstep(const FIntPoint& Village, int32 Index);
	EREWHON_API FVector2D LaneEnd(const FIntPoint& Village, int32 Index);
	/** La route que longe la maison (en monde) : nord (+X), est (+Y), ouest (−Y) du repère du village. */
	EREWHON_API FVector2D RoadDir(const FIntPoint& Village, int32 Index);

	/** Le marché : son centre, son rayon, ses étals (repère : comptoir face à −X, vers le centre). */
	EREWHON_API FVector2D MarketCenter(const FIntPoint& Village);
	constexpr float MarketRadius = 1300.f;
	constexpr int32 StallCount = 6;
	EREWHON_API FTransform StallTransform(const FIntPoint& Village, int32 Index);
	/** Où le chemin du marché quitte la route du nord. */
	EREWHON_API FVector2D MarketJunction(const FIntPoint& Village);
	/** Où l'on entre sur le marché par son chemin (son bord, côté route). */
	EREWHON_API FVector2D MarketEntry(const FIntPoint& Village);

	/** La terre et les sentiers des maisons et du marché, pour le secteur Coord (ajoutés à Clearings et Paths). */
	EREWHON_API void AddGrounds(const FIntPoint& Coord, TArray<FClearing>& Clearings, TArray<FBox2D>& Paths, TArray<bool>& Paved);
}

/**
 * Un habitant : le serveur le fait marcher (mouvement répliqué), chaque machine l'anime
 * (marche quand il avance, attente sinon, un regard de temps en temps). [E] : il vous salue.
 */
UCLASS()
class EREWHON_API AVillager : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	AVillager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(class AErewhonPlayerController* Controller) override;

	/** À régler avant BeginPlay. */
	UPROPERTY(Replicated)
	int32 ResidentIndex = 0;

	/** Son village (sa cellule, voir ErewhonVillages), à régler avant BeginPlay. */
	UPROPERTY(Replicated)
	FIntPoint Village = FIntPoint::ZeroValue;

	/** La porte de sa maison (serveur), qu'il ouvre en passant. */
	TWeakObjectPtr<ADoor> HouseDoor;

private:
	// Serveur : où il va.
	void PlanAtHome();
	void PlanOuting();
	void PlanReturn();
	void StepServer(float DeltaSeconds);
	float GroundZ(const FVector2D& At) const;
	void HandleDoor();

	// Client : l'animation.
	void Play(FName Name, bool bLoop, float Rate = 1.f);
	void StepAnimation(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAnimSequence>> Animations;

	enum class EPlace : uint8 { Home, Out, Returning };
	EPlace Place = EPlace::Home;
	TArray<FVector2D> Route;
	int32 NextPoint = 0;
	float WaitLeft = 0.f;
	float HomeLeft = 0.f;
	int32 OutingStops = 0;
	bool bOpenedDoor = false;

	FName Playing;
	double PlayingUntil = 0.0;
	double NextLook = 0.0;
	FVector LastLocation = FVector::ZeroVector;
	float SmoothedSpeed = 0.f;
	FRandomStream Rand;
};
