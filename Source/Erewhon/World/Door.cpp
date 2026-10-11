#include "World/Door.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Core/ErewhonPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "World/ErewhonVisuals.h"
#include "World/ErewhonSound.h"

#define LOCTEXT_NAMESPACE "ErewhonDoor"

using namespace ErewhonVisuals;

namespace
{
	constexpr float GSwingDegreesPerSecond = 160.f;
}

ADoor::ADoor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	RootComponent = Hinge;
}

void ADoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADoor, bOpen);
}

void ADoor::BeginPlay()
{
	Super::BeginPlay();

	// Le battant pivote avec un composant intermédiaire : la racine reste sur les gonds.
	USceneComponent* Leaf = NewObject<USceneComponent>(this, TEXT("Leaf"));
	Leaf->SetupAttachment(Hinge);
	Leaf->RegisterComponent();
	AddInstanceComponent(Leaf);

	if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_door_wood")))
	{
		// Modèle dédié : largeur sur Y, façade vers −X (l'extérieur), pied sur les gonds.
		UStaticMeshComponent* Panel = NewObject<UStaticMeshComponent>(this);
		Panel->SetStaticMesh(Mesh);
		Panel->SetupAttachment(Leaf);
		Panel->SetRelativeTransform(FitToLength(Mesh, PanelSize.Y, PanelSize.Z) * FTransform(FVector(0.f, PanelSize.Y * 0.5f, 0.f)));
		Panel->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Panel->RegisterComponent();
		AddInstanceComponent(Panel);
		return;
	}

	UStaticMeshComponent* Panel = AddShape(this, Leaf, EShape::Cube, FVector(0.f, PanelSize.Y * 0.5f, PanelSize.Z * 0.5f), PanelSize, Palette::WoodDark);
	Panel->SetMaterial(0, MakePaintedMaterial(Panel, TEXT("wood_planks"), 180.f, Palette::WoodDark, FLinearColor(0.7f, 0.6f, 0.5f)));
	// Traverses et poignée, de chaque côté du battant.
	for (const float Side : { -1.f, 1.f })
	{
		for (const float Z : { 40.f, static_cast<float>(PanelSize.Z) - 40.f })
		{
			AddShape(this, Leaf, EShape::Cube, FVector(Side * (PanelSize.X * 0.5f + 1.5f), PanelSize.Y * 0.5f, Z), FVector(3.f, PanelSize.Y - 10.f, 12.f), Palette::WoodDark, FRotator::ZeroRotator, false);
		}
		AddShape(this, Leaf, EShape::Sphere, FVector(Side * (PanelSize.X * 0.5f + 4.f), PanelSize.Y - 18.f, 100.f), FVector(7.f), FLinearColor(0.35f, 0.3f, 0.22f), FRotator::ZeroRotator, false);
	}
}

void ADoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bOpen != bLastOpen)
	{
		bLastOpen = bOpen;
		ErewhonSound::PlayAt(this, ErewhonSound::Load(bOpen ? TEXT("door_open") : TEXT("door_close")),
			GetActorLocation() + FVector(0.f, PanelSize.Y * 0.5f, 110.f), 0.9f, 300.f, 2500.f);
	}
	const float Target = bOpen ? OpenYaw : 0.f;
	if (FMath::IsNearlyEqual(CurrentYaw, Target))
	{
		return;
	}
	CurrentYaw = FMath::FixedTurn(CurrentYaw, Target, GSwingDegreesPerSecond * DeltaSeconds);
	if (USceneComponent* Leaf = Hinge->GetChildComponent(0))
	{
		Leaf->SetRelativeRotation(FRotator(0.f, CurrentYaw, 0.f));
	}
}

FText ADoor::GetInteractionLabel() const
{
	return bOpen ? LOCTEXT("Close", "Fermer la porte") : LOCTEXT("Open", "Ouvrir la porte");
}

void ADoor::Interact(AErewhonPlayerController* Controller)
{
	if (Controller)
	{
		Controller->ServerUseDoor(this);
	}
}

void ADoor::Toggle()
{
	check(HasAuthority());
	bOpen = !bOpen;
}

#undef LOCTEXT_NAMESPACE
