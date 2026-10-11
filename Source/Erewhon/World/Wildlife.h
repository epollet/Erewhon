#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Wildlife.generated.h"

class UAudioComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Vie et ambiance sonore, purement locales :
 * - papillons au-dessus de l'herbe autour du joueur ;
 * - oiseaux (modèle généré, ailes animées par le matériau) en vols et en solitaires, qui chantent ;
 * - vent (suit la force du vent du jeu) et bruissement de l'herbe quand on y marche.
 * Les animaux se reposent la nuit.
 */
UCLASS()
class EREWHON_API AWildlife : public AActor
{
	GENERATED_BODY()

public:
	AWildlife();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FCreature
	{
		TObjectPtr<USceneComponent> Body;
		TObjectPtr<USceneComponent> WingLeft;    // papillons et oiseaux de repli
		TObjectPtr<USceneComponent> WingRight;
		TObjectPtr<UStaticMeshComponent> Model;   // oiseau généré
		TObjectPtr<class UMaterialInstanceDynamic> Flap;
		float FlapAmount = 1.f;
		FVector Home = FVector::ZeroVector;       // papillon : centre de sa ronde ; oiseau : place dans le vol
		float Phase = 0.f;
		float Speed = 1.f;
		float Radius = 0.f;
		float Altitude = 0.f;
		int32 Flock = INDEX_NONE;                  // INDEX_NONE : oiseau solitaire
	};

	FCreature MakeCreature(const FLinearColor& Inner, const FLinearColor& Outer, float Span, bool bBird);
	FCreature MakeBird();
	UProceduralMeshComponent* MakeWing(USceneComponent* Parent, const FLinearColor& Inner, const FLinearColor& Outer, float Span, float Side, bool bBird);
	void UpdateButterflies(const FVector& Focus, double Time);
	void UpdateBirds(const FVector& Focus, double Time, float DeltaSeconds);
	void UpdateAmbience(const FVector& Focus, double Clock, float DeltaSeconds, bool bDay);
	float GroundAt(const FVector2D& World) const;

	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WingMaterial;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> BirdMesh;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> WindAudio;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> GrassAudio;

	TArray<FCreature> Butterflies;
	TArray<FCreature> Birds;

	FVector BirdAnchor = FVector::ZeroVector;
	FTransform BirdFit = FTransform::Identity;
	bool bBirdSpanAlongX = false;
	bool bVisible = true;
	float NextChirp = 3.f;
	float GrassLevel = 0.f;
	FRandomStream Rand;
};
