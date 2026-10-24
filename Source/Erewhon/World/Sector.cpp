#include "World/Sector.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Core/ErewhonSettings.h"
#include "Core/ErewhonUserSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Items/ItemDatabase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "World/ErewhonMaterials.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorLayout.h"
#include "World/Signpost.h"
#include "World/Village.h"
#include "Core/ErewhonGameState.h"
#include "World/SpawnLayout.h"
#include "World/Terrain.h"
#include "World/Farm.h"
#include "World/Forestry.h"
#include "TimerManager.h"
#include "Async/ParallelFor.h"

using namespace ErewhonVisuals;

namespace
{
	/** Résolution du masque terre/herbe : 512 texels pour 100 m, soit ~20 cm. */
	constexpr int32 GMaskResolution = 512;

	/** Grille du sol : 64 × 64 quads, soit un sommet tous les ~1,6 m. */
	constexpr int32 GGroundResolution = 64;

	/** Une partie de placeholder, dimensionnée pour une plante de 1 m de haut (cm). */
	struct FPartSpec
	{
		EShape Shape;
		FVector Center;
		FVector Size;
		FRotator Rotation;
		FLinearColor Color;
	};

	TArray<FPartSpec> PlaceholderParts(const FItemDef& Def)
	{
		const FLinearColor Accent = Def.Plant.AccentColor;
		switch (Def.Plant.Shape)
		{
		case EPlantShape::Flower:
			return {
				{ EShape::Cylinder, FVector(0, 0, 50), FVector(4, 4, 100), FRotator::ZeroRotator, Palette::Stem },
				{ EShape::Sphere, FVector(0, 0, 100), FVector(42, 42, 12), FRotator::ZeroRotator, Def.Color },
				{ EShape::Sphere, FVector(0, 0, 105), FVector(16, 16, 10), FRotator::ZeroRotator, Accent },
			};
		case EPlantShape::Herb:
			return {
				{ EShape::Sphere, FVector(0, 0, 22), FVector(64, 64, 44), FRotator::ZeroRotator, Accent },
				{ EShape::Sphere, FVector(0, 0, 58), FVector(34, 34, 34), FRotator::ZeroRotator, Def.Color },
			};
		case EPlantShape::Grass:
		default:
			// Touffe de deux brins, le second couché par le vent.
			return {
				{ EShape::Cone, FVector(0, 0, 50), FVector(12, 12, 100), FRotator::ZeroRotator, Def.Color },
				{ EShape::Cone, FVector(8, 0, 36), FVector(10, 10, 75), FRotator(-18, 0, 0), Def.Color * 0.85f },
			};
		}
	}

	const UErewhonUserSettings* UserSettings()
	{
		const UErewhonUserSettings* Settings = UErewhonUserSettings::Get();
		return Settings ? Settings : GetDefault<UErewhonUserSettings>();
	}
}

ASector::ASector()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

FIntPoint ASector::GetCoord() const
{
	return Layout.IsValid() ? Layout->Coord : FIntPoint::ZeroValue;
}

void ASector::Initialize(const TSharedRef<const FSectorLayout>& InLayout, bool bWithPlants, bool bInFar)
{
	Layout = InLayout;
	bFar = bInFar && !bWithPlants;
	SetActorLocation(ErewhonSector::Origin(InLayout->Coord));
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Sector_%s"), *ErewhonSector::ToString(InLayout->Coord)));
#endif
	BuildGround(GetNetMode() != NM_DedicatedServer);
	if (!bFar)
	{
		BuildTrees(bWithPlants && GetNetMode() != NM_DedicatedServer);
	}
	if (InLayout->bFenced)
	{
		BuildFences(GetNetMode() != NM_DedicatedServer);
	}
	if (bWithPlants)
	{
		BuildDecor();
		BuildBushes();
		BuildPlants();
		BuildSignposts();
	}
	if (!InLayout->Fields.IsEmpty() && GetNetMode() != NM_DedicatedServer)
	{
		RefreshField();
		GetWorldTimerManager().SetTimer(FieldTimer, this, &ASector::CheckFieldStages, 5.f, true, FMath::FRandRange(1.f, 5.f));
	}
}

// ── Champs ───────────────────────────────────────────────────────────────────

