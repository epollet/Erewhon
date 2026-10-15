#include "UI/SCauldronPanel.h"

#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "Styling/SlateTypes.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "World/Apothecary.h"
#include "World/Storage.h"

#define LOCTEXT_NAMESPACE "ErewhonCauldron"

using namespace ErewhonUI;
using namespace ErewhonApothecary;

namespace
{
	constexpr float GCauldronPanelWidth = 760.f;

	const FProgressBarStyle& GaugeStyle(const FLinearColor& Fill)
	{
		static TMap<uint32, TUniquePtr<FProgressBarStyle>> Styles;
		const uint32 Key = GetTypeHash(Fill.ToFColor(true));
		if (const TUniquePtr<FProgressBarStyle>* Found = Styles.Find(Key))
		{
			return **Found;
		}
		FProgressBarStyle Bar;
		Bar.SetBackgroundImage(*Rounded(Mix(Ink, Card, 0.25f), 6.f, Ink, 1.f));
		Bar.SetFillImage(*Rounded(Fill, 6.f));
		Bar.SetMarqueeImage(*Rounded(Fill, 6.f));
		return *Styles.Add(Key, MakeUnique<FProgressBarStyle>(Bar));
	}

	TSharedRef<SWidget> Gauge(const FLinearColor& Fill, TFunction<TOptional<float>()> Percent)
	{
		return SNew(SBox).HeightOverride(16.f)
			[
				SNew(SProgressBar).Style(&GaugeStyle(Fill)).Percent_Lambda(MoveTemp(Percent))
			];
	}

	int32 CauldronCountOwned(const AErewhonPlayerState* State, FName ItemId)
	{
		return State->GetInventory()->CountStored(ItemId) + State->GetBackpack()->CountStored(ItemId);
	}

	TSharedRef<SWidget> Line(const FText& Label, TSharedRef<SWidget> Content)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SBox).WidthOverride(120.f)[SNew(STextBlock).Text(Label).Font(Font(11, true)).ColorAndOpacity(Ink)]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Content];
	}
}

void SCauldronPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	Cauldron = InArgs._Cauldron;
	if (AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr)
	{
		State->GetInventory()->OnInventoryChanged.AddSP(this, &SCauldronPanel::Rebuild);
		State->GetBackpack()->OnBackpackChanged.AddSP(this, &SCauldronPanel::Rebuild);
	}
	if (Controller.IsValid())
	{
		Controller->OnCraftChanged.AddSP(this, &SCauldronPanel::Rebuild);
	}
	if (Cauldron.IsValid())
	{
		Cauldron->OnStorageChanged.AddSP(this, &SCauldronPanel::Rebuild);
		AStorageActor* Stand = nullptr;
		if (AApothecary* House = HouseOfHungCauldron(Cauldron.Get(), &Stand))
		{
			House->OnFireChanged.AddSP(this, &SCauldronPanel::Rebuild);
		}
		if (Stand)
		{
			Stand->OnStorageChanged.AddSP(this, &SCauldronPanel::Rebuild);
		}
	}
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(LOCTEXT("Title", "Le chaudron"), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			GCauldronPanelWidth, LOCTEXT("Subtitle", "De l'eau (pichet en main : clic gauche puiser, clic droit verser), les bons ingrédients, la bonne chaleur…"))
	];
	Rebuild();
}

