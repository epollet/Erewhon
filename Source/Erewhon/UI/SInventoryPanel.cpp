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
#include "World/Storage.h"
#include "World/Apothecary.h"

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
	OpenedStorage = InArgs._Storage;
	if (AStorageActor* Storage = OpenedStorage.Get())
	{
		Storage->OnStorageChanged.AddSP(this, &SInventoryPanel::Rebuild);
	}

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
		+ (ShowsBag() ? ColumnWidth(OpenedStorage.IsValid() && OpenedStorage->GetKind() == EStorageKind::Barrel ? 5
			: OpenedStorage.IsValid() && OpenedStorage->GetKind() == EStorageKind::Shelf ? 4 : GBagColumns) + 2 * GColumnGap + 10.f : 0.f);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(OpenedStorage.IsValid() ? FText::Format(LOCTEXT("TitleStorage", "Inventaire et {0}"), ErewhonPlaceables::KindName(OpenedStorage->GetKind()).ToLower())
			: ShowsBag() ? LOCTEXT("TitleBag", "Inventaire et sac à dos") : LOCTEXT("Title", "Inventaire"), SAssignNew(Body, SBox),
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

const TArray<FInventorySlot>& SInventoryPanel::OtherSlots() const
{
	static const TArray<FInventorySlot> None;
	if (const AStorageActor* Storage = OpenedStorage.Get())
	{
		return Storage->GetSlots();
	}
	const UBackpackComponent* Backpack = GetBackpack();
	return Backpack ? Backpack->GetSlots() : None;
}

void SInventoryPanel::MoveItem(bool bFromOther, int32 From, bool bToOther, int32 To) const
{
	if (!Controller.IsValid())
	{
		return;
	}
	if (AStorageActor* Storage = OpenedStorage.Get())
	{
		Controller->ServerMoveStorageItem(Storage, bFromOther, From, bToOther, To);
	}
	else
	{
		Controller->ServerMoveItem(bFromOther, From, bToOther, To);
	}
}

bool SInventoryPanel::ShowsBag() const
{
	if (OpenedStorage.IsValid())
	{
		return true;   // la colonne de droite montre le conteneur ouvert
	}
	const UBackpackComponent* Backpack = GetBackpack();
	// Sur le dos, le sac reste fermé : il faut le poser pour y fouiller.
	return Backpack && Backpack->IsOwned() && !Backpack->IsWorn() && OpenedBag.IsValid() && Backpack->GetDropped() == OpenedBag.Get();
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
		if (AStorageActor* Storage = OpenedStorage.Get())
		{
			Controller->ServerStashInStorage(Storage);
		}
		else
		{
			Controller->ServerStashAll();
		}
		return FReply::Handled();
	}
	if (Key == EKeys::A)
	{
		for (const FSlotRef& Ref : SlotWidgets)
		{
			const TSharedPtr<SWidget> Widget = Ref.Widget.Pin();
			if (Widget.IsValid() && Widget->IsHovered())
			{
				Discard(Ref.bBag, Ref.Index);
				break;
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SInventoryPanel::Discard(bool bBag, int32 SlotIndex)
{
	const TArray<FInventorySlot>& Slots = bBag ? OtherSlots() : GetInventory()->GetSlots();
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	const FItemDef* Def = Slots.IsValidIndex(SlotIndex) && !Slots[SlotIndex].IsEmpty() && Items ? Items->Find(Slots[SlotIndex].ItemId) : nullptr;
	if (!Def || !Controller.IsValid())
	{
		return;
	}
	if (Slots[SlotIndex].Kind == ESlotKind::Clothing)
	{
		Controller->ShowToast(LOCTEXT("TakeOffFirst", "Retirez d'abord ce vêtement."));
		return;
	}
	// Un outil ne se jette pas par mégarde : on demande confirmation.
	if (Def->IsEquipable())
	{
		PendingDiscard = FSlotRef{ nullptr, bBag, SlotIndex };
		Rebuild();
		return;
	}
	Controller->ServerDiscard(bBag, SlotIndex, bBag ? OpenedStorage.Get() : nullptr);
}

TSharedRef<SWidget> SInventoryPanel::BuildSlot(bool bBag, int32 SlotIndex, const FLinearColor& Accent, const FText& EmptyLabel) const
{
	const TArray<FInventorySlot>& Slots = bBag ? OtherSlots() : GetInventory()->GetSlots();
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
		.OnClicked_Lambda([this, WeakController, bBag, SlotIndex]()
		{
			AErewhonPlayerController* PC = WeakController.Get();
			const AErewhonPlayerState* State = PC ? PC->GetPlayerState<AErewhonPlayerState>() : nullptr;
			if (!State)
			{
				return;
			}
			if (bBag)
			{
				MoveItem(true, SlotIndex, false, INDEX_NONE);   // reprendre dans ses poches
			}
			else if (OpenedStorage.IsValid() && OpenedStorage->GetKind() == EStorageKind::Shelf)
			{
				MoveItem(false, SlotIndex, true, INDEX_NONE);   // devant les étagères, un clic y range l'objet (on ne boit pas par mégarde)
			}
			else
			{
				State->GetInventory()->ServerUseSlot(SlotIndex);       // équiper / ranger
			}
		})
		.OnItemDropped_Lambda([this, bBag, SlotIndex](bool bFromBag, int32 From)
		{
			MoveItem(bFromBag, From, bBag, SlotIndex);
		})
		[
			Content
		];
	if (Def)
	{
		Widget->SetToolTip(ItemTooltip(*Def));
	}
	SlotWidgets.Add({ Widget, bBag, SlotIndex });
	return Widget;
}

TSharedRef<SWidget> SInventoryPanel::BuildBagColumn() const
{
	if (const AStorageActor* Shelf = OpenedStorage.Get(); Shelf && Shelf->GetKind() == EStorageKind::Shelf)
	{
		// L'étagère ouverte, dessinée comme elle est : ses étages de haut en bas, une pile par étage.
		using namespace ErewhonApothecary;
		const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
		TSharedRef<SVerticalBox> Floors = SNew(SVerticalBox);
		for (int32 Row = ShelfRows - 1; Row >= 0; --Row)
		{
			const int32 Index = Row;
			const FInventorySlot* Slot = Shelf->GetSlots().IsValidIndex(Index) ? &Shelf->GetSlots()[Index] : nullptr;
			const FItemDef* Def = Slot && !Slot->IsEmpty() && Items ? Items->Find(Slot->ItemId) : nullptr;
			Floors->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
			[
				// Une planche par étage : le numéro, la pile, ce qu'il y a dessus.
				SNew(SBorder).BorderImage(Rounded(Mix(Card, Edge, 0.18f), 6.f, Mix(Card, Edge, 0.5f), 1.f)).Padding(FMargin(6.f, 4.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(SBox).WidthOverride(44.f)
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("FloorN", "Étage {0}"), FText::AsNumber(Row + 1)))
							.Font(Font(9, true)).ColorAndOpacity(MutedInk)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BuildSlot(true, Index, BagAccent, FText::GetEmpty())]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(Def ? FText::Format(LOCTEXT("FloorStack", "{0}\n{1} / {2}"), Def->Name, FText::AsNumber(Slot->Count), FText::AsNumber(PotionsPerSlot))
							: LOCTEXT("FloorEmpty", "Vide"))
						.Font(Font(9)).ColorAndOpacity(Def ? Ink : MutedInk).AutoWrapText(true)
					]
				]
			];
		}
		return SNew(SBox).WidthOverride(ColumnWidth(4))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock).Text(LOCTEXT("ShelfHeader", "Étagère"))
					.Font(Font(11, true)).ColorAndOpacity(BagAccent)
				]
				+ SVerticalBox::Slot().AutoHeight()[Separator()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Floors]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("ShelvesHint", "Potions, bases et écrasés : une pile de 10 par étage. Glissez-les sur l'étage voulu ; [G] tout ranger ici."))
					.Font(Font(9)).ColorAndOpacity(MutedInk).AutoWrapText(true)
				]
			];
	}
	if (const AStorageActor* Opened = OpenedStorage.Get())
	{
		if (Opened->IsWaterBarrel())
		{
			// Un tonneau d'eau ne tient que l'eau : sa jauge, et de quoi le vider.
			TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
			TWeakObjectPtr<AStorageActor> WeakBarrel = const_cast<AStorageActor*>(Opened);
			return SNew(SBox).WidthOverride(ColumnWidth(5))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
					[
						SNew(STextBlock).Text(LOCTEXT("WaterBarrel", "Tonneau d'eau")).Font(Font(11, true)).ColorAndOpacity(Sky)
					]
					+ SVerticalBox::Slot().AutoHeight()[Separator()]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 10)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("WaterLevel", "{0} L / {1} L"), FText::AsNumber(FMath::FloorToInt32(Opened->GetWater())),
							FText::AsNumber(FMath::RoundToInt32(ErewhonPlaceables::WaterBarrelLiters))))
						.Font(Font(16, true)).ColorAndOpacity(Ink)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
					[
						SNew(STextBlock).Text(LOCTEXT("WaterHint", "Il ne tient que l'eau. Posé sous le bec de la fontaine de la place, il se remplit (1 C le litre). Le chaudron y puise."))
						.Font(Font(9)).ColorAndOpacity(MutedInk).AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
					[
						Button(LOCTEXT("EmptyWater", "Vider le tonneau"),
							[WeakController, WeakBarrel]() { if (WeakController.IsValid()) { WeakController->ServerEmptyWater(WeakBarrel.Get()); } }, EButton::Danger, true, 160.f, 11)
					]
				];
		}
		// Cagette ou tonneau : des plantes seulement, sur 3 ou 5 colonnes.
		const int32 Columns = Opened->GetKind() == EStorageKind::Barrel ? 5 : GBagColumns;
		TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(3.f);
		for (int32 i = 0; i < Opened->GetSlots().Num(); ++i)
		{
			Grid->AddSlot(i % Columns, i / Columns)[BuildSlot(true, i, BagAccent, FText::GetEmpty())];
		}
		return SNew(SBox).WidthOverride(ColumnWidth(Columns))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock).Text(ErewhonPlaceables::KindName(Opened->GetKind())).Font(Font(11, true)).ColorAndOpacity(BagAccent)
				]
				+ SVerticalBox::Slot().AutoHeight()[Separator()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(SBox).MaxDesiredHeight(430.f)[SNew(SScrollBox) + SScrollBox::Slot()[Grid]]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(STextBlock).Text(Opened->GetKind() == EStorageKind::DryingRack
						? LOCTEXT("RackHint", "Des plantes, dix par crochet : elles sèchent en quatre minutes. [G] y ranger toutes vos plantes.")
						: LOCTEXT("StorageHint", "Des plantes seulement. [G] y ranger toutes vos plantes."))
					.Font(Font(9)).ColorAndOpacity(MutedInk).AutoWrapText(true)
				]
			];
	}
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
				.Text(bWorn ? LOCTEXT("BagWorn", "Sur votre dos. [F] le poser au sol.") : LOCTEXT("BagDropped", "[G] y ranger toutes vos poches. Hors du menu, [F] remet le sac sur le dos."))
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
	SlotWidgets.Reset();

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

	// Confirmation avant de jeter un outil.
	TSharedRef<SWidget> Confirm = SNew(SBox);
	if (PendingDiscard.IsSet())
	{
		const FSlotRef Pending = PendingDiscard.GetValue();
		const TArray<FInventorySlot>& Source = Pending.bBag ? OtherSlots() : Slots;
		const FItemDef* Def = Source.IsValidIndex(Pending.Index) && !Source[Pending.Index].IsEmpty() ? Items->Find(Source[Pending.Index].ItemId) : nullptr;
		if (Def)
		{
			TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
			Confirm = CardBox(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::Format(LOCTEXT("ConfirmDiscard", "Jeter « {0} » ? Il sera perdu."), Def->Name))
					.Font(Font(11, true)).ColorAndOpacity(Danger)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
				[
					Button(LOCTEXT("DiscardYes", "Jeter"), [this, WeakController, Pending]()
					{
						if (WeakController.IsValid())
						{
							WeakController->ServerDiscard(Pending.bBag, Pending.Index, Pending.bBag ? OpenedStorage.Get() : nullptr);
						}
						PendingDiscard.Reset();
						Rebuild();
					}, EButton::Danger, true, 90.f, 11)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
				[
					Button(LOCTEXT("DiscardNo", "Garder"), [this]() { PendingDiscard.Reset(); Rebuild(); }, EButton::Secondary, true, 90.f, 11)
				],
				8.f);
		}
		else
		{
			PendingDiscard.Reset();
		}
	}

	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SNew(SBox).HAlign(HAlign_Left)[CoinBadge(State->GetCoins(), 14)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, PendingDiscard.IsSet() ? 10 : 0)[Confirm]
		+ SVerticalBox::Slot().AutoHeight()[Columns]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(STextBlock)
			.Text(bBag
				? LOCTEXT("HelpBag", "Glisser-déposer : échanger avec le sac. Clic : équiper / ranger, ou reprendre un objet du sac. A : jeter.")
				: LOCTEXT("Help", "Clic : équiper / ranger. Survol : description. A : jeter."))
			.Font(Font(9)).ColorAndOpacity(MutedInk)
		]);
}

#undef LOCTEXT_NAMESPACE
