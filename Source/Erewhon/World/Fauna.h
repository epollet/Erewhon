#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/SectorTypes.h"
#include "Fauna.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;

/**
 * La faune au sol (locale, sur chaque machine qui affiche, comme les oiseaux) : renards, et bientôt rongeurs,
 * sangliers et biches. Squelettes et animations faits par Tools/assetgen/quadruped_rig.py (Blender).
 * Chacun flâne, s'arrête pour flairer ou brouter, et s'enfuit au trot si l'on approche.
 */
namespace ErewhonFauna
{
	struct FSpecies
	{
		const TCHAR* Character;        // Characters/<nom>/SK_<nom>, A_<nom>_<geste>
		float WalkSpeed;               // cm/s
		float TrotSpeed;
		float FleeDistance;            // cm : à moins, il s'enfuit
		int32 PerSector[5];            // combien par secteur, selon son type (Spawn, Prairie, Road, Edge, Forest)
		const TCHAR* Graze;            // le geste de pause au sol : « sniff » ou « graze »
		const TCHAR* Call;             // son cri, de temps en temps
		float Scale = 1.f;             // le rongeur est riggé dix fois trop grand (voir quadruped_rig.py)
	};
	EREWHON_API const TArray<FSpecies>& Species();
}

/** Un animal : il marche d'un point à l'autre, s'arrête, flaire, regarde, et fuit le joueur. */
UCLASS()
class EREWHON_API AFaunaAnimal : public AActor
{
	GENERATED_BODY()

public:
	AFaunaAnimal();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** À régler avant BeginPlay. */
	int32 SpeciesIndex = 0;
	FIntPoint HomeSector = FIntPoint::ZeroValue;
	/** Tests : il ne fuit pas le joueur. */
	bool bTame = false;

private:
	void Play(FName Name, bool bLoop, float Rate = 1.f);
	void PickTarget(bool bFlee, const FVector& Threat);
	float GroundAt(const FVector2D& At) const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UAnimSequence>> Animations;

	FVector2D Target = FVector2D::ZeroVector;
	float WaitLeft = 0.f;
	float Speed = 0.f;
	bool bFleeing = false;
	FName Playing;
	double PlayingUntil = 0.0;
	float NextCall = 0.f;
	FRandomStream Rand;
};

/**
 * Une buse : elle plane en grands cercles au-dessus du joueur, crie, et de temps en temps fond sur une proie
 * au sol avant de remonter en battant des ailes. Ses ailes sont animées par le matériau (M_BirdFlap).
 */
UCLASS()
class EREWHON_API ABuzzard : public AActor
{
	GENERATED_BODY()

public:
	ABuzzard();
	virtual void BeginPlay() override;

	/** Tests : elle tourne bas et près, sans piquer. */
	bool bTest = false;
	virtual void Tick(float DeltaSeconds) override;

private:
	float GroundAt(const FVector2D& At) const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TObjectPtr<class UStaticMeshComponent> Model;
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> Flap;

	enum class EMode : uint8 { Soar, Dive, Climb };
	EMode Mode = EMode::Soar;
	FVector2D Center = FVector2D::ZeroVector;
	float Angle = 0.f;
	float Radius = 3000.f;
	float Altitude = 4500.f;
	float FlapAmount = 0.1f;
	float NextCry = 8.f;
	float NextDive = 50.f;
	float NextFlaps = 6.f;
	float FlapBurst = 0.f;
	float Pause = 0.f;
	FVector Prey = FVector::ZeroVector;
	/** Part à l'abri du mauvais temps (0 → 1), lissée ; négative avant la première image. Direction de sa fuite. */
	float Shelter = -1.f;
	FVector2D Away = FVector2D(1.f, 0.f);
	FRandomStream Rand;
};

/** Fait vivre la faune autour du joueur : apparitions dans les secteurs proches, départs au loin. */
UCLASS()
class EREWHON_API AFaunaManager : public AActor
{
	GENERATED_BODY()

public:
	AFaunaManager();
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Les animaux de chaque secteur proche (secteur → animaux). */
	TMap<FIntPoint, TArray<TWeakObjectPtr<AFaunaAnimal>>> Populated;
	float RefreshTimer = 0.f;
	bool bTestSpawned = false;
	TWeakObjectPtr<ABuzzard> Buzzard;
};
