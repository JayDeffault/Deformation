// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformationComponent.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

UDeformationComponent::UDeformationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	DefaultBoneSettings.BoneName = NAME_None;
	DefaultBoneSettings.MinImpulse = 0.0f;
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

	if (BoneStates.Num() > 0 && RecoverySpeed > 0.0f)
	{
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

	// Keep the visible poseable mesh synchronized with the simulated TargetMesh even before the first dent.
	RefreshDirectBoneTransforms();
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
		TargetMesh->SetAllBodiesNotifyRigidBodyCollision(true);
		TargetMesh->OnComponentHit.AddUniqueDynamic(this, &UDeformationComponent::HandleMeshHit);
	}

	InitializeDirectBoneTransforms();
}

void UDeformationComponent::SetPoseableMesh(UPoseableMeshComponent* MeshComponent)
{
	PoseableMesh = MeshComponent;
	InitializeDirectBoneTransforms();
}

bool UDeformationComponent::InitializeDirectBoneTransforms()
{
	if (!bApplyDirectBoneTransforms || !TargetMesh)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (!PoseableMesh && bAutoCreatePoseableMesh && Owner)
	{
		PoseableMesh = NewObject<UPoseableMeshComponent>(Owner, UPoseableMeshComponent::StaticClass(), TEXT("DeformationPoseableMesh"));
		if (PoseableMesh)
		{
			Owner->AddInstanceComponent(PoseableMesh);
			PoseableMesh->SetupAttachment(TargetMesh);
			PoseableMesh->RegisterComponent();
		}
	}

	if (!PoseableMesh)
	{
		return false;
	}

	PoseableMesh->SetSkinnedAssetAndUpdate(TargetMesh->GetSkinnedAsset());
	for (int32 MaterialIndex = 0; MaterialIndex < TargetMesh->GetNumMaterials(); ++MaterialIndex)
	{
		PoseableMesh->SetMaterial(MaterialIndex, TargetMesh->GetMaterial(MaterialIndex));
	}
	if (!PoseableMesh->GetAttachParent())
	{
		PoseableMesh->AttachToComponent(TargetMesh, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}
	PoseableMesh->SetRelativeTransform(FTransform::Identity);
	PoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseableMesh->SetGenerateOverlapEvents(false);
	PoseableMesh->SetCastShadow(true);
	PoseableMesh->SetVisibility(true, true);
	PoseableMesh->SetHiddenInGame(false, true);
	PoseableMesh->CopyPoseFromSkeletalComponent(TargetMesh);

	if (bHideTargetMeshWhenUsingPoseable)
	{
		TargetMesh->SetVisibility(true, false);
		TargetMesh->SetHiddenInGame(false, false);
		TargetMesh->SetRenderInMainPass(false);
		TargetMesh->SetCastShadow(false);
		PoseableMesh->SetVisibility(true, true);
		PoseableMesh->SetHiddenInGame(false, true);
	}

	return RefreshDirectBoneTransforms();
}

bool UDeformationComponent::RefreshDirectBoneTransforms()
{
	RemoveRootBoneState();

	if (!bApplyDirectBoneTransforms || !PoseableMesh)
	{
		return false;
	}

	if (TargetMesh && bCopyTargetPoseBeforeApplyingDirectOffsets)
	{
		PoseableMesh->CopyPoseFromSkeletalComponent(TargetMesh);
		ApplyPhysicsBodyTransformsToPoseable();
	}

	for (const TPair<FName, FDeformationBoneState>& Pair : BoneStates)
	{
		ApplyDirectOffsetToPoseableBone(Pair.Key, Pair.Value.OffsetCS);
	}

	PoseableMesh->RefreshBoneTransforms();
	return true;
}

void UDeformationComponent::ResetDeformation(FName BoneName)
{
	if (BoneName.IsNone())
	{
		BoneStates.Reset();
		RefreshDirectBoneTransforms();
		return;
	}

	BoneStates.Remove(BoneName);
	RefreshDirectBoneTransforms();
}

bool UDeformationComponent::IsRootBone(FName BoneName) const
{
	return !BoneName.IsNone() && !RootBone.IsNone() && BoneName == RootBone;
}

FVector UDeformationComponent::GetBoneDeformationOffset(FName BoneName) const
{
	if (IsRootBone(BoneName))
	{
		return FVector::ZeroVector;
	}

	if (const FDeformationBoneState* State = BoneStates.Find(BoneName))
	{
		return State->OffsetCS;
	}

	return FVector::ZeroVector;
}

bool UDeformationComponent::GetBoneDeformationState(FName BoneName, FDeformationBoneState& OutState) const
{
	if (IsRootBone(BoneName))
	{
		OutState = FDeformationBoneState();
		return false;
	}

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
		if (!IsRootBone(Pair.Key))
		{
			OutStates.Add(Pair.Value);
		}
	}
}

