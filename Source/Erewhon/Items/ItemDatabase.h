#pragma once

#include "CoreMinimal.h"
#include "Items/ItemTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ItemDatabase.generated.h"

/**
 * Catalogue des objets, chargé une fois au démarrage depuis Content/Data/Items.json.
 * Le fichier reste du texte versionné : on ajoute un objet sans ouvrir l'éditeur.
 * L'ordre du fichier est significatif — il fixe l'ordre de génération des plantes.
 */
UCLASS()
class EREWHON_API UItemDatabase : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static UItemDatabase* Get(const UObject* WorldContext);

	const FItemDef* Find(FName Id) const;
	const TArray<FItemDef>& GetAll() const { return Items; }

	/** Plantes générées dans les secteurs, dans l'ordre du fichier. */
	const TArray<const FItemDef*>& GetPlants() const { return Plants; }

	/** Objets vendus par le marchand. */
	const TArray<const FItemDef*>& GetPurchasable() const { return Purchasable; }

private:
	void LoadFromJson(const FString& Path);

	TArray<FItemDef> Items;
	TMap<FName, int32> IndexById;
	TArray<const FItemDef*> Plants;
	TArray<const FItemDef*> Purchasable;
};
