// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformationVehiclePawn.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DeformationComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

ADeformationVehiclePawn::ADeformationVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	PawnRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PawnRoot"));
	SetRootComponent(PawnRoot);
	PawnRoot->SetMobility(EComponentMobility::Movable);

	TargetMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TargetMesh"));
	TargetMesh->SetupAttachment(PawnRoot);
	TargetMesh->SetMobility(EComponentMobility::Movable);
	TargetMesh->SetRelativeTransform(FTransform::Identity);
	TargetMesh->SetCollisionProfileName(CollisionProfileName);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetEnableGravity(bEnableGravity);
	TargetMesh->SetNotifyRigidBodyCollision(true);
	TargetMesh->SetGenerateOverlapEvents(false);

	PoseableMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("PoseableMesh"));
	PoseableMesh->SetupAttachment(PawnRoot);
	PoseableMesh->SetMobility(EComponentMobility::Movable);
	PoseableMesh->SetRelativeTransform(FTransform::Identity);
	PoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseableMesh->SetGenerateOverlapEvents(false);

	DeformationComponent = CreateDefaultSubobject<UDeformationComponent>(TEXT("DeformationComponent"));
	DeformationComponent->TargetMesh = TargetMesh;
	DeformationComponent->PoseableMesh = PoseableMesh;
	DeformationComponent->bApplyDirectBoneTransforms = true;
	DeformationComponent->bAutoCreatePoseableMesh = false;
	DeformationComponent->bHideTargetMeshWhenUsingPoseable = true;
	DeformationComponent->bOnlyConfiguredBones = false;
	DeformationComponent->bApplyPhysicsImpulse = !bUseKinematicPhysicsBodies;
}

void ADeformationVehiclePawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	InitialBodyTransformsRelativeToRoot.Reset();
	ConfigureDeformation();
}

void ADeformationVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// A simulated skeletal mesh drives only the chassis/root body in this pawn. Keep every non-root PHAT body kinematic
	// and explicitly attached to the root body's current world transform so Chaos cannot leave them at world zero.
	AlignKinematicBodiesToCurrentBones();
}

void ADeformationVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	InitialBodyTransformsRelativeToRoot.Reset();
	ConfigureDeformation();
}

void ADeformationVehiclePawn::ConfigureDeformation()
{
	ConfigureTargetMeshTransform();
	ConfigureTargetMeshCollisionAndPhysics();
	ConfigurePoseableMeshTransform();

	if (!DeformationComponent)
	{
		return;
	}

	DeformationComponent->TargetMesh = TargetMesh;
	DeformationComponent->PoseableMesh = PoseableMesh;
	DeformationComponent->RootBone = RootBone;
	DeformationComponent->bApplyDirectBoneTransforms = true;
	DeformationComponent->bAutoCreatePoseableMesh = false;
	DeformationComponent->bHideTargetMeshWhenUsingPoseable = true;
	DeformationComponent->bOnlyConfiguredBones = false;
	DeformationComponent->bApplyPhysicsImpulse = !bUseKinematicPhysicsBodies;

	DeformationComponent->BindToMesh(TargetMesh);
	DeformationComponent->SetPoseableMesh(PoseableMesh);
}

void ADeformationVehiclePawn::ConfigureTargetMeshTransform()
{
	if (!TargetMesh)
	{
		return;
	}

	TargetMesh->SetMobility(EComponentMobility::Movable);
	TargetMesh->AttachToComponent(PawnRoot ? PawnRoot.Get() : RootComponent.Get(), FAttachmentTransformRules::SnapToTargetIncludingScale);
	TargetMesh->SetRelativeTransform(FTransform::Identity);
}

