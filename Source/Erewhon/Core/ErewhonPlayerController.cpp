#include "Core/ErewhonPlayerController.h"
#include "World/FelledTree.h"
#include "UI/SBhurakPanel.h"
#include "World/DroppedItem.h"
#include "World/Bhurak.h"
#include "Core/ErewhonPlayerState.h"

#include "Core/ErewhonGameMode.h"
#include "Core/ErewhonLoadingScreen.h"
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
#include "UI/SCheatPanel.h"
#include "UI/SReportPanel.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Player/ErewhonCharacter.h"
#include "World/Backpack.h"
#include "World/Storage.h"
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
#include "World/Apothecary.h"
#include "World/ErewhonSound.h"
#include "UI/SApothecaryPanel.h"
#include "UI/SCauldronPanel.h"
#include "UI/SBookPanel.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"

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

namespace
{
	/**
	 * Raccourci de triche en deux temps : Ctrl+Alt+ù, puis Ctrl+Maj+* dans les 5 secondes.
	 * Signalement : AltGr+² (Windows transmet AltGr comme Ctrl+Alt).
	 * Les touches sont reconnues par le caractère qu'elles portent (clavier AZERTY).
	 */
	class FErewhonCheatKeys : public IInputProcessor
	{
	public:
		explicit FErewhonCheatKeys(AErewhonPlayerController* InController) : Controller(InController) {}

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override
		{
			const uint32 Character = Event.GetCharacter();
			const double Now = FPlatformTime::Seconds();
			if (Event.IsControlDown() && Event.IsAltDown() && Character == 0x00B2)   // ²
			{
				if (Controller.IsValid())
				{
					Controller->OpenReport();
				}
				return true;
			}
			if (Event.IsControlDown() && Event.IsAltDown() && Character == 0x00F9)   // ù
			{
				ArmedAt = Now;
				return true;
			}
			if (Event.IsControlDown() && Event.IsShiftDown() && (Character == TEXT('*') || Event.GetKey() == EKeys::Multiply)
				&& Now - ArmedAt < 5.0)
			{
				ArmedAt = -100.0;
				if (Controller.IsValid())
				{
					Controller->OpenCheats();
				}
				return true;
			}
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("ErewhonCheatKeys"); }

