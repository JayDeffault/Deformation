// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformationComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

UDeformationComponent::UDeformationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	DefaultBoneSettings.BoneName = NAME_None;
}

void UDeformationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!TargetMesh)
	{
		if (AActor* Owner = GetOwner())
		{
			TargetMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
		}
	}

	BindToMesh(TargetMesh);
}

void UDeformationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TargetMesh)
	{
		TargetMesh->OnComponentHit.RemoveDynamic(this, &UDeformationComponent::HandleMeshHit);
	}

	Super::EndPlay(EndPlayReason);
}

void UDeformationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (RecoverySpeed <= 0.0f || BoneStates.Num() == 0)
	{
		return;
	}

	for (auto It = BoneStates.CreateIterator(); It; ++It)
	{
		FDeformationBoneState& State = It.Value();
		State.OffsetCS = FMath::VInterpTo(State.OffsetCS, FVector::ZeroVector, DeltaTime, RecoverySpeed);
		if (State.OffsetCS.IsNearlyZero(0.01f))
		{
			It.RemoveCurrent();
		}
	}
}

void UDeformationComponent::BindToMesh(USkeletalMeshComponent* MeshComponent)
{
	if (TargetMesh)
	{
		TargetMesh->OnComponentHit.RemoveDynamic(this, &UDeformationComponent::HandleMeshHit);
	}

	TargetMesh = MeshComponent;

	if (TargetMesh)
	{
		TargetMesh->SetNotifyRigidBodyCollision(true);
		TargetMesh->OnComponentHit.AddUniqueDynamic(this, &UDeformationComponent::HandleMeshHit);
	}
}

void UDeformationComponent::ResetDeformation(FName BoneName)
{
	if (BoneName.IsNone())
	{
		BoneStates.Reset();
		return;
	}

	BoneStates.Remove(BoneName);
}

FVector UDeformationComponent::GetBoneDeformationOffset(FName BoneName) const
{
	if (const FDeformationBoneState* State = BoneStates.Find(BoneName))
	{
		return State->OffsetCS;
	}

	return FVector::ZeroVector;
}

bool UDeformationComponent::GetBoneDeformationState(FName BoneName, FDeformationBoneState& OutState) const
{
	if (const FDeformationBoneState* State = BoneStates.Find(BoneName))
	{
		OutState = *State;
		return true;
	}

	OutState = FDeformationBoneState();
	return false;
}

void UDeformationComponent::GetAllDeformationStates(TArray<FDeformationBoneState>& OutStates) const
{
	OutStates.Reset(BoneStates.Num());
	for (const TPair<FName, FDeformationBoneState>& Pair : BoneStates)
	{
		OutStates.Add(Pair.Value);
	}
}

bool UDeformationComponent::ApplyDeformationImpulse(FName BoneName, const FVector& HitLocationWS, const FVector& HitNormalWS, float NormalImpulse)
{
	if (!TargetMesh || BoneName.IsNone())
	{
		return false;
	}

	const FDeformationBoneSettings* Settings = FindSettings(BoneName);
	if (!Settings)
	{
		return false;
	}

	if (NormalImpulse < Settings->MinImpulse)
	{
		return false;
	}

	const float Alpha = FMath::Clamp((NormalImpulse - Settings->MinImpulse) / FMath::Max(Settings->ImpulseForMaxOffset - Settings->MinImpulse, 1.0f), 0.0f, 1.0f);
	const float OffsetAmount = Alpha * Settings->MaxOffset;
	if (OffsetAmount <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector SafeNormalWS = HitNormalWS.GetSafeNormal();
	if (SafeNormalWS.IsNearlyZero())
	{
		return false;
	}

	// ImpactNormal usually points out of the hit surface, so moving along the inverse normal dents inward.
	const FVector DeformationDirectionWS = -SafeNormalWS;
	const FVector DeformationDirectionCS = TargetMesh->GetComponentTransform().InverseTransformVectorNoScale(DeformationDirectionWS).GetSafeNormal();

	FDeformationBoneState& State = FindOrAddState(BoneName);
	State.OffsetCS += DeformationDirectionCS * OffsetAmount;
	State.OffsetCS = State.OffsetCS.GetClampedToMaxSize(Settings->MaxOffset);
	State.LastImpulse = NormalImpulse;
	State.LastHitLocationWS = HitLocationWS;

	if (bApplyPhysicsImpulse)
	{
		TargetMesh->WakeRigidBody(BoneName);
		TargetMesh->AddImpulse(DeformationDirectionWS * NormalImpulse * Settings->PhysicsImpulseScale, BoneName, bVelocityChange);
	}

	OnBoneDeformed.Broadcast(BoneName, State.OffsetCS, NormalImpulse);
	return true;
}

void UDeformationComponent::HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	const FName BoneName = ResolveHitBone(Hit);
	ApplyDeformationImpulse(BoneName, Hit.ImpactPoint, Hit.ImpactNormal, NormalImpulse.Size());
}

const FDeformationBoneSettings* UDeformationComponent::FindSettings(FName BoneName) const
{
	for (const FDeformationBoneSettings& Settings : BoneSettings)
	{
		if (Settings.BoneName == BoneName)
		{
			return &Settings;
		}
	}

	return bOnlyConfiguredBones ? nullptr : &DefaultBoneSettings;
}

FName UDeformationComponent::ResolveHitBone(const FHitResult& Hit) const
{
	if (!Hit.BoneName.IsNone())
	{
		return Hit.BoneName;
	}

	if (!Hit.MyBoneName.IsNone())
	{
		return Hit.MyBoneName;
	}

	return NAME_None;
}

FDeformationBoneState& UDeformationComponent::FindOrAddState(FName BoneName)
{
	if (FDeformationBoneState* Existing = BoneStates.Find(BoneName))
	{
		return *Existing;
	}

	FDeformationBoneState NewState;
	NewState.BoneName = BoneName;
	return BoneStates.Add(BoneName, NewState);
}