void ASector::BuildFences(bool bVisual)
{
	// Tout autour du secteur, un peu en retrait du bord : des travées de barrière qui suivent le relief.
	// Au milieu d'un côté traversé par un chemin, une ouverture sous un portique de bois.
	constexpr float Inset = 60.f;
	constexpr float Span = 250.f;
	constexpr float FenceHeight = 115.f;
	const float GateHalf = ErewhonSpawn::AccessPathWidth * 0.5f + 70.f;
	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector Origin = GetActorLocation();
	UStaticMesh* FenceMesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_fence_segment"));
	UStaticMesh* GateMesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_farm_gate"));
	const auto MakeInstances = [this](UStaticMesh* Mesh, UMaterialInterface* Material, bool bCollision, bool bVisible)
	{
		UHierarchicalInstancedStaticMeshComponent* Part = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		Part->SetStaticMesh(Mesh);
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
		Part->SetupAttachment(Root);
		Part->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Part->SetCanEverAffectNavigation(false);
		Part->SetVisibility(bVisible);
		Part->SetCullDistances(14000, 18000);
		Part->RegisterComponent();
		AddInstanceComponent(Part);
		return Part;
	};
	// Repli sans modèle : des poteaux et deux lisses, en bois.
	UMaterialInterface* Wood = bVisual ? MakePaintedMaterial(this, TEXT("wood_trim"), 140.f, Palette::WoodDark) : nullptr;
	UHierarchicalInstancedStaticMeshComponent* Fences = MakeInstances(FenceMesh ? FenceMesh : GetShape(EShape::Cube), FenceMesh ? nullptr : Wood, true, bVisual);
	UHierarchicalInstancedStaticMeshComponent* Posts = FenceMesh ? nullptr : MakeInstances(GetShape(EShape::Cube), Wood, true, bVisual);
	const FTransform FenceFit = FenceMesh ? FitToLength(FenceMesh, Span, FenceHeight) * FTransform(FRotator(0.f, -90.f, 0.f)) : FTransform::Identity;
	const auto Ground = [this, &Origin](const FVector2D& Local)
	{
		return ErewhonTerrain::Height(FVector2D(Origin) + Local, Layout->WorldSeed) - Origin.Z;
	};

	TArray<FTransform> FenceBatch, PostBatch, GateBatch, GateColliders;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		// Côtés : nord (X haut), sud (X bas), est (Y haut), ouest (Y bas) ; chacun parcouru selon son axe.
		const bool bAlongY = Side < 2;
		const float Fixed = (Side == 0 || Side == 2) ? Size - Inset : Inset;
		const auto At = [&](float Along) { return bAlongY ? FVector2D(Fixed, Along) : FVector2D(Along, Fixed); };
		const bool bGate = (Layout->FenceGates & (1 << Side)) != 0;
		const float Mid = Size * 0.5f;
		// Deux tronçons de part et d'autre du portique, ou un seul.
		TArray<TPair<float, float>> Runs;
		if (bGate)
		{
			Runs.Add({ Inset, Mid - GateHalf });
			Runs.Add({ Mid + GateHalf, Size - Inset });
		}
		else
		{
			Runs.Add({ Inset, Size - Inset });
		}
		for (const TPair<float, float>& Run : Runs)
		{
			const int32 Count = FMath::Max(1, FMath::RoundToInt32((Run.Value - Run.Key) / Span));
			const float Step = (Run.Value - Run.Key) / Count;
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector2D A = At(Run.Key + Step * i);
				const FVector2D B = At(Run.Key + Step * (i + 1));
				const FVector Start(A, Ground(A));
				const FVector End(B, Ground(B));
				const FVector Dir = (End - Start).GetSafeNormal();
				if (FenceMesh)
				{
					FenceBatch.Add(FenceFit * FTransform(Dir.Rotation(), (Start + End) * 0.5f, FVector(Step / Span, 1.f, 1.f)));
				}
				else
				{
					// Deux lisses, et un poteau au début de la travée.
					for (const float RailZ : { 45.f, 90.f })
					{
						FenceBatch.Add(FTransform(Dir.Rotation(), (Start + End) * 0.5f + FVector(0.f, 0.f, RailZ), FVector(Step / 100.f, 0.08f, 0.1f)));
					}
					PostBatch.Add(FTransform(FRotator::ZeroRotator, Start + FVector(0.f, 0.f, FenceHeight * 0.5f - 10.f), FVector(0.14f, 0.14f, FenceHeight / 100.f + 0.2f)));
				}
			}
		}
		if (bGate)
		{
			// Le portique, face au chemin ; deux poteaux invisibles l'arrêtent, le passage reste libre.
			const FVector2D Center = At(Mid);
			const FVector Foot(Center, Ground(Center));
			const FRotator Facing(0.f, bAlongY ? 0.f : 90.f, 0.f);
			GateBatch.Add(FTransform(Facing, Foot));
			for (const float Offset : { -GateHalf, GateHalf })
			{
				const FVector2D PostAt = At(Mid + Offset);
				GateColliders.Add(FTransform(FRotator::ZeroRotator, FVector(PostAt, Ground(PostAt) + 150.f), FVector(0.3f, 0.3f, 3.f)));
			}
		}
	}
	Fences->AddInstances(FenceBatch, false, false);
	if (Posts)
	{
		Posts->AddInstances(PostBatch, false, false);
	}
	UHierarchicalInstancedStaticMeshComponent* Colliders = MakeInstances(GetShape(EShape::Cube), nullptr, true, false);
	Colliders->AddInstances(GateColliders, false, false);
	if (bVisual && !GateBatch.IsEmpty())
	{
		if (GateMesh)
		{
			UHierarchicalInstancedStaticMeshComponent* Gates = MakeInstances(GateMesh, nullptr, false, true);
			// Large comme le passage et ses poteaux, la largeur sur Y (alignée sur le côté).
			const FTransform GateFit = FitToLength(GateMesh, GateHalf * 2.f + 60.f);
			for (FTransform& Gate : GateBatch)
			{
				Gate = GateFit * Gate;
			}
			Gates->AddInstances(GateBatch, false, false);
		}
		else
		{
			UHierarchicalInstancedStaticMeshComponent* Beams = MakeInstances(GetShape(EShape::Cube), Wood, false, true);
			TArray<FTransform> BeamBatch;
			for (const FTransform& Gate : GateBatch)
			{
				for (const float Offset : { -GateHalf, GateHalf })
				{
					BeamBatch.Add(FTransform(FRotator::ZeroRotator, Gate.TransformPosition(FVector(0.f, Offset, 150.f)), FVector(0.22f, 0.22f, 3.f)));
				}
				BeamBatch.Add(FTransform(Gate.Rotator(), Gate.TransformPosition(FVector(0.f, 0.f, 300.f)), FVector(0.25f, (GateHalf * 2.f + 60.f) / 100.f, 0.25f)));
			}
			Beams->AddInstances(BeamBatch, false, false);
		}
	}
}

int32 ASector::FieldStageSignature() const
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const TMap<int32, FFieldCellEntry>* Cells = State ? State->GetFieldCells(Layout->Coord) : nullptr;
	if (!Cells)
	{
		return 0;
	}
	const double Now = State->GetWorldClock();
	int32 Signature = 0;
	for (const TPair<int32, FFieldCellEntry>& Pair : *Cells)
	{
		if (Pair.Value.Crop > 0)
		{
			Signature += ErewhonFarm::Stage(Pair.Value.SownAt, Now) + 1;
		}
	}
	return Signature;
}

void ASector::CheckFieldStages()
{
	if (FieldStageSignature() != ShownStageSignature)
	{
		RefreshField();
	}
}