	private:
		TWeakObjectPtr<AErewhonPlayerController> Controller;
		double ArmedAt = -100.0;
	};
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
	Actions.Secondary = MakeAction(this, TEXT("IA_Secondary"), EInputActionValueType::Boolean);
	Actions.Inventory = MakeAction(this, TEXT("IA_Inventory"), EInputActionValueType::Boolean);
	Actions.CloseMenu = MakeAction(this, TEXT("IA_CloseMenu"), EInputActionValueType::Boolean);
	Actions.Use = MakeAction(this, TEXT("IA_Use"), EInputActionValueType::Boolean);
	Actions.Crouch = MakeAction(this, TEXT("IA_Crouch"), EInputActionValueType::Boolean);
	Actions.Bag = MakeAction(this, TEXT("IA_Bag"), EInputActionValueType::Boolean);
	Actions.Stash = MakeAction(this, TEXT("IA_Stash"), EInputActionValueType::Boolean);
	Actions.View = MakeAction(this, TEXT("IA_View"), EInputActionValueType::Boolean);
	Actions.Hotbar1 = MakeAction(this, TEXT("IA_Hotbar1"), EInputActionValueType::Boolean);
	Actions.Hotbar2 = MakeAction(this, TEXT("IA_Hotbar2"), EInputActionValueType::Boolean);
	Actions.Hotbar3 = MakeAction(this, TEXT("IA_Hotbar3"), EInputActionValueType::Boolean);
	Actions.HotbarScroll = MakeAction(this, TEXT("IA_HotbarScroll"), EInputActionValueType::Axis1D);

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
	Context->MapKey(Actions.Secondary, EKeys::RightMouseButton);
	Context->MapKey(Actions.Inventory, EKeys::I);
	Context->MapKey(Actions.Inventory, EKeys::Tab);
	Context->MapKey(Actions.CloseMenu, EKeys::Escape);
	Context->MapKey(Actions.Use, EKeys::E);
	Context->MapKey(Actions.Crouch, EKeys::LeftControl);
	Context->MapKey(Actions.Bag, EKeys::F);
	Context->MapKey(Actions.Stash, EKeys::G);
	Context->MapKey(Actions.View, EKeys::V);
	// La rangée des chiffres : en AZERTY, sans Maj, elle donne & é " — on accepte les deux.
	for (const FKey& Key : { EKeys::One, EKeys::NumPadOne, EKeys::Ampersand })
	{
		Context->MapKey(Actions.Hotbar1, Key);
	}
	for (const FKey& Key : { EKeys::Two, EKeys::NumPadTwo, EKeys::E_AccentAigu })
	{
		Context->MapKey(Actions.Hotbar2, Key);
	}
	for (const FKey& Key : { EKeys::Three, EKeys::NumPadThree, EKeys::Quote })
	{
		Context->MapKey(Actions.Hotbar3, Key);
	}
	Context->MapKey(Actions.HotbarScroll, EKeys::MouseWheelAxis);
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
	if (FSlateApplication::IsInitialized())
	{
		CheatKeys = MakeShared<FErewhonCheatKeys>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(CheatKeys);
	}

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
	if (CheatKeys.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(CheatKeys);
		CheatKeys.Reset();
	}
	CloseReport();
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
	// -ErewhonShotVillage=1,0 : sur la place de ce village (sa cellule), un peu au sud de son centre.
	FString VillageArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("ErewhonShotVillage="), VillageArg, false))
	{
		FString X, Y;
		if (VillageArg.Split(TEXT(","), &X, &Y))
		{
			ShotVillage = FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y));
			bShotAtVillage = true;
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
			if (bShotAtVillage)
			{
				// La graine n'est connue qu'une fois le monde chargé : la place se calcule au moment de la capture.
				ShotLocation = ErewhonSector::Center(ErewhonVillages::CenterOf(ShotVillage)) + FVector(-1500.f, 0.f, 0.f);
				ShotLocation.Z = 0.f;
			}
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
			else if (ShotMenu == TEXT("cheats")) { OpenCheats(); }
			else if (ShotMenu == TEXT("village")) { StoreKind = EShopKind::SectorManager; StoreVillage = GetPawn() ? ErewhonSpawn::VillageAt(GetPawn()->GetActorLocation()) : FIntPoint::ZeroValue; ShowMenu(SNew(SStorePanel).Controller(this).Kind(EShopKind::SectorManager).bVillageTab(true), true); }
			else if (ShotMenu == TEXT("inventory")) { ToggleInventory(); }
			else if (ShotMenu == TEXT("split")) { ToggleInventory(); if (OpenMenu.IsValid() || PendingMenu.IsValid()) { StaticCastSharedPtr<SInventoryPanel>(OpenMenu.IsValid() ? OpenMenu : PendingMenu)->TestSplitChooser(); } }
			else if (ShotMenu == TEXT("breeder")) { OpenStore(EShopKind::Breeder); }
			else if (ShotMenu == TEXT("workshop")) { OpenStore(EShopKind::Workshop); }
			else if (ShotMenu == TEXT("bhurak")) { for (TActorIterator<ABhurak> It(GetWorld()); It; ++It) { OpenBhurak(*It); break; } }
			else if (ShotMenu == TEXT("shelves") || ShotMenu == TEXT("mortar") || ShotMenu == TEXT("cauldron") || ShotMenu == TEXT("book"))
			{
				// Le premier meuble de ce genre.
				if (ShotMenu == TEXT("book"))
				{
					OpenBook();
				}
				const EStorageKind Wanted = ShotMenu == TEXT("shelves") ? EStorageKind::Shelf : ShotMenu == TEXT("mortar") ? EStorageKind::Mortar : EStorageKind::Cauldron;
				// Le chaudron le plus rempli d'abord (on peut en avoir plusieurs).
				AStorageActor* Fullest = nullptr;
				for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It)
				{
					if (It->GetKind() == EStorageKind::Cauldron && (!Fullest || It->GetWater() > Fullest->GetWater()))
					{
						Fullest = *It;
					}
				}
				if (Wanted == EStorageKind::Cauldron && Fullest && ShotMenu != TEXT("book"))
				{
					OpenCauldron(Fullest);
				}
				else for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It)
				{
					if (It->GetKind() == Wanted)
					{
						if (ShotMenu == TEXT("book"))
						{
							break;
						}
						Wanted == EStorageKind::Shelf ? OpenStorage(*It) : Wanted == EStorageKind::Cauldron ? OpenCauldron(*It) : OpenApothecary(*It, static_cast<uint8>(ErewhonApothecary::ECraft::Mortar));
						break;
					}
				}
			}
			else if (ShotMenu == TEXT("unstuck"))
			{
				ServerUnstuck();
				if (AErewhonCharacter* Me = GetPawn<AErewhonCharacter>())
				{
					Me->CycleView();
				}
			}
			else if (ShotMenu == TEXT("behind") || ShotMenu == TEXT("front"))
			{
				if (AErewhonCharacter* Me = GetPawn<AErewhonCharacter>())
				{
					Me->CycleView();
					if (ShotMenu == TEXT("front"))
					{
						Me->CycleView();
					}
				}
			}
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
		if (!ReportPanel.IsValid())
		{
			FocusMenu(Widget);
		}
	}));
}

