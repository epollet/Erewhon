#include "World/SectorManager.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonSettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "World/Sector.h"
#include "World/SectorLayout.h"

namespace
{
	/** Secteurs avec plantes construits au plus par image, pour lisser le chargement. */
	constexpr int32 GMaxPlantSectorsPerTick = 1;
}

ASectorManager::ASectorManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
}

void ASectorManager::BeginPlay()
{
	Super::BeginPlay();
	if (AErewhonGameState* State = AErewhonGameState::Get(this))
	{
		HarvestHandle = State->OnPlantHarvestChanged.AddUObject(this, &ASectorManager::OnPlantHarvestChanged);
	}
	// Le premier pawn apparaît avant le premier Tick : son sol doit déjà exister.
	Refresh();
}

void ASectorManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AErewhonGameState* State = AErewhonGameState::Get(this))
	{
		State->OnPlantHarvestChanged.Remove(HarvestHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void ASectorManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Refresh();
}

void ASectorManager::Refresh()
{
	AErewhonGameState* State = AErewhonGameState::Get(this);
	UWorld* World = GetWorld();
	if (!State)
	{
		return;
	}
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	const bool bCanRender = GetNetMode() != NM_DedicatedServer;

	// Secteur voulu → avec plantes ? (le rendu l'emporte sur le sol seul)
	TMap<FIntPoint, bool> Wanted;
	const auto Want = [&Wanted](const FVector& Center, int32 Radius, bool bPlants)
	{
		const FIntPoint Middle = ErewhonSector::FromWorld(Center);
		for (int32 DY = -Radius; DY <= Radius; ++DY)
		{
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				bool& bEntry = Wanted.FindOrAdd(Middle + FIntPoint(DX, DY), false);
				bEntry |= bPlants;
			}
		}
	};

	bool bHasLocalView = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const bool bLocal = Controller->IsLocalController() && bCanRender;
		bHasLocalView |= bLocal;
		Want(Pawn->GetActorLocation(), bLocal ? Settings.SectorRenderDistance : 1, bLocal);
	}
	if (!bHasLocalView)
	{
		// En attendant un pawn : le spawn, prêt à accueillir le joueur.
		Want(ErewhonSector::Center(FIntPoint::ZeroValue), 1, bCanRender);
	}

	// Décharger ce qui n'est plus voulu. Un secteur à compléter garde son sol en attendant.
	for (auto It = Loaded.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It.Key()))
		{
			It.Value()->Destroy();
			It.RemoveCurrent();
		}
	}

	// Les secteurs les plus proches du joueur local d'abord.
	const APlayerController* LocalController = World->GetFirstPlayerController();
	const FVector Focus = LocalController && LocalController->GetPawn()
		? LocalController->GetPawn()->GetActorLocation()
		: ErewhonSector::Center(FIntPoint::ZeroValue);
	TArray<FIntPoint> Pending;
	for (const auto& Pair : Wanted)
	{
		const TObjectPtr<ASector>* Existing = Loaded.Find(Pair.Key);
		if (!Existing || (Pair.Value && !(*Existing)->HasPlants()))
		{
			Pending.Add(Pair.Key);
		}
	}
	Pending.Sort([&Focus](const FIntPoint& A, const FIntPoint& B)
	{
		return FVector::DistSquared2D(ErewhonSector::Center(A), Focus) < FVector::DistSquared2D(ErewhonSector::Center(B), Focus);
	});

	USectorLayoutSubsystem* Layouts = World->GetSubsystem<USectorLayoutSubsystem>();
	int32 PlantBudget = GMaxPlantSectorsPerTick;
	for (const FIntPoint& Coord : Pending)
	{
		const bool bPlants = Wanted[Coord];
		TObjectPtr<ASector>* Existing = Loaded.Find(Coord);
		if (bPlants)
		{
			if (PlantBudget <= 0)
			{
				// Faute de budget, au moins le sol pour ne pas tomber dans le vide.
				if (!Existing)
				{
					ASector* Ground = World->SpawnActor<ASector>();
					Ground->Initialize(Layouts->GetLayout(Coord, State->GetWorldSeed()), false);
					Loaded.Add(Coord, Ground);
				}
				continue;
			}
			--PlantBudget;
		}

		if (Existing)
		{
			(*Existing)->Destroy();
			Loaded.Remove(Coord);
		}
		ASector* Sector = World->SpawnActor<ASector>();
		Sector->Initialize(Layouts->GetLayout(Coord, State->GetWorldSeed()), bPlants);
		if (bPlants)
		{
			if (const TSet<int32>* Harvested = State->GetHarvestedIn(Coord))
			{
				for (const int32 PlantIndex : *Harvested)
				{
					Sector->SetPlantHarvested(PlantIndex, true);
				}
			}
		}
		Loaded.Add(Coord, Sector);
	}
}

void ASectorManager::OnPlantHarvestChanged(const FIntPoint& Sector, int32 PlantIndex, bool bHarvested)
{
	if (const TObjectPtr<ASector>* Found = Loaded.Find(Sector))
	{
		(*Found)->SetPlantHarvested(PlantIndex, bHarvested);
	}
}
