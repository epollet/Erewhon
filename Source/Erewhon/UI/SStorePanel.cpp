#include "UI/SStorePanel.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "World/SectorTypes.h"

#define LOCTEXT_NAMESPACE "ErewhonStorePanel"

using namespace ErewhonUI;

namespace
{
	constexpr float GPanelWidth = 560.f;
	constexpr float GListHeight = 420.f;

	TSharedRef<SWidget> ActionButton(const FText& Label, bool bEnabled, TFunction<void()> OnClick)
	{
		return SNew(SBox).MinDesiredWidth(96.f)
			[
				SNew(SButton)
				.IsEnabled(bEnabled)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(Label).Font(Font(10, true))
				]
			];
	}

	/** Ligne de liste : vignette, libellé, détail, puis boutons. */
	TSharedRef<SHorizontalBox> ListRow(TSharedRef<SWidget> Icon, const FText& Label, const FText& Detail, const FLinearColor& DetailColor)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Icon]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label).Font(Font(11)).ColorAndOpacity(TextColor)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0)
			[
				SNew(STextBlock).Text(Detail).Font(Font(11, true)).ColorAndOpacity(DetailColor)
			];
	}

	TSharedRef<SWidget> Scrolling(TSharedRef<SVerticalBox> List)
	{
		return SNew(SBox).HeightOverride(GListHeight)[SNew(SScrollBox) + SScrollBox::Slot()[List]];
	}

	FText Coins(int32 Amount)
	{
		return FText::Format(LOCTEXT("CoinsFmt", "{0} C"), FText::AsNumber(Amount));
	}
}

void SStorePanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	if (State)
	{
		State->OnCoinsChanged.AddSP(this, &SStorePanel::Rebuild);
		State->GetInventory()->OnInventoryChanged.AddSP(this, &SStorePanel::Rebuild);
	}
	if (World)
	{
		World->OnSectorOwnerChanged.AddSP(this, &SStorePanel::OnSectorOwnerChanged);
	}
	// Sans secteur, rien à récolter : on ouvre d'abord sur les secteurs.
	if (State && World && World->GetSectorsOwnedBy(State->GetPlayerKey()).IsEmpty())
	{
		Tab = ETab::Sectors;
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(LOCTEXT("Title", "Marchand"), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			GPanelWidth)
	];
	Rebuild();
}

FReply SStorePanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SStorePanel::Rebuild()
{
	if (!Body.IsValid() || !Controller.IsValid() || !Controller->GetPlayerState<AErewhonPlayerState>())
	{
		return;
	}
	TSharedRef<SWidget> Content = Tab == ETab::Sectors ? BuildSectors() : Tab == ETab::Buy ? BuildBuy() : BuildSell();
	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[BuildTabs()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Coins(Controller->GetPlayerState<AErewhonPlayerState>()->GetCoins()))
				.Font(Font(16, true)).ColorAndOpacity(CoinColor)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[Separator()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Content]);
}

TSharedRef<SWidget> SStorePanel::BuildTabs()
{
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	const TPair<ETab, FText> Entries[] = {
		{ ETab::Sectors, LOCTEXT("TabSectors", "Secteurs") },
		{ ETab::Buy, LOCTEXT("TabBuy", "Acheter") },
		{ ETab::Sell, LOCTEXT("TabSell", "Vendre") },
	};
	for (const TPair<ETab, FText>& Entry : Entries)
	{
		const ETab Value = Entry.Key;
		const bool bActive = Tab == Value;
		Tabs->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(bActive ? EquipmentAccent : FLinearColor(0.3f, 0.26f, 0.2f, 1.f))
			.OnClicked_Lambda([this, Value]() { Tab = Value; Rebuild(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Entry.Value).Font(Font(11, true)).ColorAndOpacity(TextColor)
			]
		];
	}
	return Tabs;
}