FReply SCauldronPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if ((InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F) && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SCauldronPanel::BuildGauges() const
{
	// Le feu, la hauteur du support, la température qui en découle.
	AStorageActor* Stand = nullptr;
	AApothecary* House = HouseOfHungCauldron(Cauldron.Get(), &Stand);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TWeakObjectPtr<AApothecary> WeakHouse = House;
	TWeakObjectPtr<AStorageActor> WeakStand = Stand;
	TWeakObjectPtr<AStorageActor> WeakCauldron = Cauldron;
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	const int32 Carried = State ? CauldronCountOwned(State, TEXT("firewood")) : 0;
	const int32 Racked = ErewhonApothecary::WoodNearFire(HouseOfHungCauldron(Cauldron.Get()));
	const int32 Wood = Carried + Racked;

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (!House || !Stand)
	{
		Box->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(LOCTEXT("NotHung", "Le chaudron doit pendre à un support de chaudron posé dans la cheminée de l'apothicairerie."))
			.Font(Font(11, true)).ColorAndOpacity(Danger).AutoWrapText(true)
		];
	}
	else
	{
		const bool bLit = House->IsFireLit();
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Line(LOCTEXT("Fire", "Le feu"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text_Lambda([WeakHouse]()
					{
						const AApothecary* Current = WeakHouse.Get();
						return Current && Current->IsFireLit()
							? FText::Format(LOCTEXT("FireLit", "il brûle (encore {0} min)"), FText::AsNumber(FMath::CeilToInt32(Current->FireRemaining() / 60.0)))
							: LOCTEXT("FireOut", "éteint");
					}).Font(Font(11)).ColorAndOpacity(bLit ? Clay : MutedInk)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Button(Racked > 0
							? FText::Format(bLit ? LOCTEXT("AddWoodRack", "Remettre du bois ({0} + {1} au rack)") : LOCTEXT("LightWoodRack", "Allumer ({0} + {1} au rack)"), FText::AsNumber(Carried), FText::AsNumber(Racked))
							: FText::Format(bLit ? LOCTEXT("AddWood", "Remettre du bois ({0})") : LOCTEXT("LightWood", "Allumer ({0} petit bois)"), FText::AsNumber(Wood)),
						[WeakController, WeakHouse]() { if (WeakController.IsValid()) { WeakController->ServerFeedFire(WeakHouse.Get()); } },
						bLit ? EButton::Secondary : EButton::Primary, Wood > 0, 0.f, 11)
				])
		];
		// La hauteur du support : plus bas, plus chaud.
		const float Level = Stand->GetLevel();
		const auto SetLevel = [WeakController, WeakStand](float Delta)
		{
			if (WeakController.IsValid() && WeakStand.IsValid())
			{
				WeakController->ServerAdjustStand(WeakStand.Get(), FMath::Clamp(WeakStand->GetLevel() + Delta, 0.f, 1.f));
			}
		};
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Line(LOCTEXT("Height", "Hauteur"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					Button(LOCTEXT("Lower", "▼ Descendre"), [SetLevel]() { SetLevel(-0.1f); }, EButton::Secondary, Level > 0.001f, 0.f, 11)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					Button(LOCTEXT("Raise", "▲ Monter"), [SetLevel]() { SetLevel(0.1f); }, EButton::Secondary, Level < 0.999f, 0.f, 11)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("HeightValue", "cran {0} / 10"), FText::AsNumber(FMath::RoundToInt32(Level * 10.f))))
					.Font(Font(10)).ColorAndOpacity(MutedInk)
				])
		];
	}
	// La température, en direct (0 à 120 °C).
	Box->AddSlot().AutoHeight()
	[
		Line(LOCTEXT("Temperature", "Température"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				Gauge(Clay, [WeakCauldron]() -> TOptional<float> { return WeakCauldron.IsValid() ? FMath::Clamp(WeakCauldron->GetTemperature() / 120.f, 0.f, 1.f) : 0.f; })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(64.f)
				[
					SNew(STextBlock).Text_Lambda([WeakCauldron]()
					{
						return FText::Format(LOCTEXT("Degrees", "{0} °C"), FText::AsNumber(WeakCauldron.IsValid() ? FMath::RoundToInt32(WeakCauldron->GetTemperature()) : 0));
					}).Font(Font(12, true)).ColorAndOpacity(Ink)
				]
			])
	];
	return CardBox(Box, 10.f);
}

