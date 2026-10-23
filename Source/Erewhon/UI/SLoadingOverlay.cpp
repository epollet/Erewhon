#include "UI/SLoadingOverlay.h"

#include "UI/ErewhonSlate.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ErewhonLoadingOverlay"

namespace
{
	/** Largeur de la barre d'avancement (px). */
	constexpr float GLoadingBarWidth = 360.f;
}

void SLoadingOverlay::Construct(const FArguments& InArgs)
{
	Progress = InArgs._Progress;
	// Le monde ne doit pas transparaître, et la souris passe au travers (le contrôleur bloque les entrées).
	SetVisibility(EVisibility::HitTestInvisible);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(ErewhonUI::WhiteBrush()).BorderBackgroundColor(ErewhonUI::Parchment)
		]
		// Un filet de reliure autour de la page.
		+ SOverlay::Slot().Padding(22.f)
		[
			SNew(SBorder).BorderImage(ErewhonUI::Rounded(FLinearColor::Transparent, 16.f, ErewhonUI::Edge.CopyWithNewOpacity(0.45f), 1.5f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("Title", "Erewhon")).Font(ErewhonUI::Font(44, true)).ColorAndOpacity(ErewhonUI::Ink)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 10)
			[
				SNew(SBox).WidthOverride(64.f).HeightOverride(3.f)
				[
					SNew(SBorder).BorderImage(ErewhonUI::Rounded(ErewhonUI::Leaf, 1.5f))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 34)
			[
				SNew(STextBlock).Text(LOCTEXT("Tagline", "Le village s'éveille, les prés se déplient…")).Font(ErewhonUI::Font(14)).ColorAndOpacity(ErewhonUI::MutedInk)
			]
			// La barre : un sillon d'argile clair qui se remplit de vert.
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(GLoadingBarWidth).HeightOverride(8.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SBorder).BorderImage(ErewhonUI::Rounded(ErewhonUI::Edge.CopyWithNewOpacity(0.22f), 4.f))
					]
					+ SOverlay::Slot().HAlign(HAlign_Left)
					[
						SNew(SBox)
						.WidthOverride_Lambda([this]() { return FOptionalSize(FMath::Clamp(Progress.Get(0.f), 0.f, 1.f) * GLoadingBarWidth); })
						[
							SNew(SBorder).BorderImage(ErewhonUI::Rounded(ErewhonUI::Leaf, 4.f))
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12, 0, 0)
			[
				SNew(STextBlock).Text(InArgs._Status).Font(ErewhonUI::Font(12)).ColorAndOpacity(ErewhonUI::MutedInk)
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
