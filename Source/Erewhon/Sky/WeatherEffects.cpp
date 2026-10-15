#include "Sky/WeatherEffects.h"

#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Sky/CelestialSystem.h"
#include "Sky/ErewhonWind.h"
#include "World/ErewhonSound.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorTypes.h"
#include "World/SpawnLayout.h"
#include "World/Terrain.h"

using namespace ErewhonVisuals;

namespace
{
	constexpr int32 GDropCount = 1400;
	constexpr float GRainRadius = 1600.f;
	constexpr float GRainTop = 1400.f;
	constexpr float GDropSpeed = 1100.f;
	constexpr int32 GBitCount = 150;

	UInstancedStaticMeshComponent* MakeInstances(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FLinearColor& Color)
	{
		UInstancedStaticMeshComponent* Part = NewObject<UInstancedStaticMeshComponent>(Owner);
		Part->SetStaticMesh(Mesh);
		Part->SetupAttachment(Parent);
		Part->SetUsingAbsoluteLocation(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->SetUsingAbsoluteScale(true);
		Part->SetWorldTransform(FTransform::Identity);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->bVisibleInRayTracing = false;
		Part->SetMaterial(0, MakeColorMaterial(Part, Color));
		Part->SetBoundsScale(100.f);
		Part->RegisterComponent();
		return Part;
	}

	UAudioComponent* MakeLoop(AActor* Owner, USceneComponent* Parent, const TCHAR* Sound)
	{
		USoundWave* Wave = ErewhonSound::Load(Sound);
		if (!Wave)
		{
			return nullptr;
		}
		UAudioComponent* Audio = NewObject<UAudioComponent>(Owner);
		Audio->SetupAttachment(Parent);
		Audio->SetSound(Wave);
		Audio->bAllowSpatialization = false;
		Audio->SetVolumeMultiplier(0.f);
		Audio->RegisterComponent();
		Audio->Play(FMath::FRandRange(0.f, 4.f));
		return Audio;
	}
}

AWeatherEffects::AWeatherEffects()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWeatherEffects::BeginPlay()
{
	Super::BeginPlay();
	Rand.Initialize(FMath::Rand());
	// Les gouttes : de fins traits clairs, étirés dans le sens de la chute.
	Drops = MakeInstances(this, Root, GetShape(EShape::Cylinder), FLinearColor(0.85f, 0.9f, 0.97f));
	RainDrops.SetNum(GDropCount);
	// Les feuilles et les brins : de minces plaquettes, en trois teintes.
	for (const FLinearColor& Tint : { FLinearColor::FromSRGBColor(FColor(110, 160, 60)), FLinearColor::FromSRGBColor(FColor(214, 176, 72)), FLinearColor::FromSRGBColor(FColor(150, 96, 52)) })
	{
		Debris.Add(MakeInstances(this, Root, GetShape(EShape::Cube), Tint));
	}
	Bits.SetNum(GBitCount);
	// L'éclair : une lumière blanche qui ne vit qu'une fraction de seconde.
	Flash = NewObject<UDirectionalLightComponent>(this);
	Flash->SetupAttachment(Root);
	Flash->SetMobility(EComponentMobility::Movable);
	Flash->SetLightColor(FLinearColor(0.85f, 0.9f, 1.f));
	Flash->SetIntensity(0.f);
	Flash->SetCastShadows(false);
	Flash->SetVisibility(false);
	Flash->RegisterComponent();
	RainLight = MakeLoop(this, Root, TEXT("amb_rain_light"));
	RainHeavy = MakeLoop(this, Root, TEXT("amb_rain_heavy"));
	WindStrong = MakeLoop(this, Root, TEXT("amb_wind_strong"));
}

void AWeatherEffects::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ACelestialSystem* Sky = ACelestialSystem::Find(this);
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!Sky || !State || !PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const ErewhonWeather::FState& W = Sky->GetWeather();
	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	// Le vent du moment (m/s → cm/s), renforcé ou calmé par la météo.
	const FVector2D Wind = ErewhonWind::Velocity(State->GetWorldClock()) * 100.0 * W.Wind;
	const float WindForce = static_cast<float>(Wind.Size() / 100.0);   // m/s
	// Sous un toit, pas de pluie ; on l'entend, étouffée.
	const bool bSheltered = ErewhonSpawn::IsInsideCabin(Eye - FVector(0.f, 0.f, 150.f));
	UpdateRain(DeltaSeconds, Eye, W.Rain, Wind, bSheltered);
	UpdateDebris(DeltaSeconds, Eye, Wind, WindForce);
	UpdateLightning(DeltaSeconds, Eye, W.LightningPerMinute);
	UpdateAudio(DeltaSeconds, W.Rain, WindForce, bSheltered);
}