TSharedRef<SWidget> SCauldronPanel::BuildLiquid() const
{
	// Ce qu'il contient, et de quoi remplir, vider, mettre en fiole.
	const AStorageActor* Pot = Cauldron.Get();
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TWeakObjectPtr<AStorageActor> WeakCauldron = Cauldron;
	const float Liters = Pot->GetWater();
	const FName Liquid = Pot->GetLiquid();
	const bool bBrew = FindBrew(Liquid) != nullptr;
	const bool bBusy = Controller->GetCraftJob().IsActive();

	TSharedRef<SWrapBox> Actions = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.f, 6.f));
	const auto Action = [&Actions](TSharedRef<SWidget> Widget) { Actions->AddSlot()[Widget]; };
	const int32 Vials = CauldronCountOwned(State, EmptyVial);
	const int32 Possible = FMath::Min(Vials, FMath::FloorToInt32(Liters / VialLiters + 0.001f));
	Action(Button(FText::Format(LOCTEXT("FillVials", "Remplir des fioles ({0})"), FText::AsNumber(Possible)),
		[WeakController, WeakCauldron, Possible]() { if (WeakController.IsValid()) { WeakController->ServerStartPour(WeakCauldron.Get(), 2, NAME_None, Possible); } },
		EButton::Primary, !bBusy && bBrew && Possible > 0, 0.f, 11));
	Action(Button(LOCTEXT("Empty", "Vider le chaudron"),
		[WeakController, WeakCauldron]() { if (WeakController.IsValid()) { WeakController->ServerEmptyWater(WeakCauldron.Get()); } },
		EButton::Danger, !bBusy && (Liters > 0.01f || Pot->GetSlots().ContainsByPredicate([](const FInventorySlot& S) { return !S.IsEmpty(); })), 0.f, 11));

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		Line(LOCTEXT("Contents", "Contenu"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				Gauge(LiquidColor(Liquid), [WeakCauldron]() -> TOptional<float> { return WeakCauldron.IsValid() ? WeakCauldron->GetWater() / ErewhonPlaceables::CauldronLiters : 0.f; })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(Liters > 0.01f ? FText::Format(LOCTEXT("LitersOf", "{0} L de {1}"), FText::AsNumber(FMath::FloorToFloat(Liters * 10.f) / 10.f), LiquidName(Liquid).ToLower())
					: LOCTEXT("EmptyPot", "vide (10 L au plus)"))
				.Font(Font(11, true)).ColorAndOpacity(bBrew ? Leaf : Ink)
			])
	];
	Box->AddSlot().AutoHeight()[Actions];
	return CardBox(Box, 10.f);
}

