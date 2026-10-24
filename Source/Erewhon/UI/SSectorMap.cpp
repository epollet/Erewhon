#include "UI/SSectorMap.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UI/ErewhonSlate.h"
#include "World/VillageNetwork.h"

#define LOCTEXT_NAMESPACE "ErewhonSectorMap"

using namespace ErewhonUI;

namespace
{
	/** Couleur du pays libre : prairie claire, lisière plus verte, forêt sombre. */
	FLinearColor SectorMapLandColor(ESectorType Type)
	{
		switch (Type)
		{
		case ESectorType::Edge:   return Mix(Leaf, Ink, 0.25f);
		case ESectorType::Forest: return Mix(Leaf, Ink, 0.5f);
		default:                  return Mix(Leaf, Card, 0.25f);
		}
	}

	/** Une teinte par village, stable : le liseré de ses secteurs. */
	FLinearColor SectorMapVillageTint(const FIntPoint& Village)
	{
		const uint32 Hash = GetTypeHash(Village) * 2654435761u;
		return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hash >> 24), 170, 215);
	}

	/** Flèche du côté (ordre d'ErewhonSector::Sides), nord en haut de la carte. */
	const TCHAR* SectorMapArrow(int32 Side)
	{
		static const TCHAR* const Arrows[4] = { TEXT("↑"), TEXT("↓"), TEXT("→"), TEXT("←") };
		return Arrows[FMath::Clamp(Side, 0, 3)];
	}

	/** Le côté vu sur la carte : nord vers le haut (−Y du widget), est vers la droite. */
	FVector2f SectorMapScreenDir(int32 Side)
	{
		static const FVector2f Dirs[4] = { FVector2f(0.f, -1.f), FVector2f(0.f, 1.f), FVector2f(1.f, 0.f), FVector2f(-1.f, 0.f) };
		return Dirs[FMath::Clamp(Side, 0, 3)];
	}
}

void SSectorMap::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	Village = InArgs._Village;
	Selected = InArgs._Selected;
	OnSectorPicked = InArgs._OnSectorPicked;
	BackBrush = Rounded(Mix(Parchment, Edge, 0.12f), 8.f, Mix(Parchment, Edge, 0.45f), 1.f);
	PillBrush = Rounded(Card.CopyWithNewOpacity(0.92f), 6.f, Mix(Card, Edge, 0.6f), 1.f);
	HomePillBrush = Rounded(Card, 6.f, Gold, 1.5f);
	PlayerBrush = Rounded(Gold, 6.f, Ink, 1.5f);
	SetClipping(EWidgetClipping::ClipToBounds);
	SetToolTipText(TAttribute<FText>::CreateSP(this, &SSectorMap::HoverText));
	Recenter();
	// Le marqueur du joueur et les achats des autres : la carte se repeint d'elle-même.
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SSectorMap::Refresh));
}