void AErewhonPlayerController::FocusMenu(const TSharedRef<SWidget>& Widget)
{
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(Widget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockInFullscreen);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
}

FString AErewhonPlayerController::DescribeOpenMenu() const
{
	return OpenMenu.IsValid() ? OpenMenu->GetTypeAsString() : TEXT("aucun");
}

void AErewhonPlayerController::OpenReport()
{
	if (IsReportOpen())
	{
		return;
	}
	// À l'image suivante, hors du traitement de la touche : l'écran tel que le joueur le voit, popup exclue.
	bReportPending = true;
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		bReportPending = false;
		UGameViewportClient* Viewport = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient : nullptr;
		if (!Viewport)
		{
			return;
		}
		const TSharedRef<SReportPanel> Panel = SNew(SReportPanel, ErewhonReport::Capture(this)).Controller(this);
		ReportPanel = Panel;
		Viewport->AddViewportWidgetContent(Panel, 100);

		// Rien ne passe au jeu tant que la popup est ouverte : on y tape du texte.
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Panel->GetFocusTarget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockInFullscreen);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
	}));
}

void AErewhonPlayerController::CloseReport()
{
	bReportPending = false;
	if (!ReportPanel.IsValid())
	{
		return;
	}
	if (UGameViewportClient* Viewport = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(ReportPanel.ToSharedRef());
	}
	ReportPanel.Reset();
	if (OpenMenu.IsValid())
	{
		FocusMenu(OpenMenu.ToSharedRef());
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}
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
	if (!ReportPanel.IsValid())   // la popup de signalement garde le clavier
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}
}

void AErewhonPlayerController::ToggleInventory()
{
	if (IsMenuOpen())
	{
		CloseMenu();
		return;
	}
	OpenInventory(nullptr);
}

void AErewhonPlayerController::OpenInventory(ABackpack* Bag)
{
	OpenedBag = Bag;
	OpenedStorage.Reset();
	ShowMenu(SNew(SInventoryPanel).Controller(this).Bag(Bag), false);
}

void AErewhonPlayerController::OpenCart(ACart* Cart)
{
	OpenedCart = Cart;
	ShowMenu(SNew(SInventoryPanel).Controller(this).Cart(Cart), false);
}

void AErewhonPlayerController::ServerMoveCartItem_Implementation(ACart* Cart, bool bFromCart, int32 From, bool bToCart, int32 To)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryMoveCartItem(this, Cart, bFromCart, From, bToCart, To));
	}
}

void AErewhonPlayerController::ServerStashInCart_Implementation(ACart* Cart)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryStashInCart(this, Cart));
	}
}

