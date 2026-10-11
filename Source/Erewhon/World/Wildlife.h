#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Wildlife.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Petite faune d'ambiance, purement visuelle et locale : papillons au-dessus de l'herbe
 * autour du joueur, vols d'oiseaux dans le ciel. Tous se reposent la nuit.
 * Corps et ailes sont construits par le code et animés à chaque image.
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
	/** Un animal : une racine qui se déplace, deux ailes qui battent. */
	struct FCreature
	{
		TObjectPtr<USceneComponent> Body;
		TObjectPtr<USceneComponent> WingLeft;
		TObjectPtr<USceneComponent> WingRight;
		FVector Home = FVector::ZeroVector;   // papillon : centre de sa ronde ; oiseau : place dans le vol
		float Phase = 0.f;
		float Speed = 1.f;
		float Radius = 0.f;
	};

	FCreature MakeCreature(const FLinearColor& Inner, const FLinearColor& Outer, float Span, bool bBird);
	UProceduralMeshComponent* MakeWing(USceneComponent* Parent, const FLinearColor& Inner, const FLinearColor& Outer, float Span, float Side, bool bBird);
	void UpdateButterflies(const FVector& Focus, double Time, float DeltaSeconds);
	void UpdateBirds(const FVector& Focus, double Time);
	float GroundAt(const FVector2D& World) const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WingMaterial;

	TArray<FCreature> Butterflies;
	TArray<FCreature> Birds;

	/** Centre autour duquel tournent les vols ; il suit le joueur par grands pas. */
	FVector BirdAnchor = FVector::ZeroVector;
	bool bVisible = true;
	FRandomStream Rand;
};
