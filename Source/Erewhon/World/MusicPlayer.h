#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusicPlayer.generated.h"

class UAudioComponent;
class USoundWave;

/**
 * Musique d'ambiance, purement locale : quelques secondes après l'arrivée en jeu, un morceau
 * tiré au hasard (jamais deux fois le même de suite) monte en fondu, s'éteint en fondu à sa
 * fin, puis un silence de durée variable précède le suivant.
 */
UCLASS()
class EREWHON_API AMusicPlayer : public AActor
{
	GENERATED_BODY()

public:
	AMusicPlayer();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void StartTrack(double Now);
	float TargetVolume() const;

	UPROPERTY(Transient) TObjectPtr<UAudioComponent> Audio;
	UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> Tracks;

	int32 LastTrack = INDEX_NONE;
	double NextStart = 0.0;     // en attente : heure de départ du prochain morceau
	double TrackEnd = 0.0;      // en lecture : heure de fin du morceau
	bool bPlaying = false;
	bool bFadingOut = false;
	float AppliedVolume = -1.f;
	FRandomStream Rand;
};