TSharedRef<SWidget> SStorePanel::BuildSectors()
{
	const AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (!World)
	{
		return List;
	}

	// Secteurs autour du spawn, du plus proche au plus lointain.
	TArray<FIntPoint> Sectors;
	const int32 R = Settings.StoreSectorRadius;
	for (int32 Y = -R; Y <= R; ++Y)
	{
		for (int32 X = -R; X <= R; ++X)
		{
			if (ErewhonSector::IsPurchasable(FIntPoint(X, Y)))
			{
				Sectors.Add(FIntPoint(X, Y));
			}
		}
	}
	Sectors.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		const int32 DA = A.X * A.X + A.Y * A.Y;
		const int32 DB = B.X * B.X + B.Y * B.Y;
		return DA != DB ? DA < DB : (A.X != B.X ? A.X < B.X : A.Y < B.Y);
	});

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	for (const FIntPoint& Sector : Sectors)
	{
		const FString* OwnerKey = World->FindSectorOwnerKey(Sector);
		const bool bMine = OwnerKey && *OwnerKey == State->GetPlayerKey();
		const FText Label = FText::Format(LOCTEXT("SectorLabel", "{0} · {1}"),
			FText::FromString(ErewhonSector::ToString(Sector)), ErewhonSector::TypeLabel(ErewhonSector::TypeAt(Sector, World->GetWorldSeed())));

		FText Detail;
		FLinearColor DetailColor = MutedText;
		TSharedPtr<SWidget> Action;
		if (bMine)
		{
			Detail = LOCTEXT("Mine", "à vous");
			DetailColor = InventoryAccent;
			Action = ActionButton(FText::Format(LOCTEXT("SellSector", "Revendre {0}"), Coins(Settings.SectorSellPrice)), true,
				[WeakController, Sector]() { if (WeakController.IsValid()) { WeakController->ServerSellSector(Sector); } });
		}
		else if (OwnerKey)
		{
			Detail = FText::FromString(World->GetSectorOwnerName(Sector));
			Action = SNew(SBox).MinDesiredWidth(96.f);
		}
		else
		{
			Detail = LOCTEXT("Free", "libre");
			DetailColor = ClothingAccent;
			Action = ActionButton(FText::Format(LOCTEXT("BuySector", "Acheter {0}"), Coins(Settings.SectorBuyPrice)),
				State->GetCoins() >= Settings.SectorBuyPrice,
				[WeakController, Sector]() { if (WeakController.IsValid()) { WeakController->ServerBuySector(Sector); } });
		}

		TSharedRef<SHorizontalBox> Row = ListRow(SNew(SBox), Label, Detail, DetailColor);
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[Action.ToSharedRef()];
		List->AddSlot().AutoHeight().Padding(0, 2)[Row];
	}
	return Scrolling(List);
}

TSharedRef<SWidget> SStorePanel::BuildBuy()
{
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (!Items)
	{
		return List;
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	for (const FItemDef* Def : Items->GetPurchasable())
	{
		const bool bOwned = Def->IsUnique() && State->GetInventory()->Owns(Def->Id);
		const FName Id = Def->Id;
		TSharedRef<SHorizontalBox> Row = ListRow(ItemIcon(Def, 32.f), Def->Name, Coins(Def->BuyPrice), CoinColor);
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			ActionButton(bOwned ? LOCTEXT("Owned", "Possédé") : LOCTEXT("Buy", "Acheter"),
				!bOwned && State->GetCoins() >= Def->BuyPrice,
				[WeakController, Id]() { if (WeakController.IsValid()) { WeakController->ServerBuyItem(Id); } })
		];
		Row->SetToolTip(ItemTooltip(*Def));
		List->AddSlot().AutoHeight().Padding(0, 3)[Row];
	}
	return Scrolling(List);
}

TSharedRef<SWidget> SStorePanel::BuildSell()
{
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	const float ResaleRatio = UErewhonSettings::Get().ResaleRatio;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (!Items)
	{
		return List;
	}

	// Une ligne par objet rangé (les objets équipés ne se vendent pas sans être rangés).
	TArray<FName> Seen;
	for (const FInventorySlot& Slot : State->GetInventory()->GetSlots())
	{
		if (Slot.Kind == ESlotKind::Inventory && !Slot.IsEmpty() && !Seen.Contains(Slot.ItemId))
		{
			Seen.Add(Slot.ItemId);
		}
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	for (const FName Id : Seen)
	{
		const FItemDef* Def = Items->Find(Id);
		const int32 Price = Def ? Def->ResalePrice(ResaleRatio) : 0;
		if (Price <= 0)
		{
			continue;
		}
		const int32 Count = State->GetInventory()->CountStored(Id);
		const FText Label = FText::Format(LOCTEXT("SellLabel", "{0} × {1}"), Def->Name, FText::AsNumber(Count));
		TSharedRef<SHorizontalBox> Row = ListRow(ItemIcon(Def, 32.f), Label, Coins(Price), CoinColor);
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
		[
			ActionButton(LOCTEXT("SellOne", "Vendre"), true,
				[WeakController, Id]() { if (WeakController.IsValid()) { WeakController->ServerSellItem(Id, 1); } })
		];
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			ActionButton(FText::Format(LOCTEXT("SellAll", "Tout ({0})"), Coins(Price * Count)), true,
				[WeakController, Id, Count]() { if (WeakController.IsValid()) { WeakController->ServerSellItem(Id, Count); } })
		];
		Row->SetToolTip(ItemTooltip(*Def));
		List->AddSlot().AutoHeight().Padding(0, 3)[Row];
	}

	if (Seen.IsEmpty())
	{
		List->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(LOCTEXT("NothingToSell", "Rien à vendre. Récoltez herbes et fleurs sur vos secteurs !"))
			.Font(Font(11)).ColorAndOpacity(MutedText)
		];
	}
	return Scrolling(List);
}

#undef LOCTEXT_NAMESPACE
