#include "UI/SInventoryPanel.h"

#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ErewhonInventoryPanel"

using namespace ErewhonUI;

namespace
{
	constexpr float GSlotSize = 64.f;
	constexpr int32 GInventoryColumns = 3;
	constexpr int32 GClothingColumns = 2;
	constexpr int32 GEquipmentColumns = 1;
	constexpr float GColumnGap = 18.f;

	float ColumnWidth(int32 Columns) { return Columns * (GSlotSize + 6.f); }

	/** Les slots d'une ligne, répartis par colonne. */
	struct FRow
	{
		FText Label;
		TArray<int32> Inventory;
		TArray<int32> Clothing;
		TArray<int32> Equipment;
	};
}

void SInventoryPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;

	if (UInventoryComponent* Inventory = GetInventory())
	{
		Inventory->OnInventoryChanged.AddSP(this, &SInventoryPanel::Rebuild);
	}
	if (AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr)
	{
		State->OnCoinsChanged.AddSP(this, &SInventoryPanel::Rebuild);
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(LOCTEXT("Title", "Inventaire"), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			ColumnWidth(GInventoryColumns) + ColumnWidth(GClothingColumns) + ColumnWidth(GEquipmentColumns) + 2 * GColumnGap + 70.f)
	];
	Rebuild();
}

UInventoryComponent* SInventoryPanel::GetInventory() const
{
	const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	return State ? State->GetInventory() : nullptr;
}

FReply SInventoryPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if ((Key == EKeys::Escape || Key == EKeys::I || Key == EKeys::Tab) && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SInventoryPanel::BuildSlot(int32 SlotIndex, const FLinearColor& Accent, const FText& EmptyLabel) const
{
	const UInventoryComponent* Inventory = GetInventory();
	const FInventorySlot& Slot = Inventory->GetSlots()[SlotIndex];
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	const FItemDef* Def = Slot.IsEmpty() || !Items ? nullptr : Items->Find(Slot.ItemId);

	TSharedRef<SOverlay> Content = SNew(SOverlay);
	if (Def)
	{
		Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ItemIcon(Def, GSlotSize - 18.f)];
		if (Slot.Count > 1)
		{
			Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
			[
				SNew(SBorder).BorderImage(Rounded(Ink, 7.f)).Padding(FMargin(5.f, 0.f))
				[
					SNew(STextBlock).Text(FText::AsNumber(Slot.Count)).Font(Font(9, true)).ColorAndOpacity(FLinearColor::White)
				]
			];
		}
	}
	else if (!EmptyLabel.IsEmpty())
	{
		Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(EmptyLabel).Font(Font(8)).ColorAndOpacity(Mix(Accent, MutedInk, 0.4f))
		];
	}

	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TSharedRef<SButton> Button = SNew(SButton)
		.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
		.ContentPadding(3.f)
		.OnClicked_Lambda([WeakController, SlotIndex]()
		{
			const AErewhonPlayerState* State = WeakController.IsValid() ? WeakController->GetPlayerState<AErewhonPlayerState>() : nullptr;
			if (State)
			{
				State->GetInventory()->ServerUseSlot(SlotIndex);
			}
			return FReply::Handled();
		})
		[
			Content
		];
	if (Def)
	{
		Button->SetToolTip(ItemTooltip(*Def));
	}

	// Case arrondie, teintée par son type ; elle s'éclaire au survol.
	const TWeakPtr<SButton> WeakButton = Button;
	const FSlateBrush* Normal = Rounded(Mix(Card, Accent, Def ? 0.12f : 0.06f), 10.f, Mix(Accent, Card, 0.25f), 2.f);
	const FSlateBrush* Hovered = Rounded(Mix(Card, Gold, 0.25f), 10.f, Gold, 2.5f);
	return SNew(SBox).WidthOverride(GSlotSize).HeightOverride(GSlotSize)
		[
			SNew(SBorder)
			.BorderImage_Lambda([WeakButton, Normal, Hovered]()
			{
				const TSharedPtr<SButton> Pinned = WeakButton.Pin();
				return Pinned.IsValid() && Pinned->IsHovered() ? Hovered : Normal;
			})
			.Padding(0.f)
			[
				Button
			]
		];
}

