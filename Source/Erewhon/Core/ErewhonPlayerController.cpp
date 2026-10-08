#include "Core/ErewhonPlayerController.h"

#include "Core/ErewhonGameMode.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/PlatformProcess.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/SInventoryPanel.h"
#include "UI/SStorePanel.h"
#include "Core/ErewhonSettings.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "World/SectorTypes.h"

#define LOCTEXT_NAMESPACE "ErewhonPlayerController"

namespace
{
	constexpr double GToastSeconds = 3.5;

	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = Type;
		return Action;
	}

	/** Associe une touche à un axe 2D : Swizzle envoie la valeur sur Y, Negate l'inverse. */
	void MapAxisKey(UInputMappingContext* Context, UInputAction* Action, const FKey& Key, bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (bSwizzle)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	}
}

const FErewhonInputActions& AErewhonPlayerController::GetInputActions()
{
	if (Actions.Context)
	{
		return Actions;
	}

	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Erewhon"));
	Actions.Context = Context;
	Actions.Move = MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	Actions.Look = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	Actions.Jump = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	Actions.Run = MakeAction(this, TEXT("IA_Run"), EInputActionValueType::Boolean);
	Actions.Primary = MakeAction(this, TEXT("IA_Primary"), EInputActionValueType::Boolean);
	Actions.Inventory = MakeAction(this, TEXT("IA_Inventory"), EInputActionValueType::Boolean);
	Actions.CloseMenu = MakeAction(this, TEXT("IA_CloseMenu"), EInputActionValueType::Boolean);

	// ZQSD (AZERTY) et WASD (QWERTY) coexistent sans conflit : Z/W avancent, Q/A vont à gauche.
	for (const FKey& Key : { EKeys::Z, EKeys::W, EKeys::Up })
	{
		MapAxisKey(Context, Actions.Move, Key, true, false);
	}
	for (const FKey& Key : { EKeys::S, EKeys::Down })
	{
		MapAxisKey(Context, Actions.Move, Key, true, true);
	}
	for (const FKey& Key : { EKeys::D, EKeys::Right })
	{
		MapAxisKey(Context, Actions.Move, Key, false, false);
	}
	for (const FKey& Key : { EKeys::Q, EKeys::A, EKeys::Left })
	{
		MapAxisKey(Context, Actions.Move, Key, false, true);
	}

	// Souris : Y inversé pour que pousser la souris lève le regard.
	FEnhancedActionKeyMapping& LookMapping = Context->MapKey(Actions.Look, EKeys::Mouse2D);
	UInputModifierNegate* InvertY = NewObject<UInputModifierNegate>(Context);
	InvertY->bX = false;
	InvertY->bZ = false;
	LookMapping.Modifiers.Add(InvertY);

	Context->MapKey(Actions.Jump, EKeys::SpaceBar);
	Context->MapKey(Actions.Run, EKeys::LeftShift);
	Context->MapKey(Actions.Primary, EKeys::LeftMouseButton);
	Context->MapKey(Actions.Inventory, EKeys::I);
	Context->MapKey(Actions.Inventory, EKeys::Tab);
	Context->MapKey(Actions.CloseMenu, EKeys::Escape);
	return Actions;
}

void AErewhonPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Input->AddMappingContext(GetInputActions().Context, 0);
	}
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);

	FString Name = FPlatformProcess::UserName();
	const int32 PieInstance = GetOutermost()->GetPIEInstanceID();
	if (PieInstance != INDEX_NONE)
	{
		Name += FString::Printf(TEXT(" %d"), PieInstance);
	}
	ServerRegisterPlayer(LoadOrCreatePlayerKey(), Name);
}

void AErewhonPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseMenu();
	Super::EndPlay(EndPlayReason);
}

FString AErewhonPlayerController::LoadOrCreatePlayerKey() const
{
	// Une clé par poste, comme le player.json de 3DSimEco ; une variante par client PIE.
	const FString Path = FPaths::ProjectSavedDir() / TEXT("PlayerKey.txt");
	FString Key;
	if (!FFileHelper::LoadFileToString(Key, *Path) || Key.TrimStartAndEnd().IsEmpty())
	{
		Key = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		FFileHelper::SaveStringToFile(Key, *Path);
	}
	Key.TrimStartAndEndInline();

	const int32 PieInstance = GetOutermost()->GetPIEInstanceID();
	if (PieInstance != INDEX_NONE)
	{
		Key += FString::Printf(TEXT("_pie%d"), PieInstance);
	}
	return Key;
}

