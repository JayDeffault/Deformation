#include "CustomCarPhysicsComponent.h"

#include "CollisionSolver.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshResources.h"

UCustomCarPhysicsComponent::UCustomCarPhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UCustomCarPhysicsComponent::BeginPlay()
{
    Super::BeginPlay();
    InitializeFromTaggedMeshes();
}

bool UCustomCarPhysicsComponent::InitializeFromTaggedMeshes()
{
    if (!GetOwner()) return false;

    TArray<UStaticMeshComponent*> MeshComps;
    GetOwner()->GetComponents(MeshComps);

    for (UStaticMeshComponent* C : MeshComps)
    {
        if (C->ComponentHasTag(VisualMeshTag)) VisualMesh = C;
        if (C->ComponentHasTag(CollisionMeshTag)) CollisionSourceMesh = C;
    }

    if (!VisualMesh)
    {
        VisualMesh = GetOwner()->FindComponentByClass<UStaticMeshComponent>();
    }
    if (!CollisionSourceMesh)
    {
        CollisionSourceMesh = VisualMesh;
    }

    BuildRuntimeMeshFromStatic();
    return RuntimeMesh != nullptr;
}

void UCustomCarPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!RuntimeMesh) return;

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

    if (Force < MinImpactForceForDeformation)
    {
        return;
    }

    const FTransform WorldToLocal = GetOwner()->GetActorTransform().Inverse();
    FDeformationEvent Event;
    Event.LocalPoint = WorldToLocal.TransformPosition(Point);
    Event.LocalNormal = WorldToLocal.TransformVectorNoScale(Normal).GetSafeNormal();
    Event.Force = Force;
    Event.Radius = DeformRadius;
    DeformationQueue.Add(Event);

    FCollisionSolver::ResolveImpulse(PhysicsState.Velocity, PhysicsState.AngularVelocity, Mass, FVector(350000.0f, 350000.0f, 350000.0f), Point - GetOwner()->GetActorLocation(), Normal, Force * 0.001f, Restitution);
}

bool UCustomCarPhysicsComponent::ExtractMeshData(UStaticMeshComponent* Source, TArray<FVector>& OutVertices, TArray<int32>& OutTriangles) const
{
    if (!Source || !Source->GetStaticMesh()) return false;

    UStaticMesh* SM = Source->GetStaticMesh();
    if (!(SM->GetRenderData() && SM->GetRenderData()->LODResources.Num() > 0)) return false;

    const FStaticMeshLODResources& LOD = SM->GetRenderData()->LODResources[0];
    for (uint32 i = 0; i < LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices(); ++i)
    {
        OutVertices.Add((FVector)LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(i));
    }

    const FRawStaticIndexBuffer& IndexBuffer = LOD.IndexBuffer;
    for (int32 i = 0; i < IndexBuffer.GetNumIndices(); ++i)
    {
        OutTriangles.Add((int32)IndexBuffer.GetIndex(i));
    }

    return OutVertices.Num() > 0 && OutTriangles.Num() > 0;
}

void UCustomCarPhysicsComponent::BuildRuntimeMeshFromStatic()
{
    if (!VisualMesh || !CollisionSourceMesh) return;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;

    // Визуальный runtime меш строим из визуального source, а collision solver использует отдельный proxy.
    if (!ExtractMeshData(VisualMesh, Vertices, Triangles))
    {
        const FVector E = CollisionSourceMesh->GetStaticMesh()->GetBounds().BoxExtent;
        Vertices = {FVector(-E.X,-E.Y,-E.Z), FVector(E.X,-E.Y,-E.Z), FVector(E.X,E.Y,-E.Z), FVector(-E.X,E.Y,-E.Z), FVector(-E.X,-E.Y,E.Z), FVector(E.X,-E.Y,E.Z), FVector(E.X,E.Y,E.Z), FVector(-E.X,E.Y,E.Z)};
        Triangles = {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7};
    }

    TArray<FVector> CollisionVertices;
    TArray<int32> CollisionTriangles;
    if (!ExtractMeshData(CollisionSourceMesh, CollisionVertices, CollisionTriangles))
    {
        CollisionVertices = Vertices;
        CollisionTriangles = Triangles;
    }

    ConvexMesh.Initialize(CollisionVertices, CollisionTriangles);
    ConvexMesh.BuildSpatialHash(20.0f);

    // Нормали для визуального меша считаем по визуальным данным, чтобы отображение было корректным.
    ConvexMesh.Vertices = Vertices;
    ConvexMesh.Triangles = Triangles;
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

    if (bFullRebuildNormals) ConvexMesh.RecalculateNormalsAll(CachedNormals);
    else if (DirtyVertices.Num() > 0) ConvexMesh.RecalculateNormalsPartial(DirtyVertices, CachedNormals);

    TArray<FVector2D> UV0; UV0.Init(FVector2D::ZeroVector, ConvexMesh.Vertices.Num());
    TArray<FColor> Colors; Colors.Init(FColor::White, ConvexMesh.Vertices.Num());
    TArray<FProcMeshTangent> Tangents; Tangents.Init(FProcMeshTangent(1,0,0), ConvexMesh.Vertices.Num());

    if (!RuntimeMesh->GetProcMeshSection(0)) RuntimeMesh->CreateMeshSection(0, ConvexMesh.Vertices, ConvexMesh.Triangles, CachedNormals, UV0, Colors, Tangents, true);
    else if (DirtyVertices.Num() > 0) RuntimeMesh->UpdateMeshSection(0, ConvexMesh.Vertices, CachedNormals, UV0, Colors, Tangents);

    DirtyVertices.Reset();
}

