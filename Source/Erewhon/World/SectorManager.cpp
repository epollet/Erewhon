#include "World/SectorManager.h"
#include "World/Forestry.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "World/Terrain.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonSettings.h"
#include "Core/ErewhonUserSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "World/ErewhonVisuals.h"
#include "World/Sector.h"
#include "Components/StaticMeshComponent.h"
#include "World/SectorLayout.h"

namespace
{
	/** Secteurs avec plantes construits au plus par image, pour lisser le chargement. */
	constexpr int32 GMaxPlantSectorsPerTick = 1;
	/** Au-delà des secteurs détaillés, quatre rangées de secteurs lointains (sol et arbres), deux construits par image. */
	constexpr int32 GFarRing = 4;
	constexpr int32 GMaxFarSectorsPerTick = 2;
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
		OwnerHandle = State->OnSectorOwnerChanged.AddUObject(this, &ASectorManager::OnSectorOwnerChanged);
		FieldHandle = State->OnFieldChanged.AddUObject(this, &ASectorManager::OnFieldChanged);
		VillageHandle = State->OnVillageChanged.AddWeakLambda(this, [this]()
		{
			// Pavés, tailleur, panneaux, lanternes : tous les secteurs se refont.
			GetWorld()->GetSubsystem<USectorLayoutSubsystem>()->InvalidateAll();
			RebuildAll();
		});
	}
	if (UErewhonUserSettings* User = UErewhonUserSettings::Get())
	{
		DetailHandle = User->OnWorldDetailChanged.AddUObject(this, &ASectorManager::RebuildAll);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		// Une dalle peinte de 1 000 km, quelques centimètres sous le sol réel : l'horizon
		// reste une prairie au-delà des secteurs chargés. Sans collision ni ombre.
		RootComponent = NewObject<USceneComponent>(this, TEXT("Root"));
		RootComponent->RegisterComponent();
		FarGround = ErewhonVisuals::AddShape(this, RootComponent, ErewhonVisuals::EShape::Plane, FVector(0, 0, -4.f),
			FVector(100000000.f, 100000000.f, 1.f), ErewhonVisuals::Palette::Meadow, FRotator::ZeroRotator, false);
		FarGround->SetMaterial(0, ErewhonVisuals::MakePaintedMaterial(FarGround, TEXT("ground_grass"), ASector::GroundTileSize, ErewhonVisuals::Palette::Meadow));
		FarGround->SetCastShadow(false);
		BuildBackdrop();
	}
	// Le premier pawn apparaît avant le premier Tick : son sol doit déjà exister.
	Refresh();
}

void ASectorManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AErewhonGameState* State = AErewhonGameState::Get(this))
	{
		State->OnPlantHarvestChanged.Remove(HarvestHandle);
		State->OnSectorOwnerChanged.Remove(OwnerHandle);
		State->OnFieldChanged.Remove(FieldHandle);
		State->OnVillageChanged.Remove(VillageHandle);
	}
	if (UErewhonUserSettings* User = UErewhonUserSettings::Get())
	{
		User->OnWorldDetailChanged.Remove(DetailHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void ASectorManager::Tick(float DeltaSeconds)
{
	for (const FIntPoint& Sector : DirtyFields)
	{
		if (const TObjectPtr<ASector>* Found = Loaded.Find(Sector); Found && *Found)
		{
			(*Found)->RefreshField();
		}
	}
	DirtyFields.Reset();
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
	const UErewhonUserSettings* User = UErewhonUserSettings::Get();
	const int32 ViewDistance = User ? User->ViewDistanceSectors : Settings.SectorRenderDistance;

	// Secteur voulu → avec plantes ? (le rendu l'emporte sur le sol seul) ; et ceux, lointains, qu'on voit sans détail.
	TMap<FIntPoint, bool> Wanted;
	TSet<FIntPoint> FarWanted;
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
		Want(Pawn->GetActorLocation(), bLocal ? ViewDistance : 1, bLocal);
		if (bLocal)
		{
			// L'anneau lointain : sol simplifié et arbres, pour que l'horizon ne soit pas vide.
			const FIntPoint Middle = ErewhonSector::FromWorld(Pawn->GetActorLocation());
			const int32 Far = ViewDistance + GFarRing;
			for (int32 DY = -Far; DY <= Far; ++DY)
			{
				for (int32 DX = -Far; DX <= Far; ++DX)
				{
					const FIntPoint Coord = Middle + FIntPoint(DX, DY);
					if (!Wanted.Contains(Coord))
					{
						Wanted.Add(Coord, false);
						FarWanted.Add(Coord);
					}
				}
			}
		}
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
	if (FarGround)
	{
		// Recentrée par pas d'un secteur : la texture, projetée sur le monde, ne glisse pas. Elle passe juste
		// sous le point le plus bas des secteurs affichés (leurs coins), pour ne jamais percer le sol d'un creux.
		const FIntPoint Here = ErewhonSector::FromWorld(Focus);
		const FVector Snap = ErewhonSector::Center(Here);
		if (Here != FarGroundSector || !bFarGroundPlaced)
		{
			FarGroundSector = Here;
			bFarGroundPlaced = true;
			float Lowest = 0.f;
			const float Size = Settings.SectorSize();
			for (int32 DY = -ViewDistance - GFarRing; DY <= ViewDistance + GFarRing + 1; ++DY)
			{
				for (int32 DX = -ViewDistance - GFarRing; DX <= ViewDistance + GFarRing + 1; ++DX)
				{
					const FVector2D Corner(FVector2D(ErewhonSector::Origin(Here + FIntPoint(DX, DY))));
					for (const FVector2D& Probe : { Corner, Corner + FVector2D(Size * 0.5f, Size * 0.5f) })
					{
						Lowest = FMath::Min(Lowest, ErewhonTerrain::Height(Probe, State->GetWorldSeed()));
					}
				}
			}
			SetActorLocation(FVector(Snap.X, Snap.Y, FMath::Min(0.f, Lowest - 150.f)));
		}
	}
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
	LocalPending = bHasLocalView ? Pending.Num() : -1;
	LocalWanted = Wanted.Num();

	USectorLayoutSubsystem* Layouts = World->GetSubsystem<USectorLayoutSubsystem>();
	// Les arbres des secteurs lointains : redessinés en lot quand l'anneau change (au plus toutes les deux secondes).
	if (bCanRender)
	{
		TSet<FIntPoint> FarLoaded;
		for (const auto& Pair : Loaded)
		{
			if (Pair.Value && Pair.Value->IsFar())
			{
				FarLoaded.Add(Pair.Key);
			}
		}
		if (FarLoaded.Num() != FarForestCoords.Num() || !FarLoaded.Includes(FarForestCoords))
		{
			const bool bSettled = !Pending.ContainsByPredicate([&FarWanted](const FIntPoint& Coord) { return FarWanted.Contains(Coord); });
			if (bSettled || World->GetTimeSeconds() - FarForestBuiltAt > 2.0)
			{
				RebuildFarForest(FarLoaded);
			}
		}
	}
	int32 PlantBudget = GMaxPlantSectorsPerTick;
	int32 FarBudget = GMaxFarSectorsPerTick;
	for (const FIntPoint& Coord : Pending)
	{
		const bool bPlants = Wanted[Coord];
		const bool bFar = !bPlants && FarWanted.Contains(Coord);
		TObjectPtr<ASector>* Existing = Loaded.Find(Coord);
		if (bFar)
		{
			if (Existing || FarBudget <= 0)
			{
				continue;
			}
			--FarBudget;
			ASector* Distant = World->SpawnActor<ASector>();
			Distant->Initialize(Layouts->GetFarLayout(Coord, State->GetWorldSeed()), false, true);
			FarTreesOf(Coord);   // ses arbres, mis de côté pour le lot
			Loaded.Add(Coord, Distant);
			continue;
		}
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

const TArray<TArray<FTransform>>& ASectorManager::FarTreesOf(const FIntPoint& Coord)
{
	if (const TArray<TArray<FTransform>>* Found = FarTreeCache.Find(Coord))
	{
		return *Found;
	}
	// Par modèle d'arbre, puis la canopée ; avant mise à l'échelle des modèles (voir RebuildFarForest).
	TArray<TArray<FTransform>>& Batches = FarTreeCache.Add(Coord);
	Batches.SetNum(ErewhonForest::TreeModelCount + 1);
	AErewhonGameState* State = AErewhonGameState::Get(this);
	USectorLayoutSubsystem* Layouts = GetWorld()->GetSubsystem<USectorLayoutSubsystem>();
	if (!State || !Layouts)
	{
		return Batches;
	}
	const TSharedRef<const FSectorLayout> Layout = Layouts->GetFarLayout(Coord, State->GetWorldSeed());
	for (int32 Index = 0; Index < Layout->Trees.Num(); ++Index)
	{
		const FTreeInstance& Tree = Layout->Trees[Index];
		if (!Batches.IsValidIndex(Tree.Model) || State->IsHarvested(Coord, ErewhonForestry::HarvestIndexOfTree(Index)))
		{
			continue;
		}
		const float Scale = Tree.Height / 100.f;
		Batches[Tree.Model].Add(FTransform(FRotator(0.f, Tree.Yaw, 0.f), Tree.Location - FVector(0, 0, 10.f), FVector(Scale * Tree.Girth, Scale * Tree.Girth, Scale)));
	}
	ASector::CanopyTransforms(*Layout, Batches.Last());
	// Un cache borné : les secteurs les plus anciens s'en vont.
	if (FarTreeCache.Num() > 600)
	{
		FarTreeCache.Remove(FarTreeCache.CreateConstIterator()->Key);
	}
	return FarTreeCache.FindChecked(Coord);
}

void ASectorManager::RebuildFarForest(const TSet<FIntPoint>& Coords)
{
	AErewhonGameState* State = AErewhonGameState::Get(this);
	USectorLayoutSubsystem* Layouts = GetWorld()->GetSubsystem<USectorLayoutSubsystem>();
	if (!State || !Layouts)
	{
		return;
	}
	FarForestCoords = Coords;
	FarForestBuiltAt = GetWorld()->GetTimeSeconds();
	// Les modèles d'arbres, puis la canopée.
	TArray<UStaticMesh*> Models;
	for (int32 i = 0; i < ErewhonForest::TreeModelCount; ++i)
	{
		Models.Add(ErewhonVisuals::LoadGenerated<UStaticMesh>(TEXT("Meshes"), ErewhonForest::TreeModels[i]));
	}
	Models.Add(ErewhonVisuals::LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bush_box")));
	if (FarForest.Num() != Models.Num())
	{
		FarForest.Init(nullptr, Models.Num());
	}
	TArray<TArray<FTransform>> Batches;
	Batches.SetNum(Models.Num());
	TArray<FTransform> Fits;
	for (UStaticMesh* Model : Models)
	{
		Fits.Add(Model ? ErewhonVisuals::FitToHeight(Model, 100.f) : FTransform::Identity);
	}
	for (const FIntPoint& Coord : Coords)
	{
		// Les arbres de chaque secteur sont déjà calculés : on ne fait que les assembler.
		const TArray<TArray<FTransform>>& Cached = FarTreesOf(Coord);
		for (int32 i = 0; i < Models.Num() && i < Cached.Num(); ++i)
		{
			if (Models[i])
			{
				for (const FTransform& At : Cached[i])
				{
					Batches[i].Add(Fits[i] * At);
				}
			}
		}
	}
	for (int32 i = 0; i < Models.Num(); ++i)
	{
		if (!Models[i])
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Part = FarForest[i];
		if (!Part)
		{
			// Monde absolu (le gestionnaire se déplace avec le joueur), sans ombre ni collision, visible de loin.
			Part = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			Part->SetStaticMesh(Models[i]);
			Part->SetupAttachment(RootComponent);
			Part->SetUsingAbsoluteLocation(true);
			Part->SetUsingAbsoluteRotation(true);
			Part->SetUsingAbsoluteScale(true);
			Part->SetWorldTransform(FTransform::Identity);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCastShadow(false);
			Part->SetCanEverAffectNavigation(false);
			Part->bAffectDistanceFieldLighting = false;
			Part->bVisibleInRayTracing = false;
			Part->WorldPositionOffsetDisableDistance = 1;
			Part->SetCullDistances(120000, 160000);
			Part->RegisterComponent();
			FarForest[i] = Part;
		}
		Part->ClearInstances();
		Part->AddInstances(Batches[i], false, true);
	}
}

void ASectorManager::BuildBackdrop()
{
	// Deux couronnes de relief au-delà des secteurs : des collines vertes, puis des montagnes bleutées,
	// que la brume du ciel adoucit. Un profil bombé (pied, épaule, crête, revers), une crête qui ondule.
	struct FLayer { float Radius; float Depth; float MinHeight; float MaxHeight; FLinearColor Color; float Waves; float Seed; const TCHAR* Texture; float Tile; FLinearColor Tint; };
	const FLayer Rings[] = {
		{ 160000.f, 90000.f, 6000.f, 22000.f, FLinearColor::FromSRGBColor(FColor(92, 132, 70)), 7.f, 1.3f, TEXT("ground_grass"), 9000.f, FLinearColor(0.75f, 0.85f, 0.7f) },
		{ 340000.f, 160000.f, 30000.f, 85000.f, FLinearColor::FromSRGBColor(FColor(128, 148, 170)), 5.f, 7.7f, nullptr, 0.f, FLinearColor::White },
	};
	constexpr int32 Segments = 384;
	for (const FLayer& Layer : Rings)
	{
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FLinearColor> Colors;
		TArray<int32> Triangles;
		const bool bMountains = Layer.Texture == nullptr;
		// Les rangées : du pied (sous le sol) jusqu'au revers, rayon et hauteur relatifs.
		const float RadiusAt[] = { 0.f, 0.3f, 0.6f, 1.f };
		const float HeightAt[] = { -0.15f, 0.7f, 1.f, 0.55f };
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float Angle = UE_TWO_PI * i / Segments;
			const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			const float Noise = 0.5f + 0.35f * FMath::PerlinNoise1D(Angle * Layer.Waves + Layer.Seed) + 0.15f * FMath::PerlinNoise1D(Angle * Layer.Waves * 3.1f + Layer.Seed * 2.f);
			const float Peak = FMath::Lerp(Layer.MinHeight, Layer.MaxHeight, FMath::Clamp(Noise, 0.f, 1.f));
			for (int32 Row = 0; Row < 4; ++Row)
			{
				const float Z = Row == 0 ? -5000.f : Peak * HeightAt[Row];
				Vertices.Add(FVector(Dir * (Layer.Radius + Layer.Depth * RadiusAt[Row]), Z));
				Normals.Add(FVector(-Dir * 0.5f, 1.f).GetSafeNormal());   // vers le joueur et vers le ciel
				// Les couleurs : des versants qui varient (bruit le long de la crête), plus sombres au pied ;
				// la neige sur les plus hauts sommets.
				const float Patch = 0.5f + 0.5f * FMath::PerlinNoise1D(Angle * 23.f + Row * 1.7f + Layer.Seed);
				const float Streak = 0.5f + 0.5f * FMath::PerlinNoise1D(Angle * 61.f + Layer.Seed * 3.f);
				FLinearColor Color;
				if (bMountains)
				{
					const FLinearColor Foot = FLinearColor::FromSRGBColor(FColor(84, 108, 112));
					const FLinearColor Rock = FMath::Lerp(FLinearColor::FromSRGBColor(FColor(118, 128, 150)), FLinearColor::FromSRGBColor(FColor(150, 140, 150)), Patch);
					const FLinearColor Snow = FLinearColor::FromSRGBColor(FColor(232, 236, 244));
					Color = Row <= 1 ? FMath::Lerp(Foot, Rock, Row * 0.6f + Streak * 0.3f) : Rock;
					if (Row == 2 && Peak > FMath::Lerp(Layer.MinHeight, Layer.MaxHeight, 0.62f + 0.15f * Streak))
					{
						Color = Snow;
					}
				}
				else
				{
					// Les collines : prés clairs, bois sombres, champs dorés par endroits.
					const FLinearColor Meadow = FLinearColor::FromSRGBColor(FColor(110, 150, 74));
					const FLinearColor Woods = FLinearColor::FromSRGBColor(FColor(58, 92, 56));
					const FLinearColor Fields = FLinearColor::FromSRGBColor(FColor(168, 158, 84));
					Color = FMath::Lerp(Meadow, Woods, FMath::SmoothStep(0.45f, 0.7f, Patch));
					Color = FMath::Lerp(Color, Fields, FMath::SmoothStep(0.75f, 0.9f, Streak) * 0.7f);
				}
				Colors.Add(Color);
			}
		}
		for (int32 i = 0; i < Segments; ++i)
		{
			for (int32 Row = 0; Row < 3; ++Row)
			{
				const int32 A = i * 4 + Row, B = A + 1, C = A + 4, D = C + 1;
				Triangles.Append({ A, C, B, B, C, D });
			}
		}
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
		Mesh->SetupAttachment(RootComponent);
		Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, {}, Colors, {}, false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		// Les couleurs portées par les sommets : rien ne se répète d'un versant à l'autre.
		if (UMaterialInterface* VertexColor = ErewhonVisuals::LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_VertexColor")))
		{
			Mesh->SetMaterial(0, VertexColor);
		}
		else
		{
			Mesh->SetMaterial(0, ErewhonVisuals::MakeColorMaterial(Mesh, Layer.Color));
		}
		Mesh->RegisterComponent();
		Backdrop.Add(Mesh);
	}
}

ASector* ASectorManager::FindLoaded(const UObject* WorldContext, const FIntPoint& Coord)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	TActorIterator<ASectorManager> It(World);
	if (!World || !It)
	{
		return nullptr;
	}
	const TObjectPtr<ASector>* Found = It->Loaded.Find(Coord);
	return Found ? Found->Get() : nullptr;
}

bool ASectorManager::GetLocalLoadState(const UObject* WorldContext, int32& OutPending, int32& OutWanted)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	TActorIterator<ASectorManager> It(World);
	if (!It || It->LocalPending < 0)
	{
		return false;
	}
	OutPending = It->LocalPending;
	OutWanted = It->LocalWanted;
	return true;
}

void ASectorManager::RebuildAll()
{
	// Réglages graphiques changés : on repart de zéro, les secteurs se reconstruisent au fil des images.
	for (const auto& Pair : Loaded)
	{
		Pair.Value->Destroy();
	}
	Loaded.Reset();
	Refresh();
}

void ASectorManager::OnSectorOwnerChanged(const FIntPoint& Sector)
{
	// Un achat (ou une revente) trace ou efface un chemin d'accès : le secteur et ses voisins
	// se reconstruisent au prochain rafraîchissement.
	GetWorld()->GetSubsystem<USectorLayoutSubsystem>()->InvalidateAround(Sector);
	for (const FIntPoint& Offset : { FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
	{
		if (TObjectPtr<ASector>* Found = Loaded.Find(Sector + Offset))
		{
			(*Found)->Destroy();
			Loaded.Remove(Sector + Offset);
		}
	}
}

void ASectorManager::OnFieldChanged(const FIntPoint& Sector)
{
	DirtyFields.Add(Sector);
}

void ASectorManager::OnPlantHarvestChanged(const FIntPoint& Sector, int32 PlantIndex, bool bHarvested)
{
	if (const TObjectPtr<ASector>* Found = Loaded.Find(Sector))
	{
		(*Found)->SetPlantHarvested(PlantIndex, bHarvested);
	}
}
