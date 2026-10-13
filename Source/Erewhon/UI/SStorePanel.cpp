#include "UI/SStorePanel.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "UI/ErewhonSlate.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "World/SectorTypes.h"
#include "World/SpawnLayout.h"
#include "World/Village.h"
#include "World/Apothecary.h"

#define LOCTEXT_NAMESPACE "ErewhonStorePanel"

using namespace ErewhonUI;

namespace
{
	constexpr float GPanelWidth = 760.f;
	constexpr float GContentHeight = 430.f;
	constexpr float GTileSize = 40.f;
	constexpr int32 GBuyColumns = 4;

	FText CoinsText(int32 Amount)
	{
		return FText::Format(LOCTEXT("CoinsFmt", "{0} C"), FText::AsNumber(Amount));
	}

	/** État d'un secteur vu par le joueur : public (commerces, routes), libre, à lui, privé (à un autre). */
	enum class ESectorState : uint8 { Public, Free, Mine, Private };

	FLinearColor StateColor(ESectorState State)
	{
		switch (State)
		{
		case ESectorState::Public:  return RoadColor;
		case ESectorState::Mine:    return Sky;
		case ESectorState::Private: return Mix(Danger, Card, 0.35f);
		default:                    return Mix(Leaf, Card, 0.25f);
		}
	}
}

void SStorePanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	Kind = InArgs._Kind;
	AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	if (State)
	{
		State->OnCoinsChanged.AddSP(this, &SStorePanel::Rebuild);
		State->GetInventory()->OnInventoryChanged.AddSP(this, &SStorePanel::Rebuild);
		State->GetBackpack()->OnBackpackChanged.AddSP(this, &SStorePanel::Rebuild);
	}
	if (World)
	{
		World->OnSectorOwnerChanged.AddSP(this, &SStorePanel::OnSectorOwnerChanged);
		World->OnVillageChanged.AddSP(this, &SStorePanel::OnVillageChanged);
	}
	// Le gérant ne parle que de secteurs ; ailleurs, rien à vendre ici ? On ouvre sur les achats.
	const bool bManager = Kind == EShopKind::SectorManager;
	if (bManager)
	{
		Tab = InArgs._bVillageTab ? ETab::Village : ETab::Sectors;
	}
	else if (State && !State->GetInventory()->GetSlots().ContainsByPredicate([this, State, World](const FInventorySlot& Slot)
	{
		const FItemDef* Def = Slot.Kind == ESlotKind::Inventory && !Slot.IsEmpty() ? UItemDatabase::Get(Controller.Get())->Find(Slot.ItemId) : nullptr;
		return Def && World && World->ShopFor(*Def) == Kind && Def->ResalePrice(UErewhonSettings::Get().ResaleRatio) > 0;
	}))
	{
		Tab = ETab::Buy;
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(FText::FromString(ErewhonSpawn::ShopName(Kind)), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			GPanelWidth, bManager
				? LOCTEXT("ManagerSubtitle", "« Un coin de prairie à vous ? Regardons la carte ensemble. »")
				: Kind == EShopKind::Tailor
				? LOCTEXT("TailorSubtitle", "« Entrez donc ! Un peu d'étoffe, quelques poches de plus ? »")
				: LOCTEXT("Subtitle", "« Bonjour ! Qu'est-ce qui vous ferait plaisir ? »"))
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
	const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (!Body.IsValid() || !State)
	{
		return;
	}
	TSharedRef<SWidget> Content = Tab == ETab::Sectors ? BuildSectors() : Tab == ETab::Village ? BuildVillage() : Tab == ETab::Buy ? BuildBuy() : BuildSell();
	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[BuildTabs()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CoinBadge(State->GetCoins(), 14)]
		]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(GContentHeight)[Content]]);
}

TSharedRef<SWidget> SStorePanel::BuildTabs()
{
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	const TPair<ETab, FText> Entries[] = {
		{ ETab::Sectors, LOCTEXT("TabSectors", "Secteurs") },
		{ ETab::Village, LOCTEXT("TabVillage", "Village") },
		{ ETab::Buy, LOCTEXT("TabBuy", "Acheter") },
		{ ETab::Sell, LOCTEXT("TabSell", "Vendre") },
	};
	for (const TPair<ETab, FText>& Entry : Entries)
	{
		// Chaque commerce son métier : la carte chez le gérant, le reste au General Store.
		if ((Entry.Key == ETab::Sectors || Entry.Key == ETab::Village) != (Kind == EShopKind::SectorManager))
		{
			continue;
		}
		const ETab Value = Entry.Key;
		Tabs->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
		[
			Button(Entry.Value, [this, Value]() { Tab = Value; Rebuild(); }, Tab == Value ? EButton::TabActive : EButton::Tab, true, 110.f, 11)
		];
	}
	return Tabs;
}

