#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Inventory/InventoryComponent.h"
#include "ErewhonWorldSave.generated.h"

USTRUCT()
struct FSavedPlayer
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	int32 Coins = 0;

	UPROPERTY()
	TArray<FInventorySlot> Slots;

	UPROPERTY()
	bool bHasLocation = false;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.f;

	/** Sac à dos : possédé, porté ou posé (et où), son contenu. */
	UPROPERTY()
	bool bHasBackpack = false;

	UPROPERTY()
	bool bBackpackWorn = true;

	UPROPERTY()
	FTransform BackpackTransform;

	UPROPERTY()
	TArray<FInventorySlot> BackpackSlots;
};

USTRUCT()
struct FSavedHarvest
{
	GENERATED_BODY()

	UPROPERTY()
	FIntPoint Sector = FIntPoint::ZeroValue;

	UPROPERTY()
	int32 PlantIndex = INDEX_NONE;

	UPROPERTY()
	double HarvestTime = 0.0;
};

USTRUCT()
struct FSavedSectorOwner
{
	GENERATED_BODY()

	UPROPERTY()
	FIntPoint Sector = FIntPoint::ZeroValue;

	UPROPERTY()
	FString OwnerKey;

	UPROPERTY()
	FString OwnerName;

	UPROPERTY()
	uint8 RoadLinks = 0;
};

/** Sauvegarde serveur du monde (Saved/SaveGames/<WorldSaveSlot>.sav). */
UCLASS()
class EREWHON_API UErewhonWorldSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 WorldSeed = 0;

	UPROPERTY()
	double WorldAge = 0.0;

	UPROPERTY()
	TArray<FSavedSectorOwner> SectorOwners;

	UPROPERTY()
	TArray<FSavedHarvest> Harvests;

	UPROPERTY()
	TMap<FString, FSavedPlayer> Players;
};
