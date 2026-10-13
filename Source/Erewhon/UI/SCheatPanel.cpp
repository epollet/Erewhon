#include "UI/SCheatPanel.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Sky/CelestialSystem.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ErewhonCheats"

using namespace ErewhonUI;

void SCheatPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;

	// Une heure du Soleil, en secondes réelles : sa journée dure SunPeriodSeconds.
	const float Hour = UErewhonSettings::Get().SunPeriodSeconds / 24.f;
	const auto Advance = [WeakController](float Seconds)
	{
		return [WeakController, Seconds]() { if (WeakController.IsValid()) { WeakController->ServerCheatAdvanceTime(Seconds); } };
	};
	const auto AddCoins = [WeakController](int32 Amount)
	{
		return [WeakController, Amount]()
		{
			const AErewhonPlayerState* PS = WeakController.IsValid() ? WeakController->GetPlayerState<AErewhonPlayerState>() : nullptr;
			if (PS)
			{
				WeakController->ServerCheatSetCoins(PS->GetCoins() + Amount);
			}
		};
	};

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Text(LOCTEXT("Time", "Temps")).Font(Font(13, true)).ColorAndOpacity(Ink)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(STextBlock).Text(this, &SCheatPanel::ClockText).Font(Font(11)).ColorAndOpacity(MutedInk)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(LOCTEXT("PlusHour", "+1 h"), Advance(Hour), EButton::Secondary, true, 80.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(LOCTEXT("PlusSix", "+6 h"), Advance(Hour * 6.f), EButton::Secondary, true, 80.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(LOCTEXT("PlusTwelve", "+12 h"), Advance(Hour * 12.f), EButton::Secondary, true, 80.f)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(LOCTEXT("PlusDay", "+1 jour"), Advance(Hour * 24.f), EButton::Secondary, true, 90.f)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Text(LOCTEXT("Coins", "Pièces")).Font(Font(13, true)).ColorAndOpacity(Ink)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(120.f)
				[
					SAssignNew(CoinsBox, SEditableTextBox)
					.Text(FText::AsNumber(State ? State->GetCoins() : 0, &FNumberFormattingOptions::DefaultNoGrouping()))
					.Font(Font(12))
					.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit) { if (Commit == ETextCommit::OnEnter) { SetCoins(); } })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()[Button(LOCTEXT("SetCoins", "Définir"), [this]() { SetCoins(); }, EButton::Primary, true, 100.f)]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(LOCTEXT("Plus100", "+100"), AddCoins(100), EButton::Secondary, true, 80.f)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(LOCTEXT("Plus1000", "+1000"), AddCoins(1000), EButton::Secondary, true, 80.f)]
		];

	ChildSlot
	[
		Window(LOCTEXT("Title", "Triche"), Content,
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			420.f, LOCTEXT("Subtitle", "Pour tester. Réservé à l'hôte de la partie."))
	];
}

FText SCheatPanel::ClockText() const
{
	const AErewhonGameState* State = AErewhonGameState::Get(Controller.Get());
	const ACelestialSystem* Celestial = ACelestialSystem::Find(Controller.Get());
	if (!State || !Celestial)
	{
		return FText::GetEmpty();
	}
	const double Clock = State->GetWorldClock();
	return FText::FromString(FString::Printf(TEXT("Soleil %s   ·   Usha %s"),
		*ACelestialSystem::ClockString(Celestial->GetSunOrbit(), Clock), *ACelestialSystem::ClockString(Celestial->GetUshaOrbit(), Clock)));
}

void SCheatPanel::SetCoins()
{
	if (Controller.IsValid() && CoinsBox.IsValid())
	{
		const FString Text = CoinsBox->GetText().ToString().Replace(TEXT(" "), TEXT(""));
		if (Text.IsNumeric())
		{
			Controller->ServerCheatSetCoins(FMath::Max(0, FCString::Atoi(*Text)));
		}
	}
}

FReply SCheatPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && Controller.IsValid())
	{
		Controller->CloseMenu();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