void ASector::RefreshField()
{
	using namespace ErewhonFarm;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!Layout.IsValid() || Layout->Fields.IsEmpty() || !State || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FIntPoint Coord = Layout->Coord;
	const TMap<int32, FFieldCellEntry>* Cells = State->GetFieldCells(Coord);
	const double Now = State->GetWorldClock();
	const FVector2D Origin(GetActorLocation());

	// La terre, case par case : grossière ou retournée (un peu plus haute, pour passer devant).
	TArray<FVector> Vertices[3];
	TArray<FVector> Normals[3];
	TArray<FVector2D> UVs[3];
	TArray<int32> Triangles[3];
	// Les touffes, par modèle.
	TMap<FName, TArray<FTransform>> Batches;
	const auto Clump = [&](const TCHAR* MeshName, float Height, const FVector2D& World, float Yaw)
	{
		Batches.FindOrAdd(MeshName).Add(FTransform(FRotator(0.f, Yaw, 0.f),
			FVector(World - Origin, ErewhonTerrain::Height(World, Layout->WorldSeed)), FVector(Height / 100.f)));
	};
	for (const FBox2D& Box : Layout->Fields)
	{
		const int32 X0 = FMath::RoundToInt32((Box.Min.X - Origin.X) / CellSize);
		const int32 X1 = FMath::RoundToInt32((Box.Max.X - Origin.X) / CellSize);
		const int32 Y0 = FMath::RoundToInt32((Box.Min.Y - Origin.Y) / CellSize);
		const int32 Y1 = FMath::RoundToInt32((Box.Max.Y - Origin.Y) / CellSize);
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				const int32 Cell = Y * CellsPerSide + X;
				const FFieldCellEntry* Entry = Cells ? Cells->Find(Cell) : nullptr;
				// Grossière, retournée, ou semée (un peu plus sombre : on voit où l'on a semé).
				const int32 Section = !Entry ? 0 : Entry->Crop == 0 ? 1 : 2;
				const int32 Base = Vertices[Section].Num();
				for (const FIntPoint Corner : { FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(1, 1), FIntPoint(0, 1) })
				{
					const FVector2D Local((X + Corner.X) * CellSize, (Y + Corner.Y) * CellSize);
					const FVector2D World = Origin + Local;
					Vertices[Section].Add(FVector(Local, ErewhonTerrain::Height(World, Layout->WorldSeed) + (Section ? 7.f : 5.f)));
					Normals[Section].Add(ErewhonTerrain::Normal(World, Layout->WorldSeed));
					UVs[Section].Add(Local / CellSize);
				}
				Triangles[Section].Append({ Base, Base + 3, Base + 1, Base + 1, Base + 3, Base + 2 });

				// Ce qui y pousse : rien le premier jour, des pousses ensuite, la culture mûre au troisième.
				if (!Entry || Entry->Crop == 0 || !Crops().IsValidIndex(Entry->Crop - 1))
				{
					continue;
				}
				const FCrop& Crop = Crops()[Entry->Crop - 1];
				const int32 Stage = ErewhonFarm::Stage(Entry->SownAt, Now);
				if (Stage == 0)
				{
					continue;
				}
				FRandomStream Rand(static_cast<int32>(HashCombine(GetTypeHash(Coord), GetTypeHash(Cell))));
				const int32 Count = Stage == RipeStage ? Crop.PerCell : 8;
				const FVector2D Center = Origin + FVector2D((X + 0.5f) * CellSize, (Y + 0.5f) * CellSize);
				for (int32 i = 0; i < Count; ++i)
				{
					// En rangs (le long de X), un peu de désordre.
					const float Row = (i % 2 == 0 ? -1.f : 1.f) * CellSize * 0.22f;
					const float Along = ((i / 2 + 0.5f) / FMath::Max(1, (Count + 1) / 2) - 0.5f) * CellSize * 0.8f;
					const FVector2D At = Center + FVector2D(Along + Rand.FRandRange(-12.f, 12.f), Row + Rand.FRandRange(-12.f, 12.f));
					const float Yaw = Rand.FRandRange(0.f, 360.f);
					const float Scale = Rand.FRandRange(0.85f, 1.1f);
					if (Stage == RipeStage)
					{
						Clump(Crop.Mesh, Crop.Height * Scale, At, Yaw);
					}
					else
					{
						Clump(TEXT("SM_crop_sprout"), (Stage == 1 ? 20.f : 42.f) * Scale, At, Yaw);
					}
				}
			}
		}
	}

	if (!FieldSoil)
	{
		FieldSoil = NewObject<UProceduralMeshComponent>(this, TEXT("FieldSoil"));
		FieldSoil->SetupAttachment(Root);
		FieldSoil->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FieldSoil->SetCastShadow(false);
		FieldSoil->RegisterComponent();
		AddInstanceComponent(FieldSoil);
	}
	for (int32 Section = 0; Section < 3; ++Section)
	{
		FieldSoil->ClearMeshSection(Section);
		if (Vertices[Section].Num() > 0)
		{
			FieldSoil->CreateMeshSection_LinearColor(Section, Vertices[Section], Triangles[Section], Normals[Section], UVs[Section], {}, {}, false);
		}
	}
	if (FieldSoil->GetNumMaterials() < 3 || !FieldSoil->GetMaterial(2))
	{
		FieldSoil->SetMaterial(2, MakePaintedMaterial(FieldSoil, TEXT("field_tilled"), 200.f, FLinearColor::FromSRGBColor(FColor(70, 48, 34)), FLinearColor(0.72f, 0.66f, 0.6f)));
		FieldSoil->SetMaterial(0, MakePaintedMaterial(FieldSoil, TEXT("field_raw"), 400.f, FLinearColor::FromSRGBColor(FColor(128, 96, 66))));
		FieldSoil->SetMaterial(1, MakePaintedMaterial(FieldSoil, TEXT("field_tilled"), 200.f, FLinearColor::FromSRGBColor(FColor(92, 64, 44))));
	}

	// Les touffes : on vide chaque modèle et on le remplit de nouveau.
	for (const TPair<FName, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Pair : CropParts)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();
		}
	}
	for (TPair<FName, TArray<FTransform>>& Pair : Batches)
	{
		TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Part = CropParts.FindOrAdd(Pair.Key);
		UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), Pair.Key.ToString());
		if (!Part)
		{
			if (!Mesh)
			{
				continue;
			}
			Part = MakeInstancedPart(Mesh, nullptr, 140.f);
		}
		// Ramené à 1 m de haut, posé au sol : l'échelle de chaque instance donne sa hauteur.
		const FTransform Fit = FitToHeight(Mesh, 100.f);
		for (FTransform& Instance : Pair.Value)
		{
			Instance = Fit * Instance;
		}
		Part->AddInstances(Pair.Value, false, false);
	}
	ShownStageSignature = FieldStageSignature();
}