EActiveTimerReturnType SSectorMap::Refresh(double InCurrentTime, float InDeltaTime)
{
	Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

void SSectorMap::SetSelected(const FIntPoint& Sector)
{
	Selected = Sector;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSectorMap::ZoomBy(float Factor)
{
	Zoom = FMath::Clamp(Zoom * Factor, MinZoom, MaxZoom);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSectorMap::Recenter()
{
	const FIntPoint Plaza = ErewhonVillages::CenterOf(Village);
	ViewCenter = FVector2f(Plaza.X + 0.5f, Plaza.Y + 0.5f);
	Zoom = StartZoom;
	Invalidate(EInvalidateWidgetReason::Paint);
}

SSectorMap::FMapCell SSectorMap::CellAt(const FIntPoint& Sector) const
{
	if (const FMapCell* Found = Cells.Find(Sector))
	{
		return *Found;
	}
	// Une longue promenade sur la carte : on oublie tout plutôt que de grossir sans fin.
	if (Cells.Num() > 60000)
	{
		Cells.Reset();
	}
	const AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	FMapCell Cell;
	Cell.Type = ErewhonSector::TypeAt(Sector, World ? World->GetWorldSeed() : 0);
	Cell.Nearest = ErewhonVillages::NearestVillage(Sector);
	Cell.bPlaza = ErewhonVillages::IsCenter(Sector);
	Cell.bRoad = !Cell.bPlaza && ErewhonVillages::IsRoad(Sector);
	Cell.Roads = Cell.bPlaza || Cell.bRoad ? ErewhonVillages::RoadSides(Sector) : 0;
	Cell.bCurve = Cell.bRoad && ErewhonVillages::RoadCurve(Sector, Cell.Curve);
	return Cells.Add(Sector, Cell);
}

FVector2f SSectorMap::ToLocal(const FVector2f& MapPoint, const FVector2f& Size) const
{
	return FVector2f(Size.X * 0.5f + (MapPoint.Y - ViewCenter.Y) * Zoom, Size.Y * 0.5f - (MapPoint.X - ViewCenter.X) * Zoom);
}

FVector2f SSectorMap::ToMap(const FVector2f& Local, const FVector2f& Size) const
{
	return FVector2f(ViewCenter.X - (Local.Y - Size.Y * 0.5f) / Zoom, ViewCenter.Y + (Local.X - Size.X * 0.5f) / Zoom);
}

FIntPoint SSectorMap::SectorAt(const FVector2f& Local, const FVector2f& Size) const
{
	const FVector2f MapPoint = ToMap(Local, Size);
	return FIntPoint(FMath::FloorToInt(MapPoint.X), FMath::FloorToInt(MapPoint.Y));
}

// ── Dessin ───────────────────────────────────────────────────────────────────

int32 SSectorMap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), BackBrush);
	const AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	const AErewhonPlayerState* State = Controller.IsValid() ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (!World || !State || Size.X < 1.f || Size.Y < 1.f)
	{
		return LayerId;
	}
	const int32 TileLayer = LayerId + 1;
	const int32 LineLayer = LayerId + 2;
	const int32 MarkLayer = LayerId + 3;
	const int32 LabelLayer = LayerId + 4;
	const FString MyKey = State->GetPlayerKey();

	const auto Rect = [&](const FVector2f& TopLeft, const FVector2f& RectSize)
	{
		return AllottedGeometry.ToPaintGeometry(RectSize, FSlateLayoutTransform(TopLeft));
	};
	const auto DrawFrame = [&](const FVector2f& TopLeft, float Side, const FLinearColor& Color, float Thickness, int32 Layer)
	{
		TArray<FVector2f> Points = { TopLeft, TopLeft + FVector2f(Side, 0.f), TopLeft + FVector2f(Side, Side), TopLeft + FVector2f(0.f, Side), TopLeft };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Color, true, Thickness);
	};
	const auto DrawSegment = [&](const FVector2f& From, const FVector2f& To, const FLinearColor& Color, float Thickness, int32 Layer)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>({ From, To }), ESlateDrawEffect::None, Color, true, Thickness);
	};
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// Une étiquette sur pastille, centrée sur Anchor mais toujours entière dans la carte.
	const auto DrawLabel = [&](const FString& Text, const FVector2f& Anchor, const FSlateFontInfo& Info, const FLinearColor& Color, const FSlateBrush* Brush)
	{
		const FVector2f TextSize = Measure->Measure(Text, Info);
		const FVector2f PillSize = TextSize + FVector2f(10.f, 4.f);
		const FVector2f TopLeft(FMath::Clamp(Anchor.X - PillSize.X * 0.5f, 3.f, FMath::Max(3.f, Size.X - PillSize.X - 3.f)),
			FMath::Clamp(Anchor.Y - PillSize.Y * 0.5f, 3.f, FMath::Max(3.f, Size.Y - PillSize.Y - 3.f)));
		FSlateDrawElement::MakeBox(OutDrawElements, LabelLayer, Rect(TopLeft, PillSize), Brush);
		FSlateDrawElement::MakeText(OutDrawElements, LabelLayer + 1, Rect(TopLeft + FVector2f(5.f, 2.f), TextSize), Text, Info, ESlateDrawEffect::None, Color);
	};

	// Les secteurs visibles, même en partie.
	const float HalfH = Size.Y * 0.5f / Zoom;
	const float HalfW = Size.X * 0.5f / Zoom;
	const int32 XMin = FMath::FloorToInt(ViewCenter.X - HalfH);
	const int32 XMax = FMath::FloorToInt(ViewCenter.X + HalfH);
	const int32 YMin = FMath::FloorToInt(ViewCenter.Y - HalfW);
	const int32 YMax = FMath::FloorToInt(ViewCenter.Y + HalfW);

	const float Gap = Zoom >= 16.f ? 1.f : 0.5f;
	const float RoadWidth = FMath::Max(2.f, Zoom * 0.22f);
	const FLinearColor PlazaFill = Mix(RoadColor, Clay, 0.35f);
	const FLinearColor RoadFill = Mix(RoadColor, Card, 0.35f);
	const FLinearColor RoadInk = Mix(RoadColor, Ink, 0.3f);
	const FLinearColor OthersTint = Mix(Danger, Card, 0.35f);
	const FLinearColor BoundaryInk = Mix(Edge, Ink, 0.3f).CopyWithNewOpacity(0.55f);
	const FSlateFontInfo GlyphFont = Font(FMath::Clamp(FMath::RoundToInt(Zoom * 0.4f), 8, 20), true);

	for (int32 X = XMin; X <= XMax; ++X)
	{
		for (int32 Y = YMin; Y <= YMax; ++Y)
		{
			const FIntPoint Sector(X, Y);
			const FMapCell Cell = CellAt(Sector);
			const FVector2f TopLeft = ToLocal(FVector2f(X + 1.f, static_cast<float>(Y)), Size);

			FLinearColor Fill = Cell.bPlaza ? PlazaFill : Cell.bRoad ? RoadFill : SectorMapLandColor(Cell.Type);
			const FString* Owner = World->FindSectorOwnerKey(Sector);
			if (Owner)
			{
				Fill = Mix(Fill, *Owner == MyKey ? Sky : OthersTint, 0.65f);
			}
			FSlateDrawElement::MakeBox(OutDrawElements, TileLayer, Rect(TopLeft + FVector2f(Gap, Gap), FVector2f(Zoom - 2.f * Gap, Zoom - 2.f * Gap)),
				WhiteBrush(), ESlateDrawEffect::None, Fill);

			// Le liseré du village de ses secteurs achetés.
			if (Owner && Zoom >= 12.f)
			{
				const float Inset = Gap + 1.f;
				DrawFrame(TopLeft + FVector2f(Inset, Inset), Zoom - 2.f * Inset, SectorMapVillageTint(Cell.Nearest), Zoom >= 24.f ? 2.f : 1.f, LineLayer);
			}

			// La frontière entre deux mairies : chacune vend les secteurs plus proches de sa place.
			if (CellAt(Sector + FIntPoint(1, 0)).Nearest != Cell.Nearest)
			{
				DrawSegment(TopLeft, TopLeft + FVector2f(Zoom, 0.f), BoundaryInk, 1.5f, LineLayer);
			}
			if (CellAt(Sector + FIntPoint(0, 1)).Nearest != Cell.Nearest)
			{
				DrawSegment(TopLeft + FVector2f(Zoom, 0.f), TopLeft + FVector2f(Zoom, Zoom), BoundaryInk, 1.5f, LineLayer);
			}

			// La route : hors des places, son tracé qui serpente, en quelques segments ; sur une place, un
			// ruban du milieu vers chaque côté où elle continue.
			if (Cell.bCurve)
			{
				TArray<FVector2f> Points;
				for (int32 Step = 0; Step <= 8; ++Step)
				{
					const FVector2D AtMap = FVector2D(Sector) + ErewhonVillages::RoadCurvePoint(Cell.Curve, Step / 8.0);
					Points.Add(ToLocal(FVector2f(AtMap), Size));
				}
				FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, AllottedGeometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, RoadInk, true, RoadWidth);
			}
			else if (Cell.Roads)
			{
				const FVector2f Middle = TopLeft + FVector2f(Zoom * 0.5f, Zoom * 0.5f);
				for (int32 Side = 0; Side < 4; ++Side)
				{
					if (Cell.Roads & (1 << Side))
					{
						DrawSegment(Middle, Middle + SectorMapScreenDir(Side) * (Zoom * 0.5f), RoadInk, RoadWidth, LineLayer);
					}
				}
				// Le coude, sans encoche.
				FSlateDrawElement::MakeBox(OutDrawElements, LineLayer, Rect(Middle - FVector2f(RoadWidth, RoadWidth) * 0.5f, FVector2f(RoadWidth, RoadWidth)),
					WhiteBrush(), ESlateDrawEffect::None, RoadInk);
			}
			if (Cell.bPlaza && Zoom >= 16.f)
			{
				const FString Glyph = TEXT("⌂");   // maisonnette
				const FVector2f GlyphSize = Measure->Measure(Glyph, GlyphFont);
				FSlateDrawElement::MakeText(OutDrawElements, MarkLayer, Rect(TopLeft + (FVector2f(Zoom, Zoom) - GlyphSize) * 0.5f, GlyphSize), Glyph, GlyphFont,
					ESlateDrawEffect::None, FLinearColor::White);
			}
		}
	}

	// Le survol, puis le secteur choisi.
	if (Hovered.IsSet() && !bDragging)
	{
		DrawFrame(ToLocal(FVector2f(Hovered->X + 1.f, static_cast<float>(Hovered->Y)), Size), Zoom, FLinearColor::White.CopyWithNewOpacity(0.75f), 1.5f, MarkLayer);
	}
	DrawFrame(ToLocal(FVector2f(Selected.X + 1.f, static_cast<float>(Selected.Y)), Size), Zoom, Gold, Zoom >= 20.f ? 3.f : 2.f, MarkLayer);

	// Le joueur, ramené au bord s'il est hors de vue.
	if (const APawn* Pawn = Controller->GetPawn())
	{
		const double SectorSize = UErewhonSettings::Get().SectorSize();
		const FVector Location = Pawn->GetActorLocation();
		const FVector2f At = ToLocal(FVector2f(static_cast<float>(Location.X / SectorSize), static_cast<float>(Location.Y / SectorSize)), Size);
		const FVector2f Dot(FMath::Clamp(At.X, 7.f, Size.X - 7.f), FMath::Clamp(At.Y, 7.f, Size.Y - 7.f));
		FSlateDrawElement::MakeBox(OutDrawElements, LabelLayer + 2, Rect(Dot - FVector2f(6.f, 6.f), FVector2f(12.f, 12.f)), PlayerBrush);
	}

	// Les noms des villages, sur leur place.
	const FSlateFontInfo NameFont = Font(Zoom >= 20.f ? 11 : 9, true);
	const FSlateFontInfo SignFont = Font(Zoom >= 20.f ? 9 : 8, true);
	TArray<FIntPoint> Villages;
	ErewhonVillages::VillagesIn(FIntPoint(XMin - 1, YMin - 1), FIntPoint(XMax + 1, YMax + 1), Villages);
	for (const FIntPoint& Shown : Villages)
	{
		const FIntPoint Plaza = ErewhonVillages::CenterOf(Shown);
		const FVector2f Above = ToLocal(FVector2f(Plaza.X + 1.f, Plaza.Y + 0.5f), Size) - FVector2f(0.f, 11.f);
		DrawLabel(ErewhonVillages::VillageName(Shown).ToString(), Above, NameFont, Ink, Shown == Village ? HomePillBrush : PillBrush);

		// De près, à chaque sortie de la place, le village où mène la route.
		if (Zoom >= 36.f)
		{
			for (int32 Side = 0; Side < 4; ++Side)
			{
				FIntPoint Destination;
				if (ErewhonVillages::RoadDestination(Plaza, Side, Destination))
				{
					const FIntPoint Exit = Plaza + ErewhonSector::Sides[Side] * 2;
					DrawLabel(FString::Printf(TEXT("%s %s"), SectorMapArrow(Side), *ErewhonVillages::VillageName(Destination).ToString()),
						ToLocal(FVector2f(Exit.X + 0.5f, Exit.Y + 0.5f), Size), SignFont, MutedInk, PillBrush);
				}
			}
		}
	}

	// Au bord de la carte, là où une route en sort : le village où elle mène, s'il est hors de vue.
	const int32 FXMin = FMath::CeilToInt(ViewCenter.X - HalfH);
	const int32 FXMax = FMath::FloorToInt(ViewCenter.X + HalfH) - 1;
	const int32 FYMin = FMath::CeilToInt(ViewCenter.Y - HalfW);
	const int32 FYMax = FMath::FloorToInt(ViewCenter.Y + HalfW) - 1;
	if (FXMin <= FXMax && FYMin <= FYMax)
	{
		const auto InView = [&](const FIntPoint& Sector)
		{
			return Sector.X >= FXMin && Sector.X <= FXMax && Sector.Y >= FYMin && Sector.Y <= FYMax;
		};
		for (int32 Side = 0; Side < 4; ++Side)
		{
			// Nord et sud : la rangée du haut ou du bas ; est et ouest : la colonne de droite ou de gauche.
			const bool bRow = Side < 2;
			const int32 Fixed = Side == 0 ? FXMax : Side == 1 ? FXMin : Side == 2 ? FYMax : FYMin;
			const int32 First = bRow ? FYMin : FXMin;
			const int32 Last = bRow ? FYMax : FXMax;
			TSet<FIntPoint> Signed;
			for (int32 Along = First; Along <= Last; ++Along)
			{
				const FIntPoint Sector = bRow ? FIntPoint(Fixed, Along) : FIntPoint(Along, Fixed);
				FIntPoint Destination;
				if (!(CellAt(Sector).Roads & (1 << Side)) || !ErewhonVillages::RoadDestination(Sector, Side, Destination)
					|| InView(ErewhonVillages::CenterOf(Destination)) || Signed.Contains(Destination))
				{
					continue;
				}
				Signed.Add(Destination);
				const FVector2f Middle = ToLocal(FVector2f(Sector.X + 0.5f, Sector.Y + 0.5f), Size);
				const FVector2f Anchor = Side == 0 ? FVector2f(Middle.X, 0.f) : Side == 1 ? FVector2f(Middle.X, Size.Y)
					: Side == 2 ? FVector2f(Size.X, Middle.Y) : FVector2f(0.f, Middle.Y);
				DrawLabel(FString::Printf(TEXT("%s %s"), SectorMapArrow(Side), *ErewhonVillages::VillageName(Destination).ToString()),
					Anchor, SignFont, Ink, PillBrush);
			}
		}
	}
	return LabelLayer + 2;
}

