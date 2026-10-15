#include "World/FelledTree.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "World/ErewhonSound.h"
#include "World/ErewhonVisuals.h"
#include "World/Farm.h"
#include "World/Forestry.h"
#include "World/SectorLayout.h"
#include "World/Terrain.h"

#define LOCTEXT_NAMESPACE "ErewhonForestry"

using namespace ErewhonVisuals;

namespace ErewhonForestry
{
	double RegrowSeconds(int32 HarvestIndex)
	{
		return IsTreeHarvest(HarvestIndex) ? ErewhonFarm::DaySeconds() * TreeRegrowDays : UErewhonSettings::Get().RegrowSeconds;
	}

	int32 HitsToFell(const FTreeInstance& Tree)
	{
		// Un jeune arbre en quatre coups, un vieux chêne de forêt en huit.
		return FMath::Clamp(FMath::RoundToInt32(2.f + Tree.Height * Tree.Girth / 400.f), 4, 8);
	}

	float TrunkLength(const FTreeInstance& Tree)
	{
		return Tree.Height * 0.55f;
	}

	float TrunkRadius(const FTreeInstance& Tree)
	{
		// Le même que le tronc invisible qui arrête le joueur.
		return ErewhonForest::TrunkRadius * Tree.Girth * Tree.Height / 1100.f;
	}

	int32 LogsIn(const FTreeInstance& Tree)
	{
		// Une bûche par mètre de tronc.
		return FMath::Clamp(FMath::RoundToInt32(TrunkLength(Tree) / 100.f), 3, 12);
	}

	int32 CarriedLogs(const UInventoryComponent* Inventory)
	{
		int32 Count = 0;
		for (const FInventorySlot& Slot : Inventory ? Inventory->GetSlots() : TArray<FInventorySlot>())
		{
			Count += Slot.ItemId == Log ? Slot.Count : 0;
		}
		return Count;
	}
}

namespace
{
	const FLinearColor GBark = FLinearColor::FromSRGBColor(FColor(104, 80, 60));
	const FLinearColor GLeaf = FLinearColor::FromSRGBColor(FColor(96, 150, 64));

	FTreeInstance AsTree(int32 Model, float Height, float Girth, float Yaw)
	{
		FTreeInstance Tree;
		Tree.Model = Model;
		Tree.Height = Height;
		Tree.Girth = Girth;
		Tree.Yaw = Yaw;
		return Tree;
	}
}

AFelledTree::AFelledTree()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
}

void AFelledTree::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFelledTree, Model);
	DOREPLIFETIME(AFelledTree, TreeHeight);
	DOREPLIFETIME(AFelledTree, Girth);
	DOREPLIFETIME(AFelledTree, TreeYaw);
	DOREPLIFETIME(AFelledTree, FallYaw);
	DOREPLIFETIME(AFelledTree, FellAt);
	DOREPLIFETIME(AFelledTree, LogsTotal);
	DOREPLIFETIME(AFelledTree, LogsLeft);
	DOREPLIFETIME(AFelledTree, OwnerKey);
	DOREPLIFETIME(AFelledTree, OwnerName);
}

void AFelledTree::Setup(const FTreeInstance& Tree, float InFallYaw, double InFellAt, const FString& InOwnerKey, const FString& InOwnerName, int32 InLogsLeft)
{
	Model = Tree.Model;
	TreeHeight = Tree.Height;
	Girth = Tree.Girth;
	TreeYaw = Tree.Yaw;
	FallYaw = InFallYaw;
	FellAt = InFellAt;
	OwnerKey = InOwnerKey;
	OwnerName = InOwnerName;
	LogsTotal = ErewhonForestry::LogsIn(Tree);
	LogsLeft = InLogsLeft >= 0 ? FMath::Min(InLogsLeft, LogsTotal) : LogsTotal;
}

void AFelledTree::SetLogsLeft(int32 Count)
{
	LogsLeft = FMath::Max(0, Count);
	OnRep_Logs();
	ForceNetUpdate();
}

void AFelledTree::OnRep_Logs()
{
	ApplyTrunk();
}

float AFelledTree::PitchAt(double Now) const
{
	// Il penche à peine, puis de plus en plus vite.
	const float T = FMath::Clamp(static_cast<float>((Now - FellAt) / ErewhonForestry::FallSeconds), 0.f, 1.f);
	return LandPitch * T * T * T;
}

bool AFelledTree::HasLanded() const
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	return !State || State->GetWorldClock() - FellAt >= ErewhonForestry::FallSeconds;
}

FVector AFelledTree::CrownCenter() const
{
	const FVector Along = FRotator(0.f, FallYaw, 0.f).Vector();
	return GetActorLocation() + Along * TreeHeight * 0.75f;
}

float AFelledTree::CrownRadius() const
{
	return TreeHeight * 0.2f * Girth;
}