void ASector::BuildSignposts()
{
	// Aux carrefours de la route : la direction et la distance de chaque commerce (amélioration
	// « panneaux »), et une lanterne au coin d'en face (amélioration « lanternes »).
	// Ceux du village le plus proche, s'il est assez près pour que la route soit la sienne.
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	FIntPoint HomeVillage;
	const bool bInVillage = ErewhonVillage::IsInVillage(Layout->Coord, &HomeVillage);
	const bool bSigns = State && State->HasVillageUpgrade(HomeVillage, EVillageUpgrade::Signposts);
	const bool bLanterns = State && State->HasVillageUpgrade(HomeVillage, EVillageUpgrade::Lanterns);
	for (const FSignpostSpot& Spot : Layout->Signposts)
	{
		if (!bInVillage)
		{
			continue;
		}
		if (bLanterns && !bSigns)
		{
			// Sans panneau, la lanterne a son propre poteau ; sinon elle s'accroche au sien.
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Spot.TowardShops.Y, Spot.TowardShops.X));
			ErewhonVillage::AddLantern(this, Root, Spot.Location - GetActorLocation(), Yaw, false);
		}
		if (!bSigns)
		{
			continue;
		}
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Spot.TowardShops.Y, Spot.TowardShops.X));
		TArray<ErewhonSigns::FArm> Arms;
		for (const EShopKind Kind : ErewhonSpawn::ShopKinds)
		{
			if (State->IsShopBuilt(HomeVillage, Kind))
			{
				Arms.Add(ErewhonSigns::ShopArm(Kind, Spot.Location, Yaw));
			}
		}
		ErewhonSigns::AddSignpost(this, Root, Spot.Location - GetActorLocation(), Arms, bLanterns);
	}

	// Sur les routes, les villages où elles mènent : à chaque sortie de place (premier secteur de la
	// route) et à chaque coude. Toujours là, amélioration ou non : c'est ainsi qu'on voyage.
	if (Layout->Type != ESectorType::Road || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const uint8 Exits = ErewhonVillages::RoadSides(Layout->Coord);
	const FVector2D Middle(ErewhonSector::Center(Layout->Coord));
	const float HalfSector = UErewhonSettings::Get().SectorSize() * 0.5f;
	const float Beside = ErewhonSpawn::PathWidth * 0.5f + 60.f;
	const auto YawOf = [](int32 Side) { return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(ErewhonSector::Sides[Side].Y), static_cast<float>(ErewhonSector::Sides[Side].X))); };
	const auto PlantRoadSign = [&](const FVector2D& Foot2D, std::initializer_list<int32> Toward)
	{
		const FVector Foot(Foot2D, ErewhonTerrain::Height(Foot2D, Layout->WorldSeed));
		TArray<ErewhonSigns::FArm> Arms;
		for (const int32 Side : Toward)
		{
			FIntPoint Village;
			if (ErewhonVillages::RoadDestination(Layout->Coord, Side, Village))
			{
				Arms.Add(ErewhonSigns::VillageArm(Village, Foot, YawOf(Side)));
			}
		}
		ErewhonSigns::AddSignpost(this, Root, Foot - GetActorLocation(), Arms, false);
	};
	for (int32 Side = 0; Side < 4; ++Side)
	{
		if ((Exits & (1 << Side)) && ErewhonVillages::IsCenter(Layout->Coord + ErewhonSector::Sides[Side]))
		{
			// À la sortie de la place, sur la droite en la quittant : le village d'en face, puis celui-ci.
			const int32 Leave = Side ^ 1;
			const FVector2D Ahead(ErewhonSector::Sides[Leave]);
			const FVector2D Right(-Ahead.Y, Ahead.X);
			PlantRoadSign(Middle - Ahead * (HalfSector - 400.f) + Right * Beside, { Leave, Side });
			return;
		}
	}
	// Un coude : un bras le long de X, l'autre le long de Y. La route y décrit un arc : le panneau à
	// l'intérieur, au milieu de l'arc, du côté du coin entre les deux bras.
	const int32 AlongX = (Exits & 1) ? 0 : (Exits & 2) ? 1 : -1;
	const int32 AlongY = (Exits & 4) ? 2 : (Exits & 8) ? 3 : -1;
	if (AlongX >= 0 && AlongY >= 0)
	{
		const FVector2D Corner = FVector2D(ErewhonSector::Sides[AlongX]) + FVector2D(ErewhonSector::Sides[AlongY]);
		TArray<FVector2D> RoadLine;
		if (!ErewhonSpawn::RoadCenterline(Layout->Coord, RoadLine))
		{
			PlantRoadSign(Middle + Corner * Beside, { AlongX, AlongY });
			return;
		}
		FVector2D Tangent;
		const FVector2D Bend = ErewhonSpawn::ClosestOnLine(RoadLine, RoadLine[RoadLine.Num() / 2], &Tangent);
		FVector2D Inside(-Tangent.Y, Tangent.X);
		if (FVector2D::DotProduct(Inside, Corner) < 0.f)
		{
			Inside = -Inside;
		}
		PlantRoadSign(Bend + Inside * Beside, { AlongX, AlongY });
	}
}

// ── Sol ──────────────────────────────────────────────────────────────────────

void ASector::BuildGround(bool bVisual)
{
	const float Size = UErewhonSettings::Get().SectorSize();
	const FLinearColor Grass = Layout->Type == ESectorType::Spawn ? Palette::MeadowSpawn : Palette::Meadow;
	const FVector2D Origin(GetActorLocation());

	// Sol en relief : une grille dont les bords suivent exactement la fonction de hauteur,
	// si bien qu'elle se raccorde sans couture à celle des secteurs voisins.
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Triangles;
	// Au loin, une grille quatre fois plus lâche suffit (et pas de collision : personne n'y marche).
	const int32 N = bFar ? GGroundResolution / 4 : GGroundResolution;
	// Le relief, rangée par rangée, sur tous les cœurs (la fonction de hauteur est sans état partagé).
	Vertices.SetNum((N + 1) * (N + 1));
	Normals.SetNum((N + 1) * (N + 1));
	UVs.SetNum((N + 1) * (N + 1));
	const int32 Seed = Layout->WorldSeed;
	ParallelFor(N + 1, [&](int32 Y)
	{
		for (int32 X = 0; X <= N; ++X)
		{
			const FVector2D Local(X * Size / N, Y * Size / N);
			const FVector2D World = Origin + Local;
			const int32 Index = Y * (N + 1) + X;
			Vertices[Index] = FVector(Local.X, Local.Y, ErewhonTerrain::Height(World, Seed));
			Normals[Index] = ErewhonTerrain::Normal(World, Seed);
			UVs[Index] = Local / Size;   // 0 → 1 sur le secteur : le masque terre/herbe s'y lit
		}
	});
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 A = Y * (N + 1) + X;
			Triangles.Append({ A, A + N + 1, A + 1, A + 1, A + N + 1, A + N + 2 });
		}
	}

	UProceduralMeshComponent* Ground = NewObject<UProceduralMeshComponent>(this, TEXT("Ground"));
	Ground->SetupAttachment(Root);
	Ground->bUseAsyncCooking = false;   // le joueur doit pouvoir marcher dessus dès son apparition
	Ground->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, {}, {}, !bFar);
	Ground->SetCollisionProfileName(bFar ? UCollisionProfile::NoCollision_ProfileName : UCollisionProfile::BlockAll_ProfileName);
	Ground->SetCastShadow(false);
	Ground->RegisterComponent();
	AddInstanceComponent(Ground);
	if (!bVisual)
	{
		return;
	}

	// Le sol mêle herbe et terre selon le masque du secteur : chemins, place et bords fondus d'un seul tenant.
	// Les secteurs sans chemin gardent le même matériau (masque noir par défaut : que de l'herbe) :
	// son herbe, rompue contre la répétition, se raccorde ainsi sans couture à celle des voisins.
	UMaterialInterface* Blend = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_GroundBlend"));
	UTexture* GrassTexture = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_ground_grass"));
	UTexture* DirtTexture = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_ground_dirt"));
	if (Blend && GrassTexture && DirtTexture)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Blend, Ground);
		Material->SetTextureParameterValue(TEXT("Albedo"), GrassTexture);
		Material->SetScalarParameterValue(TEXT("TileSize"), GroundTileSize);
		Material->SetTextureParameterValue(TEXT("AlbedoDirt"), DirtTexture);
		Material->SetScalarParameterValue(TEXT("DirtTileSize"), DirtTileSize);
		if (!Layout->Paths.IsEmpty() || !Layout->Curves.IsEmpty() || !Layout->Clearings.IsEmpty())
		{
			DirtMask = BuildDirtMask();
			Material->SetTextureParameterValue(TEXT("DirtMask"), DirtMask);
		}
		if (UTexture* Cobbles = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_cobbles")))
		{
			Material->SetTextureParameterValue(TEXT("AlbedoPaved"), Cobbles);
			Material->SetScalarParameterValue(TEXT("PavedTileSize"), PavedTileSize);
		}
		Ground->SetMaterial(0, Material);
		return;
	}
	Ground->SetMaterial(0, MakePaintedMaterial(Ground, TEXT("ground_grass"), GroundTileSize, Grass));
}