void AWeatherEffects::UpdateRain(float DeltaSeconds, const FVector& Eye, float Rain, const FVector2D& Wind, bool bSheltered)
{
	const int32 Wanted = bSheltered ? 0 : FMath::RoundToInt32(GDropCount * FMath::Clamp(Rain, 0.f, 1.f));
	if (Wanted == 0 && Drops->GetInstanceCount() == 0)
	{
		return;
	}
	// La goutte tombe et dérive avec le vent : un trait penché dans le sens de sa course.
	const FVector Fall(Wind.X * 0.35f, Wind.Y * 0.35f, -GDropSpeed * FMath::Lerp(0.85f, 1.15f, Rain));
	const FQuat Lean = FRotationMatrix::MakeFromZ(-Fall.GetSafeNormal()).ToQuat();
	const float Length = FMath::Lerp(40.f, 75.f, Rain);
	Scratch.Reset();
	for (int32 i = 0; i < Wanted; ++i)
	{
		FParticle& Drop = RainDrops[i];
		const bool bFar = !Drop.bAlive || FVector::Dist2D(Drop.Location, Eye) > GRainRadius * 1.1f || Drop.Location.Z < Eye.Z - 400.f;
		if (bFar)
		{
			// Une nouvelle goutte, quelque part au-dessus du joueur.
			const float A = Rand.FRandRange(0.f, UE_TWO_PI);
			const float R = GRainRadius * FMath::Sqrt(Rand.FRand());
			Drop.Location = Eye + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Drop.bAlive ? Rand.FRandRange(GRainTop * 0.8f, GRainTop) : Rand.FRandRange(-400.f, GRainTop));
			Drop.bAlive = true;
		}
		Drop.Location += Fall * DeltaSeconds;
		Scratch.Add(FTransform(Lean, Drop.Location, FVector(0.011f, 0.011f, Length / 100.f)));
	}
	if (Drops->GetInstanceCount() != Scratch.Num())
	{
		Drops->ClearInstances();
		Drops->AddInstances(Scratch, false, true);
	}
	else if (Scratch.Num() > 0)
	{
		Drops->BatchUpdateInstancesTransforms(0, Scratch, true, true);
	}
}

void AWeatherEffects::UpdateDebris(float DeltaSeconds, const FVector& Eye, const FVector2D& Wind, float WindForce)
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const ESectorType Type = ErewhonSector::TypeAt(ErewhonSector::FromWorld(Eye), State->GetWorldSeed());
	// Dans les bois, des feuilles qui tombent (plus en forêt) ; dans la prairie et les champs, des brins emportés quand ça souffle.
	const bool bWoods = Type == ESectorType::Forest || Type == ESectorType::Edge;
	const float Amount = bWoods ? (Type == ESectorType::Forest ? 1.f : 0.45f) * FMath::Lerp(0.5f, 1.f, FMath::Clamp(WindForce / 6.f, 0.f, 1.f))
		: FMath::SmoothStep(3.5f, 9.f, WindForce) * (Type == ESectorType::Spawn ? 0.3f : 1.f);
	const int32 Wanted = FMath::RoundToInt32(GBitCount * Amount);
	TArray<FTransform> PerTint[3];
	const FVector2D WindDir = Wind.GetSafeNormal();
	for (int32 i = 0; i < GBitCount; ++i)
	{
		FParticle& Bit = Bits[i];
		if (i >= Wanted)
		{
			Bit.bAlive = false;
			continue;
		}
		if (!Bit.bAlive || Bit.Life <= 0.f || FVector::Dist2D(Bit.Location, Eye) > 2600.f)
		{
			// Les feuilles naissent dans les cimes ; les brins, au ras de l'herbe, plutôt en amont du vent.
			const float A = Rand.FRandRange(0.f, UE_TWO_PI);
			const float R = Rand.FRandRange(200.f, 2200.f);
			FVector2D At = FVector2D(Eye) + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
			if (!bWoods)
			{
				At -= WindDir * Rand.FRandRange(300.f, 900.f);
			}
			const float Ground = ErewhonTerrain::Height(At, State->GetWorldSeed());
			Bit.Location = FVector(At, Ground + (bWoods ? Rand.FRandRange(400.f, 1100.f) : Rand.FRandRange(20.f, 160.f)));
			Bit.Velocity = FVector::ZeroVector;
			Bit.Rotation = FRotator(Rand.FRandRange(0.f, 360.f), Rand.FRandRange(0.f, 360.f), Rand.FRandRange(0.f, 360.f));
			Bit.Spin = FRotator(Rand.FRandRange(-300.f, 300.f), Rand.FRandRange(-300.f, 300.f), Rand.FRandRange(-300.f, 300.f));
			Bit.Life = bWoods ? Rand.FRandRange(8.f, 16.f) : Rand.FRandRange(2.5f, 6.f);
			Bit.bAlive = true;
		}
		// Le vent pousse ; la feuille descend en voletant, le brin bondit au ras du sol.
		const FVector Target = FVector(Wind * (bWoods ? 0.35f : 0.8f), bWoods ? -60.f + 40.f * FMath::Sin(Bit.Life * 4.f + i) : 30.f * FMath::Sin(Bit.Life * 6.f + i));
		Bit.Velocity = FMath::VInterpTo(Bit.Velocity, Target, DeltaSeconds, 1.5f);
		Bit.Location += Bit.Velocity * DeltaSeconds;
		Bit.Rotation += Bit.Spin * DeltaSeconds;
		Bit.Life -= DeltaSeconds;
		const float Ground = ErewhonTerrain::Height(FVector2D(Bit.Location), State->GetWorldSeed());
		if (Bit.Location.Z < Ground + 3.f)
		{
			Bit.Life = 0.f;   // posée au sol : elle disparaît, une autre naîtra
		}
		const float Shrink = FMath::Clamp(Bit.Life, 0.f, 1.f);
		const FVector Size = bWoods ? FVector(0.07f, 0.05f, 0.004f) : FVector(0.09f, 0.012f, 0.004f);
		PerTint[i % 3].Add(FTransform(Bit.Rotation, Bit.Location, Size * Shrink));
	}
	for (int32 Tint = 0; Tint < 3; ++Tint)
	{
		UInstancedStaticMeshComponent* Part = Debris[Tint];
		if (Part->GetInstanceCount() != PerTint[Tint].Num())
		{
			Part->ClearInstances();
			Part->AddInstances(PerTint[Tint], false, true);
		}
		else if (PerTint[Tint].Num() > 0)
		{
			Part->BatchUpdateInstancesTransforms(0, PerTint[Tint], true, true);
		}
	}
}

