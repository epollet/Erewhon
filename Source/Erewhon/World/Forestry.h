#pragma once

#include "CoreMinimal.h"

struct FTreeInstance;
class UInventoryComponent;

/**
 * Le métier de forestier : la hache abat les arbres de ses secteurs ; le tronc couché se débite en bûches,
 * la cime éclate en branches feuillues. Un arbre abattu laisse sa souche, et repousse au bout de quelques jours.
 * Les arbres abattus voyagent avec les récoltes (même liste, répliquée et sauvegardée), sous un index négatif.
 */
namespace ErewhonForestry
{
	inline const FName Axe = TEXT("axe");
	inline const FName Log = TEXT("log");
	inline const FName Branch = TEXT("branch");

	/** On ne porte que deux bûches ; un tas en tient vingt ; la charrette, six (trois places de deux). */
	constexpr int32 LogCarry = 2;
	constexpr int32 PileCapacity = 20;

	/** La chute d'un arbre (s), le temps de détacher une bûche, l'écart minimal entre deux coups de hache. */
	constexpr float FallSeconds = 2.4f;
	constexpr float ChopLogSeconds = 1.f;
	constexpr float HitInterval = 0.3f;
	/** Les branches restent à terre un quart d'heure ; l'arbre repousse en trois jours. */
	constexpr double BranchSeconds = 900.0;
	constexpr double TreeRegrowDays = 3.0;

	/** L'arbre d'index Tree, dans la liste des récoltes. */
	inline int32 HarvestIndexOfTree(int32 Tree) { return -(Tree + 1); }
	inline bool IsTreeHarvest(int32 Index) { return Index < 0; }
	inline int32 TreeOfHarvestIndex(int32 Index) { return -Index - 1; }

	/** Le temps qu'il faut à une récolte (plante ou arbre) pour repousser. */
	EREWHON_API double RegrowSeconds(int32 HarvestIndex);

	/** Coups de hache pour l'abattre ; son tronc (longueur, rayon, en cm) ; les bûches qu'on en tire. */
	EREWHON_API int32 HitsToFell(const FTreeInstance& Tree);
	EREWHON_API float TrunkLength(const FTreeInstance& Tree);
	EREWHON_API float TrunkRadius(const FTreeInstance& Tree);
	EREWHON_API int32 LogsIn(const FTreeInstance& Tree);

	/** Les bûches qu'on porte (poches et main), et combien on peut encore en prendre. */
	EREWHON_API int32 CarriedLogs(const UInventoryComponent* Inventory);
	inline int32 RoomForLogs(const UInventoryComponent* Inventory) { return FMath::Max(0, LogCarry - CarriedLogs(Inventory)); }
}