UTexture2D* ASector::BuildDirtMask() const
{
	// R : part de terre, G : part de pavés.
	// Au loin, un masque quatre fois moins fin suffit.
	const int32 Res = bFar ? GMaskResolution / 4 : GMaskResolution;
	UTexture2D* Texture = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8);
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;

	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector2D Origin(GetActorLocation());
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	uint8* Texels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	// Rangée par rangée, sur tous les cœurs : 262 000 texels, chacun mesurant sa distance aux chemins.
	const FSectorLayout& Shapes = *Layout;
	ParallelFor(Res, [&](int32 Y)
	{
		for (int32 X = 0; X < Res; ++X)
		{
			// Texel (X, Y) ↔ monde (X, Y) : le matériau échantillonne avec les mêmes axes.
			const FVector2D World = Origin + FVector2D(X + 0.5f, Y + 0.5f) * (Size / Res);
			uint8* Texel = Texels + (Y * Res + X) * 4;   // B, G, R, A
			Texel[0] = 0;
			Texel[1] = static_cast<uint8>(FMath::RoundToInt32(Shapes.PavedAmount(World) * 255.f));
			Texel[2] = static_cast<uint8>(FMath::RoundToInt32(Shapes.DirtAmount(World) * 255.f));
			Texel[3] = 255;
		}
	});
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

// ── Végétation décorative ────────────────────────────────────────────────────

bool ASector::IsSoftlyExcluded(const FVector2D& World, float Roll) const
{
	// Sur les bords de la terre, la végétation se raréfie progressivement.
	return Layout->DirtAmount(World) > 0.05f + 0.25f * Roll;
}

void ASector::BuildDecor()
{
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonNoDecor")))
	{
		return;   // debug : ne montrer que les plantes récoltables
	}
#endif
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	const UErewhonUserSettings* User = UserSettings();
	const float Size = Settings.SectorSize();
	const FVector Origin = GetActorLocation();
	UMaterialInterface* Foliage = GetWorld()->GetSubsystem<UErewhonMaterials>()->GetFoliage();

	for (int32 PlantIndex = 0; PlantIndex < Settings.DecorPlants.Num(); ++PlantIndex)
	{
		const FDecorPlant& Plant = Settings.DecorPlants[PlantIndex];
		// Sous les arbres, moins d'herbes folles qu'en prairie.
		const float Woods = Layout->Type == ESectorType::Edge ? 0.7f : Layout->Type == ESectorType::Forest ? 0.35f : 1.f;
		const int32 BaseCount = Layout->Type == ESectorType::Spawn ? Plant.CountSpawn : FMath::RoundToInt32(Plant.CountPrairie * Woods);
		const int32 Count = FMath::RoundToInt32(BaseCount * User->VegetationDensity);
		UStaticMesh* Mesh = LoadDecorMesh(Plant);
		if (Count <= 0 || Plant.bSolid || !Mesh || (Plant.bFoliageMaterial && !Foliage))
		{
			continue;   // les buissons (bSolid) viennent du FSectorLayout, voir BuildBushes
		}

		// Un flux par espèce et par secteur : régler une espèce ne déplace pas les autres.
		FRandomStream Rand(static_cast<int32>(HashCombine(GetTypeHash(Layout->Coord), GetTypeHash(PlantIndex) * 0x9E3779B9u)));
		const FTransform Fit = FitToHeight(Mesh, 100.f);
		const float PatchRadius = Plant.PatchRadius * 100.f;
		const int32 PatchSize = FMath::Max(1, Plant.PatchSize);

		TArray<FTransform> Instances;
		Instances.Reserve(Count);
		FVector2D PatchCenter = FVector2D::ZeroVector;
		for (int32 i = 0; i < Count; ++i)
		{
			if (i % PatchSize == 0)
			{
				PatchCenter = FVector2D(Rand.FRand() * Size, Rand.FRand() * Size);
			}
			// Dans un bosquet, les exemplaires se resserrent vers le centre.
			const float Distance = PatchRadius * FMath::Sqrt(Rand.FRand());
			const float Angle = Rand.FRandRange(0.f, UE_TWO_PI);
			const FVector2D Local = PatchRadius > 0.f
				? PatchCenter + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance
				: FVector2D(Rand.FRand() * Size, Rand.FRand() * Size);
			const float Height = Rand.FRandRange(Plant.HeightMin, Plant.HeightMax);
			const float Yaw = Rand.FRandRange(0.f, 360.f);
			const float Roll = Rand.FRand();
			if (Local.X < 0.f || Local.Y < 0.f || Local.X >= Size || Local.Y >= Size
				|| IsSoftlyExcluded(Local + FVector2D(Origin), Roll))
			{
				continue;
			}
			const float Ground = ErewhonTerrain::Height(Local + FVector2D(Origin), Layout->WorldSeed);
			Instances.Add(Fit * FTransform(FRotator(0.f, Yaw, 0.f), FVector(Local.X, Local.Y, Ground), FVector(Height)));
		}

		UHierarchicalInstancedStaticMeshComponent* Part = MakeInstancedPart(Mesh, Plant.bFoliageMaterial ? Foliage : nullptr, Plant.CullMeters);
		Part->SetCastShadow(Plant.bCastShadows && User->bVegetationShadows);
		Part->AddInstances(Instances, false, false);
	}
}

UStaticMesh* ASector::LoadDecorMesh(const FDecorPlant& Plant)
{
	FString Folder, Name;
	return Plant.Mesh.Split(TEXT("/"), &Folder, &Name) ? LoadGenerated<UStaticMesh>(*Folder, Name) : nullptr;
}