void ADeformationVehiclePawn::ConfigureTargetMeshCollisionAndPhysics()
{
	if (!TargetMesh)
	{
		return;
	}

	TargetMesh->SetCollisionProfileName(CollisionProfileName);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetEnableGravity(bEnableGravity);
	TargetMesh->SetNotifyRigidBodyCollision(true);
	TargetMesh->SetAllBodiesNotifyRigidBodyCollision(true);
	TargetMesh->SetGenerateOverlapEvents(false);

	if (bForceBlockingPhysicsCollision)
	{
		TargetMesh->SetCollisionObjectType(ECC_PhysicsBody);
		TargetMesh->SetCollisionResponseToAllChannels(ECR_Block);
	}

	TargetMesh->SetEnableGravity(bEnableGravity);

	if (bSimulatePhysics && bUseKinematicPhysicsBodies)
	{
		const FName SimulationRootBone = GetEffectiveSimulationRootBone();
		TargetMesh->SetSimulatePhysics(true);

		// Start from a fully kinematic skeletal asset, then enable simulation only on the root/chassis body.
		// This prevents child PHAT bodies from behaving like independent simulated bodies and falling through the floor.
		TargetMesh->SetAllBodiesSimulatePhysics(false);
		if (!SimulationRootBone.IsNone())
		{
			TargetMesh->SetAllBodiesBelowSimulatePhysics(SimulationRootBone, true, true);
			TargetMesh->SetAllBodiesBelowSimulatePhysics(SimulationRootBone, false, false);
		}
	}
	else
	{
		TargetMesh->SetSimulatePhysics(bSimulatePhysics);
		TargetMesh->SetAllBodiesSimulatePhysics(bSimulatePhysics);
	}

	AlignKinematicBodiesToCurrentBones();

	if (bWakeRigidBodies)
	{
		TargetMesh->WakeAllRigidBodies();
	}
}

void ADeformationVehiclePawn::ConfigurePoseableMeshTransform()
{
	if (!PoseableMesh)
	{
		return;
	}

	PoseableMesh->SetMobility(EComponentMobility::Movable);
	USceneComponent* MeshAttachParent = (bSimulatePhysics && TargetMesh)
		? static_cast<USceneComponent*>(TargetMesh.Get())
		: static_cast<USceneComponent*>(PawnRoot ? PawnRoot.Get() : RootComponent.Get());
	if (MeshAttachParent)
	{
		PoseableMesh->AttachToComponent(MeshAttachParent, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}
	PoseableMesh->SetRelativeTransform(FTransform::Identity);
	PoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseableMesh->SetGenerateOverlapEvents(false);
}

void ADeformationVehiclePawn::AlignKinematicBodiesToCurrentBones()
{
	if (!TargetMesh || !bUseKinematicPhysicsBodies)
	{
		return;
	}

	UPhysicsAsset* PhysicsAsset = TargetMesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	const FName SimulationRootBone = GetEffectiveSimulationRootBone();
	FBodyInstance* RootBodyInstance = SimulationRootBone.IsNone() ? nullptr : TargetMesh->GetBodyInstance(SimulationRootBone);
	if (!RootBodyInstance)
	{
		return;
	}

	const FTransform RootBodyTransform = RootBodyInstance->GetUnrealWorldTransform();
	const int32 RootBoneIndex = TargetMesh->GetBoneIndex(SimulationRootBone);
	const FTransform RootBoneTransform = RootBoneIndex == INDEX_NONE
		? TargetMesh->GetComponentTransform()
		: TargetMesh->GetBoneTransform(RootBoneIndex);

	for (USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!BodySetup || BodySetup->BoneName.IsNone() || BodySetup->BoneName == SimulationRootBone)
		{
			continue;
		}

		const int32 BoneIndex = TargetMesh->GetBoneIndex(BodySetup->BoneName);
		FBodyInstance* BodyInstance = TargetMesh->GetBodyInstance(BodySetup->BoneName);
		if (BoneIndex == INDEX_NONE || !BodyInstance)
		{
			continue;
		}

		FTransform* InitialRelativeTransform = InitialBodyTransformsRelativeToRoot.Find(BodySetup->BoneName);
		if (!InitialRelativeTransform)
		{
			const FTransform BoneTransform = TargetMesh->GetBoneTransform(BoneIndex);
			InitialRelativeTransform = &InitialBodyTransformsRelativeToRoot.Add(
				BodySetup->BoneName,
				BoneTransform.GetRelativeTransform(RootBoneTransform));
		}

		FTransform DesiredBodyTransform = (*InitialRelativeTransform) * RootBodyTransform;
		if (DeformationComponent)
		{
			const FVector DeformationOffsetWS = TargetMesh->GetComponentTransform().TransformVectorNoScale(
				DeformationComponent->GetBoneDeformationOffset(BodySetup->BoneName));
			DesiredBodyTransform.AddToTranslation(DeformationOffsetWS);
		}

		BodyInstance->SetInstanceSimulatePhysics(false);
		BodyInstance->SetBodyTransform(DesiredBodyTransform, ETeleportType::TeleportPhysics);
	}
}

FName ADeformationVehiclePawn::GetEffectiveSimulationRootBone() const
{
	if (!RootBone.IsNone())
	{
		return RootBone;
	}

	if (TargetMesh && TargetMesh->GetNumBones() > 0)
	{
		return TargetMesh->GetBoneName(0);
	}

	return NAME_None;
}
