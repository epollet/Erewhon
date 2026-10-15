#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeatherEffects.generated.h"

class UAudioComponent;
class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;

/**
 * Ce que la météo fait autour du joueur (acteur local, sur chaque machine qui affiche) :
 * la pluie qui tombe autour de la caméra, les éclairs et le tonnerre, le bruit de la pluie et du vent,
 * et ce que le vent emporte : des brins d'herbe dans la prairie, des feuilles en lisière et en forêt.
 */
UCLASS()
class EREWHON_API AWeatherEffects : public AActor
{
	GENERATED_BODY()

public:
	AWeatherEffects();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FParticle
	{
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FRotator Spin = FRotator::ZeroRotator;
		float Life = 0.f;
		bool bAlive = false;
	};

	void UpdateRain(float DeltaSeconds, const FVector& Eye, float Rain, const FVector2D& Wind, bool bSheltered);
	void UpdateDebris(float DeltaSeconds, const FVector& Eye, const FVector2D& Wind, float WindForce);
	void UpdateLightning(float DeltaSeconds, const FVector& Eye, float PerMinute);
	void UpdateAudio(float DeltaSeconds, float Rain, float WindForce, bool bSheltered);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Drops;
	/** Trois teintes de feuilles et de brins (vert, jaune, brun). */
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> Debris;
	UPROPERTY(Transient) TObjectPtr<UDirectionalLightComponent> Flash;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> RainLight;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> RainHeavy;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> WindStrong;

	TArray<FParticle> RainDrops;
	TArray<FParticle> Bits;
	TArray<FTransform> Scratch;
	FRandomStream Rand;

	/** L'éclair en cours (s écoulées, -1 : aucun), et le tonnerre qui le suit. */
	float FlashTime = -1.f;
	float ThunderDelay = -1.f;
	int32 ThunderSound = 0;
	float ThunderVolume = 1.f;
	float NextStrikeCheck = 0.f;
};
