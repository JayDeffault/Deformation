#include "CustomCarPhysicsComponent.h"

#include "CollisionSolver.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshResources.h"
#include "EngineUtils.h"

UCustomCarPhysicsComponent::UCustomCarPhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UCustomCarPhysicsComponent::BeginPlay()
{
    Super::BeginPlay();
    BuildRuntimeMeshFromStatic();
}

void UCustomCarPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    TimeAccumulator += DeltaTime;
    while (TimeAccumulator >= FixedTimeStep)
    {
        SimulateFixedStep(FixedTimeStep);
        TimeAccumulator -= FixedTimeStep;
    }

    ProcessDeformationQueue();
    UpdateRuntimeMesh(false);
}

void UCustomCarPhysicsComponent::ApplyImpact(FVector Point, FVector Normal, float Force)
{
    if (!GetOwner()) return;

    const FTransform WorldToLocal = GetOwner()->GetActorTransform().Inverse();
    FDeformationEvent Event;
    Event.LocalPoint = WorldToLocal.TransformPosition(Point);
    Event.LocalNormal = WorldToLocal.TransformVectorNoScale(Normal).GetSafeNormal();
    Event.Force = Force;
    Event.Radius = DeformRadius;
    DeformationQueue.Add(Event);

    FCollisionSolver::ResolveImpulse(
        PhysicsState.Velocity,
        PhysicsState.AngularVelocity,
        Mass,
        FVector(350000.0f, 350000.0f, 350000.0f),
        Point - GetOwner()->GetActorLocation(),
        Normal,
        Force * 0.001f,
        Restitution);
}

void UCustomCarPhysicsComponent::BuildRuntimeMeshFromStatic()
{
    VisualMesh = GetOwner() ? GetOwner()->FindComponentByClass<UStaticMeshComponent>() : nullptr;
    if (!VisualMesh || !VisualMesh->GetStaticMesh()) return;

    UStaticMesh* SM = VisualMesh->GetStaticMesh();
    TArray<FVector> Vertices;
    TArray<int32> Triangles;

    if (SM->GetRenderData() && SM->GetRenderData()->LODResources.Num() > 0)
    {
        const FStaticMeshLODResources& LOD = SM->GetRenderData()->LODResources[0];
        for (uint32 i = 0; i < LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices(); ++i)
        {
            Vertices.Add((FVector)LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(i));
        }

        const FRawStaticIndexBuffer& IndexBuffer = LOD.IndexBuffer;
        for (int32 i = 0; i < IndexBuffer.GetNumIndices(); ++i)
        {
            Triangles.Add((int32)IndexBuffer.GetIndex(i));
        }
    }

    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        const FVector E = SM->GetBounds().BoxExtent;
        Vertices = {
            FVector(-E.X,-E.Y,-E.Z), FVector(E.X,-E.Y,-E.Z), FVector(E.X,E.Y,-E.Z), FVector(-E.X,E.Y,-E.Z),
            FVector(-E.X,-E.Y,E.Z), FVector(E.X,-E.Y,E.Z), FVector(E.X,E.Y,E.Z), FVector(-E.X,E.Y,E.Z)
        };
        Triangles = {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7};
    }

    ConvexMesh.Initialize(Vertices, Triangles);
    ConvexMesh.BuildSpatialHash(20.0f);
    ConvexMesh.RecalculateNormalsAll(CachedNormals);

    RuntimeMesh = NewObject<UProceduralMeshComponent>(GetOwner(), TEXT("DeformationRuntimeMesh"));
    RuntimeMesh->SetupAttachment(VisualMesh);
    RuntimeMesh->RegisterComponent();

    VisualMesh->SetVisibility(false);
    UpdateRuntimeMesh(true);
}

void UCustomCarPhysicsComponent::UpdateRuntimeMesh(bool bFullRebuildNormals)
{
    if (!RuntimeMesh) return;

    if (bFullRebuildNormals)
    {
        ConvexMesh.RecalculateNormalsAll(CachedNormals);
    }
    else if (DirtyVertices.Num() > 0)
    {
        ConvexMesh.RecalculateNormalsPartial(DirtyVertices, CachedNormals);
    }

    TArray<FVector2D> UV0; UV0.Init(FVector2D::ZeroVector, ConvexMesh.Vertices.Num());
    TArray<FColor> Colors; Colors.Init(FColor::White, ConvexMesh.Vertices.Num());
    TArray<FProcMeshTangent> Tangents; Tangents.Init(FProcMeshTangent(1,0,0), ConvexMesh.Vertices.Num());

    if (!RuntimeMesh->GetProcMeshSection(0))
    {
        RuntimeMesh->CreateMeshSection(0, ConvexMesh.Vertices, ConvexMesh.Triangles, CachedNormals, UV0, Colors, Tangents, true);
    }
    else if (DirtyVertices.Num() > 0)
    {
        RuntimeMesh->UpdateMeshSection(0, ConvexMesh.Vertices, CachedNormals, UV0, Colors, Tangents);
    }

    DirtyVertices.Reset();
}

