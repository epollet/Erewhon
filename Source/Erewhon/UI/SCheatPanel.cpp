#include "UI/SCheatPanel.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Sky/CelestialSystem.h"
#include "Sky/ErewhonWeather.h"
#include "UI/ErewhonSlate.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
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
			SNew(STextBlock).Text(LOCTEXT("Weather", "Météo")).Font(Font(13, true)).ColorAndOpacity(Ink)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16).HAlign(HAlign_Left)
		[
			SAssignNew(WeatherMenu, SMenuAnchor)
			.Placement(MenuPlacement_ComboBox)
			.Method(EPopupMethod::UseCurrentWindow)
			.OnGetMenuContent(this, &SCheatPanel::WeatherChoices)
			[
				SNew(SButton)
				.ButtonStyle(&ButtonStyle(EButton::Secondary))
				.OnClicked_Lambda([this]() { WeatherMenu->SetIsOpen(!WeatherMenu->IsOpen()); return FReply::Handled(); })
				[
					SNew(SBox).WidthOverride(240.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(this, &SCheatPanel::WeatherLabel).Font(Font(11, true)).ColorAndOpacity(ButtonTextColor(EButton::Secondary))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
						[
							SNew(SImage).Image(FCoreStyle::Get().GetBrush("ComboButton.Arrow")).ColorAndOpacity(ButtonTextColor(EButton::Secondary))
						]
					]
				]
			]
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

uint8 SCheatPanel::CurrentWeather(bool& bOutForced) const
{
	const AErewhonGameState* State = AErewhonGameState::Get(Controller.Get());
	bOutForced = State && State->IsWeatherForced();
	return State ? static_cast<uint8>(State->SampleWeather().Kind) + 1 : 0;
}

FText SCheatPanel::WeatherLabel() const
{
	bool bForced = false;
	const uint8 Weather = CurrentWeather(bForced);
	if (Weather == 0)
	{
		return FText::GetEmpty();
	}
	return FText::Format(bForced ? LOCTEXT("WeatherForced", "{0} · forcée") : LOCTEXT("WeatherNatural", "{0} · naturelle"),
		ErewhonWeather::KindName(static_cast<ErewhonWeather::EKind>(Weather - 1)));
}

TSharedRef<SWidget> SCheatPanel::WeatherChoices()
{
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	TWeakPtr<SMenuAnchor> WeakMenu = WeatherMenu;
	const auto Choose = [WeakController, WeakMenu](uint8 Weather)
	{
		return [WeakController, WeakMenu, Weather]()
		{
			if (WeakController.IsValid())
			{
				WeakController->ServerCheatSetWeather(Weather);
			}
			if (const TSharedPtr<SMenuAnchor> Menu = WeakMenu.Pin())
			{
				Menu->SetIsOpen(false);
			}
		};
	};

	// Les sept types de temps, du soleil à l'orage, puis le retour au naturel ; le choix en cours surligné.
	bool bForced = false;
	const uint8 Current = CurrentWeather(bForced);
	TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox);
	for (uint8 Weather = 1; Weather <= static_cast<uint8>(ErewhonWeather::EKind::Storm) + 2; ++Weather)
	{
		const bool bNatural = Weather > static_cast<uint8>(ErewhonWeather::EKind::Storm) + 1;
		const bool bActive = bNatural ? !bForced : bForced && Weather == Current;
		const FText Label = bNatural ? LOCTEXT("NaturalWeather", "Naturelle")
			: ErewhonWeather::KindName(static_cast<ErewhonWeather::EKind>(Weather - 1));
		Choices->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			Button(Label, Choose(bNatural ? 0 : Weather), bActive ? EButton::TabActive : EButton::Tab, true, 240.f, 11)
		];
	}
	return SNew(SBorder)
		.BorderImage(Rounded(Card, 8.f, Edge, 1.f))
		.Padding(FMargin(8.f, 8.f, 8.f, 4.f))
		[
			Choices
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