bool UDeformationComponent::ApplyDeformationImpulse(FName BoneName, const FVector& HitLocationWS, const FVector& HitNormalWS, float NormalImpulse)
{
	if (!TargetMesh || BoneName.IsNone())
	{
		return false;
	}

	if (IsRootBone(BoneName))
	{
		BoneStates.Remove(BoneName);
		RefreshDirectBoneTransforms();
		return false;
	}

	if (!CanDeformPhysicsBody(BoneName))
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

	const FVector HitDerivedDirectionWS = ResolveInwardDeformationDirection(HitLocationWS, SafeNormalWS);
	const FVector HitDerivedDirectionCS = TargetMesh->GetComponentTransform().InverseTransformVectorNoScale(HitDerivedDirectionWS).GetSafeNormal();
	if (HitDerivedDirectionCS.IsNearlyZero())
	{
		return false;
	}

	FDeformationBoneState& State = FindOrAddState(BoneName);
	const FVector BoneNormalDirectionCS = bUseBoneNormalForDeformation ? ResolveBoneNormalDirectionCS(BoneName) : FVector::ZeroVector;
	FVector DeformationDirectionCS = Settings->bUseCustomDeformationDirection
		? Settings->DeformationDirectionCS.GetSafeNormal()
		: (BoneNormalDirectionCS.IsNearlyZero() ? HitDerivedDirectionCS : BoneNormalDirectionCS);
	if (DeformationDirectionCS.IsNearlyZero())
	{
		return false;
	}

	const FVector PreviousOffsetCS = State.OffsetCS;
	float DirectionScale = 1.0f;
	const bool bUsingBoneNormalDirection = !Settings->bUseCustomDeformationDirection && !BoneNormalDirectionCS.IsNearlyZero();
	if (Settings->bUseCustomDeformationDirection || Settings->bLockDeformationDirection || bAccumulateHitsToMaxOffset)
	{
		if (Settings->bUseCustomDeformationDirection)
		{
			DirectionScale = FVector::DotProduct(HitDerivedDirectionCS, DeformationDirectionCS);
			if (DirectionScale <= KINDA_SMALL_NUMBER)
			{
				return false;
			}
		}
		else if (bUsingBoneNormalDirection)
		{
			DirectionScale = 1.0f;
		}
		else if (!State.OffsetCS.IsNearlyZero())
		{
			DeformationDirectionCS = State.OffsetCS.GetSafeNormal();
			DirectionScale = FVector::DotProduct(HitDerivedDirectionCS, DeformationDirectionCS);
			if (DirectionScale <= KINDA_SMALL_NUMBER)
			{
				return false;
			}
		}

		const float PreviousDepth = FMath::Max(0.0f, FVector::DotProduct(State.OffsetCS, DeformationDirectionCS));
		const float NewDepth = FMath::Min(PreviousDepth + OffsetAmount * DirectionScale, Settings->MaxOffset);
		State.OffsetCS = DeformationDirectionCS * NewDepth;
	}
	else
	{
		State.OffsetCS += DeformationDirectionCS * OffsetAmount;
		State.OffsetCS = State.OffsetCS.GetClampedToMaxSize(Settings->MaxOffset);
	}
	const FVector AppliedDeltaCS = State.OffsetCS - PreviousOffsetCS;
	const FVector AppliedDeltaWS = TargetMesh->GetComponentTransform().TransformVectorNoScale(AppliedDeltaCS);
	State.LastImpulse = NormalImpulse;
	State.LastHitLocationWS = HitLocationWS;

	if (bMovePhysicsBodyWithDeformation && !AppliedDeltaWS.IsNearlyZero())
	{
		MovePhysicsBodyByOffset(BoneName, AppliedDeltaWS);
	}

	if (bApplyPhysicsImpulse)
	{
		const FVector DeformationDirectionWS = TargetMesh->GetComponentTransform().TransformVectorNoScale(DeformationDirectionCS).GetSafeNormal();
		TargetMesh->WakeRigidBody(BoneName);
		TargetMesh->AddImpulse(DeformationDirectionWS * NormalImpulse * Settings->PhysicsImpulseScale, BoneName, bVelocityChange);
	}

	RefreshDirectBoneTransforms();
	OnBoneDeformed.Broadcast(BoneName, State.OffsetCS, NormalImpulse);
	return true;
}

