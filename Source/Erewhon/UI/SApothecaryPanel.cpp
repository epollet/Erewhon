#include "UI/SApothecaryPanel.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "World/Apothecary.h"
#include "World/Storage.h"

#define LOCTEXT_NAMESPACE "ErewhonApothecaryPanel"

using namespace ErewhonUI;

namespace
{
	constexpr float GPanelWidth = 680.f;
	constexpr float GListHeight = 380.f;

	/** Barre de préparation bien visible : un fond brun foncé cerné, un remplissage doré. */
	const FProgressBarStyle& BarStyle()
	{
		static FProgressBarStyle Style = []()
		{
			FProgressBarStyle Bar;
			Bar.SetBackgroundImage(*Rounded(Mix(Ink, Card, 0.25f), 6.f, Ink, 1.f));
			Bar.SetFillImage(*Rounded(Gold, 6.f));
			Bar.SetMarqueeImage(*Rounded(Gold, 6.f));
			return Bar;
		}();
		return Style;
	}

	int32 CountOwned(const AErewhonPlayerState* State, FName ItemId)
	{
		return State->GetInventory()->CountStored(ItemId) + State->GetBackpack()->CountStored(ItemId);
	}
}

void SApothecaryPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	Station = InArgs._Station;
	Craft = InArgs._Craft;
	bCauldron = Craft == static_cast<uint8>(ErewhonApothecary::ECraft::Cauldron);
	const bool bWorkbench = Craft == static_cast<uint8>(ErewhonApothecary::ECraft::Workbench);
	House = bCauldron ? ErewhonApothecary::HouseOfHungCauldron(Station.Get()) : nullptr;
	if (House.IsValid())
	{
		House->OnFireChanged.AddSP(this, &SApothecaryPanel::Rebuild);
	}
	if (AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr)
	{
		State->GetInventory()->OnInventoryChanged.AddSP(this, &SApothecaryPanel::Rebuild);
		State->GetBackpack()->OnBackpackChanged.AddSP(this, &SApothecaryPanel::Rebuild);
	}
	if (Controller.IsValid())
	{
		Controller->OnCraftChanged.AddSP(this, &SApothecaryPanel::Rebuild);
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(bWorkbench ? LOCTEXT("WorkbenchTitle", "L'atelier") : bCauldron ? LOCTEXT("CauldronTitle", "Le chaudron") : LOCTEXT("MortarTitle", "Le mortier"),
			SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			GPanelWidth, bWorkbench
				? LOCTEXT("WorkbenchSubtitle", "Pots-pourris de fleurs séchées, teintures d'écrasés, bouquets en pot.")
				: bCauldron
				? LOCTEXT("CauldronSubtitle", "Deux écrasés d'herbe font une base ; une base et deux écrasés d'une plante, une potion.")
				: LOCTEXT("MortarSubtitle", "Chaque plante, pilée une seconde, donne son écrasé (pris dans vos poches et votre sac)."))
	];
	Rebuild();
}

FReply SApothecaryPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SApothecaryPanel::BuildProgress() const
{
	// La préparation en cours : ce qu'on prépare, combien il en reste, l'avancée de l'exemplaire en cours.
	const FErewhonCraftJob& Job = Controller->GetCraftJob();
	const FItemDef* Def = UItemDatabase::Get(Controller.Get())->Find(Job.Output);
	if (!Job.IsActive() || !Def)
	{
		return SNew(SBox);
	}
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	return CardBox(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[ItemIcon(Def, 40.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("InProgress", "En préparation : {0} — encore {1}"), Def->Name, FText::AsNumber(Job.Remaining)))
				.Font(Font(12, true)).ColorAndOpacity(Ink)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SBox).HeightOverride(16.f)
				[
					SNew(SProgressBar)
					.Style(&BarStyle())
					.Percent_Lambda([WeakController]() -> TOptional<float>
					{
						const AErewhonGameState* State = WeakController.IsValid() ? AErewhonGameState::Get(WeakController.Get()) : nullptr;
						if (!State)
						{
							return 0.f;
						}
						const FErewhonCraftJob& Current = WeakController->GetCraftJob();
						const double Left = Current.StepEnd - State->GetWorldClock();
						return FMath::Clamp(1.f - static_cast<float>(Left / FMath::Max(0.1f, Current.StepSeconds)), 0.f, 1.f);
					})
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
		[
			Button(LOCTEXT("Stop", "Arrêter"), [WeakController]() { if (WeakController.IsValid()) { WeakController->ServerStopCraft(); } }, EButton::Secondary, true, 90.f, 11)
		],
		10.f);
}