// ── Menus ────────────────────────────────────────────────────────────────────

void AErewhonPlayerController::ShowMenu(const TSharedRef<SWidget>& Widget, bool bIsStore)
{
	// Le menu s'ouvre souvent pendant un clic : appliqué tout de suite, le viewport
	// reprendrait la capture de la souris et le focus clavier en fin d'événement.
	PendingMenu = Widget;
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Widget, bIsStore]()
	{
		if (PendingMenu != Widget)
		{
			return;   // remplacé ou annulé entre-temps
		}
		PendingMenu.Reset();
		UGameViewportClient* Viewport = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient : nullptr;
		if (!Viewport)
		{
			return;
		}
		CloseMenu();
		OpenMenu = Widget;
		bStoreOpen = bIsStore;
		Viewport->AddViewportWidgetContent(Widget, 10);

		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(Widget);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockInFullscreen);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
	}));
}

void AErewhonPlayerController::CloseMenu()
{
	PendingMenu.Reset();
	if (!OpenMenu.IsValid())
	{
		return;
	}
	if (UGameViewportClient* Viewport = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(OpenMenu.ToSharedRef());
	}
	OpenMenu.Reset();
	bStoreOpen = false;
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
}

void AErewhonPlayerController::ToggleInventory()
{
	if (IsMenuOpen())
	{
		CloseMenu();
		return;
	}
	ShowMenu(SNew(SInventoryPanel).Controller(this), false);
}

void AErewhonPlayerController::OpenStore()
{
	ShowMenu(SNew(SStorePanel).Controller(this), true);
}

void AErewhonPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// On ne repart pas avec la boutique ouverte.
	const APawn* MyPawn = GetPawn();
	if (bStoreOpen && OpenMenu.IsValid() && MyPawn
		&& FVector::Dist2D(MyPawn->GetActorLocation(), ErewhonSector::MerchantLocation()) > UErewhonSettings::Get().MerchantReach() + 150.f)
	{
		CloseMenu();
	}
}

// ── Messages ─────────────────────────────────────────────────────────────────

void AErewhonPlayerController::ShowToast(const FText& Text)
{
	const double Now = GetWorld()->GetRealTimeSeconds();
	Toasts.RemoveAll([Now](const FErewhonToast& Toast) { return Toast.ExpireTime <= Now; });
	Toasts.Add({ Text, Now + GToastSeconds });
	if (Toasts.Num() > 4)
	{
		Toasts.RemoveAt(0);
	}
}

void AErewhonPlayerController::ClientToast_Implementation(const FText& Text)
{
	ShowToast(Text);
}

void AErewhonPlayerController::ReportResult(const FText& Error)
{
	if (!Error.IsEmpty())
	{
		ClientToast(Error);
	}
}

// ── RPC serveur ──────────────────────────────────────────────────────────────

void AErewhonPlayerController::ServerRegisterPlayer_Implementation(const FString& PlayerKey, const FString& DisplayName)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		Mode->RegisterPlayer(this, PlayerKey, DisplayName);
	}
}

void AErewhonPlayerController::ServerHarvest_Implementation(FIntPoint Sector, int32 PlantIndex)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryHarvest(this, Sector, PlantIndex));
	}
}

void AErewhonPlayerController::ServerBuySector_Implementation(FIntPoint Sector)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryBuySector(this, Sector));
	}
}

void AErewhonPlayerController::ServerSellSector_Implementation(FIntPoint Sector)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TrySellSector(this, Sector));
	}
}

void AErewhonPlayerController::ServerBuyItem_Implementation(FName ItemId)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryBuyItem(this, ItemId));
	}
}

void AErewhonPlayerController::ServerSellItem_Implementation(FName ItemId, int32 Count)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TrySellItem(this, ItemId, FMath::Clamp(Count, 1, 999)));
	}
}

#undef LOCTEXT_NAMESPACE
