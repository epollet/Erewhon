#include "Core/ErewhonPlayerState.h"

#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"

AErewhonPlayerState::AErewhonPlayerState()
{
	Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
	Backpack = CreateDefaultSubobject<UBackpackComponent>(TEXT("Backpack"));
	// L'inventaire et les pièces changent souvent : fréquence plus haute que le défaut.
	SetNetUpdateFrequency(10.f);
}

void AErewhonPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AErewhonPlayerState, PlayerKey);
	DOREPLIFETIME(AErewhonPlayerState, Coins);
	DOREPLIFETIME(AErewhonPlayerState, PlayerClass);
}

void AErewhonPlayerState::SetCoins(int32 InCoins)
{
	Coins = FMath::Max(0, InCoins);
	OnCoinsChanged.Broadcast();
}

bool AErewhonPlayerState::SpendCoins(int32 Amount)
{
	if (Amount < 0 || Coins < Amount)
	{
		return false;
	}
	SetCoins(Coins - Amount);
	return true;
}

void AErewhonPlayerState::AddCoins(int32 Amount)
{
	SetCoins(Coins + FMath::Max(0, Amount));
}

void AErewhonPlayerState::OnRep_Coins()
{
	OnCoinsChanged.Broadcast();
}
