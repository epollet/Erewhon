#include "UI/SInventoryPanel.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SCanvas.h"
#include "Framework/Application/SlateApplication.h"

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
			SLATE_EVENT(FSimpleDelegate, OnRightClicked)
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
			OnRightClicked = InArgs._OnRightClicked;
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
			if (Event.GetEffectingButton() == EKeys::RightMouseButton)
			{
				OnRightClicked.ExecuteIfBound();
				return FReply::Handled();
			}
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
		FSimpleDelegate OnRightClicked;
		FOnItemDropped OnItemDropped;
	};
}

void SInventoryPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	OpenedBag = InArgs._Bag;
	OpenedStorage = InArgs._Storage;
	OpenedCart = InArgs._Cart;
	if (ACart* Cart = OpenedCart.Get())
	{
		Cart->OnWoodChanged.AddSP(this, &SInventoryPanel::Rebuild);
	}
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
			: OpenedStorage.IsValid() && OpenedStorage->GetKind() == EStorageKind::Shelf ? 7 : GBagColumns) + 2 * GColumnGap + 10.f : 0.f);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
		Window(OpenedCart.IsValid() ? LOCTEXT("TitleCart", "Inventaire et charrette")
			: OpenedStorage.IsValid() ? FText::Format(LOCTEXT("TitleStorage", "Inventaire et {0}"), ErewhonPlaceables::KindName(OpenedStorage->GetKind()).ToLower())
			: ShowsBag() ? LOCTEXT("TitleBag", "Inventaire et sac à dos") : LOCTEXT("Title", "Inventaire"), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			Width)
		]
		// « Combien ? » (Maj + clic droit), au milieu.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(ChooserHost, SBox)
		]
		// La pile qu'on tient (après une division) suit la souris.
		+ SOverlay::Slot()
		[
			SNew(SCanvas).Visibility(EVisibility::HitTestInvisible)
			+ SCanvas::Slot()
			.Position_Lambda([this]()
			{
				const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
				return PanelGeometry.AbsoluteToLocal(Cursor) - FVector2D(GSlotSize * 0.5f, GSlotSize * 0.5f);
			})
			.Size(FVector2D(GSlotSize, GSlotSize))
			[
				SNew(SBox).Visibility_Lambda([this]() { return Split.IsSet() && Split->bHolding ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SAssignNew(HeldIcon, SBox)]
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
					[
						SNew(SBorder).BorderImage(Rounded(Ink, 7.f)).Padding(FMargin(5.f, 0.f))
						[
							SNew(STextBlock).Text_Lambda([this]() { return Split.IsSet() ? FText::AsNumber(Split->Count) : FText::GetEmpty(); })
							.Font(Font(9, true)).ColorAndOpacity(FLinearColor::White)
						]
					]
				]
			]
		]
	];
	Rebuild();
}

void SInventoryPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	PanelGeometry = AllottedGeometry;
}

void SInventoryPanel::BeginSplit(bool bFromOther, int32 SlotIndex, bool bChoose)
{
	const TArray<FInventorySlot>& Slots = bFromOther ? OtherSlots() : GetInventory()->GetSlots();
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty() || Slots[SlotIndex].Kind == ESlotKind::Clothing)
	{
		return;
	}
	FSplit New;
	New.bFromOther = bFromOther;
	New.From = SlotIndex;
	New.Max = Slots[SlotIndex].Count;
	New.Count = FMath::Max(1, New.Max / 2);
	// Sans Maj, ou pour un seul exemplaire : pas de question, la moitié part sous la souris.
	New.bHolding = !bChoose || New.Max <= 1;
	Split = New;
	Rebuild();
	if (!New.bHolding && ChooserEntry.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(ChooserEntry, EFocusCause::SetDirectly);
	}
}

void SInventoryPanel::TestSplitChooser()
{
	const TArray<FInventorySlot>& Slots = GetInventory()->GetSlots();
	const int32 First = Slots.IndexOfByPredicate([](const FInventorySlot& S) { return S.Kind == ESlotKind::Inventory && S.Count > 1; });
	if (First != INDEX_NONE)
	{
		BeginSplit(false, First, true);
	}
}