void AWeatherEffects::UpdateLightning(float DeltaSeconds, const FVector& Eye, float PerMinute)
{
	// Un éclair de temps en temps : deux ou trois battements de lumière, puis le tonnerre (plus tard s'il est loin).
	NextStrikeCheck -= DeltaSeconds;
	if (NextStrikeCheck <= 0.f)
	{
		NextStrikeCheck = 1.f;
		if (FlashTime < 0.f && PerMinute > 0.f && Rand.FRand() < PerMinute / 60.f)
		{
			FlashTime = 0.f;
			const float Distance = Rand.FRandRange(0.2f, 1.f);   // 0 : tout près, 1 : loin
			ThunderDelay = Distance * 6.f + 0.2f;
			ThunderSound = Distance < 0.4f ? 1 : Distance < 0.7f ? 3 : 2;
			ThunderVolume = FMath::Lerp(1.f, 0.45f, Distance);
			Flash->SetWorldRotation(FRotator(-Rand.FRandRange(35.f, 75.f), Rand.FRandRange(0.f, 360.f), 0.f));
		}
	}
	if (FlashTime >= 0.f)
	{
		FlashTime += DeltaSeconds;
		const float Pulse = FMath::Max3(FMath::Max(0.f, 1.f - FMath::Abs(FlashTime - 0.05f) / 0.05f),
			0.7f * FMath::Max(0.f, 1.f - FMath::Abs(FlashTime - 0.22f) / 0.06f), 0.45f * FMath::Max(0.f, 1.f - FMath::Abs(FlashTime - 0.4f) / 0.08f));
		Flash->SetVisibility(Pulse > 0.01f);
		Flash->SetIntensity(14.f * Pulse);
		if (FlashTime > 0.6f)
		{
			FlashTime = -1.f;
			Flash->SetVisibility(false);
		}
	}
	if (ThunderDelay >= 0.f)
	{
		ThunderDelay -= DeltaSeconds;
		if (ThunderDelay < 0.f)
		{
			const FString Name = FString::Printf(TEXT("thunder_%d"), ThunderSound);
			ErewhonSound::PlayAt(this, ErewhonSound::Load(Name), Eye + FVector(0.f, 0.f, 2000.f), ThunderVolume, 100000.f, 200000.f);
		}
	}
}

void AWeatherEffects::UpdateAudio(float DeltaSeconds, float Rain, float WindForce, bool bSheltered)
{
	const float Ambient = ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Ambient);
	const float Muffle = bSheltered ? 0.45f : 1.f;
	if (RainLight)
	{
		RainLight->SetVolumeMultiplier(Ambient * Muffle * FMath::Clamp(Rain * 2.5f, 0.f, 1.f) * (1.f - FMath::SmoothStep(0.5f, 0.9f, Rain)) * 0.8f);
	}
	if (RainHeavy)
	{
		RainHeavy->SetVolumeMultiplier(Ambient * Muffle * FMath::SmoothStep(0.45f, 0.9f, Rain));
	}
	if (WindStrong)
	{
		WindStrong->SetVolumeMultiplier(Ambient * Muffle * FMath::SmoothStep(6.f, 13.f, WindForce) * 0.8f);
	}
}