void ASector::BuildTrees(bool bVisual)
{
	if (Layout->Trees.Num() == 0)
	{
		return;
	}
	// Les troncs : des cylindres invisibles, en instances, qui arrêtent le joueur (une souche, l'arbre abattu).
	Trunks = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	Trunks->SetStaticMesh(GetShape(EShape::Cylinder));
	Trunks->SetupAttachment(Root);
	Trunks->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Trunks->SetHiddenInGame(true);
	Trunks->SetCanEverAffectNavigation(false);
	Trunks->RegisterComponent();
	AddInstanceComponent(Trunks);
	TArray<FTransform> TrunkTransforms;
	TArray<FTransform> StumpTransforms;
	TArray<TArray<FTransform>> Batches;
	Batches.SetNum(ErewhonForest::TreeModelCount);
	TArray<UStaticMesh*> Models;
	TreeFits.Reset();
	for (int32 i = 0; i < ErewhonForest::TreeModelCount; ++i)
	{
		UStaticMesh* Model = bVisual ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), ErewhonForest::TreeModels[i]) : nullptr;
		Models.Add(Model);
		TreeFits.Add(Model ? FitToHeight(Model, 100.f) : FTransform::Identity);
	}
	// Les arbres abattus (et pas encore repoussés) : une souche à leur place.
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	TreeInstance.Init(INDEX_NONE, Layout->Trees.Num());
	for (int32 Index = 0; Index < Layout->Trees.Num(); ++Index)
	{
		const FTreeInstance& Tree = Layout->Trees[Index];
		const bool bCut = State && State->IsHarvested(Layout->Coord, ErewhonForestry::HarvestIndexOfTree(Index));
		TrunkTransforms.Add(TrunkTransform(Index, bCut));
		StumpTransforms.Add(StumpTransform(Index, bCut));
		if (Models.IsValidIndex(Tree.Model) && Models[Tree.Model])
		{
			TreeInstance[Index] = Batches[Tree.Model].Num();
			Batches[Tree.Model].Add(TreeTransform(Index, bCut));
		}
	}
	Trunks->AddInstances(TrunkTransforms, false, false);
	bTreesVisual = bVisual;
	if (bVisual && StumpTransforms.ContainsByPredicate([](const FTransform& T) { return T.GetScale3D().X > 0.01f; }))
	{
		EnsureStumps();
	}

	// En forêt, la canopée : de larges masses de feuillage entre les cimes, qui ferment le ciel au-dessus de soi.
	UStaticMesh* Crown = bVisual && Layout->Type == ESectorType::Forest ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bush_box")) : nullptr;
	if (Crown)
	{
		const FTransform CrownFit = FitToHeight(Crown, 100.f);
		TArray<FTransform> Crowns;
		CanopyTransforms(*Layout, Crowns);
		for (FTransform& At : Crowns)
		{
			At = CrownFit * FTransform(At.GetRotation(), At.GetLocation() - GetActorLocation(), At.GetScale3D());
		}
		UHierarchicalInstancedStaticMeshComponent* Canopy = MakeInstancedPart(Crown, nullptr, 400.f);
		Canopy->SetCastShadow(true);
		Canopy->AddInstances(Crowns, false, false);
	}
	TreeParts.Init(nullptr, Batches.Num());
	for (int32 i = 0; i < Batches.Num(); ++i)
	{
		if (Batches[i].Num() == 0)
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Part = MakeInstancedPart(Models[i], nullptr, 400.f);
		Part->SetCastShadow(true);   // de grands arbres : leur ombre compte
		Part->AddInstances(Batches[i], false, false);
		TreeParts[i] = Part;
	}
}

void ASector::CanopyTransforms(const FSectorLayout& InLayout, TArray<FTransform>& Out)
{
	if (InLayout.Type != ESectorType::Forest)
	{
		return;
	}
	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector2D Origin(ErewhonSector::Origin(InLayout.Coord));
	FRandomStream Rand(HashCombine(GetTypeHash(InLayout.Coord), 0xC4A0u));
	for (float X = 350.f; X < Size; X += 700.f)
	{
		for (float Y = 350.f; Y < Size; Y += 700.f)
		{
			const FVector2D World = Origin + FVector2D(X + Rand.FRandRange(-220.f, 220.f), Y + Rand.FRandRange(-220.f, 220.f));
			if (InLayout.DirtAmount(World) > 0.3f)
			{
				continue;   // au-dessus des chemins et des clairières, le ciel reste ouvert
			}
			const float Ground = ErewhonTerrain::Height(World, InLayout.WorldSeed);
			const float Height = Rand.FRandRange(350.f, 520.f);
			Out.Add(FTransform(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), FVector(World, Ground + Rand.FRandRange(950.f, 1350.f)),
				FVector(Height / 100.f * 2.1f, Height / 100.f * 2.1f, Height / 100.f)));
		}
	}
}

FTransform ASector::TreeTransform(int32 TreeIndex, bool bCut) const
{
	const FTreeInstance& Tree = Layout->Trees[TreeIndex];
	const float Scale = Tree.Height / 100.f;
	const FTransform Fit = TreeFits.IsValidIndex(Tree.Model) ? TreeFits[Tree.Model] : FTransform::Identity;
	// Abattu : réduit à rien plutôt que retiré, les index restent stables.
	return Fit * FTransform(FRotator(0.f, Tree.Yaw, 0.f), Tree.Location - GetActorLocation() - FVector(0, 0, 10.f),
		bCut ? FVector(KINDA_SMALL_NUMBER) : FVector(Scale * Tree.Girth, Scale * Tree.Girth, Scale));
}

FTransform ASector::TrunkTransform(int32 TreeIndex, bool bCut) const
{
	const FTreeInstance& Tree = Layout->Trees[TreeIndex];
	const float R = ErewhonForestry::TrunkRadius(Tree) * 2.f / 100.f;
	// Debout : trois mètres de tronc ; abattu, la souche (cinquante centimètres).
	return FTransform(FRotator::ZeroRotator, Tree.Location - GetActorLocation() + FVector(0, 0, bCut ? 25.f : 150.f), FVector(R, R, bCut ? 0.5f : 3.f));
}

FTransform ASector::StumpTransform(int32 TreeIndex, bool bCut) const
{
	const FTreeInstance& Tree = Layout->Trees[TreeIndex];
	const float R = ErewhonForestry::TrunkRadius(Tree) * 2.f / 100.f;
	return FTransform(FRotator::ZeroRotator, Tree.Location - GetActorLocation() + FVector(0, 0, 18.f), bCut ? FVector(R * 1.08f, R * 1.08f, 0.55f) : FVector(KINDA_SMALL_NUMBER));
}