void AErewhonPlayerController::OpenStorage(AStorageActor* Storage)
{
	OpenedBag.Reset();
	OpenedStorage = Storage;
	ShowMenu(SNew(SInventoryPanel).Controller(this).Storage(Storage), false);
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

void AErewhonPlayerController::OpenCheats()
{
	if (IsMenuOpen())
	{
		CloseMenu();
	}
	ShowMenu(SNew(SCheatPanel).Controller(this), false);
}

void AErewhonPlayerController::OpenStore(EShopKind Kind)
{
	StoreKind = Kind;
	// Le commerce du village où l'on se tient : c'est son étal (ses boutiques bâties) qui s'ouvre.
	StoreVillage = GetPawn() ? ErewhonSpawn::VillageAt(GetPawn()->GetActorLocation()) : FIntPoint::ZeroValue;
	ShowMenu(SNew(SStorePanel).Controller(this).Kind(Kind), true);
}

void AErewhonPlayerController::OpenApothecary(AStorageActor* Station, uint8 Craft)
{
	OpenedStation = Station;
	const TSharedRef<SWidget> Panel = SNew(SApothecaryPanel).Controller(this).Station(Station).Craft(Craft);
	ApothecaryWidget = Panel;
	ShowMenu(Panel, false);
}

void AErewhonPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AErewhonPlayerController, CraftJob, COND_OwnerOnly);
}

void AErewhonPlayerController::SetCraftJob(const FErewhonCraftJob& Job, AStorageActor* Station)
{
	CraftJob = Job;
	CraftStation = Station;
	OnCraftChanged.Broadcast();   // l'hôte d'une partie en écoute ne reçoit pas d'OnRep
}

void AErewhonPlayerController::ClientCraftStep_Implementation(uint8 Craft)
{
	const bool bCauldron = Craft == static_cast<uint8>(ErewhonApothecary::ECraft::Cauldron);
	if (const APawn* MyPawn = GetPawn())
	{
		ErewhonSound::PlayAt(this, ErewhonSound::Load(bCauldron ? TEXT("cauldron_bubble") : TEXT("mortar_grind")),
			MyPawn->GetActorLocation(), bCauldron ? 0.6f : 0.8f, 200.f, 1500.f);
	}
}

void AErewhonPlayerController::ServerBuyApothecary_Implementation(FIntPoint Sector)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryBuyApothecary(this, Sector));
	}
}

void AErewhonPlayerController::ServerFeedFire_Implementation(AApothecary* House)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryFeedFire(this, House));
	}
}

void AErewhonPlayerController::ServerStartCraft_Implementation(AStorageActor* Station, FName Output, int32 Count)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryStartCraft(this, Station, Output, Count));
	}
}

void AErewhonPlayerController::ServerStartPour_Implementation(AStorageActor* Container, uint8 Op, FName Item, int32 Count)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryStartPour(this, Container, Op, Item, Count));
	}
}

void AErewhonPlayerController::ServerAddIngredient_Implementation(AStorageActor* Cauldron, FName ItemId, int32 Count)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryAddIngredient(this, Cauldron, ItemId, Count));
	}
}

void AErewhonPlayerController::ServerAdjustStand_Implementation(AStorageActor* Stand, float Level)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryAdjustStand(this, Stand, Level));
	}
}

void AErewhonPlayerController::ServerChooseClass_Implementation(uint8 NewClass)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryChooseClass(this, NewClass));
	}
}

void AErewhonPlayerController::ServerBuyField_Implementation(FIntPoint Sector, uint8 Which)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryBuyField(this, Sector, Which));
	}
}

void AErewhonPlayerController::ServerWorkField_Implementation(FIntPoint Sector, int32 Cell, uint8 Corner)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryWorkField(this, Sector, Cell, Corner));
	}
}

void AErewhonPlayerController::ServerRideBhurak_Implementation(ABhurak* Bhurak)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryRideBhurak(this, Bhurak));
	}
}

void AErewhonPlayerController::ServerHitchBhurak_Implementation(ABhurak* Bhurak)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryHitchBhurak(this, Bhurak));
	}
}

void AErewhonPlayerController::ServerEquipBhurak_Implementation(ABhurak* Bhurak, uint8 Slot, FName Item)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryEquipBhurak(this, Bhurak, Slot, Item));
	}
}

