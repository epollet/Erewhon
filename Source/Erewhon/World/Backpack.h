#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "Backpack.generated.h"

class UStaticMeshComponent;

/**
 * Modèle du sac à dos, construit en formes simples d'après son icône : toile olive, sangles
 * de cuir, rabat bouclé, deux poches devant, un outil accroché de chaque côté.
 * Repère : base à Z = 0, poches vers +X, bretelles vers −X, largeur sur Y.
 */
namespace ErewhonBackpack
{
	/** Construit le sac sur Owner, sous Parent ; renvoie les deux composants porte-outils. */
	EREWHON_API TArray<UStaticMeshComponent*> Build(AActor* Owner, USceneComponent* Parent, bool bCollision);

	/** Accroche (ou retire) les outils des côtés. */
	EREWHON_API void SetTools(const TArray<UStaticMeshComponent*>& Holders, const TArray<FName>& Tools);
}

/**
 * Un sac à dos posé au sol. Son contenu reste sur le PlayerState de son propriétaire
 * (UBackpackComponent) ; l'acteur ne porte que l'apparence et l'interaction :
 * [E] le remettre sur le dos, [F] l'ouvrir, [G] y ranger tout son inventaire.
 */
UCLASS()
class EREWHON_API ABackpack : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	ABackpack();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	/** Le sac appartient-il au joueur de cette clé ? */
	bool IsOwnedBy(const FString& PlayerKey) const { return !PlayerKey.IsEmpty() && PlayerKey == OwnerKey; }

	// Serveur
	void SetOwnerInfo(const FString& Key, const FString& Name) { OwnerKey = Key; OwnerName = Name; }
	void SetSideTools(const TArray<FName>& Tools);

private:
	UFUNCTION()
	void OnRep_SideTools();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Replicated)
	FString OwnerKey;

	UPROPERTY(Replicated)
	FString OwnerName;

	UPROPERTY(ReplicatedUsing = OnRep_SideTools)
	TArray<FName> SideTools;

	TArray<TWeakObjectPtr<UStaticMeshComponent>> ToolHolders;
};