void AFelledTree::BeginPlay()
{
	Super::BeginPlay();
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const FTreeInstance Tree = AsTree(Model, TreeHeight, Girth, TreeYaw);
	const float Length = ErewhonForestry::TrunkLength(Tree);

	// Couché selon la pente : la cime touche le sol, un peu plus bas ou plus haut que le pied.
	if (State)
	{
		const FVector2D Tip = FVector2D(GetActorLocation()) + FVector2D(FRotator(0.f, FallYaw, 0.f).Vector()) * Length;
		const float Drop = ErewhonTerrain::Height(Tip, State->GetWorldSeed()) - GetActorLocation().Z;
		LandPitch = 90.f - FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Drop / Length, -0.5f, 0.5f)));
	}
	Pivot->SetRelativeRotation(FRotator(0.f, FallYaw, 0.f));

	// Le tronc : un cylindre d'écorce, du pied jusqu'à la naissance de la cime. Il arrête le joueur et la visée.
	const float Radius = ErewhonForestry::TrunkRadius(Tree);
	Trunk = AddShape(this, Pivot, EShape::Cylinder, FVector(-Radius, 0.f, Length * 0.5f), FVector(Radius * 2.f, Radius * 2.f, Length), GBark, FRotator::ZeroRotator, true);
	Trunk->SetMaterial(0, MakePaintedMaterial(Trunk, TEXT("bark"), 160.f, GBark, FLinearColor(0.62f, 0.5f, 0.42f)));
	if (GetNetMode() != NM_DedicatedServer && ErewhonForest::TreeModels[FMath::Clamp(Model, 0, ErewhonForest::TreeModelCount - 1)])
	{
		if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), ErewhonForest::TreeModels[FMath::Clamp(Model, 0, ErewhonForest::TreeModelCount - 1)]))
		{
			// Le même arbre qu'en forêt, à la même échelle.
			const float Scale = TreeHeight / 100.f;
			TreeMesh = NewObject<UStaticMeshComponent>(this);
			TreeMesh->SetStaticMesh(Mesh);
			TreeMesh->SetupAttachment(Pivot);
			TreeMesh->SetRelativeTransform(FitToHeight(Mesh, 100.f) * FTransform(FRotator(0.f, TreeYaw - FallYaw, 0.f), FVector(0.f, 0.f, -10.f), FVector(Scale * Girth, Scale * Girth, Scale)));
			TreeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			TreeMesh->RegisterComponent();
			AddInstanceComponent(TreeMesh);
		}
	}

	// Déjà tombé (on arrive après coup, ou au chargement) : le tronc seul, au sol.
	const double Now = State ? State->GetWorldClock() : FellAt;
	if (Now - FellAt >= ErewhonForestry::FallSeconds)
	{
		bLanded = true;
		bSoundPlayed = true;
		if (TreeMesh)
		{
			TreeMesh->SetVisibility(false);
		}
		Pivot->SetRelativeRotation(FRotator(-LandPitch, FallYaw, 0.f));
	}
	else
	{
		// Pendant la chute, c'est l'arbre entier qu'on voit ; le tronc nu n'apparaîtra qu'au sol.
		Trunk->SetVisibility(!TreeMesh);
		Trunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	ApplyTrunk();
}

void AFelledTree::ApplyTrunk()
{
	if (!Trunk)
	{
		return;
	}
	// Les bûches partent du côté de la cime : le tronc raccourcit vers son pied.
	const FTreeInstance Tree = AsTree(Model, TreeHeight, Girth, TreeYaw);
	const float Radius = ErewhonForestry::TrunkRadius(Tree);
	const float Length = ErewhonForestry::TrunkLength(Tree) * FMath::Max(0.08f, static_cast<float>(LogsLeft) / FMath::Max(1, LogsTotal));
	Trunk->SetRelativeLocation(FVector(-Radius, 0.f, Length * 0.5f));
	Trunk->SetRelativeScale3D(FVector(Radius * 2.f / 100.f, Radius * 2.f / 100.f, Length / 100.f));
}

void AFelledTree::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State)
	{
		return;
	}
	const double Now = State->GetWorldClock();
	if (!bLanded)
	{
		if (!bSoundPlayed && GetNetMode() != NM_DedicatedServer)
		{
			bSoundPlayed = true;
			ErewhonSound::PlayAt(this, ErewhonSound::Load(TEXT("tree_fall")), GetActorLocation(), 1.f, 400.f, 4000.f);
		}
		Pivot->SetRelativeRotation(FRotator(-PitchAt(Now), FallYaw, 0.f));
		if (Now - FellAt >= ErewhonForestry::FallSeconds)
		{
			// Au sol : la cime éclate, il reste le tronc.
			bLanded = true;
			Pivot->SetRelativeRotation(FRotator(-LandPitch, FallYaw, 0.f));
			Trunk->SetVisibility(true);
			Trunk->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			if (TreeMesh)
			{
				TreeMesh->SetVisibility(false);
			}
			if (GetNetMode() != NM_DedicatedServer)
			{
				StartBurst();
			}
		}
	}
	if (BurstTime >= 0.f)
	{
		UpdateBurst(DeltaSeconds);
	}
}

