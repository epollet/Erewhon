#include "UI/SInventoryPanel.h"

#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Input/DragAndDrop.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "World/Backpack.h"

#define LOCTEXT_NAMESPACE "ErewhonInventoryPanel"

using namespace ErewhonUI;

namespace
{
	constexpr float GSlotSize = 64.f;
	constexpr int32 GInventoryColumns = 3;
	constexpr int32 GClothingColumns = 2;
	constexpr int32 GEquipmentColumns = 1;
	constexpr int32 GBagColumns = 3;
	constexpr float GColumnGap = 18.f;
	const FLinearColor BagAccent = FLinearColor::FromSRGBColor(FColor(112, 128, 72));

	float ColumnWidth(int32 Columns) { return Columns * (GSlotSize + 6.f); }

	/** Les slots d'une ligne, répartis par colonne. */
	struct FRow
	{
		FText Label;
		TArray<int32> Inventory;
		TArray<int32> Clothing;
		TArray<int32> Equipment;
	};

	/** Un objet qu'on fait glisser d'un slot à un autre. */
	class FItemDragOp : public FDragDropOperation
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FItemDragOp, FDragDropOperation)

		bool bFromBag = false;
		int32 Index = INDEX_NONE;
		const FItemDef* Def = nullptr;

		static TSharedRef<FItemDragOp> New(bool bFromBag, int32 Index, const FItemDef* Def)
		{
			TSharedRef<FItemDragOp> Op = MakeShared<FItemDragOp>();
			Op->bFromBag = bFromBag;
			Op->Index = Index;
			Op->Def = Def;
			Op->Construct();
			return Op;
		}

		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNew(SBox).WidthOverride(GSlotSize).HeightOverride(GSlotSize)[ItemIcon(Def, GSlotSize - 12.f)];
		}
	};

	DECLARE_DELEGATE_TwoParams(FOnItemDropped, bool /*bFromBag*/, int32 /*Index*/);

	/** Case d'inventaire : clic, glisser, déposer. Elle s'éclaire au survol et sous un objet glissé. */
	class SItemSlot : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SItemSlot) : _bBag(false), _Index(INDEX_NONE), _Def(nullptr), _bDraggable(false), _bDropTarget(false), _Accent(FLinearColor::White) {}
			SLATE_ARGUMENT(bool, bBag)
			SLATE_ARGUMENT(int32, Index)
			SLATE_ARGUMENT(const FItemDef*, Def)
			SLATE_ARGUMENT(bool, bDraggable)
			SLATE_ARGUMENT(bool, bDropTarget)
			SLATE_ARGUMENT(FLinearColor, Accent)
			SLATE_EVENT(FSimpleDelegate, OnClicked)
			SLATE_EVENT(FOnItemDropped, OnItemDropped)
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bBag = InArgs._bBag;
			Index = InArgs._Index;
			Def = InArgs._Def;
			bDraggable = InArgs._bDraggable && Def;
			bDropTarget = InArgs._bDropTarget;
			OnClicked = InArgs._OnClicked;
			OnItemDropped = InArgs._OnItemDropped;
			Normal = Rounded(Mix(Card, InArgs._Accent, Def ? 0.12f : 0.06f), 10.f, Mix(InArgs._Accent, Card, 0.25f), 2.f);
			Hovered = Rounded(Mix(Card, Gold, 0.25f), 10.f, Gold, 2.5f);
			ChildSlot
			[
				SNew(SBox).WidthOverride(GSlotSize).HeightOverride(GSlotSize)
				[
					SNew(SBorder).BorderImage(this, &SItemSlot::GetBrush).Padding(3.f)[InArgs._Content.Widget]
				]
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			bPressed = true;
			return bDraggable ? FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton) : FReply::Handled();
		}

		virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() == EKeys::LeftMouseButton && bPressed)
			{
				bPressed = false;
				OnClicked.ExecuteIfBound();
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

		virtual FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			bPressed = false;
			return FReply::Handled().BeginDragDrop(FItemDragOp::New(bBag, Index, Def));
		}

		virtual void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) override
		{
			bDragOver = bDropTarget && Event.GetOperationAs<FItemDragOp>().IsValid();
		}

		virtual void OnDragLeave(const FDragDropEvent& Event) override
		{
			bDragOver = false;
		}

		virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
		{
			bDragOver = false;
			const TSharedPtr<FItemDragOp> Op = Event.GetOperationAs<FItemDragOp>();
			if (!Op.IsValid() || !bDropTarget)
			{
				return FReply::Unhandled();
			}
			OnItemDropped.ExecuteIfBound(Op->bFromBag, Op->Index);
			return FReply::Handled();
		}

	private:
		const FSlateBrush* GetBrush() const { return IsHovered() || bDragOver ? Hovered : Normal; }

		bool bBag = false;
		int32 Index = INDEX_NONE;
		const FItemDef* Def = nullptr;
		bool bDraggable = false;
		bool bDropTarget = false;
		bool bPressed = false;
		bool bDragOver = false;
		const FSlateBrush* Normal = nullptr;
		const FSlateBrush* Hovered = nullptr;
		FSimpleDelegate OnClicked;
		FOnItemDropped OnItemDropped;
	};
}

void SInventoryPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	OpenedBag = InArgs._Bag;

	if (UInventoryComponent* Inventory = GetInventory())
	{
		Inventory->OnInventoryChanged.AddSP(this, &SInventoryPanel::Rebuild);
	}
	if (UBackpackComponent* Backpack = GetBackpack())
	{
		Backpack->OnBackpackChanged.AddSP(this, &SInventoryPanel::Rebuild);
	}
	if (AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr)
	{
		State->OnCoinsChanged.AddSP(this, &SInventoryPanel::Rebuild);
	}

	const float Width = ColumnWidth(GInventoryColumns) + ColumnWidth(GClothingColumns) + ColumnWidth(GEquipmentColumns) + 2 * GColumnGap + 70.f
		+ (ShowsBag() ? ColumnWidth(GBagColumns) + 2 * GColumnGap + 10.f : 0.f);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(ShowsBag() ? LOCTEXT("TitleBag", "Inventaire et sac à dos") : LOCTEXT("Title", "Inventaire"), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			Width)
	];
	Rebuild();
}

UInventoryComponent* SInventoryPanel::GetInventory() const
{
	const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	return State ? State->GetInventory() : nullptr;
}

UBackpackComponent* SInventoryPanel::GetBackpack() const
{
	const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	return State ? State->GetBackpack() : nullptr;
}

bool SInventoryPanel::ShowsBag() const
{
	const UBackpackComponent* Backpack = GetBackpack();
	return Backpack && Backpack->IsOwned() && (Backpack->IsWorn() || (OpenedBag.IsValid() && Backpack->GetDropped() == OpenedBag.Get()));
}