void ASector::EnsureStumps()
{
	if (Stumps || !bTreesVisual)
	{
		return;
	}
	// Les souches : de courts cylindres d'écorce, à leur taille une fois l'arbre abattu (sinon réduites à rien).
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	TArray<FTransform> StumpTransforms;
	for (int32 Index = 0; Index < Layout->Trees.Num(); ++Index)
	{
		StumpTransforms.Add(StumpTransform(Index, State && State->IsHarvested(Layout->Coord, ErewhonForestry::HarvestIndexOfTree(Index))));
	}
	Stumps = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	Stumps->SetStaticMesh(GetShape(EShape::Cylinder));
	Stumps->SetupAttachment(Root);
	Stumps->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stumps->SetMaterial(0, MakePaintedMaterial(Stumps, TEXT("bark"), 120.f, FLinearColor::FromSRGBColor(FColor(104, 80, 60)), FLinearColor(0.62f, 0.5f, 0.42f)));
	Stumps->RegisterComponent();
	AddInstanceComponent(Stumps);
	Stumps->AddInstances(StumpTransforms, false, false);
}

void ASector::SetTreeCut(int32 TreeIndex, bool bCut)
{
	if (!Layout.IsValid() || !Layout->Trees.IsValidIndex(TreeIndex) || !Trunks)
	{
		return;
	}
	if (bCut)
	{
		EnsureStumps();
	}
	Trunks->UpdateInstanceTransform(TreeIndex, TrunkTransform(TreeIndex, bCut), false, true, false);
	if (Stumps)
	{
		Stumps->UpdateInstanceTransform(TreeIndex, StumpTransform(TreeIndex, bCut), false, true, false);
	}
	const int32 Model = Layout->Trees[TreeIndex].Model;
	if (TreeParts.IsValidIndex(Model) && TreeParts[Model] && TreeInstance.IsValidIndex(TreeIndex) && TreeInstance[TreeIndex] != INDEX_NONE)
	{
		TreeParts[Model]->UpdateInstanceTransform(TreeInstance[TreeIndex], TreeTransform(TreeIndex, bCut), false, true, false);
	}
}

int32 ASector::TreeAtTrunk(const UPrimitiveComponent* Component, int32 Instance) const
{
	return Component && Component == Trunks && Layout.IsValid() && Layout->Trees.IsValidIndex(Instance) ? Instance : INDEX_NONE;
}

void ASector::BuildBushes()
{
	// Les buissons sont des données partagées (ils ralentissent le joueur) : on rend la liste du layout.
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	const bool bShadows = UserSettings()->bVegetationShadows;
	TMap<int32, TArray<FTransform>> Batches;
	TMap<int32, FTransform> Fits;
	for (const FBushInstance& Bush : Layout->Bushes)
	{
		if (!Fits.Contains(Bush.Kind))
		{
			UStaticMesh* Mesh = Settings.DecorPlants.IsValidIndex(Bush.Kind) ? LoadDecorMesh(Settings.DecorPlants[Bush.Kind]) : nullptr;
			if (!Mesh)
			{
				continue;
			}
			Fits.Add(Bush.Kind, FitToHeight(Mesh, 100.f));
		}
		Batches.FindOrAdd(Bush.Kind).Add(Fits[Bush.Kind] * FTransform(FRotator(0.f, Bush.Yaw, 0.f), Bush.Location - GetActorLocation(), FVector(Bush.Height)));
	}
	for (const auto& Pair : Batches)
	{
		const FDecorPlant& Plant = Settings.DecorPlants[Pair.Key];
		UHierarchicalInstancedStaticMeshComponent* Part = MakeInstancedPart(LoadDecorMesh(Plant), nullptr, Plant.CullMeters);
		Part->SetCastShadow(Plant.bCastShadows && bShadows);
		Part->AddInstances(Pair.Value, false, false);
	}
}

// ── Plantes récoltables ──────────────────────────────────────────────────────

int32 ASector::FindOrCreateKind(FName ItemId, uint8 Variant)
{
	const int32 Existing = Kinds.IndexOfByPredicate([ItemId, Variant](const FPlantKind& K) { return K.ItemId == ItemId && K.Variant == Variant; });
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}

	const int32 Index = Kinds.AddDefaulted();
	Kinds[Index].ItemId = ItemId;
	Kinds[Index].Variant = Variant;

	const FItemDef* Def = UItemDatabase::Get(this)->Find(ItemId);
	const bool bShadows = UserSettings()->bVegetationShadows;
	const auto MakePart = [this, Def, bShadows](UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UHierarchicalInstancedStaticMeshComponent* Part = MakeInstancedPart(Mesh, Material, Def->Plant.CullDistanceMeters);
		Part->SetCastShadow(bShadows);
		return Part;
	};

	const FSoftObjectPath& MeshPath = Def->MeshVariants.IsValidIndex(Variant) ? Def->MeshVariants[Variant] : Def->Mesh;
	if (UStaticMesh* Generated = Cast<UStaticMesh>(MeshPath.TryLoad()))
	{
		// Mesh généré : ramené au gabarit de 1 m, pivot à la base.
		UMaterialInterface* Override = Cast<UMaterialInterface>(Def->Material.TryLoad());
		if (Override && Override->GetName() == TEXT("M_Foliage"))
		{
			Override = GetWorld()->GetSubsystem<UErewhonMaterials>()->GetFoliage();
		}
		Kinds[Index].Parts.Add(MakePart(Generated, Override));
		Kinds[Index].PartLocal.Add(FitToHeight(Generated, 100.f));
		return Index;
	}

	for (const FPartSpec& Spec : PlaceholderParts(*Def))
	{
		UHierarchicalInstancedStaticMeshComponent* Part = MakePart(GetShape(Spec.Shape), nullptr);
		Part->SetMaterial(0, MakeColorMaterial(Part, Spec.Color));
		Kinds[Index].Parts.Add(Part);
		Kinds[Index].PartLocal.Add(FTransform(Spec.Rotation, Spec.Center, Spec.Size / 100.f));
	}
	return Index;
}

UHierarchicalInstancedStaticMeshComponent* ASector::MakeInstancedPart(UStaticMesh* Mesh, UMaterialInterface* Material, float CullDistanceMeters)
{
	UHierarchicalInstancedStaticMeshComponent* Part = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	Part->SetStaticMesh(Mesh);
	if (Material)
	{
		Part->SetMaterial(0, Material);
	}
	Part->SetupAttachment(Root);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCanEverAffectNavigation(false);
	Part->bAffectDistanceFieldLighting = false;
	// Performances de la végétation, qui se compte par dizaines de milliers d'instances :
	// - l'ondulation au vent n'invalide pas le cache des ombres virtuelles à chaque image ;
	// - le vent s'arrête au-delà de 25 m, où il ne se voit plus ;
	// - la végétation reste hors du ray tracing matériel de Lumen.
	Part->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
	Part->WorldPositionOffsetDisableDistance = 2500;
	Part->bVisibleInRayTracing = false;
	const int32 CullEnd = FMath::RoundToInt32(CullDistanceMeters * 100.f * UserSettings()->VegetationDistance);
	Part->SetCullDistances(CullEnd * 3 / 4, CullEnd);
	Part->RegisterComponent();
	AddInstanceComponent(Part);
	return Part;
}