void AFelledTree::StartBurst()
{
	// Des brassées de feuilles qui jaillissent de la cime et retombent en s'effaçant.
	ErewhonSound::PlayAt(this, ErewhonSound::Load(TEXT("leaves_burst")), CrownCenter(), 1.f, 300.f, 3000.f);
	UStaticMesh* Branch = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_leafy_branch"));
	UStaticMesh* Bush = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bush_box"));
	const FVector Along = FRotator(0.f, FallYaw, 0.f).Vector();
	const FVector Crown = CrownCenter() + FVector(0.f, 0.f, CrownRadius() * 0.5f);
	const float Spread = CrownRadius();
	FRandomStream Rand(GetUniqueID());
	for (int32 i = 0; i < 46; ++i)
	{
		const bool bBranch = Branch && (i % 3 != 0 || !Bush);
		UStaticMesh* Mesh = bBranch ? Branch : Bush ? Bush : GetShape(EShape::Sphere);
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetStaticMesh(Mesh);
		Part->SetupAttachment(Root);
		Part->SetUsingAbsoluteLocation(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->SetUsingAbsoluteScale(true);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		if (!bBranch && !Bush)
		{
			Part->SetMaterial(0, MakeColorMaterial(Part, GLeaf));
		}
		const FVector Offset = Along * Rand.FRandRange(-0.6f, 0.6f) * Spread * 1.4f + FVector(Rand.FRandRange(-1.f, 1.f), Rand.FRandRange(-1.f, 1.f), Rand.FRandRange(-0.3f, 0.6f)) * Spread;
		const float Size = (bBranch ? Rand.FRandRange(60.f, 110.f) : Rand.FRandRange(80.f, 140.f)) / 100.f;
		const FTransform Fit = FitToHeight(Mesh, 100.f);
		FLeaf Leaf;
		Leaf.Part = Part;
		Leaf.Scale = Fit.GetScale3D() * Size;
		Leaf.Velocity = Offset.GetSafeNormal() * Rand.FRandRange(250.f, 650.f) + FVector(0.f, 0.f, Rand.FRandRange(150.f, 450.f));
		Leaf.Spin = FRotator(Rand.FRandRange(-260.f, 260.f), Rand.FRandRange(-260.f, 260.f), Rand.FRandRange(-260.f, 260.f));
		Part->SetWorldLocationAndRotation(Crown + Offset, FRotator(Rand.FRandRange(0.f, 360.f), Rand.FRandRange(0.f, 360.f), 0.f));
		Part->SetWorldScale3D(Leaf.Scale);
		Part->RegisterComponent();
		Leaves.Add(Leaf);
	}
	BurstTime = 0.f;
}

void AFelledTree::UpdateBurst(float DeltaSeconds)
{
	constexpr float Life = 1.4f;
	BurstTime += DeltaSeconds;
	const float Fade = FMath::Clamp(1.f - BurstTime / Life, 0.f, 1.f);
	for (FLeaf& Leaf : Leaves)
	{
		UStaticMeshComponent* Part = Leaf.Part.Get();
		if (!Part)
		{
			continue;
		}
		Leaf.Velocity += FVector(0.f, 0.f, -980.f) * DeltaSeconds;
		Leaf.Velocity *= FMath::Max(0.f, 1.f - 1.2f * DeltaSeconds);   // l'air freine les feuilles
		Part->SetWorldLocationAndRotation(Part->GetComponentLocation() + Leaf.Velocity * DeltaSeconds, Part->GetComponentRotation() + Leaf.Spin * DeltaSeconds);
		Part->SetWorldScale3D(Leaf.Scale * FMath::Max(0.01f, Fade));
	}
	if (BurstTime >= Life)
	{
		for (FLeaf& Leaf : Leaves)
		{
			if (UStaticMeshComponent* Part = Leaf.Part.Get())
			{
				Part->DestroyComponent();
			}
		}
		Leaves.Reset();
		BurstTime = -1.f;
	}
}

FText AFelledTree::GetInteractionLabel() const
{
	const UWorld* World = GetWorld();
	const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
	const AErewhonPlayerState* State = Local ? Local->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (!State || !IsOwnedBy(State->GetPlayerKey()))
	{
		return FText::Format(LOCTEXT("TrunkOf", "Tronc abattu de {0}"), FText::FromString(OwnerName));
	}
	return State->GetInventory()->GetHandItem() == ErewhonForestry::Axe
		? FText::Format(LOCTEXT("TrunkAxe", "Tronc couché ({0} bûches)    [Clic] Couper une bûche"), FText::AsNumber(LogsLeft))
		: FText::Format(LOCTEXT("TrunkNoAxe", "Tronc couché ({0} bûches) : la hache en main pour le débiter"), FText::AsNumber(LogsLeft));
}

void AFelledTree::Interact(AErewhonPlayerController* Controller)
{
	if (Controller && HasLanded())
	{
		Controller->ServerChopLog(this);
	}
}

#undef LOCTEXT_NAMESPACE