// ── Secteurs ─────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SStorePanel::BuildSectors()
{
	const AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!World)
	{
		return SNew(SBox);
	}
	const int32 R = UErewhonSettings::Get().StoreSectorRadius;

	// La carte : nord (+X) en haut, est (+Y) à droite, la cabane au centre.
	TSharedRef<SUniformGridPanel> Map = SNew(SUniformGridPanel).SlotPadding(2.f);
	for (int32 X = R; X >= -R; --X)
	{
		for (int32 Y = -R; Y <= R; ++Y)
		{
			const FIntPoint Sector(X, Y);
			const ESectorType Type = ErewhonSector::TypeAt(Sector, World->GetWorldSeed());
			const FString* Owner = World->FindSectorOwnerKey(Sector);
			const ESectorState TileState = !ErewhonSector::IsPurchasable(Sector) ? ESectorState::Public
				: !Owner ? ESectorState::Free
				: *Owner == State->GetPlayerKey() ? ESectorState::Mine : ESectorState::Private;
			const bool bSelected = Sector == Selected;
			const FLinearColor Fill = StateColor(TileState);

			FText Glyph;
			if (Type == ESectorType::Spawn)
			{
				Glyph = FText::FromString(TEXT("⌂"));   // maisonnette
			}
			else if (TileState == ESectorState::Mine)
			{
				Glyph = FText::FromString(TEXT("★"));
			}

			Map->AddSlot(Y + R, R - X)
			[
				SNew(SBox).WidthOverride(GTileSize).HeightOverride(GTileSize)
				[
					SNew(SButton)
					.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
					.ContentPadding(0.f)
					.ToolTipText(FText::Format(LOCTEXT("TileTip", "Secteur {0} · {1}"), FText::FromString(ErewhonSector::ToString(Sector)), ErewhonSector::TypeLabel(Type)))
					.OnClicked_Lambda([this, Sector]() { Selected = Sector; Rebuild(); return FReply::Handled(); })
					[
						SNew(SBorder)
						.BorderImage(Rounded(Fill, 6.f, bSelected ? Gold : Mix(Fill, Ink, 0.25f), bSelected ? 3.f : 1.f))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(Glyph).Font(Font(14, true)).ColorAndOpacity(FLinearColor::White)
						]
					]
				]
			];
		}
	}

	const auto Legend = [](ESectorState LegendState, const FText& Label)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
			[
				SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)[SNew(SBorder).BorderImage(Rounded(StateColor(LegendState), 3.f))]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label).Font(Font(9)).ColorAndOpacity(MutedInk)
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			CardBox(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Map]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Legend(ESectorState::Free, LOCTEXT("LegendFree", "Libre"))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Legend(ESectorState::Mine, LOCTEXT("LegendMine", "À vous"))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Legend(ESectorState::Private, LOCTEXT("LegendPrivate", "Privé"))]
					+ SHorizontalBox::Slot().AutoWidth()[Legend(ESectorState::Public, LOCTEXT("LegendPublic", "Public"))]
				],
				10.f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(14, 0, 0, 0)[BuildSectorDetail()];
}

