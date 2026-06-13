// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformationVehiclePawn.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DeformationComponent.h"

ADeformationVehiclePawn::ADeformationVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = false;

	TargetMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TargetMesh"));
	SetRootComponent(TargetMesh);
	TargetMesh->SetMobility(EComponentMobility::Movable);
	TargetMesh->SetRelativeTransform(FTransform::Identity);
	TargetMesh->SetCollisionProfileName(CollisionProfileName);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetNotifyRigidBodyCollision(true);
	TargetMesh->SetGenerateOverlapEvents(false);

	PoseableMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("PoseableMesh"));
	PoseableMesh->SetupAttachment(TargetMesh);
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
}

void ADeformationVehiclePawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ConfigureTargetMeshTransform();
	ConfigureTargetMeshCollisionAndPhysics(false);
	ConfigurePoseableMeshTransform();
	ConfigureDeformation();
}

void ADeformationVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	ConfigureTargetMeshTransform();
	ConfigureTargetMeshCollisionAndPhysics(bSimulatePhysics);
	ConfigurePoseableMeshTransform();
	ConfigureDeformation();
}

void ADeformationVehiclePawn::ConfigureDeformation()
{
	if (!DeformationComponent)
	{
		return;
	}

	ConfigureTargetMeshCollisionAndPhysics(GetWorld() && GetWorld()->IsGameWorld() && bSimulatePhysics);
	ConfigurePoseableMeshTransform();

	DeformationComponent->TargetMesh = TargetMesh;
	DeformationComponent->PoseableMesh = PoseableMesh;
	DeformationComponent->RootBone = RootBone;
	DeformationComponent->bApplyDirectBoneTransforms = true;
	DeformationComponent->bAutoCreatePoseableMesh = false;
	DeformationComponent->bHideTargetMeshWhenUsingPoseable = true;
	DeformationComponent->bOnlyConfiguredBones = false;

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
	TargetMesh->SetRelativeTransform(FTransform::Identity);
}

void ADeformationVehiclePawn::ConfigureTargetMeshCollisionAndPhysics(bool bEnablePhysics)
{
	if (!TargetMesh)
	{
		return;
	}

	TargetMesh->SetCollisionProfileName(CollisionProfileName);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetNotifyRigidBodyCollision(true);
	TargetMesh->SetAllBodiesNotifyRigidBodyCollision(true);
	TargetMesh->SetGenerateOverlapEvents(false);

	if (bForceBlockingPhysicsCollision)
	{
		TargetMesh->SetCollisionObjectType(ECC_PhysicsBody);
		TargetMesh->SetCollisionResponseToAllChannels(ECR_Block);
	}

	TargetMesh->SetSimulatePhysics(bEnablePhysics);
	TargetMesh->SetAllBodiesSimulatePhysics(bEnablePhysics);

	if (bEnablePhysics && bWakeRigidBodies)
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
	USceneComponent* MeshAttachParent = TargetMesh ? static_cast<USceneComponent*>(TargetMesh.Get()) : RootComponent.Get();
	if (MeshAttachParent)
	{
		PoseableMesh->AttachToComponent(MeshAttachParent, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}
	PoseableMesh->SetRelativeTransform(FTransform::Identity);
	PoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseableMesh->SetGenerateOverlapEvents(false);
}