void ASector::PlantTransforms(int32 PlantIndex, TArray<FTransform>& Out) const
{
	const FPlantInstance& Plant = Layout->Plants[PlantIndex];
	const FItemDef* Def = UItemDatabase::Get(this)->Find(Plant.ItemId);
	const int32 ClusterMin = Def ? FMath::Max(1, Def->Plant.ClusterMin) : 1;
	const int32 ClusterMax = Def ? FMath::Max(ClusterMin, Def->Plant.ClusterMax) : 1;
	const float Radius = Def ? Def->Plant.ClusterRadiusMeters * 100.f : 0.f;

	// Le premier exemplaire est au point d'apparition (celui qu'on vise) ; les autres l'entourent.
	FRandomStream Rand(static_cast<int32>(HashCombine(GetTypeHash(Layout->Coord), GetTypeHash(PlantIndex))));
	const int32 Count = Rand.RandRange(ClusterMin, ClusterMax);
	const FVector Base = Plant.Location - GetActorLocation();
	Out.Reset(Count);
	Out.Add(FTransform(FRotator(0, Plant.Yaw, 0), Base, FVector(Plant.Height)));
	for (int32 i = 1; i < Count; ++i)
	{
		const float Angle = Rand.FRandRange(0.f, UE_TWO_PI);
		const float Distance = Radius * FMath::Sqrt(Rand.FRandRange(0.15f, 1.f));
		const FVector Offset(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
		Out.Add(FTransform(FRotator(0, Rand.FRandRange(0.f, 360.f), 0), Base + Offset, FVector(Plant.Height * Rand.FRandRange(0.75f, 1.05f))));
	}
}

void ASector::GetPlantVisual(int32 PlantIndex, TArray<TPair<UStaticMesh*, FTransform>>& Out) const
{
	Out.Reset();
	if (!bPlantsBuilt || !KindOfPlant.IsValidIndex(PlantIndex))
	{
		return;
	}
	const FPlantKind& Kind = Kinds[KindOfPlant[PlantIndex]];
	TArray<FTransform> Members;
	PlantTransforms(PlantIndex, Members);
	for (const FTransform& Member : Members)
	{
		for (int32 PartIndex = 0; PartIndex < Kind.Parts.Num(); ++PartIndex)
		{
			Out.Emplace(Kind.Parts[PartIndex]->GetStaticMesh(), Kind.PartLocal[PartIndex] * Member * GetActorTransform());
		}
	}
}

void ASector::GetRipeCropVisuals(const FVector2D& Center, float Radius, TArray<TPair<UStaticMesh*, FTransform>>& Out) const
{
	// Seules les cultures mûres portent le modèle de leur culture (les pousses ont le leur).
	for (const ErewhonFarm::FCrop& Crop : ErewhonFarm::Crops())
	{
		const TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Part = CropParts.Find(FName(Crop.Mesh));
		if (!Part || !*Part)
		{
			continue;
		}
		for (int32 Instance = 0; Instance < (*Part)->GetInstanceCount(); ++Instance)
		{
			FTransform World;
			if ((*Part)->GetInstanceTransform(Instance, World, true) && FVector2D::Distance(FVector2D(World.GetLocation()), Center) <= Radius)
			{
				Out.Emplace((*Part)->GetStaticMesh(), World);
			}
		}
	}
}

void ASector::BuildPlants()
{
	const int32 Count = Layout->Plants.Num();
	KindOfPlant.SetNumUninitialized(Count);
	InstanceOfPlant.SetNumUninitialized(Count);

	// Regroupe les transformations par espèce et variante pour un ajout en lot.
	TArray<TArray<TArray<FTransform>>> Batches;
	TArray<FTransform> Members;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FPlantInstance& Plant = Layout->Plants[Index];
		const int32 KindIndex = FindOrCreateKind(Plant.ItemId, Plant.Variant);
		if (Batches.Num() <= KindIndex)
		{
			Batches.SetNum(KindIndex + 1);
		}
		const FPlantKind& Kind = Kinds[KindIndex];
		Batches[KindIndex].SetNum(Kind.Parts.Num());

		PlantTransforms(Index, Members);
		KindOfPlant[Index] = KindIndex;
		InstanceOfPlant[Index] = Batches[KindIndex][0].Num();
		const bool bCovered = Layout->CoveredPlants.Contains(Index);
		for (FTransform Member : Members)
		{
			if (bCovered)
			{
				Member.SetScale3D(FVector(KINDA_SMALL_NUMBER));   // sous un chemin d'accès
			}
			for (int32 PartIndex = 0; PartIndex < Kind.Parts.Num(); ++PartIndex)
			{
				Batches[KindIndex][PartIndex].Add(Kind.PartLocal[PartIndex] * Member);
			}
		}
	}

	for (int32 KindIndex = 0; KindIndex < Kinds.Num(); ++KindIndex)
	{
		for (int32 PartIndex = 0; PartIndex < Kinds[KindIndex].Parts.Num(); ++PartIndex)
		{
			Kinds[KindIndex].Parts[PartIndex]->AddInstances(Batches[KindIndex][PartIndex], false, false);
		}
	}
	bPlantsBuilt = true;
}

void ASector::SetPlantHarvested(int32 PlantIndex, bool bHarvested)
{
	if (ErewhonForestry::IsTreeHarvest(PlantIndex))
	{
		SetTreeCut(ErewhonForestry::TreeOfHarvestIndex(PlantIndex), bHarvested);
		return;
	}
	if (!bPlantsBuilt || !KindOfPlant.IsValidIndex(PlantIndex) || Layout->CoveredPlants.Contains(PlantIndex))
	{
		return;
	}
	FPlantKind& Kind = Kinds[KindOfPlant[PlantIndex]];
	const int32 FirstInstance = InstanceOfPlant[PlantIndex];

	// Une plante récoltée est réduite à rien plutôt que retirée : les index restent stables.
	// Toute la touffe part avec elle.
	TArray<FTransform> Members;
	PlantTransforms(PlantIndex, Members);
	for (int32 m = 0; m < Members.Num(); ++m)
	{
		FTransform Member = Members[m];
		if (bHarvested)
		{
			Member.SetScale3D(FVector(KINDA_SMALL_NUMBER));
		}
		for (int32 PartIndex = 0; PartIndex < Kind.Parts.Num(); ++PartIndex)
		{
			Kind.Parts[PartIndex]->UpdateInstanceTransform(FirstInstance + m, Kind.PartLocal[PartIndex] * Member, false, true, false);
		}
	}
}
