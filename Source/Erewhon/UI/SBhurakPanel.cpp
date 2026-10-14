#include "UI/SBhurakPanel.h"

#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "World/Bhurak.h"
#include "World/Storage.h"

#define LOCTEXT_NAMESPACE "ErewhonBhurakPanel"

using namespace ErewhonUI;

namespace
{
	constexpr float GBhurakPanelWidth = 560.f;
}

void SBhurakPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	Bhurak = InArgs._Bhurak;
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(LOCTEXT("Title", "Votre bhurak"), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			GBhurakPanelWidth, LOCTEXT("Subtitle", "Harnais, sacoches aux flancs, sacoche ou selle sur le dos."))
	];
	if (ABhurak* Beast = Bhurak.Get())
	{
		Beast->OnGearChanged.AddSP(this, &SBhurakPanel::Rebuild);
	}
	if (const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr)
	{
		State->GetInventory()->OnInventoryChanged.AddSP(this, &SBhurakPanel::Rebuild);
	}
	Rebuild();
}

FReply SBhurakPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if ((Key == EKeys::Escape || Key == EKeys::E || Key == EKeys::Tab) && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SBhurakPanel::Rebuild()
{
	ABhurak* Beast = Bhurak.Get();
	AErewhonPlayerController* PC = Controller.Get();
	const AErewhonPlayerState* State = PC ? PC->GetPlayerState<AErewhonPlayerState>() : nullptr;
	const UItemDatabase* Items = PC ? UItemDatabase::Get(PC) : nullptr;
	if (!Body.IsValid() || !Beast || !State || !Items)
	{
		return;
	}
	const UInventoryComponent* Inventory = State->GetInventory();
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TWeakObjectPtr<ABhurak> WeakBhurak = Bhurak;
	const auto Equip = [WeakController, WeakBhurak](uint8 Slot, FName Item)
	{
		return [WeakController, WeakBhurak, Slot, Item]()
		{
			if (WeakController.IsValid() && WeakBhurak.IsValid())
			{
				WeakController->ServerEquipBhurak(WeakBhurak.Get(), Slot, Item);
			}
		};
	};
	const auto Row = [&](const FName& ItemId, const FText& Label, const FText& Status, TSharedRef<SWidget> Actions)
	{
		return CardBox(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[ItemIcon(Items->Find(ItemId), 40.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Label).Font(Font(12, true)).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Status).Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Actions],
			10.f);
	};

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	// Le harnais.
	{
		const bool bHarness = Beast->HasHarness();
		const bool bOwnsHarness = Inventory->CountStored(ABhurak::HarnessItem) > 0;
		TSharedRef<SWidget> Action = bHarness
			? Button(LOCTEXT("RemoveHarness", "Retirer"), Equip(3, NAME_None), EButton::Secondary, !Beast->HasAnyGear() && !Beast->GetTowedCart(), 110.f, 11)
			: Button(LOCTEXT("PutHarness", "Harnacher"), Equip(3, ABhurak::HarnessItem), EButton::Primary, bOwnsHarness, 110.f, 11);
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Row(ABhurak::HarnessItem, LOCTEXT("Harness", "Harnais"),
				bHarness ? LOCTEXT("HarnessOn", "Harnaché : il porte sacoches et selle, et tire une charrette.")
				: bOwnsHarness ? LOCTEXT("HarnessReady", "Vous en avez un sur vous.") : LOCTEXT("HarnessNone", "Il vous en faut un (General Store, 500 C)."),
				Action)
		];
	}
	// Les trois places.
	const FText SlotNames[ABhurak::GearSlots] = { LOCTEXT("Left", "Flanc gauche"), LOCTEXT("Back", "Dos"), LOCTEXT("Right", "Flanc droit") };
	const bool bOwnsBag = Inventory->CountStored(ABhurak::SaddlebagItem) > 0;
	const bool bOwnsSaddle = Inventory->CountStored(ABhurak::SaddleItem) > 0;
	for (int32 Slot = 0; Slot < ABhurak::GearSlots; ++Slot)
	{
		const FName Worn = Beast->GetGear(Slot);
		const uint8 SlotByte = static_cast<uint8>(Slot);
		TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
		FText Status;
		if (Worn == ABhurak::SaddlebagItem)
		{
			AStorageActor* Bag = Beast->GetBag(Slot);
			int32 Used = 0;
			for (const FInventorySlot& Inside : Bag ? Bag->GetSlots() : TArray<FInventorySlot>())
			{
				Used += Inside.IsEmpty() ? 0 : 1;
			}
			Status = FText::Format(LOCTEXT("BagStatus", "Une sacoche : {0} / {1} places prises."), FText::AsNumber(Used), FText::AsNumber(Bag ? Bag->GetSlots().Num() : 0));
			TWeakObjectPtr<AStorageActor> WeakBag = Bag;
			Actions->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
			[
				Button(LOCTEXT("OpenBag", "Ouvrir"), [WeakController, WeakBag]() { if (WeakController.IsValid() && WeakBag.IsValid()) { WeakController->OpenStorage(WeakBag.Get()); } },
					EButton::Primary, Bag != nullptr, 90.f, 11)
			];
			Actions->AddSlot().AutoWidth()[Button(LOCTEXT("RemoveBag", "Retirer"), Equip(SlotByte, NAME_None), EButton::Secondary, Used == 0, 90.f, 11)];
		}
		else if (Worn == ABhurak::SaddleItem)
		{
			Status = LOCTEXT("SaddleStatus", "La selle : [F] en visant le bhurak pour monter (+50 % de vitesse).");
			Actions->AddSlot().AutoWidth()[Button(LOCTEXT("RemoveSaddle", "Retirer"), Equip(SlotByte, NAME_None), EButton::Secondary, !Beast->GetRider(), 90.f, 11)];
		}
		else
		{
			Status = !Beast->HasHarness() ? LOCTEXT("NeedHarness", "Libre — il faut d'abord le harnais.")
				: Slot == ABhurak::BackSlot ? LOCTEXT("FreeBack", "Libre : une sacoche, ou la selle.") : LOCTEXT("FreeSide", "Libre : une sacoche.");
			Actions->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
			[
				Button(LOCTEXT("PutBag", "Sacoche"), Equip(SlotByte, ABhurak::SaddlebagItem), EButton::Primary, Beast->HasHarness() && bOwnsBag, 90.f, 11)
			];
			if (Slot == ABhurak::BackSlot)
			{
				Actions->AddSlot().AutoWidth()
				[
					Button(LOCTEXT("PutSaddle", "Selle"), Equip(SlotByte, ABhurak::SaddleItem), EButton::Primary, Beast->HasHarness() && bOwnsSaddle, 90.f, 11)
				];
			}
		}
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Row(Worn.IsNone() ? (Slot == ABhurak::BackSlot ? ABhurak::SaddleItem : ABhurak::SaddlebagItem) : Worn, SlotNames[Slot], Status, Actions)
		];
	}
	// Le reprendre (sans rien sur le dos), et les gestes à connaître.
	List->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Help", "[G] Atteler une charrette : en selle à côté d'elle, ou en conduisant la charrette jusqu'à lui. [G] de nouveau pour dételer."))
			.Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
		[
			Button(LOCTEXT("TakeBack", "Le reprendre"), Equip(4, NAME_None), EButton::Secondary,
				!Beast->HasHarness() && !Beast->HasAnyGear() && !Beast->GetRider() && !Beast->GetTowedCart(), 130.f, 11)
		]
	];
	Body->SetContent(List);
}

#undef LOCTEXT_NAMESPACE