TSharedRef<SWidget> SStorePanel::BuildSectorDetail()
{
	const AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	const FIntPoint Sector = Selected;
	const ESectorType Type = ErewhonSector::TypeAt(Sector, World->GetWorldSeed());
	const FString* Owner = World->FindSectorOwnerKey(Sector);
	const bool bMine = Owner && *Owner == State->GetPlayerKey();

	FText Status;
	TSharedRef<SWidget> Action = SNew(SBox);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	if (Type == ESectorType::Spawn)
	{
		Status = LOCTEXT("StatusMerchant", "La place du village, avec le General Store et mon bureau. C'est public : pas à vendre !");
	}
	else if (Type == ESectorType::Road)
	{
		Status = LOCTEXT("StatusRoad", "Une route : c'est public. Achetez un secteur qui la borde, un chemin vous y mènera.");
	}
	else if (bMine)
	{
		Status = LOCTEXT("StatusMine", "C'est chez vous. Je peux l'aménager, ou vous le reprendre à moitié prix.");
		Action = Button(FText::Format(LOCTEXT("Resell", "Revendre · {0}"), CoinsText(Settings.SectorSellPrice)),
			[WeakController, Sector]() { if (WeakController.IsValid()) { WeakController->ServerSellSector(Sector); } }, EButton::Secondary, true, 200.f);
	}
	else if (Owner)
	{
		Status = FText::Format(LOCTEXT("StatusTaken", "Secteur privé : il appartient à {0}."), FText::FromString(World->GetSectorOwnerName(Sector)));
	}
	else
	{
		bool bNearRoad = false;
		for (const FIntPoint& Side : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			bNearRoad |= ErewhonSector::IsRoad(Sector + Side);
		}
		Status = bNearRoad
			? LOCTEXT("StatusFreeRoad", "Une belle prairie au bord de la route. Je pourrai vous y tracer un chemin d'accès.")
			: LOCTEXT("StatusFree", "Une prairie libre, rien que pour vous.");
		const bool bAfford = State->GetCoins() >= Settings.SectorBuyPrice;
		Action = Button(FText::Format(LOCTEXT("Buy", "Acheter · {0}"), CoinsText(Settings.SectorBuyPrice)),
			[WeakController, Sector]() { if (WeakController.IsValid()) { WeakController->ServerBuySector(Sector); } }, EButton::Primary, bAfford, 200.f);
	}

	const TArray<FIntPoint> Owned = World->GetSectorsOwnedBy(State->GetPlayerKey());
	FString OwnedList;
	for (const FIntPoint& Mine : Owned)
	{
		OwnedList += (OwnedList.IsEmpty() ? TEXT("") : TEXT(", ")) + ErewhonSector::ToString(Mine);
	}

	// Améliorations d'un secteur à soi : un chemin vers chaque route qui le borde.
	TSharedRef<SVerticalBox> Upgrades = SNew(SVerticalBox);
	if (bMine)
	{
		Upgrades->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(STextBlock).Text(LOCTEXT("UpgradesTitle", "Améliorations")).Font(Font(13, true)).ColorAndOpacity(Ink)
		];
		const uint8 Links = World->GetRoadLinks(Sector);
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (!ErewhonSector::IsRoad(Sector + ErewhonSector::Sides[Side]))
			{
				continue;
			}
			const bool bBuilt = (Links & (1 << Side)) != 0;
			const bool bAfford = State->GetCoins() >= Settings.RoadLinkPrice;
			const uint8 SideByte = static_cast<uint8>(Side);
			TSharedRef<SWidget> UpgradeAction = bBuilt
				? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("Built", "✓ Construit")).Font(Font(11, true)).ColorAndOpacity(Leaf))
				: Button(FText::Format(LOCTEXT("BuildLink", "Construire · {0}"), CoinsText(Settings.RoadLinkPrice)),
					[WeakController, Sector, SideByte]() { if (WeakController.IsValid()) { WeakController->ServerBuyRoadLink(Sector, SideByte); } },
					EButton::Primary, bAfford, 150.f);
			Upgrades->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("LinkUpgrade", "Accès à la route {0}"), ErewhonSector::SideLabel(Side)))
					.Font(Font(11)).ColorAndOpacity(Ink).AutoWrapText(true)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[UpgradeAction]
			];
			// Une fois le village pavé, l'accès de terre peut l'être à son tour.
			if (bBuilt && World->IsVillagePaved())
			{
				const bool bPaved = (World->GetPavedLinks(Sector) & (1 << Side)) != 0;
				TSharedRef<SWidget> PaveAction = bPaved
					? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("Paved", "✓ Pavé")).Font(Font(11, true)).ColorAndOpacity(Leaf))
					: Button(FText::Format(LOCTEXT("PaveLink", "Paver · {0}"), CoinsText(Settings.PavedLinkPrice)),
						[WeakController, Sector, SideByte]() { if (WeakController.IsValid()) { WeakController->ServerBuyPavedLink(Sector, SideByte); } },
						EButton::Primary, State->GetCoins() >= Settings.PavedLinkPrice, 150.f);
				Upgrades->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("PaveUpgrade", "Accès {0} en pavés"), ErewhonSector::SideLabel(Side)))
						.Font(Font(11)).ColorAndOpacity(Ink).AutoWrapText(true)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[PaveAction]
				];
			}
		}
		// L'apothicairerie, au centre d'une prairie.
		if (Type == ESectorType::Prairie)
		{
			const bool bBuilt = (World->GetBuildings(Sector) & ESectorBuilding::Apothecary) != 0;
			TSharedRef<SWidget> BuildAction = bBuilt
				? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("Built", "✓ Construit")).Font(Font(11, true)).ColorAndOpacity(Leaf))
				: Button(FText::Format(LOCTEXT("BuildApothecary", "Construire · {0}"), CoinsText(Settings.ApothecaryPrice)),
					[WeakController, Sector]() { if (WeakController.IsValid()) { WeakController->ServerBuyApothecary(Sector); } },
					EButton::Primary, State->GetCoins() >= Settings.ApothecaryPrice, 150.f);
			Upgrades->AddSlot().AutoHeight().Padding(0, 4, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ApothecaryUpgrade", "Apothicairerie (cheminée, mortier, chaudron)"))
					.Font(Font(11)).ColorAndOpacity(Ink).AutoWrapText(true)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[BuildAction]
			];
		}
		if (Upgrades->NumSlots() == 1)
		{
			Upgrades->AddSlot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("NoUpgrade", "Aucune route ne borde ce secteur : rien à améliorer pour l'instant."))
				.Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
			];
		}
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CardBox(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::Format(LOCTEXT("DetailTitle", "Secteur {0}"), FText::FromString(ErewhonSector::ToString(Sector))))
					.Font(Font(18, true)).ColorAndOpacity(Ink)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 10)
				[
					SNew(STextBlock).Text(ErewhonSector::TypeLabel(Type)).Font(Font(11, true)).ColorAndOpacity(Clay)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
				[
					SNew(STextBlock).Text(Status).Font(Font(11)).ColorAndOpacity(Ink).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[Action],
				16.f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			bMine ? CardBox(Upgrades, 14.f) : StaticCastSharedRef<SWidget>(SNew(SBox))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 14, 0, 0)
		[
			SNew(STextBlock)
			.Text(Owned.IsEmpty()
				? LOCTEXT("NoneOwned", "Vous ne possédez encore aucun secteur : on ne récolte que chez soi.")
				: FText::Format(LOCTEXT("OwnedList", "Vos secteurs : {0}"), FText::FromString(OwnedList)))
			.Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
		];
}

