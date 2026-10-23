#include "UI/SReportPanel.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Core/ErewhonPlayerController.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ErewhonReportPanel"

using namespace ErewhonUI;

void SReportPanel::Construct(const FArguments& InArgs, ErewhonReport::FCapture InCapture)
{
	Controller = InArgs._Controller;
	Capture = MoveTemp(InCapture);

	// L'aperçu : la capture elle-même, réduite par sa boîte (FColor est en BGRA, comme le veut la brosse).
	constexpr float PreviewWidth = 360.f;
	TSharedRef<SWidget> PreviewWidget = SNew(STextBlock).Text(LOCTEXT("NoShot", "Capture d'écran indisponible.")).Font(Font(11)).ColorAndOpacity(MutedInk);
	if (!Capture.Pixels.IsEmpty())
	{
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Capture.Pixels.GetData()), Capture.Pixels.Num() * sizeof(FColor));
		Preview = FSlateDynamicImageBrush::CreateWithImageData(
			*FString::Printf(TEXT("ErewhonReport_%s"), *Capture.Time.ToString(TEXT("%H%M%S%s"))), FVector2D(Capture.Size), Bytes);
		PreviewWidget = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SBox).WidthOverride(PreviewWidth).HeightOverride(PreviewWidth * Capture.Size.Y / FMath::Max(1, Capture.Size.X))
				[
					SNew(SImage).Image(Preview.Get())
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bWithScreenshot ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bWithScreenshot = State == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(LOCTEXT("WithShot", "Joindre la capture d'écran")).Font(Font(11)).ColorAndOpacity(Ink)
				]
			];
	}

	const FString Where = FString::Printf(TEXT("Secteur %s\n%s\n%s"),
		*Capture.Find(TEXT("Joueur"), TEXT("Secteur")), *Capture.Find(TEXT("Joueur"), TEXT("Position")), *Capture.Find(TEXT("Partie"), TEXT("Heure")));

	const TSharedRef<SVerticalBox> Form = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Text(LOCTEXT("Kind", "Type")).Font(Font(13, true)).ColorAndOpacity(Ink)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SAssignNew(CategoryRow, SBox)[CategoryButtons()]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Text(LOCTEXT("TitleLabel", "Titre")).Font(Font(13, true)).ColorAndOpacity(Ink)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SAssignNew(TitleBox, SEditableTextBox)
			.Font(Font(12))
			.HintText(LOCTEXT("TitleHint", "En quelques mots"))
			.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter && DescriptionBox.IsValid())
				{
					FSlateApplication::Get().SetKeyboardFocus(DescriptionBox);
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Text(LOCTEXT("DescriptionLabel", "Description")).Font(Font(13, true)).ColorAndOpacity(Ink)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SNew(SBox).HeightOverride(150.f)
			[
				SAssignNew(DescriptionBox, SMultiLineEditableTextBox)
				.Font(Font(12))
				.AutoWrapText(true)
				.HintText(LOCTEXT("DescriptionHint", "Ce qui se passe, ce qui était attendu, comment y arriver…"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Button(LOCTEXT("Save", "Enregistrer"), [this]() { Submit(); }, EButton::Primary, true, 130.f)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(LOCTEXT("Cancel", "Annuler"), [this]() { Cancel(); }, EButton::Secondary, true, 100.f)]
		];

	const TSharedRef<SWidget> Content = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 16, 0)[Form]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[PreviewWidget]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).WidthOverride(PreviewWidth)
				[
					SNew(STextBlock).Text(FText::FromString(Where)).Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
				]
			]
		];

	ChildSlot
	[
		Window(LOCTEXT("Title", "Signaler"), Content, FSimpleDelegate::CreateLambda([this]() { Cancel(); }), 900.f,
			LOCTEXT("Subtitle", "Un problème ou un constat : l'écran et l'état du jeu sont joints au signalement."))
	];
}

TSharedRef<SWidget> SReportPanel::GetFocusTarget() const
{
	return TitleBox.IsValid() ? StaticCastSharedRef<SWidget>(TitleBox.ToSharedRef()) : ConstCastSharedRef<SWidget>(AsShared());
}

TSharedRef<SWidget> SReportPanel::CategoryButtons()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const ErewhonReport::ECategory Each : { ErewhonReport::ECategory::Bug, ErewhonReport::ECategory::Observation, ErewhonReport::ECategory::Idea })
	{
		Row->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
		[
			Button(ErewhonReport::CategoryLabel(Each), [this, Each]() { SetCategory(Each); },
				Each == Category ? EButton::TabActive : EButton::Tab, true, 100.f)
		];
	}
	return Row;
}

void SReportPanel::SetCategory(ErewhonReport::ECategory InCategory)
{
	Category = InCategory;
	if (CategoryRow.IsValid())
	{
		CategoryRow->SetContent(CategoryButtons());
	}
}

void SReportPanel::Submit()
{
	const FString Title = TitleBox.IsValid() ? TitleBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString Description = DescriptionBox.IsValid() ? DescriptionBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString Folder = ErewhonReport::Save(Capture, Category, Title, Description, bWithScreenshot);
	if (AErewhonPlayerController* PC = Controller.Get())
	{
		PC->CloseReport();
		PC->ShowToast(Folder.IsEmpty()
			? LOCTEXT("Failed", "Le signalement n'a pas pu être enregistré.")
			: FText::Format(LOCTEXT("Saved", "Signalement enregistré : {0}"), FText::FromString(FPaths::GetCleanFilename(Folder))));
	}
}

void SReportPanel::Cancel()
{
	if (AErewhonPlayerController* PC = Controller.Get())
	{
		PC->CloseReport();
	}
}

FReply SReportPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Enter && InKeyEvent.IsControlDown())
	{
		Submit();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