void UDeformationComponent::HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	const FName BoneName = ResolveHitBone(Hit);
	float ImpactImpulse = NormalImpulse.Size();

	if (ImpactImpulse <= KINDA_SMALL_NUMBER && bEstimateKinematicHitImpulse)
	{
		const FVector HitVelocity = HitComponent ? HitComponent->GetComponentVelocity() : FVector::ZeroVector;
		const FVector OtherVelocity = OtherComp ? OtherComp->GetComponentVelocity() : FVector::ZeroVector;
		const FVector RelativeVelocity = OtherVelocity - HitVelocity;
		const float NormalSpeed = FMath::Abs(FVector::DotProduct(RelativeVelocity, Hit.ImpactNormal.GetSafeNormal()));
		ImpactImpulse = NormalSpeed * KinematicHitImpulseScale;
	}

	ApplyDeformationImpulse(BoneName, Hit.ImpactPoint, Hit.ImpactNormal, ImpactImpulse);
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
	// For OnComponentHit on TargetMesh, MyBoneName is the PHAT body/bone that belongs to this vehicle.
	if (!Hit.MyBoneName.IsNone() && !IsRootBone(Hit.MyBoneName) && CanDeformPhysicsBody(Hit.MyBoneName))
	{
		FBodyInstance* BodyInstance = TargetMesh ? TargetMesh->GetBodyInstance(Hit.MyBoneName) : nullptr;
		if (!bPreferKinematicBodiesForDeformation || (BodyInstance && !BodyInstance->IsInstanceSimulatingPhysics()))
		{
			return Hit.MyBoneName;
		}
	}

	if (!Hit.BoneName.IsNone() && !IsRootBone(Hit.BoneName) && CanDeformPhysicsBody(Hit.BoneName))
	{
		FBodyInstance* BodyInstance = TargetMesh ? TargetMesh->GetBodyInstance(Hit.BoneName) : nullptr;
		if (!bPreferKinematicBodiesForDeformation || (BodyInstance && !BodyInstance->IsInstanceSimulatingPhysics()))
		{
			return Hit.BoneName;
		}
	}

	// When only the chassis/root body is simulated and the dent bodies are kinematic, Chaos can report the root body
	// for the component hit. Use the closest non-root PHAT body to the impact point so side panels/doors still deform.
	return FindClosestDeformableBody(Hit.ImpactPoint);
}

FName UDeformationComponent::FindClosestDeformableBody(const FVector& HitLocationWS) const
{
	if (!TargetMesh)
	{
		return NAME_None;
	}

	UPhysicsAsset* PhysicsAsset = TargetMesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return NAME_None;
	}

	FName ClosestBone = NAME_None;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	FName ClosestKinematicBone = NAME_None;
	float ClosestKinematicDistanceSquared = TNumericLimits<float>::Max();

	for (USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!BodySetup || BodySetup->BoneName.IsNone() || IsRootBone(BodySetup->BoneName))
		{
			continue;
		}

		if (!CanDeformPhysicsBody(BodySetup->BoneName))
		{
			continue;
		}

		const FBodyInstance* BodyInstance = TargetMesh->GetBodyInstance(BodySetup->BoneName);
		const int32 BoneIndex = TargetMesh->GetBoneIndex(BodySetup->BoneName);
		const FVector BodyLocationWS = BodyInstance
			? BodyInstance->GetUnrealWorldTransform().GetLocation()
			: (BoneIndex == INDEX_NONE ? FVector::ZeroVector : TargetMesh->GetBoneTransform(BoneIndex).GetLocation());

		const float DistanceSquared = FVector::DistSquared(BodyLocationWS, HitLocationWS);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestBone = BodySetup->BoneName;
		}

		if (bPreferKinematicBodiesForDeformation && BodyInstance && !BodyInstance->IsInstanceSimulatingPhysics() && DistanceSquared < ClosestKinematicDistanceSquared)
		{
			ClosestKinematicDistanceSquared = DistanceSquared;
			ClosestKinematicBone = BodySetup->BoneName;
		}
	}

	return ClosestKinematicBone.IsNone() ? ClosestBone : ClosestKinematicBone;
}

