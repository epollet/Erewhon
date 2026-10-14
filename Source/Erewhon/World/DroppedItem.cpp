#include "World/DroppedItem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/ItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "World/ErewhonVisuals.h"

#define LOCTEXT_NAMESPACE "ErewhonDropped"

using namespace ErewhonVisuals;

ADroppedItem::ADroppedItem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	SetReplicatingMovement(true);
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(22.f, 22.f, 16.f));
	Box->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);   // on marche dessus sans buter
	RootComponent = Box;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Box);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ADroppedItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADroppedItem, ItemId);
	DOREPLIFETIME(ADroppedItem, Count);
	DOREPLIFETIME(ADroppedItem, LandOwnerKey);
	DOREPLIFETIME(ADroppedItem, LandOwnerName);
	DOREPLIFETIME(ADroppedItem, ExpiresAt);
}

void ADroppedItem::Setup(FName InItemId, int32 InCount, const FString& InLandOwnerKey, const FString& InLandOwnerName, double InExpiresAt)
{
	ItemId = InItemId;
	Count = InCount;
	LandOwnerKey = InLandOwnerKey;
	LandOwnerName = InLandOwnerName;
	ExpiresAt = InExpiresAt;
}

void ADroppedItem::AddCount(int32 More, double InExpiresAt)
{
	Count += More;
	ExpiresAt = InExpiresAt;
	ForceNetUpdate();
}

void ADroppedItem::Take(int32 Taken)
{
	Count -= Taken;
	if (Count <= 0)
	{
		Destroy();
		return;
	}
	ForceNetUpdate();
}

void ADroppedItem::BeginPlay()
{
	Super::BeginPlay();
	ApplyItem();
}

void ADroppedItem::OnRep_Item()
{
	ApplyItem();
}

void ADroppedItem::ApplyItem()
{
	if (AppliedItem == ItemId || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	AppliedItem = ItemId;
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* Def = Items ? Items->Find(ItemId) : nullptr;
	UStaticMesh* Mesh = Def ? Cast<UStaticMesh>(Def->Mesh.TryLoad()) : nullptr;
	if (!Mesh && Def && Def->Category == EItemCategory::Plant && Def->MeshVariants.Num() > 0)
	{
		Mesh = Cast<UStaticMesh>(Def->MeshVariants[0].TryLoad());
	}
	if (Mesh)
	{
		// Le modèle de l'objet, couché à terre, à taille d'objet qu'on tient.
		Body->SetStaticMesh(Mesh);
		Body->SetRelativeTransform(FitToLength(Mesh, 40.f) * FTransform(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), FVector(0.f, 0.f, -16.f)));
	}
	else
	{
		// Sans modèle : un petit ballot de la couleur de l'objet.
		Body->SetStaticMesh(GetShape(EShape::Sphere));
		Body->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, -6.f), FVector(0.3f, 0.3f, 0.22f)));
		Body->SetMaterial(0, MakeColorMaterial(Body, Def ? Def->Color : FLinearColor::Gray));
	}
}

void ADroppedItem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State)
	{
		return;
	}
	const double Left = ExpiresAt - State->GetWorldClock();
	if (HasAuthority() && Left <= 0.0)
	{
		Destroy();
		return;
	}
}

double ADroppedItem::GetSecondsLeft() const
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	return State ? FMath::Max(0.0, ExpiresAt - State->GetWorldClock()) : 0.0;
}

FString ADroppedItem::GetLabel() const
{
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* Def = Items ? Items->Find(ItemId) : nullptr;
	const int32 Seconds = FMath::CeilToInt32(GetSecondsLeft());
	const FString Name = Def ? Def->Name.ToString() : ItemId.ToString();
	const FString Many = Count > 1 ? FString::Printf(TEXT(" x%d"), Count) : FString();
	return FString::Printf(TEXT("%s%s  %d:%02d"), *Name, *Many, Seconds / 60, Seconds % 60);
}

FText ADroppedItem::GetInteractionLabel() const
{
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* Def = Items ? Items->Find(ItemId) : nullptr;
	const FText Name = Def ? Def->Name : FText::FromName(ItemId);
	const APlayerController* Local = GetWorld()->GetFirstPlayerController();
	const AErewhonPlayerState* State = Local ? Local->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (State && !CanPickUp(State->GetPlayerKey()))
	{
		return FText::Format(LOCTEXT("NotYours", "{0} ×{1} — sur le terrain de {2}"), Name, FText::AsNumber(Count), FText::FromString(LandOwnerName));
	}
	return FText::Format(LOCTEXT("PickUp", "Ramasser : {0} ×{1}"), Name, FText::AsNumber(Count));
}

void ADroppedItem::Interact(AErewhonPlayerController* User)
{
	if (User)
	{
		User->ServerPickUpDropped(this);
	}
}

#undef LOCTEXT_NAMESPACE