void UCustomCarPhysicsComponent::SimulateFixedStep(float Dt)
{
    const FBox LocalBounds = ConvexMesh.GetLocalBounds();
    if (LocalBounds.IsValid)
    {
        ProxyHalfExtents = LocalBounds.GetExtent().GetAbs();
    }

    HandleWorldCollision();
    HandleCarCollisions();
    IntegrateMovement(Dt);
}

void UCustomCarPhysicsComponent::IntegrateMovement(float Dt)
{
    if (!GetOwner()) return;
    PhysicsState.Velocity += FVector(0, 0, -980.0f) * Dt;
    const FVector Position = GetOwner()->GetActorLocation() + PhysicsState.Velocity * Dt;
    const FRotator Rotation = GetOwner()->GetActorRotation() + FRotator::MakeFromEuler(PhysicsState.AngularVelocity * Dt);
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
        const float ImpactForce = FMath::Max(Hit.PenetrationDepth * 700.0f, PhysicsState.Velocity.Size());
        if (PhysicsState.Velocity.Size() > MinImpactSpeedForDeformation || Hit.PenetrationDepth > 3.0f)
        {
            ApplyImpact(Hit.ImpactPoint, Hit.ImpactNormal, ImpactForce);
        }

        // Базовая стабилизация против "проваливания" под землю.
        const float PushOut = FMath::Max(Hit.PenetrationDepth, 2.0f);
        GetOwner()->AddActorWorldOffset(Hit.ImpactNormal * PushOut, false);

        const float VN = FVector::DotProduct(PhysicsState.Velocity, Hit.ImpactNormal);
        if (VN < 0.0f)
        {
            PhysicsState.Velocity -= Hit.ImpactNormal * VN;
            PhysicsState.Velocity *= 0.95f;
        }
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
            if (FVector::DistSquared(C->GetOwner()->GetActorLocation(), GetOwner()->GetActorLocation()) <= FMath::Square(CollisionNeighborRadius)) Cars.Add(C);
        }
    }

    TArray<FBroadPhaseBody> Bodies;
    for (UCustomCarPhysicsComponent* C : Cars)
    {
        FBroadPhaseBody B; B.Owner = C->GetOwner();
        B.WorldAABB = C->ConvexMesh.GetLocalBounds().TransformBy(C->GetOwner()->GetActorTransform());
        Bodies.Add(B);
    }

    BroadPhase.Build(Bodies);
    TArray<TPair<int32,int32>> Pairs; BroadPhase.QueryPotentialPairs(Pairs);

    for (const TPair<int32,int32>& Pair : Pairs)
    {
        UCustomCarPhysicsComponent* AComp = Cars[Pair.Key];
        UCustomCarPhysicsComponent* BComp = Cars[Pair.Value];
        if (AComp != this) continue;

        const FCollisionContact Contact = FCollisionSolver::SolveConvexSAT(
            FProxyConvex{AComp->GetOwner()->GetActorTransform(), AComp->ConvexMesh.Vertices},
            FProxyConvex{BComp->GetOwner()->GetActorTransform(), BComp->ConvexMesh.Vertices});

        if (Contact.bHasContact) ApplyImpact(Contact.Point, Contact.Normal, Contact.PenetrationDepth * 1200.0f);
    }
}

void UCustomCarPhysicsComponent::ProcessDeformationQueue()
{
    if (DeformationQueue.Num() == 0) return;

    const int32 EventCount = FMath::Min(MaxDeformationEventsPerFrame, DeformationQueue.Num());
    for (int32 EvtIdx = 0; EvtIdx < EventCount; ++EvtIdx)
    {
        TArray<int32> Dirty;
        ConvexMesh.ApplyDeformationEvent(DeformationQueue[EvtIdx], MaxDeform, Dirty);
        for (int32 Idx : Dirty)
        {
            if (DirtyVertices.Num() >= MaxDirtyVerticesPerFrame) break;
            DirtyVertices.Add(Idx);
        }
    }

    ConvexMesh.BuildSpatialHash(20.0f);
    DeformationQueue.Reset();
}
