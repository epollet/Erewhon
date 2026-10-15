#include "UI/SBookPanel.h"

#include "Core/ErewhonPlayerController.h"
#include "Items/ItemDatabase.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "World/Apothecary.h"

#define LOCTEXT_NAMESPACE "ErewhonBook"

using namespace ErewhonUI;

namespace
{
	constexpr float GBookPanelWidth = 620.f;
	constexpr float GPageHeight = 380.f;
}

void SBookPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(LOCTEXT("Title", "Herbier des breuvages"), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			GBookPanelWidth, LOCTEXT("Subtitle", "« Ce que la marmite sait faire, pour qui sait la conduire. »"))
	];
	ShowPage(0);
}

int32 SBookPanel::PageCount() const
{
	return 1 + ErewhonApothecary::Brews().Num();
}

FReply SBookPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if ((Key == EKeys::Escape || Key == EKeys::I || Key == EKeys::Tab) && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	if (Key == EKeys::Left || Key == EKeys::Q)
	{
		ShowPage(Page - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::D)
	{
		ShowPage(Page + 1);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SBookPanel::ShowPage(int32 NewPage)
{
	Page = FMath::Clamp(NewPage, 0, PageCount() - 1);
	if (!Body.IsValid())
	{
		return;
	}
	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(Rounded(Mix(Card, Parchment, 0.4f), 10.f, Mix(Card, Edge, 0.6f), 1.f)).Padding(18.f)
			[
				SNew(SBox).HeightOverride(GPageHeight)[BuildPage()]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(LOCTEXT("Prev", "◀ Page précédente"), [this]() { ShowPage(Page - 1); }, EButton::Secondary, Page > 0, 160.f, 11)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::Format(LOCTEXT("PageN", "{0} / {1}"), FText::AsNumber(Page + 1), FText::AsNumber(PageCount())))
				.Font(Font(11)).ColorAndOpacity(MutedInk)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(LOCTEXT("Next", "Page suivante ▶"), [this]() { ShowPage(Page + 1); }, EButton::Secondary, Page < PageCount() - 1, 160.f, 11)
			]
		]);
}

TSharedRef<SWidget> SBookPanel::BuildPage() const
{
	using namespace ErewhonApothecary;
	if (Page == 0)
	{
		// L'introduction : comment on conduit le chaudron.
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(STextBlock).Text(LOCTEXT("IntroTitle", "Conduire le chaudron")).Font(Font(18, true)).ColorAndOpacity(Ink)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).AutoWrapText(true).Font(Font(12)).ColorAndOpacity(Ink).LineHeightPercentage(1.15f)
				.Text(LOCTEXT("IntroText",
					"1. Posez un support de chaudron dans la cheminée, et le chaudron dessus.\n"
					"2. D'abord la base de potion : de l'eau (au pichet, puisée au tonneau ; il en tient 10 L) et vingt écrasés d'herbe par litre.\n"
					"3. Toutes les autres potions se font dans la base : jetez-y les ingrédients, en comptant par litre de base.\n"
					"4. Allumez le feu (petit bois). La chaleur dépend de la hauteur du support : en bas, jusqu'à 110 °C ; en haut, 50 °C. "
					"Le chaudron met du temps à chauffer, et refroidit doucement.\n"
					"5. Tenez la température dans la fourchette le temps qu'il faut : tout le liquide devient breuvage.\n"
					"6. Remplissez des fioles (0,5 L chacune ; une fiole en main, clic droit sur le chaudron) : ce sont des potions. Une potion bue rend sa fiole.\n\n"
					"Chaque page qui suit donne un breuvage."))
			];
	}
	const FBrew& Brew = Brews()[Page - 1];
	const UItemDatabase* Items = UItemDatabase::Get(Controller.Get());
	TSharedRef<SVerticalBox> Ingredients = SNew(SVerticalBox);
	for (const TPair<FName, float>& Need : Brew.PerLiter)
	{
		const FItemDef* Def = Items ? Items->Find(Need.Key) : nullptr;
		Ingredients->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[ItemIcon(Def, 40.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::Format((Brew.Liquid == BaseLiquid ? LOCTEXT("PerLiterWater", "{0} × {1} par litre d'eau") : LOCTEXT("PerLiter", "{0} × {1} par litre de base de potion")), FText::AsNumber(Need.Value), Def ? Def->Name : FText::FromName(Need.Key)))
				.Font(Font(12)).ColorAndOpacity(Ink)
			]
		];
	}
	const FItemDef* Potion = Items ? Items->Find(Brew.Potion) : nullptr;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[ItemIcon(Potion, 56.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Brew.Name).Font(Font(18, true)).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Potion ? FText::Format(LOCTEXT("Gives", "En fiole : {0}"), Potion->Name) : FText::GetEmpty()).Font(Font(11)).ColorAndOpacity(MutedInk)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[SNew(STextBlock).Text(LOCTEXT("Ingredients", "Ingrédients")).Font(Font(13, true)).ColorAndOpacity(Ink)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Ingredients]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(STextBlock).Text(FText::Format(LOCTEXT("Heat", "Chaleur : entre {0} et {1} °C, pendant {2} secondes."),
				FText::AsNumber(FMath::RoundToInt32(Brew.MinTemp)), FText::AsNumber(FMath::RoundToInt32(Brew.MaxTemp)), FText::AsNumber(FMath::RoundToInt32(Brew.Seconds))))
			.Font(Font(12, true)).ColorAndOpacity(Clay).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(LOCTEXT("Tolerance", "Rien d'autre dans le chaudron. La proportion souffre un écart d'un cinquième ; la chaleur, aucun."))
			.Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
		];
}

#undef LOCTEXT_NAMESPACE