void SInventoryPanel::Rebuild()
{
	if (!Body.IsValid())
	{
		return;
	}
	const UInventoryComponent* Inventory = GetInventory();
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (!Inventory || !Items || !State || Inventory->GetSlots().IsEmpty())
	{
		Body->SetContent(SNew(STextBlock).Text(LOCTEXT("Loading", "Chargement…")).ColorAndOpacity(MutedInk));
		return;
	}
	const TArray<FInventorySlot>& Slots = Inventory->GetSlots();

	// Ligne « Base » puis une ligne par vêtement porté qui apporte des slots.
	TArray<FRow> Rows;
	FRow& Base = Rows.AddDefaulted_GetRef();
	Base.Label = LOCTEXT("Base", "Base");
	for (const EClothingSlot Part : { EClothingSlot::Torso, EClothingSlot::Legs })
	{
		const int32 Worn = Slots.IndexOfByPredicate([Part](const FInventorySlot& S) { return S.Kind == ESlotKind::Clothing && S.BodyPart == Part; });
		const FItemDef* Def = Worn != INDEX_NONE && !Slots[Worn].IsEmpty() ? Items->Find(Slots[Worn].ItemId) : nullptr;
		if (Def && (Def->PocketSlots > 0 || Def->ExtraEquipSlots > 0))
		{
			Rows.AddDefaulted_GetRef().Label = Def->Name;
		}
	}
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FInventorySlot& Slot = Slots[i];
		FRow* Row = &Rows[0];
		if (Slot.ProvidedBy != EClothingSlot::None)
		{
			// Retrouve la ligne du vêtement qui fournit ce slot.
			const int32 Worn = Slots.IndexOfByPredicate([&Slot](const FInventorySlot& S) { return S.Kind == ESlotKind::Clothing && S.BodyPart == Slot.ProvidedBy; });
			const FItemDef* Def = Worn != INDEX_NONE ? Items->Find(Slots[Worn].ItemId) : nullptr;
			Row = Def ? Rows.FindByPredicate([Def](const FRow& R) { return R.Label.EqualTo(Def->Name); }) : nullptr;
			if (!Row)
			{
				continue;
			}
		}
		switch (Slot.Kind)
		{
		case ESlotKind::Inventory: Row->Inventory.Add(i); break;
		case ESlotKind::Clothing:  Row->Clothing.Add(i); break;
		case ESlotKind::Equipment: Row->Equipment.Add(i); break;
		}
	}

	const auto Column = [this, &Slots](const TArray<int32>& Indices, int32 Columns, const FLinearColor& Accent, bool bBaseRow)
	{
		TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(3.f);
		bool bFirstEquipment = bBaseRow;
		for (int32 n = 0; n < Indices.Num(); ++n)
		{
			const FInventorySlot& Slot = Slots[Indices[n]];
			FText Empty;
			if (Slot.Kind == ESlotKind::Clothing)
			{
				Empty = Slot.BodyPart == EClothingSlot::Torso ? LOCTEXT("Torso", "Torse") : LOCTEXT("Legs", "Jambes");
			}
			else if (Slot.Kind == ESlotKind::Equipment)
			{
				Empty = bFirstEquipment ? LOCTEXT("Hands", "Mains") : LOCTEXT("Equip", "Équip.");
				bFirstEquipment = false;
			}
			Grid->AddSlot(n % Columns, n / Columns)[BuildSlot(Indices[n], Accent, Empty)];
		}
		return SNew(SBox).WidthOverride(ColumnWidth(Columns))[Grid];
	};

	TSharedRef<SVerticalBox> RowsBox = SNew(SVerticalBox);
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const FRow& Row = Rows[RowIndex];
		const bool bBase = RowIndex == 0;
		RowsBox->AddSlot().AutoHeight().Padding(0, bBase ? 0 : 10, 0, 4)[Separator()];
		RowsBox->AddSlot().AutoHeight().Padding(0, 2, 0, 4)
		[
			SNew(STextBlock).Text(Row.Label).Font(Font(11, true)).ColorAndOpacity(MutedInk)
		];
		RowsBox->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[Column(Row.Inventory, GInventoryColumns, InventoryAccent, bBase)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(GColumnGap, 0, 0, 0)[Column(Row.Clothing, GClothingColumns, ClothingAccent, bBase)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(GColumnGap, 0, 0, 0)[Column(Row.Equipment, GEquipmentColumns, EquipmentAccent, bBase)]
		];
	}

	const auto Header = [](const FText& Text, int32 Columns, const FLinearColor& Accent, bool bLast = false)
	{
		// La dernière colonne, étroite, laisse déborder son titre vers la droite.
		return SNew(SBox).WidthOverride(bLast ? FOptionalSize() : FOptionalSize(ColumnWidth(Columns)))
			[
				SNew(STextBlock).Text(Text).Font(Font(11, true)).ColorAndOpacity(Accent)
			];
	};

	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SNew(SBox).HAlign(HAlign_Left)[CoinBadge(State->GetCoins(), 14)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[Header(LOCTEXT("InventoryHeader", "Inventory"), GInventoryColumns, InventoryAccent)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(GColumnGap, 0, 0, 0)[Header(LOCTEXT("ClothesHeader", "Clothes"), GClothingColumns, ClothingAccent)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(GColumnGap, 0, 0, 0)[Header(LOCTEXT("EquipmentHeader", "Equipment"), GEquipmentColumns, EquipmentAccent, true)]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(460.f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[RowsBox]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("Help", "Clic : équiper / ranger. Survol : description."))
			.Font(Font(9)).ColorAndOpacity(MutedInk)
		]);
}

#undef LOCTEXT_NAMESPACE