void UCustomCarPhysicsComponent::SimulateFixedStep(float Dt)
{
    const FBox LocalBounds = ConvexMesh.GetLocalBounds();
    if (LocalBounds.IsValid) ProxyHalfExtents = LocalBounds.GetExtent().GetAbs();

    HandleWorldCollision();
    HandleCarCollisions();
    IntegrateMovement(Dt);
}

void UCustomCarPhysicsComponent::IntegrateMovement(float Dt)
{
    if (!GetOwner()) return;
    PhysicsState.Velocity += FVector(0, 0, -980.0f) * Dt;

    FVector Position = GetOwner()->GetActorLocation() + PhysicsState.Velocity * Dt;
    FRotator Rotation = GetOwner()->GetActorRotation() + FRotator::MakeFromEuler(PhysicsState.AngularVelocity * Dt);
    GetOwner()->SetActorLocationAndRotation(Position, Rotation);
}

void UCustomCarPhysicsComponent::HandleWorldCollision()
{
    if (!GetOwner()) return;

    FHitResult Hit;
    const FVector Start = GetOwner()->GetActorLocation();
    const FVector End = Start + PhysicsState.Velocity * 0.05f;

    if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeBox(ProxyHalfExtents)))
    {
        ApplyImpact(Hit.ImpactPoint, Hit.ImpactNormal, FMath::Max(Hit.PenetrationDepth * 700.0f, PhysicsState.Velocity.Size()));
    }
}

void UCustomCarPhysicsComponent::HandleCarCollisions()
{
    if (!GetOwner()) return;

    TArray<UCustomCarPhysicsComponent*> Cars;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (UCustomCarPhysicsComponent* C = It->FindComponentByClass<UCustomCarPhysicsComponent>())
        {
            if (FVector::DistSquared(C->GetOwner()->GetActorLocation(), GetOwner()->GetActorLocation()) <= FMath::Square(CollisionNeighborRadius))
            {
                Cars.Add(C);
            }
        }
    }

    TArray<FBroadPhaseBody> Bodies;
    for (UCustomCarPhysicsComponent* C : Cars)
    {
        FBroadPhaseBody B;
        B.Owner = C->GetOwner();
        const FBox Local = C->ConvexMesh.GetLocalBounds();
        B.WorldAABB = Local.TransformBy(C->GetOwner()->GetActorTransform());
        Bodies.Add(B);
    }

    BroadPhase.Build(Bodies);
    TArray<TPair<int32,int32>> Pairs;
    BroadPhase.QueryPotentialPairs(Pairs);

    for (const TPair<int32,int32>& Pair : Pairs)
    {
        UCustomCarPhysicsComponent* AComp = Cars[Pair.Key];
        UCustomCarPhysicsComponent* BComp = Cars[Pair.Value];
        if (AComp != this) continue;

        FProxyConvex A{AComp->GetOwner()->GetActorTransform(), AComp->ConvexMesh.Vertices};
        FProxyConvex B{BComp->GetOwner()->GetActorTransform(), BComp->ConvexMesh.Vertices};
        const FCollisionContact Contact = FCollisionSolver::SolveConvexSAT(A, B);
        if (Contact.bHasContact)
        {
            ApplyImpact(Contact.Point, Contact.Normal, Contact.PenetrationDepth * 1200.0f);
        }
    }
}

void UCustomCarPhysicsComponent::ProcessDeformationQueue()
{
    if (DeformationQueue.Num() == 0) return;

    const int32 EventCount = FMath::Min(MaxDeformationEventsPerFrame, DeformationQueue.Num());
    for (int32 EvtIdx = 0; EvtIdx < EventCount; ++EvtIdx)
    {
        const FDeformationEvent& Event = DeformationQueue[EvtIdx];

        TArray<int32> Dirty;
        ConvexMesh.ApplyDeformationEvent(Event, MaxDeform, Dirty);
        for (int32 Idx : Dirty)
        {
            if (DirtyVertices.Num() >= MaxDirtyVerticesPerFrame) break;
            DirtyVertices.Add(Idx);
        }
    }

    ConvexMesh.BuildSpatialHash(20.0f);
    DeformationQueue.Reset();
}
