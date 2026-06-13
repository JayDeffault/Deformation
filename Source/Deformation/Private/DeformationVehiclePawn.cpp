// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformationVehiclePawn.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DeformationComponent.h"

ADeformationVehiclePawn::ADeformationVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = false;

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
	ConfigureDeformation();
}

void ADeformationVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
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