void SInventoryPanel::ConfirmSplit()
{
	if (Split.IsSet() && !Split->bHolding)
	{
		Split->bHolding = true;
		Rebuild();
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

bool SInventoryPanel::DropSplit(bool bToOther, int32 SlotIndex)
{
	if (!Split.IsSet() || !Split->bHolding)
	{
		return false;
	}
	const FSplit Held = Split.GetValue();
	Split.Reset();
	if (Controller.IsValid() && (Held.bFromOther != bToOther || Held.From != SlotIndex))
	{
		Controller->ServerSplitMove(Held.bFromOther, Held.From, bToOther, SlotIndex, Held.Count, OpenedStorage.Get(), OpenedCart.Get());
	}
	Rebuild();
	return true;
}

void SInventoryPanel::DropOne(bool bToOther, int32 SlotIndex)
{
	if (!Split.IsSet() || !Split->bHolding || (Split->bFromOther == bToOther && Split->From == SlotIndex) || !Controller.IsValid())
	{
		return;
	}
	Controller->ServerSplitMove(Split->bFromOther, Split->From, bToOther, SlotIndex, 1, OpenedStorage.Get(), OpenedCart.Get());
	if (--Split->Count <= 0)
	{
		Split.Reset();
	}
	Rebuild();
}

TSharedRef<SWidget> SInventoryPanel::BuildSplitChooser() const
{
	if (!Split.IsSet() || Split->bHolding)
	{
		return SNullWidget::NullWidget;
	}
	SInventoryPanel* Self = const_cast<SInventoryPanel*>(this);
	const auto Step = [Self](int32 Delta)
	{
		return [Self, Delta]()
		{
			if (Self->Split.IsSet())
			{
				Self->Split->Count = FMath::Clamp(Self->Split->Count + Delta, 1, Self->Split->Max);
			}
		};
	};
	// Une petite flèche (celle des arbres de Slate, retournée pour monter).
	const auto Arrow = [](bool bUp, TFunction<void()> OnClick)
	{
		return SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
			.ContentPadding(FMargin(8.f, 2.f))
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(SImage)
				.Image(FCoreStyle::Get().GetBrush("TreeArrow_Expanded"))
				.ColorAndOpacity(FSlateColor(FLinearColor::White))
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
				.RenderTransform(bUp ? FSlateRenderTransform(FQuat2D(UE_PI)) : FSlateRenderTransform())
			];
	};
	return SNew(SBorder).BorderImage(Rounded(Card, 10.f, Gold, 2.f)).Padding(14.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock).Text(FText::Format(LOCTEXT("SplitHow", "Combien dans la nouvelle pile ? (sur {0})"), FText::AsNumber(Split->Max)))
				.Font(Font(11, true)).ColorAndOpacity(Ink)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(80.f)
					[
						SAssignNew(Self->ChooserEntry, SEditableTextBox)
						.Font(Font(14, true))
						.Justification(ETextJustify::Center)
						.SelectAllTextWhenFocused(true)
						.Text_Lambda([Self]() { return Self->Split.IsSet() ? FText::AsNumber(Self->Split->Count, &FNumberFormattingOptions::DefaultNoGrouping()) : FText::GetEmpty(); })
						.OnTextChanged_Lambda([Self](const FText& Typed)
						{
							if (Self->Split.IsSet() && Typed.ToString().IsNumeric())
							{
								Self->Split->Count = FMath::Clamp(FCString::Atoi(*Typed.ToString()), 1, Self->Split->Max);
							}
						})
						.OnTextCommitted_Lambda([Self](const FText&, ETextCommit::Type How)
						{
							if (How == ETextCommit::OnEnter)
							{
								Self->ConfirmSplit();
							}
						})
						.OnKeyDownHandler_Lambda([Self](const FGeometry&, const FKeyEvent& Event)
						{
							// Les flèches du clavier : un de plus, un de moins.
							if (Self->Split.IsSet() && (Event.GetKey() == EKeys::Up || Event.GetKey() == EKeys::Down))
							{
								Self->Split->Count = FMath::Clamp(Self->Split->Count + (Event.GetKey() == EKeys::Up ? 1 : -1), 1, Self->Split->Max);
								return FReply::Handled();
							}
							return FReply::Unhandled();
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[Arrow(true, Step(1))]
					+ SVerticalBox::Slot().AutoHeight()[Arrow(false, Step(-1))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
				[
					Button(LOCTEXT("SplitOk", "OK"), [Self]() { Self->ConfirmSplit(); }, EButton::Primary, true, 60.f, 11)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("SplitKeys", "Entrée : prendre    Échap : annuler")).Font(Font(9)).ColorAndOpacity(MutedInk)
			]
		];
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
	if (const ACart* Cart = OpenedCart.Get())
	{
		return Cart->GetWood();
	}
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
	if (ACart* Cart = OpenedCart.Get())
	{
		Controller->ServerMoveCartItem(Cart, bFromOther, From, bToOther, To);
	}
	else if (AStorageActor* Storage = OpenedStorage.Get())
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
	if (OpenedStorage.IsValid() || OpenedCart.IsValid())
	{
		return true;   // la colonne de droite montre le conteneur ouvert, ou la charrette
	}
	const UBackpackComponent* Backpack = GetBackpack();
	// Sur le dos, le sac reste fermé : il faut le poser pour y fouiller.
	return Backpack && Backpack->IsOwned() && !Backpack->IsWorn() && OpenedBag.IsValid() && Backpack->GetDropped() == OpenedBag.Get();
}

FReply SInventoryPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape && Split.IsSet())
	{
		Split.Reset();
		Rebuild();
		return FReply::Handled();
	}
	if ((Key == EKeys::Escape || Key == EKeys::I || Key == EKeys::Tab || Key == EKeys::F) && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	if (Key == EKeys::G && ShowsBag() && Controller.IsValid())
	{
		if (ACart* Cart = OpenedCart.Get())
		{
			Controller->ServerStashInCart(Cart);
		}
		else if (AStorageActor* Storage = OpenedStorage.Get())
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
	if (bBag && OpenedCart.IsValid())
	{
		return;   // on reprend son petit bois, on ne le jette pas depuis la charrette
	}
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
	// Ses propres poches et sa barre : on y glisse ses objets d'une case à l'autre (les outils dans la barre).
	const bool bOwnSlot = !bBag && (Slot.Kind == ESlotKind::Inventory || Slot.Kind == ESlotKind::Equipment);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TSharedRef<SItemSlot> Widget = SNew(SItemSlot)
		.bBag(bBag)
		.Index(SlotIndex)
		.Def(Def)
		.bDraggable(bExchange || bOwnSlot)
		.bDropTarget(bExchange || bOwnSlot)
		.Accent(Accent)
		.OnRightClicked_Lambda([Self = const_cast<SInventoryPanel*>(this), bBag, SlotIndex]()
		{
			// Clic droit : diviser la pile ; une pile en main, on en pose un exemplaire ici.
			if (Self->Split.IsSet() && Self->Split->bHolding)
			{
				Self->DropOne(bBag, SlotIndex);
				return;
			}
			Self->BeginSplit(bBag, SlotIndex, FSlateApplication::Get().GetModifierKeys().IsShiftDown());
		})
		.OnClicked_Lambda([this, WeakController, bBag, SlotIndex]()
		{
			if (const_cast<SInventoryPanel*>(this)->DropSplit(bBag, SlotIndex))
			{
				return;
			}
			AErewhonPlayerController* PC = WeakController.Get();
			const AErewhonPlayerState* State = PC ? PC->GetPlayerState<AErewhonPlayerState>() : nullptr;
			if (!State)
			{
				return;
			}
			// Ctrl + clic : la pile passe de l'autre côté (cagette, tonneau, sac, charrette), là où il y a de la place.
			if (FSlateApplication::Get().GetModifierKeys().IsControlDown() && ShowsBag())
			{
				MoveItem(bBag, SlotIndex, !bBag, INDEX_NONE);
			}
			else if (bBag)
			{
				MoveItem(true, SlotIndex, false, INDEX_NONE);   // reprendre dans ses poches
			}
			else if (GetInventory()->GetSlots().IsValidIndex(SlotIndex) && GetInventory()->GetSlots()[SlotIndex].ItemId == TEXT("herbal_book"))
			{
				PC->OpenBook();   // le livre se lit
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
			if (!bFromBag && !bBag)
			{
				GetInventory()->ServerMoveSlot(From, SlotIndex);   // dans ses poches, dans sa barre
				return;
			}
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
	if (const ACart* Cart = OpenedCart.Get())
	{
		TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(3.f);
		for (int32 i = 0; i < Cart->GetWood().Num(); ++i)
		{
			Grid->AddSlot(i, 0)[BuildSlot(true, i, BagAccent, FText::GetEmpty())];
		}
		return SNew(SBox).WidthOverride(ColumnWidth(GBagColumns))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock).Text(Cart->GetCartKind() == ECartKind::Seeder ? LOCTEXT("SeederHeader", "Trémie du semoir") : LOCTEXT("CartHeader", "Charrette"))
					.Font(Font(11, true)).ColorAndOpacity(BagAccent)
				]
				+ SVerticalBox::Slot().AutoHeight()[Separator()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Grid]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(STextBlock).Text(Cart->GetCartKind() == ECartKind::Seeder
						? LOCTEXT("SeederHint", "Des sacs de graines : chaque sac sème neuf cases. [G] y verser tous les vôtres.")
						: Cart->HasLoads()
						? LOCTEXT("CartBusy", "Elle porte des conteneurs : pas de place pour du petit bois en vrac.")
						: LOCTEXT("CartHint", "Du petit bois en vrac, trente brassées. [G] y charger tout le vôtre."))
					.Font(Font(9)).ColorAndOpacity(MutedInk).AutoWrapText(true)
				]
			];
	}
	if (const AStorageActor* Shelf = OpenedStorage.Get(); Shelf && Shelf->GetKind() == EStorageKind::Shelf)
	{
		// L'étagère ouverte, dessinée comme elle est : ses étages de haut en bas, une pile par étage.
		using namespace ErewhonApothecary;
		const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
		TSharedRef<SVerticalBox> Floors = SNew(SVerticalBox);
		for (int32 Row = ShelfRows - 1; Row >= 0; --Row)
		{
			TSharedRef<SHorizontalBox> Places = SNew(SHorizontalBox);
			for (int32 Column = 0; Column < ShelfSlotsPerRow; ++Column)
			{
				const int32 Index = Row * ShelfSlotsPerRow + Column;
				if (Shelf->GetSlots().IsValidIndex(Index))
				{
					Places->AddSlot().AutoWidth().Padding(0, 0, 3, 0)[BuildSlot(true, Index, BagAccent, FText::GetEmpty())];
				}
			}
			Floors->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
			[
				// Une planche par étage : son numéro, puis ses cinq emplacements.
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
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Places]
				]
			];
		}
		(void)Items;
		return SNew(SBox).WidthOverride(ColumnWidth(ShelfSlotsPerRow) + 70.f)
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
					SNew(STextBlock).Text(LOCTEXT("ShelvesHint", "Potions, bases et écrasés : cinq emplacements par étage, une pile entière par emplacement. Glissez-les où vous voulez ; [G] tout ranger ici."))
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
					SNew(STextBlock).Text(Opened->GetKind() == EStorageKind::FirewoodRack
						? LOCTEXT("WoodRackHint", "Du petit bois, dix brassées par place. Chacun peut s'y servir. [G] y ranger le vôtre.")
						: Opened->GetKind() == EStorageKind::DryingRack
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
					SNew(STextBlock).Text(FText::Format(LOCTEXT("ConfirmDiscard", "Poser « {0} » à terre ? Il y restera trois minutes."), Def->Name))
					.Font(Font(11, true)).ColorAndOpacity(Danger)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
				[
					Button(LOCTEXT("DiscardYes", "Poser à terre"), [this, WeakController, Pending]()
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

	if (ChooserHost.IsValid())
	{
		ChooserHost->SetContent(BuildSplitChooser());
	}
	if (HeldIcon.IsValid())
	{
		const TArray<FInventorySlot>& HeldFrom = Split.IsSet() && Split->bFromOther ? OtherSlots() : Slots;
		const FItemDef* HeldDef = Split.IsSet() && HeldFrom.IsValidIndex(Split->From) && !HeldFrom[Split->From].IsEmpty() ? Items->Find(HeldFrom[Split->From].ItemId) : nullptr;
		HeldIcon->SetContent(HeldDef ? ItemIcon(HeldDef, GSlotSize - 12.f) : SNullWidget::NullWidget);
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
				? LOCTEXT("HelpBag", "Glisser-déposer : échanger avec le sac. Clic : équiper / ranger, ou reprendre un objet du sac. Ctrl + clic : de l'autre côté. Clic droit : la moitié (Maj : combien). A : poser à terre.")
				: LOCTEXT("Help", "Clic : équiper / ranger. Glisser : ranger, ou mettre dans la barre. Clic droit : la moitié de la pile (Maj : combien), puis un à un. A : poser à terre."))
			.Font(Font(9)).ColorAndOpacity(MutedInk)
		]);
}

#undef LOCTEXT_NAMESPACE