void SApothecaryPanel::Rebuild()
{
	const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	if (!Body.IsValid() || !State || !Items)
	{
		return;
	}

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TWeakObjectPtr<AStorageActor> WeakStation = Station;
	const bool bBusy = Controller->GetCraftJob().IsActive();
	const bool bCold = bCauldron && !(House.IsValid() && House->IsFireLit());
	for (const ErewhonApothecary::FRecipe& Recipe : ErewhonApothecary::Recipes(*Items, static_cast<ErewhonApothecary::ECraft>(Craft)))
	{
		const FItemDef* Def = Items->Find(Recipe.Output);
		if (!Def)
		{
			continue;
		}
		// Combien on peut en faire avec ce qu'on a, et la liste des ingrédients (ce qu'on en a entre parenthèses).
		int32 Possible = TNumericLimits<int32>::Max();
		FString Needs;
		for (const TPair<FName, int32>& Input : Recipe.Inputs)
		{
			const FItemDef* InputDef = Items->Find(Input.Key);
			const int32 Have = CountOwned(State, Input.Key);
			Possible = FMath::Min(Possible, Have / Input.Value);
			Needs += FString::Printf(TEXT("%s%d × %s (%d)"), Needs.IsEmpty() ? TEXT("") : TEXT("  +  "), Input.Value,
				InputDef ? *InputDef->Name.ToString() : *Input.Key.ToString(), Have);
		}
		const FName Output = Recipe.Output;
		const bool bCan = Possible > 0 && !bBusy && !bCold;
		TSharedRef<SWidget> Row = CardBox(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[ItemIcon(Def, 44.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Def->Name)
					.Font(Font(12, true)).ColorAndOpacity(Possible > 0 ? Ink : MutedInk)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(Needs)).Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 6, 0)
			[
				Button(LOCTEXT("MakeOne", "1"), [WeakController, WeakStation, Output]()
				{
					if (WeakController.IsValid()) { WeakController->ServerStartCraft(WeakStation.Get(), Output, 1); }
				}, EButton::Secondary, bCan, 50.f, 11)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Button(FText::Format(LOCTEXT("MakeAll", "Tout ({0})"), FText::AsNumber(Possible > 0 && Possible < TNumericLimits<int32>::Max() ? Possible : 0)),
					[WeakController, WeakStation, Output, Possible]()
				{
					if (WeakController.IsValid()) { WeakController->ServerStartCraft(WeakStation.Get(), Output, Possible); }
				}, EButton::Primary, bCan, 100.f, 11)
			],
			8.f);
		Row->SetToolTip(ItemTooltip(*Def));
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Row];
	}

	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, bCauldron ? 10 : 0)[bCauldron ? BuildFire() : StaticCastSharedRef<SWidget>(SNew(SBox))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, bBusy ? 10 : 0)[BuildProgress()]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(GListHeight)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[List]
			]
		]);
}

TSharedRef<SWidget> SApothecaryPanel::BuildFire() const
{
	// Sous le chaudron : le feu, et de quoi le nourrir.
	const AApothecary* Home = House.Get();
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!State)
	{
		return SNew(SBox);
	}
	if (!Home)
	{
		return CardBox(
			SNew(STextBlock).Text(LOCTEXT("NotHung", "Le chaudron doit pendre à la crémaillère de la cheminée : prenez-le et posez-le près du foyer."))
			.Font(Font(11, true)).ColorAndOpacity(Danger).AutoWrapText(true),
			8.f);
	}
	const bool bLit = Home->IsFireLit();
	const int32 Wood = CountOwned(State, TEXT("firewood"));
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TWeakObjectPtr<AApothecary> WeakHouse = House;
	return CardBox(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([WeakHouse]()
			{
				const AApothecary* Current = WeakHouse.Get();
				if (!Current || !Current->IsFireLit())
				{
					return LOCTEXT("FireOut", "Le feu est éteint : le chaudron ne chauffe pas.");
				}
				return FText::Format(LOCTEXT("FireLit", "Le feu brûle sous le chaudron (encore {0} min)."),
					FText::AsNumber(FMath::CeilToInt32(Current->FireRemaining() / 60.0)));
			})
			.Font(Font(11, true)).ColorAndOpacity(bLit ? Clay : Danger)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
		[
			Button(FText::Format(bLit ? LOCTEXT("AddWood", "Remettre du petit bois ({0})") : LOCTEXT("LightWood", "Allumer avec du petit bois ({0})"), FText::AsNumber(Wood)),
				[WeakController, WeakHouse]() { if (WeakController.IsValid()) { WeakController->ServerFeedFire(WeakHouse.Get()); } },
				bLit ? EButton::Secondary : EButton::Primary, Wood > 0, 0.f, 11)
		],
		8.f);
}

#undef LOCTEXT_NAMESPACE
