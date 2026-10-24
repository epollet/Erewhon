#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "World/SectorTypes.h"

class AErewhonPlayerController;
struct FSlateBrush;

DECLARE_DELEGATE_OneParam(FOnMapSectorPicked, const FIntPoint& /*Sector*/);

/**
 * La carte du maire : le pays à perte de vue, peint secteur par secteur (prairie, lisière, forêt,
 * routes et places), les secteurs achetés teintés (à soi, à d'autres) et cernés de la couleur de
 * leur village, la frontière entre deux mairies, le nom de chaque village sur sa place et, au bord
 * de la carte comme aux sorties des places, le village où mène chaque route.
 * On la fait glisser à la souris, on zoome à la molette (ou par ZoomBy), un clic choisit un secteur.
 * Nord (+X) en haut, est (+Y) à droite. Seuls les secteurs visibles sont parcourus.
 */
class SSectorMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSectorMap) : _Controller(nullptr), _Village(FIntPoint::ZeroValue), _Selected(FIntPoint::ZeroValue) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
		/** Le village de la mairie : la carte s'ouvre sur sa place. */
		SLATE_ARGUMENT(FIntPoint, Village)
		SLATE_ARGUMENT(FIntPoint, Selected)
		SLATE_EVENT(FOnMapSectorPicked, OnSectorPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetSelected(const FIntPoint& Sector);
	/** Zoom autour du milieu de la carte (> 1 : on s'approche). */
	void ZoomBy(float Factor);
	/** Revient sur la place du village de la mairie, au zoom d'ouverture. */
	void Recenter();

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(400.0, 310.0); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	/** Pixels par secteur : de tout le pays d'un coup d'œil à un secteur bien lisible. */
	static constexpr float MinZoom = 8.f;
	static constexpr float MaxZoom = 64.f;
	static constexpr float StartZoom = 28.f;

private:
	/** Ce qui ne change pas d'un secteur, calculé une fois : son type, la mairie dont il dépend, ses routes. */
	struct FMapCell
	{
		ESectorType Type = ESectorType::Undefined;
		FIntPoint Nearest = FIntPoint::ZeroValue;
		/** Côtés par lesquels passe la route (ErewhonVillages::RoadSides). */
		uint8 Roads = 0;
		bool bRoad = false;
		bool bPlaza = false;
		/** Sur une route : son tracé qui serpente (ErewhonVillages::RoadCurve), en fraction du secteur. */
		bool bCurve = false;
		FVector2D Curve[4];
	};
	FMapCell CellAt(const FIntPoint& Sector) const;

	/** Carte (en secteurs, continue : le secteur x couvre [x, x + 1[) → widget (px), et retour. */
	FVector2f ToLocal(const FVector2f& MapPoint, const FVector2f& Size) const;
	FVector2f ToMap(const FVector2f& Local, const FVector2f& Size) const;
	FIntPoint SectorAt(const FVector2f& Local, const FVector2f& Size) const;

	FText HoverText() const;
	EActiveTimerReturnType Refresh(double InCurrentTime, float InDeltaTime);

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	FOnMapSectorPicked OnSectorPicked;
	FIntPoint Village = FIntPoint::ZeroValue;
	FIntPoint Selected = FIntPoint::ZeroValue;

	/** Le point de la carte au milieu du widget, en secteurs (X nord, Y est). */
	FVector2f ViewCenter = FVector2f::ZeroVector;
	float Zoom = StartZoom;

	bool bPressed = false;
	bool bDragging = false;
	FVector2f PressLocal = FVector2f::ZeroVector;
	FVector2f LastLocal = FVector2f::ZeroVector;
	TOptional<FIntPoint> Hovered;

	/** Pinceaux arrondis, pris une fois (ErewhonUI::Rounded les met en cache par couleur). */
	const FSlateBrush* BackBrush = nullptr;
	const FSlateBrush* PillBrush = nullptr;
	const FSlateBrush* HomePillBrush = nullptr;
	const FSlateBrush* PlayerBrush = nullptr;

	mutable TMap<FIntPoint, FMapCell> Cells;
};
