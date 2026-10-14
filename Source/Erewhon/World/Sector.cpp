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

void ASector::Initialize(const TSharedRef<const FSectorLayout>& InLayout, bool bWithPlants)
{
	Layout = InLayout;
	SetActorLocation(ErewhonSector::Origin(InLayout->Coord));
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Sector_%s"), *ErewhonSector::ToString(InLayout->Coord)));
#endif
	BuildGround(GetNetMode() != NM_DedicatedServer);
	BuildTrees(bWithPlants && GetNetMode() != NM_DedicatedServer);
	if (bWithPlants)
	{
		BuildDecor();
		BuildBushes();
		BuildPlants();
		BuildSignposts();
	}
}

void ASector::BuildSignposts()
{
	// Aux carrefours de la route : la direction et la distance de chaque commerce (amélioration
	// « panneaux »), et une lanterne au coin d'en face (amélioration « lanternes »).
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const bool bSigns = State && State->HasVillageUpgrade(EVillageUpgrade::Signposts);
	const bool bLanterns = State && State->HasVillageUpgrade(EVillageUpgrade::Lanterns);
	for (const FSignpostSpot& Spot : Layout->Signposts)
	{
		if (!ErewhonVillage::IsInVillage(Layout->Coord))
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
			if (Kind != EShopKind::Tailor || State->HasVillageUpgrade(EVillageUpgrade::Tailor))
			{
				Arms.Add(ErewhonSigns::ShopArm(Kind, Spot.Location, Yaw));
			}
		}
		ErewhonSigns::AddSignpost(this, Root, Spot.Location - GetActorLocation(), Arms, bLanterns);
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
	const int32 N = GGroundResolution;
	Vertices.Reserve((N + 1) * (N + 1));
	for (int32 Y = 0; Y <= N; ++Y)
	{
		for (int32 X = 0; X <= N; ++X)
		{
			const FVector2D Local(X * Size / N, Y * Size / N);
			const FVector2D World = Origin + Local;
			Vertices.Add(FVector(Local.X, Local.Y, ErewhonTerrain::Height(World, Layout->WorldSeed)));
			Normals.Add(ErewhonTerrain::Normal(World, Layout->WorldSeed));
			UVs.Add(Local / Size);   // 0 → 1 sur le secteur : le masque terre/herbe s'y lit
		}
	}
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
	Ground->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, {}, {}, true);
	Ground->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Ground->SetCastShadow(false);
	Ground->RegisterComponent();
	AddInstanceComponent(Ground);
	if (!bVisual)
	{
		return;
	}

	// Le sol mêle herbe et terre selon le masque du secteur : chemins, place et bords fondus d'un seul tenant.
	UMaterialInterface* Blend = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_GroundBlend"));
	UTexture* GrassTexture = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_ground_grass"));
	UTexture* DirtTexture = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_ground_dirt"));
	if ((!Layout->Paths.IsEmpty() || !Layout->Clearings.IsEmpty()) && Blend && GrassTexture && DirtTexture)
	{
		DirtMask = BuildDirtMask();
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Blend, Ground);
		Material->SetTextureParameterValue(TEXT("Albedo"), GrassTexture);
		Material->SetScalarParameterValue(TEXT("TileSize"), GroundTileSize);
		Material->SetTextureParameterValue(TEXT("AlbedoDirt"), DirtTexture);
		Material->SetScalarParameterValue(TEXT("DirtTileSize"), DirtTileSize);
		Material->SetTextureParameterValue(TEXT("DirtMask"), DirtMask);
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
	UTexture2D* Texture = UTexture2D::CreateTransient(GMaskResolution, GMaskResolution, PF_B8G8R8A8);
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;

	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector2D Origin(GetActorLocation());
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	uint8* Texels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < GMaskResolution; ++Y)
	{
		for (int32 X = 0; X < GMaskResolution; ++X)
		{
			// Texel (X, Y) ↔ monde (X, Y) : le matériau échantillonne avec les mêmes axes.
			const FVector2D World = Origin + FVector2D(X + 0.5f, Y + 0.5f) * (Size / GMaskResolution);
			uint8* Texel = Texels + (Y * GMaskResolution + X) * 4;   // B, G, R, A
			Texel[0] = 0;
			Texel[1] = static_cast<uint8>(FMath::RoundToInt32(Layout->PavedAmount(World) * 255.f));
			Texel[2] = static_cast<uint8>(FMath::RoundToInt32(Layout->DirtAmount(World) * 255.f));
			Texel[3] = 255;
		}
	}
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
	// Les troncs : des cylindres invisibles, en instances, qui arrêtent le joueur.
	UHierarchicalInstancedStaticMeshComponent* Trunks = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	Trunks->SetStaticMesh(GetShape(EShape::Cylinder));
	Trunks->SetupAttachment(Root);
	Trunks->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Trunks->SetHiddenInGame(true);
	Trunks->SetCanEverAffectNavigation(false);
	Trunks->RegisterComponent();
	AddInstanceComponent(Trunks);
	TArray<FTransform> TrunkTransforms;
	TArray<TArray<FTransform>> Batches;
	Batches.SetNum(ErewhonForest::TreeModelCount);
	TArray<UStaticMesh*> Models;
	TArray<FTransform> Fits;
	for (int32 i = 0; i < ErewhonForest::TreeModelCount; ++i)
	{
		UStaticMesh* Model = bVisual ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), ErewhonForest::TreeModels[i]) : nullptr;
		Models.Add(Model);
		Fits.Add(Model ? FitToHeight(Model, 100.f) : FTransform::Identity);
	}
	for (const FTreeInstance& Tree : Layout->Trees)
	{
		const FVector Local = Tree.Location - GetActorLocation();
		const float R = ErewhonForest::TrunkRadius * 2.f / 100.f * Tree.Girth * Tree.Height / 1100.f;
		TrunkTransforms.Add(FTransform(FRotator::ZeroRotator, Local + FVector(0, 0, 150.f), FVector(R, R, 3.f)));
		if (Models.IsValidIndex(Tree.Model) && Models[Tree.Model])
		{
			const float Scale = Tree.Height / 100.f;
			Batches[Tree.Model].Add(Fits[Tree.Model] * FTransform(FRotator(0.f, Tree.Yaw, 0.f), Local - FVector(0, 0, 10.f), FVector(Scale * Tree.Girth, Scale * Tree.Girth, Scale)));
		}
	}
	Trunks->AddInstances(TrunkTransforms, false, false);

	// En forêt, la canopée : de larges masses de feuillage entre les cimes, qui ferment le ciel au-dessus de soi.
	UStaticMesh* Crown = bVisual && Layout->Type == ESectorType::Forest ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bush_box")) : nullptr;
	if (Crown)
	{
		const FTransform CrownFit = FitToHeight(Crown, 100.f);
		const float Size = UErewhonSettings::Get().SectorSize();
		FRandomStream Rand(HashCombine(GetTypeHash(Layout->Coord), 0xC4A0u));
		TArray<FTransform> Crowns;
		for (float X = 350.f; X < Size; X += 700.f)
		{
			for (float Y = 350.f; Y < Size; Y += 700.f)
			{
				const FVector2D Local(X + Rand.FRandRange(-220.f, 220.f), Y + Rand.FRandRange(-220.f, 220.f));
				const FVector2D World = FVector2D(GetActorLocation()) + Local;
				if (Layout->DirtAmount(World) > 0.3f)
				{
					continue;   // au-dessus des chemins et des clairières, le ciel reste ouvert
				}
				const float Ground = ErewhonTerrain::Height(World, Layout->WorldSeed) - GetActorLocation().Z;
				const float Height = Rand.FRandRange(350.f, 520.f);
				Crowns.Add(CrownFit * FTransform(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), FVector(Local, Ground + Rand.FRandRange(950.f, 1350.f)),
					FVector(Height / 100.f * 2.1f, Height / 100.f * 2.1f, Height / 100.f)));
			}
		}
		UHierarchicalInstancedStaticMeshComponent* Canopy = MakeInstancedPart(Crown, nullptr, 400.f);
		Canopy->SetCastShadow(true);
		Canopy->AddInstances(Crowns, false, false);
	}
	for (int32 i = 0; i < Batches.Num(); ++i)
	{
		if (Batches[i].Num() == 0)
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Part = MakeInstancedPart(Models[i], nullptr, 400.f);
		Part->SetCastShadow(true);   // de grands arbres : leur ombre compte
		Part->AddInstances(Batches[i], false, false);
	}
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