void AErewhonPlayerController::ServerToggleImplement_Implementation(ACart* Implement)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryToggleImplement(this, Implement));
	}
}

void AErewhonPlayerController::ServerSplitMove_Implementation(bool bFromOther, int32 From, bool bToOther, int32 To, int32 Count, AStorageActor* Storage, ACart* Cart)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TrySplitMove(this, bFromOther, From, bToOther, To, Count, Storage, Cart));
	}
}

void AErewhonPlayerController::ServerPickUpDropped_Implementation(ADroppedItem* Dropped)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryPickUpDropped(this, Dropped));
	}
}

void AErewhonPlayerController::OpenBhurak(ABhurak* Bhurak)
{
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	if (!Bhurak || !State)
	{
		return;
	}
	if (!Bhurak->IsOwnedBy(State->GetPlayerKey()))
	{
		ShowToast(FText::Format(NSLOCTEXT("ErewhonPC", "OthersBhurak", "Le bhurak de {0}."), FText::FromString(Bhurak->GetOwnerName())));
		return;
	}
	OpenedBhurak = Bhurak;
	TSharedRef<SBhurakPanel> Panel = SNew(SBhurakPanel).Controller(this).Bhurak(Bhurak);
	BhurakWidget = Panel;
	ShowMenu(Panel, false);
}

void AErewhonPlayerController::ServerScythe_Implementation()
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryScythe(this));
	}
}

void AErewhonPlayerController::OpenCauldron(AStorageActor* Cauldron)
{
	OpenedStation = Cauldron;
	const TSharedRef<SWidget> Panel = SNew(SCauldronPanel).Controller(this).Cauldron(Cauldron);
	ApothecaryWidget = Panel;
	ShowMenu(Panel, false);
}

void AErewhonPlayerController::OpenBook()
{
	ShowMenu(SNew(SBookPanel).Controller(this), false);
}

void AErewhonPlayerController::ServerEmptyWater_Implementation(AStorageActor* Barrel)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryEmptyWater(this, Barrel));
	}
}

void AErewhonPlayerController::ServerChopTree_Implementation(FIntPoint Sector, int32 TreeIndex)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryChopTree(this, Sector, TreeIndex));
	}
}

void AErewhonPlayerController::ServerChopLog_Implementation(AFelledTree* Felled)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryChopLog(this, Felled));
	}
}

void AErewhonPlayerController::ServerTakeLogs_Implementation(AStorageActor* Pile)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryTakeLogs(this, Pile));
	}
}

void AErewhonPlayerController::ServerUnstuck_Implementation()
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->Unstuck(this));
	}
}

void AErewhonPlayerController::ServerBuyStoreroom_Implementation(FIntPoint Sector)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryBuyStoreroom(this, Sector));
	}
}

void AErewhonPlayerController::ServerStopCraft_Implementation()
{
	GetWorldTimerManager().ClearTimer(CraftTimer);
	SetCraftJob(FErewhonCraftJob(), nullptr);
}

void AErewhonPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// On ne laisse pas le mortier ouvert en sortant de l'apothicairerie.
	if (const APawn* Walker = GetPawn(); Walker && OpenedStation.IsValid() && OpenMenu.IsValid() && OpenMenu == ApothecaryWidget.Pin()
		&& FVector::Dist2D(Walker->GetActorLocation(), OpenedStation->GetActorLocation()) > ErewhonApothecary::UseReach + 150.f)
	{
		OpenedStation.Reset();
		CloseMenu();
	}

	// Le harnachement du bhurak : il se ferme si l'on s'éloigne, ou si on l'a repris.
	if (OpenMenu.IsValid() && OpenMenu == BhurakWidget.Pin())
	{
		const APawn* Groom = GetPawn();
		if (!OpenedBhurak.IsValid() || (Groom && FVector::Dist2D(Groom->GetActorLocation(), OpenedBhurak->GetActorLocation()) > OpenedBhurak->GetHalfLength() + 550.f))
		{
			OpenedBhurak.Reset();
			CloseMenu();
		}
	}

	// On ne repart pas avec la boutique ouverte, ni avec un sac posé derrière soi.
	const APawn* MyPawn = GetPawn();
	const AActor* Opened = OpenedBag.IsValid() ? static_cast<const AActor*>(OpenedBag.Get()) : OpenedStorage.IsValid() ? static_cast<const AActor*>(OpenedStorage.Get()) : OpenedCart.Get();
	if (Opened && OpenMenu.IsValid() && !bStoreOpen && MyPawn
		&& FVector::Dist(MyPawn->GetActorLocation(), Opened->GetActorLocation()) > 450.f)
	{
		OpenedBag.Reset();
		OpenedStorage.Reset();
		OpenedCart.Reset();
		CloseMenu();
	}
	if (bStoreOpen && OpenMenu.IsValid() && MyPawn
		&& FVector::Dist2D(MyPawn->GetActorLocation(), ErewhonSpawn::MerchantLocation(StoreVillage, StoreKind)) > UErewhonSettings::Get().MerchantReach() + 150.f)
	{
		CloseMenu();
	}
}