// ── Souris ───────────────────────────────────────────────────────────────────

FReply SSectorMap::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	bDragging = false;
	PressLocal = LastLocal = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSectorMap::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	// Un clic sans glisser : on choisit le secteur.
	if (!bDragging)
	{
		const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		SetSelected(SectorAt(Local, MyGeometry.GetLocalSize()));
		OnSectorPicked.ExecuteIfBound(Selected);
	}
	bDragging = false;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SSectorMap::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Size = MyGeometry.GetLocalSize();
	if (bPressed && HasMouseCapture())
	{
		if (!bDragging && FVector2f::Distance(Local, PressLocal) > 4.f)
		{
			bDragging = true;
		}
		if (bDragging)
		{
			// La carte suit la main : vers la droite, on découvre l'ouest.
			const FVector2f Delta = Local - LastLocal;
			ViewCenter.Y -= Delta.X / Zoom;
			ViewCenter.X += Delta.Y / Zoom;
		}
	}
	LastLocal = Local;
	const TOptional<FIntPoint> Over = Local.X >= 0.f && Local.Y >= 0.f && Local.X < Size.X && Local.Y < Size.Y
		? TOptional<FIntPoint>(SectorAt(Local, Size)) : TOptional<FIntPoint>();
	if (Over != Hovered || bDragging)
	{
		Hovered = Over;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return bPressed ? FReply::Handled() : FReply::Unhandled();
}