bool UDeformationComponent::CanDeformPhysicsBody(FName BoneName) const
{
	if (!TargetMesh || BoneName.IsNone() || IsRootBone(BoneName))
	{
		return false;
	}

	if (!bDeformOnlyKinematicBodies)
	{
		return true;
	}

	FBodyInstance* BodyInstance = TargetMesh->GetBodyInstance(BoneName);
	return BodyInstance && !BodyInstance->IsInstanceSimulatingPhysics();
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

FVector UDeformationComponent::ResolveInwardDeformationDirection(const FVector& HitLocationWS, const FVector& HitNormalWS) const
{
	if (!bForceInwardDeformation)
	{
		return HitNormalWS.GetSafeNormal();
	}

	if (!TargetMesh)
	{
		return HitNormalWS.GetSafeNormal();
	}

	const FVector ToMeshCenterWS = (TargetMesh->Bounds.Origin - HitLocationWS).GetSafeNormal();
	FVector CandidateDirectionWS = HitNormalWS.GetSafeNormal();

	if (CandidateDirectionWS.IsNearlyZero())
	{
		return ToMeshCenterWS.IsNearlyZero() ? FVector::ZeroVector : ToMeshCenterWS;
	}

	if (!ToMeshCenterWS.IsNearlyZero() && FVector::DotProduct(CandidateDirectionWS, ToMeshCenterWS) < 0.0f)
	{
		CandidateDirectionWS *= -1.0f;
	}

	return CandidateDirectionWS;
}

FVector UDeformationComponent::ResolveBoneNormalDirectionCS(FName BoneName) const
{
	if (!TargetMesh || BoneName.IsNone())
	{
		return FVector::ZeroVector;
	}

	const int32 BoneIndex = TargetMesh->GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}

	const FVector AxisWS = TargetMesh->GetBoneTransform(BoneIndex).TransformVectorNoScale(GetAxisVector(BoneNormalAxis)).GetSafeNormal();
	return TargetMesh->GetComponentTransform().InverseTransformVectorNoScale(AxisWS).GetSafeNormal();
}

FVector UDeformationComponent::GetAxisVector(EDeformationBoneNormalAxis Axis) const
{
	switch (Axis)
	{
	case EDeformationBoneNormalAxis::X:
		return FVector::ForwardVector;
	case EDeformationBoneNormalAxis::NegativeX:
		return -FVector::ForwardVector;
	case EDeformationBoneNormalAxis::Y:
		return FVector::RightVector;
	case EDeformationBoneNormalAxis::NegativeY:
		return -FVector::RightVector;
	case EDeformationBoneNormalAxis::Z:
		return FVector::UpVector;
	case EDeformationBoneNormalAxis::NegativeZ:
		return -FVector::UpVector;
	default:
		return FVector::ZeroVector;
	}
}

void UDeformationComponent::ApplyPhysicsBodyTransformsToPoseable() const
{
	if (!TargetMesh || !PoseableMesh)
	{
		return;
	}

	UPhysicsAsset* PhysicsAsset = TargetMesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	const FTransform PoseableWorldTransform = PoseableMesh->GetComponentTransform();
	for (USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!BodySetup || BodySetup->BoneName.IsNone() || IsRootBone(BodySetup->BoneName))
		{
			continue;
		}

		FBodyInstance* BodyInstance = TargetMesh->GetBodyInstance(BodySetup->BoneName);
		if (!BodyInstance)
		{
			continue;
		}

		const FTransform BodyTransformCS = BodyInstance->GetUnrealWorldTransform().GetRelativeTransform(PoseableWorldTransform);
		PoseableMesh->SetBoneTransformByName(BodySetup->BoneName, BodyTransformCS, EBoneSpaces::ComponentSpace);
	}
}

void UDeformationComponent::ApplyDirectOffsetToPoseableBone(FName BoneName, const FVector& OffsetCS) const
{
	if (IsRootBone(BoneName))
	{
		return;
	}

	if (!PoseableMesh || BoneName.IsNone())
	{
		return;
	}

	const int32 BoneIndex = PoseableMesh->GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return;
	}

	const FVector CurrentLocationCS = PoseableMesh->GetBoneLocationByName(BoneName, EBoneSpaces::ComponentSpace);
	PoseableMesh->SetBoneLocationByName(BoneName, CurrentLocationCS + OffsetCS, EBoneSpaces::ComponentSpace);
}

void UDeformationComponent::MovePhysicsBodyByOffset(FName BoneName, const FVector& OffsetWS) const
{
	if (!TargetMesh || BoneName.IsNone() || IsRootBone(BoneName) || OffsetWS.IsNearlyZero())
	{
		return;
	}

	FBodyInstance* BodyInstance = TargetMesh->GetBodyInstance(BoneName);
	if (!BodyInstance)
	{
		return;
	}

	if (bDeformOnlyKinematicBodies && BodyInstance->IsInstanceSimulatingPhysics())
	{
		return;
	}

	FTransform BodyTransform = BodyInstance->GetUnrealWorldTransform();
	BodyTransform.AddToTranslation(OffsetWS);
	BodyInstance->SetBodyTransform(BodyTransform, ETeleportType::TeleportPhysics);
	TargetMesh->WakeRigidBody(BoneName);
}

void UDeformationComponent::RemoveRootBoneState()
{
	if (!RootBone.IsNone())
	{
		BoneStates.Remove(RootBone);
	}
}
