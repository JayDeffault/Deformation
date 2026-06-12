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
	TargetMesh->SetRelativeTransform(FTransform::Identity);
	TargetMesh->SetCollisionProfileName(CollisionProfileName);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetNotifyRigidBodyCollision(true);
	TargetMesh->SetGenerateOverlapEvents(false);

	PoseableMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("PoseableMesh"));
	PoseableMesh->SetupAttachment(TargetMesh);
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
	ConfigureDeformation();
}

void ADeformationVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	ConfigureDeformation();
}

void ADeformationVehiclePawn::ConfigureDeformation()
{
	if (!DeformationComponent)
	{
		return;
	}

	if (TargetMesh)
	{
		TargetMesh->SetRelativeTransform(FTransform::Identity);
		TargetMesh->SetWorldTransform(GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
		TargetMesh->SetCollisionProfileName(CollisionProfileName);
		TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TargetMesh->SetNotifyRigidBodyCollision(true);
		TargetMesh->SetGenerateOverlapEvents(false);
		TargetMesh->SetSimulatePhysics(bSimulatePhysics);
		if (bSimulatePhysics && bWakeRigidBodies)
		{
			TargetMesh->WakeAllRigidBodies();
		}
	}

	if (PoseableMesh)
	{
		PoseableMesh->SetRelativeTransform(FTransform::Identity);
		PoseableMesh->SetWorldTransform(TargetMesh ? TargetMesh->GetComponentTransform() : GetActorTransform());
		PoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PoseableMesh->SetGenerateOverlapEvents(false);
	}

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