// ── Acheter ──────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SStorePanel::BuildBuy()
{
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(6.f);
	if (!Items)
	{
		return Grid;
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	int32 Index = 0;
	for (const FItemDef* Def : Items->GetPurchasable())
	{
		if (AErewhonGameState::Get(Controller.Get())->ShopFor(*Def) != Kind)
		{
			continue;   // chaque commerce ne vend que sa marchandise
		}
		const bool bOwned = Def->IsBackpack() ? State->GetBackpack()->IsOwned() : Def->IsUnique() && State->GetInventory()->Owns(Def->Id);
		const bool bAfford = State->GetCoins() >= Def->BuyPrice;
		const FName Id = Def->Id;
		TSharedRef<SWidget> ItemCard = CardBox(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ItemIcon(Def, 72.f)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 2)
			[
				SNew(STextBlock).Text(Def->Name).Font(Font(11, true)).ColorAndOpacity(Ink).Justification(ETextJustify::Center)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 8)[CoinBadge(Def->BuyPrice, 10)]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Button(bOwned ? LOCTEXT("Owned", "Déjà à vous") : LOCTEXT("BuyItem", "Acheter"),
					[WeakController, Id]() { if (WeakController.IsValid()) { WeakController->ServerBuyItem(Id); } },
					EButton::Primary, !bOwned && bAfford, 0.f, 11)
			],
			10.f);
		ItemCard->SetToolTip(ItemTooltip(*Def));
		Grid->AddSlot(Index % GBuyColumns, Index / GBuyColumns)[ItemCard];
		++Index;
	}
	return SNew(SScrollBox) + SScrollBox::Slot()[Grid];
}

