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

	/** La classe choisie à la mairie (EPlayerClass). */
	UPROPERTY()
	uint8 PlayerClass = 0;
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

	UPROPERTY()
	uint8 PavedLinks = 0;

	/** Bâtiments (ESectorBuilding) et, pour l'apothicairerie, l'horloge du monde jusqu'à laquelle son feu brûle. */
	UPROPERTY()
	uint8 Buildings = 0;

	UPROPERTY()
	double FireUntil = 0.0;

	/** Les potions rangées sur ses étagères. */
	UPROPERTY()
	TArray<FInventorySlot> Shelves;
};

/** Une charrette posée dans le monde. */
/** Un bhurak, son harnachement et le contenu de ses sacoches (12 places par place du harnais). */
USTRUCT()
struct FSavedBhurak
{
	GENERATED_BODY()

	UPROPERTY() FString OwnerKey;
	UPROPERTY() FString OwnerName;
	UPROPERTY() FTransform Transform;
	UPROPERTY() bool bHarness = false;
	UPROPERTY() uint8 Coat = 0;
	UPROPERTY() TArray<FName> Gear;
	UPROPERTY() TArray<FInventorySlot> BagSlots;
};

/** Une case de champ travaillée (voir FFieldCellEntry). */
USTRUCT()
struct FSavedField
{
	GENERATED_BODY()

	UPROPERTY() FIntPoint Sector = FIntPoint::ZeroValue;
	UPROPERTY() int32 Cell = INDEX_NONE;
	UPROPERTY() uint8 Crop = 0;
	UPROPERTY() double SownAt = 0.0;
};

USTRUCT()
struct FSavedCart
{
	GENERATED_BODY()

	UPROPERTY() FString OwnerKey;
	UPROPERTY() FString OwnerName;
	UPROPERTY() FTransform Transform;
	/** Le petit bois en vrac. */
	UPROPERTY() TArray<FInventorySlot> Wood;
	/** Charrette, cultivateur ou semoir (ECartKind), et son levier. */
	UPROPERTY() uint8 Kind = 0;
	UPROPERTY() bool bEngaged = false;
};

/** Une cagette ou un tonneau, posé au sol, sur une pile ou dans une charrette. */
USTRUCT()
struct FSavedStorage
{
	GENERATED_BODY()

	UPROPERTY() uint8 Kind = 0;
	UPROPERTY() FString OwnerKey;
	UPROPERTY() FString OwnerName;
	UPROPERTY() FTransform Transform;
	UPROPERTY() TArray<FInventorySlot> Slots;
	/** Index dans Carts (−1 : pas dans une charrette) et emplacement. */
	UPROPERTY() int32 Cart = INDEX_NONE;
	UPROPERTY() int32 CartSlot = INDEX_NONE;
	/** Index (dans Storages, toujours plus petit) de la cagette sur laquelle elle est posée. */
	UPROPERTY() int32 Below = INDEX_NONE;
	/** Séchoir : le début du séchage de chaque place ; pot-pourri : l'heure où il s'éteint (horloge du monde). */
	UPROPERTY() TArray<double> SlotTimes;
	UPROPERTY() double ExpiresAt = 0.0;
	/** Tonneau, chaudron : le liquide et ses litres ; chaudron : sa température ; support : sa hauteur. */
	UPROPERTY() float Water = 0.f;
	UPROPERTY() FName Liquid;
	UPROPERTY() float Temperature = 18.f;
	UPROPERTY() float Level = 0.5f;
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
	TArray<FSavedCart> Carts;

	UPROPERTY()
	TArray<FSavedStorage> Storages;

	/** Améliorations du village (un bit par EVillageUpgrade). */
	UPROPERTY()
	int32 VillageUpgrades = 0;

	UPROPERTY()
	TArray<FSavedHarvest> Harvests;

	UPROPERTY()
	TArray<FSavedField> Fields;

	UPROPERTY()
	TArray<FSavedBhurak> Bhuraks;

	UPROPERTY()
	TMap<FString, FSavedPlayer> Players;
};