TSharedRef<SWidget> SCauldronPanel::BuildIngredients() const
{
	// Ce qu'on y a jeté, ce qu'on peut y jeter, et si quelque chose prend.
	const AStorageActor* Pot = Cauldron.Get();
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TWeakObjectPtr<AStorageActor> WeakCauldron = Cauldron;
	const bool bWater = Pot->GetLiquid() == Water || Pot->GetLiquid() == BaseLiquid || Pot->GetWater() <= 0.01f;

	TSharedRef<SWrapBox> Inside = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 6.f));
	for (const FInventorySlot& Slot : Pot->GetSlots())
	{
		const FItemDef* Def = Slot.IsEmpty() ? nullptr : Items->Find(Slot.ItemId);
		if (Def)
		{
			Inside->AddSlot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ItemIcon(Def, 28.f)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
				[
					SNew(STextBlock).Text(FText::Format(LOCTEXT("InCount", "{0} × {1}"), FText::AsNumber(Slot.Count), Def->Name)).Font(Font(10)).ColorAndOpacity(Ink)
				]
			];
		}
	}
	if (Inside->GetChildren()->Num() == 0)
	{
		Inside->AddSlot()[SNew(STextBlock).Text(LOCTEXT("Nothing", "Aucun ingrédient.")).Font(Font(10)).ColorAndOpacity(MutedInk)];
	}

	// Ses ingrédients, à jeter un par un ou par cinq.
	TSharedRef<SWrapBox> Mine = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.f, 6.f));
	for (const FItemDef& Def : Items->GetAll())
	{
		const int32 Have = IsIngredient(Def) ? CauldronCountOwned(State, Def.Id) : 0;
		if (Have <= 0)
		{
			continue;
		}
		const FName Id = Def.Id;
		TSharedRef<SWidget> Chip = SNew(SBorder).BorderImage(Rounded(Card, 8.f, Mix(Card, Edge, 0.5f), 1.f)).Padding(FMargin(6.f, 3.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ItemIcon(&Def, 26.f)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 6, 0)
				[
					SNew(STextBlock).Text(FText::AsNumber(Have)).Font(Font(10, true)).ColorAndOpacity(Ink)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
				[
					Button(LOCTEXT("AddOne", "+1"), [WeakController, WeakCauldron, Id]() { if (WeakController.IsValid()) { WeakController->ServerAddIngredient(WeakCauldron.Get(), Id, 1); } },
						EButton::Secondary, bWater, 34.f, 10)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Button(LOCTEXT("AddFive", "+5"), [WeakController, WeakCauldron, Id]() { if (WeakController.IsValid()) { WeakController->ServerAddIngredient(WeakCauldron.Get(), Id, 5); } },
						EButton::Secondary, bWater && Have >= 5, 34.f, 10)
				]
			];
		Chip->SetToolTip(ItemTooltip(Def));
		Mine->AddSlot()[Chip];
	}

	return CardBox(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Line(LOCTEXT("Inside", "Dans le chaudron"), Inside)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Line(LOCTEXT("Brewing", "Infusion"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					// L'avancée, sans dire ce qui prend : on la voit monter quand tout est juste.
					Gauge(Leaf, [WeakCauldron]() -> TOptional<float>
					{
						const AStorageActor* Current = WeakCauldron.Get();
						const FBrew* Brew = Current ? MatchBrew(Current->GetSlots(), Current->GetWater()) : nullptr;
						return Brew && Current->GetLiquid() == SolventOf(*Brew) ? FMath::Clamp(Current->GetBrewProgress() / Brew->Seconds, 0.f, 1.f) : 0.f;
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
				[
					SNew(STextBlock).Text_Lambda([WeakCauldron]()
					{
						const AStorageActor* Current = WeakCauldron.Get();
						return Current && Current->GetBrewProgress() > 0.01f ? LOCTEXT("Simmers", "ça frémit, ça prend…") : LOCTEXT("Still", "rien ne se passe");
					}).Font(Font(10)).ColorAndOpacity(MutedInk)
				])
		]

		+ SVerticalBox::Slot().AutoHeight()[Line(LOCTEXT("YourIngredients", "Vos ingrédients"),
			SNew(SBox).MaxDesiredHeight(150.f)[SNew(SScrollBox) + SScrollBox::Slot()[Mine]])],
		10.f);
}

void SCauldronPanel::Rebuild()
{
	if (!Body.IsValid() || !Controller.IsValid() || !Cauldron.IsValid() || !Controller->GetPlayerState<AErewhonPlayerState>())
	{
		return;
	}
	// La barre de la manipulation en cours (transvasement), s'il y en a une.
	TSharedRef<SWidget> Busy = SNew(SBox);
	const FErewhonCraftJob& Job = Controller->GetCraftJob();
	if (Job.IsActive())
	{
		const FItemDef* Def = UItemDatabase::Get(Controller.Get())->Find(Job.Output);
		TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
		Busy = CardBox(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(STextBlock).Text(FText::Format(LOCTEXT("Pouring", "On transvase : {0} — encore {1}"), Def ? Def->Name : FText::GetEmpty(), FText::AsNumber(Job.Remaining)))
				.Font(Font(11, true)).ColorAndOpacity(Ink)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Button(LOCTEXT("Stop", "Arrêter"), [WeakController]() { if (WeakController.IsValid()) { WeakController->ServerStopCraft(); } }, EButton::Secondary, true, 90.f, 11)
			],
			8.f);
	}
	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[BuildGauges()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[BuildLiquid()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, Job.IsActive() ? 8 : 0)[BuildIngredients()]
		+ SVerticalBox::Slot().AutoHeight()[Busy]);
}

#undef LOCTEXT_NAMESPACE