// ── Vendre ───────────────────────────────────────────────────────────────────

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

	// Une ligne par objet rangé, dans les poches comme dans le sac à dos (les objets équipés
	// ne se vendent pas sans être rangés).
	TArray<FName> Seen;
	const UBackpackComponent* Bag = State->GetBackpack();
	TArray<FInventorySlot> Stored = State->GetInventory()->GetSlots();
	Stored.Append(Bag->GetSlots());
	for (const FInventorySlot& Slot : Stored)
	{
		if (Slot.Kind == ESlotKind::Inventory && !Slot.IsEmpty() && !Seen.Contains(Slot.ItemId))
		{
			Seen.Add(Slot.ItemId);
		}
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	int32 Total = 0;
	for (const FName Id : Seen)
	{
		const FItemDef* Def = Items->Find(Id);
		const int32 Price = Def && AErewhonGameState::Get(Controller.Get())->ShopFor(*Def) == Kind ? Def->ResalePrice(ResaleRatio) : 0;
		if (Price <= 0)
		{
			continue;
		}
		const int32 Count = State->GetInventory()->CountStored(Id) + Bag->CountStored(Id);
		Total += Price * Count;
		TSharedRef<SWidget> Row = CardBox(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[ItemIcon(Def, 44.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Def->Name).Font(Font(12, true)).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::Format(LOCTEXT("SellCount", "{0} en stock · {1} pièce"), FText::AsNumber(Count), CoinsText(Price)))
					.Font(Font(10)).ColorAndOpacity(MutedInk)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				Button(LOCTEXT("SellOne", "Vendre 1"), [WeakController, Id]() { if (WeakController.IsValid()) { WeakController->ServerSellItem(Id, 1); } }, EButton::Secondary, true, 100.f, 11)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Button(FText::Format(LOCTEXT("SellAll", "Tout · {0}"), CoinsText(Price * Count)),
					[WeakController, Id, Count]() { if (WeakController.IsValid()) { WeakController->ServerSellItem(Id, Count); } }, EButton::Primary, true, 130.f, 11)
			],
			8.f);
		Row->SetToolTip(ItemTooltip(*Def));
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Row];
	}

	if (List->NumSlots() == 0)
	{
		return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NothingToSell", "Rien à vendre pour l'instant.\nRécoltez herbes et fleurs sur vos secteurs, je vous les rachète !"))
				.Justification(ETextJustify::Center)
				.Font(Font(12)).ColorAndOpacity(MutedInk)
			];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[List]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0).HAlign(HAlign_Right)
		[
			SNew(STextBlock).Text(FText::Format(LOCTEXT("SellTotal", "Valeur de tout votre stock : {0}"), CoinsText(Total)))
			.Font(Font(11, true)).ColorAndOpacity(Ink)
		];
}

// ── Village ──────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SStorePanel::BuildVillage()
{
	// Les améliorations du village, offertes par un joueur à tous les autres.
	const AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	const AErewhonPlayerState* State = Controller->GetPlayerState<AErewhonPlayerState>();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (!World || !State)
	{
		return List;
	}
	List->AddSlot().AutoHeight().Padding(4, 0, 0, 10)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("VillageIntro", "Le village, c'est la place et tous les secteurs autour. Ce que vous y construisez profite à tout le monde."))
		.Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
	];
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	for (const ErewhonVillage::FUpgradeInfo& Info : ErewhonVillage::Upgrades())
	{
		const bool bBuilt = World->HasVillageUpgrade(Info.Id);
		const bool bLocked = Info.Requires != EVillageUpgrade::Count && !World->HasVillageUpgrade(Info.Requires);
		const uint8 Id = static_cast<uint8>(Info.Id);
		TSharedRef<SWidget> Action = bBuilt
			? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("VillageBuilt", "✓ Construit")).Font(Font(12, true)).ColorAndOpacity(Leaf))
			: bLocked
			? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::Format(LOCTEXT("VillageLocked", "Après : {0}"), ErewhonVillage::Info(Info.Requires).Name))
				.Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true))
			: Button(FText::Format(LOCTEXT("VillageBuild", "Construire · {0}"), CoinsText(Info.Price)),
				[WeakController, Id]() { if (WeakController.IsValid()) { WeakController->ServerBuyVillageUpgrade(Id); } },
				EButton::Primary, State->GetCoins() >= Info.Price, 170.f);
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			CardBox(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Info.Name).Font(Font(13, true)).ColorAndOpacity(Ink)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(STextBlock).Text(Info.Description).Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)
				[
					SNew(SBox).WidthOverride(170.f).HAlign(HAlign_Right)[Action]
				],
				12.f)
		];
	}
	return SNew(SScrollBox) + SScrollBox::Slot()[List];
}

#undef LOCTEXT_NAMESPACE
