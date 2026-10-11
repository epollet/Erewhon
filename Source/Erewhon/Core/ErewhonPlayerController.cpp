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
#include "UI/SGameMenu.h"
#include "Core/ErewhonUserSettings.h"
#include "World/Door.h"
#include "World/Terrain.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonSettings.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "World/SectorTypes.h"
#include "World/SpawnLayout.h"

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
	Actions.Use = MakeAction(this, TEXT("IA_Use"), EInputActionValueType::Boolean);
	Actions.Crouch = MakeAction(this, TEXT("IA_Crouch"), EInputActionValueType::Boolean);

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
	Context->MapKey(Actions.Use, EKeys::E);
	Context->MapKey(Actions.Crouch, EKeys::LeftControl);
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

	// Premier lancement sur ce poste : un court test matériel choisit la qualité graphique,
	// un cran en dessous de sa recommandation — la prairie dense pèse plus que sa mesure.
	if (UErewhonUserSettings* User = UErewhonUserSettings::Get(); User && User->GetLastCPUBenchmarkResult() < 0.f)
	{
		User->RunHardwareBenchmark();
		User->ApplyHardwareBenchmarkResults();
		User->SetOverallScalabilityLevel(FMath::Max(0, User->GetOverallScalabilityLevel() - 1));
		User->ApplySettings(false);
		User->SaveSettings();
	}
#if !UE_BUILD_SHIPPING
	StartAutoShots();
#endif

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

// ── Captures automatiques (tests visuels sans clavier ni souris) ────────────

#if !UE_BUILD_SHIPPING
void AErewhonPlayerController::StartAutoShots()
{
	// -ErewhonShots=0,180    caps successifs (degrés, 0 = nord)
	// -ErewhonShotPitch=-8   inclinaison du regard
	// -ErewhonShotAt=50,40   position (m, monde) avant les captures
	// -ErewhonShotDelay=35   attente du chargement (s)
	FString Yaws;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ErewhonShots="), Yaws, false))
	{
		return;
	}
	TArray<FString> Parts;
	Yaws.ParseIntoArray(Parts, TEXT(","));
	for (const FString& Part : Parts)
	{
		ShotYaws.Add(FCString::Atof(*Part));
	}
	FParse::Value(FCommandLine::Get(), TEXT("ErewhonShotPitch="), ShotPitch);
	FString At;
	if (FParse::Value(FCommandLine::Get(), TEXT("ErewhonShotAt="), At, false))
	{
		FString X, Y;
		if (At.Split(TEXT(","), &X, &Y))
		{
			ShotLocation = FVector(FCString::Atof(*X) * 100.f, FCString::Atof(*Y) * 100.f, 0.f);
			bShotHasLocation = true;
		}
	}
	float Delay = 35.f;
	FParse::Value(FCommandLine::Get(), TEXT("ErewhonShotDelay="), Delay);

	// -ErewhonOpenMenu=store|inventory|menu|options : ouvre un menu une fois le joueur placé.
	FParse::Value(FCommandLine::Get(), TEXT("ErewhonOpenMenu="), ShotMenu);
	GetWorldTimerManager().SetTimer(ShotTimer, this, &AErewhonPlayerController::NextAutoShot, Delay, false);
}

void AErewhonPlayerController::NextAutoShot()
{
	if (ShotIndex >= ShotYaws.Num() * 2)
	{
		// La dernière capture s'écrit sur disque à l'image suivante : on laisse un peu de marge.
		FTimerHandle ExitTimer;
		GetWorldTimerManager().SetTimer(ExitTimer, FTimerDelegate::CreateLambda([]() { FGenericPlatformMisc::RequestExit(false); }), 3.f, false);
		return;
	}
	const int32 Shot = ShotIndex / 2;
	if (ShotIndex % 2 == 0)
	{
		// Étape 1 : se placer et orienter le regard, puis laisser l'image se stabiliser.
		if (APawn* MyPawn = GetPawn(); MyPawn && bShotHasLocation)
		{
			const AErewhonGameState* State = AErewhonGameState::Get(this);
			const float Ground = State ? ErewhonTerrain::Height(FVector2D(ShotLocation), State->GetWorldSeed()) : 0.f;
			MyPawn->TeleportTo(ShotLocation + FVector(0, 0, Ground + 120.f), FRotator(0, ShotYaws[Shot], 0));
		}
		SetControlRotation(FRotator(ShotPitch, ShotYaws[Shot], 0));
		if (Shot == 0 && !ShotMenu.IsEmpty())
		{
			if (ShotMenu == TEXT("store")) { OpenStore(EShopKind::GeneralStore); }
			else if (ShotMenu == TEXT("sectors")) { OpenStore(EShopKind::SectorManager); }
			else if (ShotMenu == TEXT("tailor")) { OpenStore(EShopKind::Tailor); }
			else if (ShotMenu == TEXT("inventory")) { ToggleInventory(); }
			else
			{
				const TSharedRef<SGameMenu> GameMenu = SNew(SGameMenu).Controller(this);
				if (ShotMenu == TEXT("options"))
				{
					GameMenu->ShowOptionsForTest();
				}
				ShowMenu(GameMenu, false);
			}
		}
	}
	else
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("ErewhonShot_%d"), Shot), true, false);
		// -ErewhonProfile : détail du temps GPU de cette image, écrit dans le log.
		if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonProfile")))
		{
			ConsoleCommand(TEXT("ProfileGPU"));
		}
	}
	++ShotIndex;
	GetWorldTimerManager().SetTimer(ShotTimer, this, &AErewhonPlayerController::NextAutoShot, ShotIndex % 2 == 1 ? 3.f : 1.f, false);
}
#endif

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

void AErewhonPlayerController::HandleEscape()
{
	if (IsMenuOpen())
	{
		CloseMenu();
		return;
	}
	ShowMenu(SNew(SGameMenu).Controller(this), false);
}

void AErewhonPlayerController::OpenStore(EShopKind Kind)
{
	StoreKind = Kind;
	ShowMenu(SNew(SStorePanel).Controller(this).Kind(Kind), true);
}

void AErewhonPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// On ne repart pas avec la boutique ouverte.
	const APawn* MyPawn = GetPawn();
	if (bStoreOpen && OpenMenu.IsValid() && MyPawn
		&& FVector::Dist2D(MyPawn->GetActorLocation(), ErewhonSpawn::MerchantLocation(StoreKind)) > UErewhonSettings::Get().MerchantReach() + 150.f)
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

void AErewhonPlayerController::ServerUseDoor_Implementation(ADoor* Door)
{
	const APawn* MyPawn = GetPawn();
	// Il faut être à portée de main (avec une marge pour la latence).
	if (Door && MyPawn && FVector::Dist(MyPawn->GetActorLocation(), Door->GetActorLocation()) < 450.f)
	{
		Door->Toggle();
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

void AErewhonPlayerController::ServerBuyRoadLink_Implementation(FIntPoint Sector, uint8 Side)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryBuyRoadLink(this, Sector, Side));
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