FReply SSectorMap::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// On zoome vers le curseur : le secteur pointé reste sous la souris.
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Size = MyGeometry.GetLocalSize();
	const FVector2f Pointed = ToMap(Local, Size);
	Zoom = FMath::Clamp(Zoom * FMath::Pow(1.2f, MouseEvent.GetWheelDelta()), MinZoom, MaxZoom);
	ViewCenter = FVector2f(Pointed.X + (Local.Y - Size.Y * 0.5f) / Zoom, Pointed.Y - (Local.X - Size.X * 0.5f) / Zoom);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SSectorMap::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);
	if (!bPressed)
	{
		Hovered.Reset();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FCursorReply SSectorMap::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(bDragging ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
}

FText SSectorMap::HoverText() const
{
	const AErewhonGameState* World = AErewhonGameState::Get(Controller.Get());
	if (!Hovered.IsSet() || bDragging || !World)
	{
		return FText::GetEmpty();
	}
	const FIntPoint Sector = Hovered.GetValue();
	const FText Name = FText::FromString(ErewhonSector::ToString(Sector));
	FIntPoint PlazaOf;
	if (ErewhonVillages::IsCenter(Sector, &PlazaOf))
	{
		return FText::Format(LOCTEXT("TipPlaza", "Secteur {0} · la place de {1}"), Name, ErewhonVillages::VillageName(PlazaOf));
	}
	const FMapCell Cell = CellAt(Sector);
	const FText Land = ErewhonSector::TypeLabel(Cell.Type);
	if (World->FindSectorOwnerKey(Sector))
	{
		return FText::Format(LOCTEXT("TipOwned", "Secteur {0} · {1} · {2}, à {3}"), Name, Land,
			ErewhonVillages::VillageName(Cell.Nearest), FText::FromString(World->GetSectorOwnerName(Sector)));
	}
	return FText::Format(LOCTEXT("TipFree", "Secteur {0} · {1} · mairie de {2}"), Name, Land, ErewhonVillages::VillageName(Cell.Nearest));
}

#undef LOCTEXT_NAMESPACE