bool AErewhonPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// Écran de chargement : on ne marche pas dans un monde qui se construit. Le signalement
	// (AltGr+²) passe avant, par FErewhonCheatKeys.
	if (UErewhonLoadingScreen::IsBlockingInput(this))
	{
		return true;
	}
	return Super::InputKey(Params);
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

void AErewhonPlayerController::ServerDropBackpack_Implementation()
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryDropBackpack(this));
	}
}

void AErewhonPlayerController::ServerPickUpBackpack_Implementation(ABackpack* Backpack)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryPickUpBackpack(this, Backpack));
	}
}

void AErewhonPlayerController::ServerMoveItem_Implementation(bool bFromBag, int32 From, bool bToBag, int32 To)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryMoveItem(this, bFromBag, From, bToBag, To));
	}
}

void AErewhonPlayerController::ServerDiscard_Implementation(bool bFromBag, int32 SlotIndex, AStorageActor* Storage)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryDiscard(this, bFromBag, SlotIndex, Storage));
	}
}

void AErewhonPlayerController::ServerPickUpStorage_Implementation(AStorageActor* Storage)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryPickUpStorage(this, Storage));
	}
}

void AErewhonPlayerController::ServerPlace_Implementation(uint8 Type, FVector Location, float Yaw, AActor* Target, int32 Slot)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryPlace(this, Type, Location, Yaw, Target, Slot));
	}
}

void AErewhonPlayerController::ServerToggleDrive_Implementation(ACart* Cart)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryToggleDrive(this, Cart));
	}
}

void AErewhonPlayerController::ServerMoveStorageItem_Implementation(AStorageActor* Storage, bool bFromStorage, int32 From, bool bToStorage, int32 To)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryMoveStorageItem(this, Storage, bFromStorage, From, bToStorage, To));
	}
}

void AErewhonPlayerController::ServerStashInStorage_Implementation(AStorageActor* Storage)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryStashInStorage(this, Storage));
	}
}

void AErewhonPlayerController::ServerBuyVillageUpgrade_Implementation(uint8 Upgrade)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryBuyVillageUpgrade(this, Upgrade));
	}
}

void AErewhonPlayerController::ServerBuyPavedLink_Implementation(FIntPoint Sector, uint8 Side)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryBuyPavedLink(this, Sector, Side));
	}
}

void AErewhonPlayerController::ServerCheatSetCoins_Implementation(int32 Coins)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryCheatSetCoins(this, Coins));
	}
}

void AErewhonPlayerController::ServerCheatAdvanceTime_Implementation(float Seconds)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryCheatAdvanceTime(this, Seconds));
	}
}

void AErewhonPlayerController::ServerCheatSetWeather_Implementation(uint8 Weather)
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryCheatSetWeather(this, Weather));
	}
}

void AErewhonPlayerController::ServerStashAll_Implementation()
{
	if (AErewhonGameMode* Mode = GetWorld()->GetAuthGameMode<AErewhonGameMode>())
	{
		ReportResult(Mode->TryStashAll(this));
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