FReply SInventoryPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if ((Key == EKeys::Escape || Key == EKeys::I || Key == EKeys::Tab || Key == EKeys::F) && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	if (Key == EKeys::G && ShowsBag() && Controller.IsValid())
	{
		Controller->ServerStashAll();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SInventoryPanel::BuildSlot(bool bBag, int32 SlotIndex, const FLinearColor& Accent, const FText& EmptyLabel) const
{
	const TArray<FInventorySlot>& Slots = bBag ? GetBackpack()->GetSlots() : GetInventory()->GetSlots();
	const FInventorySlot& Slot = Slots[SlotIndex];
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

	// Avec le sac ouvert, les objets rangés (pas ce qu'on porte ou tient) passent d'un côté à l'autre.
	const bool bExchange = ShowsBag() && (bBag || Slot.Kind == ESlotKind::Inventory);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TSharedRef<SItemSlot> Widget = SNew(SItemSlot)
		.bBag(bBag)
		.Index(SlotIndex)
		.Def(Def)
		.bDraggable(bExchange)
		.bDropTarget(bExchange)
		.Accent(Accent)
		.OnClicked_Lambda([WeakController, bBag, SlotIndex]()
		{
			AErewhonPlayerController* PC = WeakController.Get();
			const AErewhonPlayerState* State = PC ? PC->GetPlayerState<AErewhonPlayerState>() : nullptr;
			if (!State)
			{
				return;
			}
			if (bBag)
			{
				PC->ServerMoveItem(true, SlotIndex, false, INDEX_NONE);   // reprendre dans ses poches
			}
			else
			{
				State->GetInventory()->ServerUseSlot(SlotIndex);       // équiper / ranger
			}
		})
		.OnItemDropped_Lambda([WeakController, bBag, SlotIndex](bool bFromBag, int32 From)
		{
			if (WeakController.IsValid())
			{
				WeakController->ServerMoveItem(bFromBag, From, bBag, SlotIndex);
			}
		})
		[
			Content
		];
	if (Def)
	{
		Widget->SetToolTip(ItemTooltip(*Def));
	}
	return Widget;
}

TSharedRef<SWidget> SInventoryPanel::BuildBagColumn() const
{
	const TArray<FInventorySlot>& Slots = GetBackpack()->GetSlots();
	TSharedRef<SUniformGridPanel> Storage = SNew(SUniformGridPanel).SlotPadding(3.f);
	TSharedRef<SUniformGridPanel> Tools = SNew(SUniformGridPanel).SlotPadding(3.f);
	int32 StorageCount = 0;
	int32 ToolCount = 0;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].Kind == ESlotKind::Inventory)
		{
			Storage->AddSlot(StorageCount % GBagColumns, StorageCount / GBagColumns)[BuildSlot(true, i, BagAccent, FText::GetEmpty())];
			++StorageCount;
		}
		else
		{
			Tools->AddSlot(ToolCount, 0)[BuildSlot(true, i, EquipmentAccent, LOCTEXT("ToolSide", "Outil"))];
			++ToolCount;
		}
	}
	const bool bWorn = GetBackpack()->IsWorn();
	return SNew(SBox).WidthOverride(ColumnWidth(GBagColumns))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(STextBlock).Text(LOCTEXT("BagHeader", "Sac à dos")).Font(Font(11, true)).ColorAndOpacity(BagAccent)
			]
			+ SVerticalBox::Slot().AutoHeight()[Separator()]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Storage]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 4)
			[
				SNew(STextBlock).Text(LOCTEXT("BagTools", "Outils accrochés")).Font(Font(10, true)).ColorAndOpacity(MutedInk)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[Tools]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[
				SNew(STextBlock)
				.Text(bWorn ? LOCTEXT("BagWorn", "Sur votre dos. [F] le poser au sol.") : LOCTEXT("BagDropped", "[G] y ranger toutes vos poches."))
				.Font(Font(9)).ColorAndOpacity(MutedInk).AutoWrapText(true)
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
			Grid->AddSlot(n % Columns, n / Columns)[BuildSlot(false, Indices[n], Accent, Empty)];
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

	const bool bBag = ShowsBag();
	TSharedRef<SWidget> Own = SNew(SVerticalBox)
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
		];

	TSharedRef<SHorizontalBox> Columns = SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[Own];
	if (bBag)
	{
		Columns->AddSlot().AutoWidth().Padding(GColumnGap * 2.f, 0, 0, 0)[BuildBagColumn()];
	}

	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SNew(SBox).HAlign(HAlign_Left)[CoinBadge(State->GetCoins(), 14)]
		]
		+ SVerticalBox::Slot().AutoHeight()[Columns]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(STextBlock)
			.Text(bBag
				? LOCTEXT("HelpBag", "Glisser-déposer : échanger avec le sac. Clic : équiper / ranger, ou reprendre un objet du sac.")
				: LOCTEXT("Help", "Clic : équiper / ranger. Survol : description."))
			.Font(Font(9)).ColorAndOpacity(MutedInk)
		]);
}

#undef LOCTEXT_NAMESPACE
