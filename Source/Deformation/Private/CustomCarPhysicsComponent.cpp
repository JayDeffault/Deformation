#include "CustomCarPhysicsComponent.h"

#include "CollisionSolver.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshResources.h"

UCustomCarPhysicsComponent::UCustomCarPhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bRunOnAnyThread = false; // Явно отключаем async tick.
}

void UCustomCarPhysicsComponent::BeginPlay()
{
    Super::BeginPlay();
    InitializeFromTaggedMeshes();
    if (GetOwner())
    {
        PreviousActorLocation = GetOwner()->GetActorLocation();
    }
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

    if (bDisableCollisionOnCollisionMeshComponent && CollisionSourceMesh)
    {
        CollisionSourceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    BuildRuntimeMeshFromStatic();
    return RuntimeMesh != nullptr;
}

void UCustomCarPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!RuntimeMesh) return;

    SimulateFrame(DeltaTime);

    ProcessDeformationQueue();
    UpdateRuntimeMesh(false);
    if (GetOwner())
    {
        PreviousActorLocation = GetOwner()->GetActorLocation();
    }
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

    PhysicsMesh.Initialize(CollisionVertices, CollisionTriangles);
    PhysicsMesh.BuildSpatialHash(20.0f);

    // Визуал полностью синхронизируется с физической деформируемой сеткой.
    PhysicsMesh.RecalculateNormalsAll(CachedNormals);

    RuntimeMesh = NewObject<UProceduralMeshComponent>(GetOwner(), TEXT("DeformationRuntimeMesh"));
    RuntimeMesh->SetupAttachment(VisualMesh);
    RuntimeMesh->RegisterComponent();

    VisualMesh->SetVisibility(false);
    UpdateRuntimeMesh(true);
}

void UCustomCarPhysicsComponent::UpdateRuntimeMesh(bool bFullRebuildNormals)
{
    if (!RuntimeMesh) return;

    if (bFullRebuildNormals) PhysicsMesh.RecalculateNormalsAll(CachedNormals);
    else if (DirtyVertices.Num() > 0) PhysicsMesh.RecalculateNormalsPartial(DirtyVertices, CachedNormals);

    TArray<FVector2D> UV0; UV0.Init(FVector2D::ZeroVector, PhysicsMesh.Vertices.Num());
    TArray<FColor> Colors; Colors.Init(FColor::White, PhysicsMesh.Vertices.Num());
    TArray<FProcMeshTangent> Tangents; Tangents.Init(FProcMeshTangent(1,0,0), PhysicsMesh.Vertices.Num());

    if (!RuntimeMesh->GetProcMeshSection(0)) RuntimeMesh->CreateMeshSection(0, PhysicsMesh.Vertices, PhysicsMesh.Triangles, CachedNormals, UV0, Colors, Tangents, true);
    else if (DirtyVertices.Num() > 0) RuntimeMesh->UpdateMeshSection(0, PhysicsMesh.Vertices, CachedNormals, UV0, Colors, Tangents);

    DirtyVertices.Reset();
}

void UCustomCarPhysicsComponent::SimulateFrame(float Dt)
{
    const FBox LocalBounds = PhysicsMesh.GetLocalBounds();
    if (LocalBounds.IsValid)
    {
        ProxyHalfExtents = LocalBounds.GetExtent().GetAbs();
        ProxyLocalCenter = LocalBounds.GetCenter();
    }

    bGrounded = false;
    HandleWorldCollision();
    HandleCarCollisions();
    IntegrateMovement(Dt);
}

void UCustomCarPhysicsComponent::IntegrateMovement(float Dt)
{
    if (!GetOwner()) return;
    if (!bGrounded)
    {
        PhysicsState.Velocity += FVector(0, 0, -980.0f) * Dt;
    }

    const float DampingFactor = FMath::Clamp(1.0f - LinearDamping * Dt, 0.0f, 1.0f);
    PhysicsState.Velocity *= DampingFactor;
    PhysicsState.Velocity = PhysicsState.Velocity.GetClampedToMaxSize(MaxLinearSpeed);
    const float AngularDamp = FMath::Clamp(1.0f - 4.0f * Dt, 0.0f, 1.0f);
    PhysicsState.AngularVelocity *= AngularDamp;
    PhysicsState.AngularVelocity = PhysicsState.AngularVelocity.GetClampedToMaxSize(1.5f);

    const FVector Position = GetOwner()->GetActorLocation() + PhysicsState.Velocity * Dt;
    const FRotator Rotation = GetOwner()->GetActorRotation() + FRotator::MakeFromEuler(PhysicsState.AngularVelocity * Dt);
    GetOwner()->SetActorLocationAndRotation(Position, Rotation);
}

void UCustomCarPhysicsComponent::HandleWorldCollision()
{
    if (!GetOwner()) return;

    // Используем реальные вершины физического деформируемого меша для контактов с миром,
    // чтобы не было "столкновений с воздухом" от грубого box-proxy.
    TArray<FVector> WorldVerts;
    PhysicsMesh.GetWorldVertices(GetOwner()->GetActorTransform(), WorldVerts);
    if (WorldVerts.Num() == 0) return;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CustomCarWorldVertexTrace), false, GetOwner());

    const FVector Delta = GetOwner()->GetActorLocation() - PreviousActorLocation;
    const FVector MoveDir = Delta.IsNearlyZero() ? FVector::DownVector : Delta.GetSafeNormal();

    // Берём несколько support-точек по ключевым направлениям для устойчивой детекции.
    const FVector LocalSupportMove = PhysicsMesh.GetSupportPoint(GetOwner()->GetActorTransform().InverseTransformVectorNoScale(MoveDir));
    const FVector LocalSupportDown = PhysicsMesh.GetSupportPoint(GetOwner()->GetActorTransform().InverseTransformVectorNoScale(FVector::DownVector));
    const FVector LocalSupportUp = PhysicsMesh.GetSupportPoint(GetOwner()->GetActorTransform().InverseTransformVectorNoScale(FVector::UpVector));

    TArray<FVector> ProbePoints;
    ProbePoints.Reserve(3);
    ProbePoints.Add(GetOwner()->GetActorTransform().TransformPosition(LocalSupportMove));
    ProbePoints.Add(GetOwner()->GetActorTransform().TransformPosition(LocalSupportDown));
    ProbePoints.Add(GetOwner()->GetActorTransform().TransformPosition(LocalSupportUp));

    if (bDebugDraw)
    {
        for (const FVector& P : ProbePoints)
        {
            DrawDebugPoint(GetWorld(), P, 8.0f, FColor::Cyan, false, 0.05f);
        }
    }

    for (const FVector& ProbeWS : ProbePoints)
    {
        FHitResult Hit;
        const FVector Start = ProbeWS - Delta;
        const FVector End = ProbeWS;

        if (GetWorld()->LineTraceSingleByObjectType(
            Hit,
            Start,
            End,
            FCollisionObjectQueryParams(
                ECC_TO_BITFIELD(ECC_WorldStatic) |
                ECC_TO_BITFIELD(ECC_WorldDynamic) |
                ECC_TO_BITFIELD(ECC_Pawn) |
                ECC_TO_BITFIELD(ECC_PhysicsBody) |
                ECC_TO_BITFIELD(ECC_Vehicle) |
                ECC_TO_BITFIELD(ECC_Destructible)),
            QueryParams))
        {
            const float ImpactForce = FMath::Max(Hit.PenetrationDepth * 700.0f, PhysicsState.Velocity.Size());
            if (PhysicsState.Velocity.Size() > MinImpactSpeedForDeformation || Hit.PenetrationDepth > 0.5f)
            {
                const FTransform WorldToLocal = GetOwner()->GetActorTransform().Inverse();
                FDeformationEvent Event;
                Event.LocalPoint = WorldToLocal.TransformPosition(Hit.ImpactPoint);
                Event.LocalNormal = WorldToLocal.TransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();
                Event.Force = ImpactForce;
                Event.Radius = DeformRadius;
                if (Event.Force >= MinImpactForceForDeformation)
                {
                    DeformationQueue.Add(Event);
                }
            }

            const float VN = FVector::DotProduct(PhysicsState.Velocity, Hit.ImpactNormal);
            if (VN < -1.0f)
            {
                PhysicsState.Velocity -= Hit.ImpactNormal * VN;
                PhysicsState.Velocity *= 0.98f;
            }

            bGrounded = bGrounded || (Hit.ImpactNormal.Z > 0.65f);

            if (bDebugDraw)
            {
                DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 12.0f, FColor::Red, false, 0.05f);
                DrawDebugLine(GetWorld(), Hit.ImpactPoint, Hit.ImpactPoint + Hit.ImpactNormal * 60.0f, FColor::Yellow, false, 0.05f, 0, 1.5f);
                DrawDebugBox(GetWorld(), GetOwner()->GetActorTransform().TransformPosition(ProxyLocalCenter), ProxyHalfExtents, GetOwner()->GetActorQuat(), FColor::Green, false, 0.05f);
            }
            break;
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
        B.WorldAABB = C->PhysicsMesh.GetLocalBounds().TransformBy(C->GetOwner()->GetActorTransform());
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
            FProxyConvex{AComp->GetOwner()->GetActorTransform(), AComp->PhysicsMesh.Vertices},
            FProxyConvex{BComp->GetOwner()->GetActorTransform(), BComp->PhysicsMesh.Vertices});

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
        PhysicsMesh.ApplyDeformationEvent(DeformationQueue[EvtIdx], MaxDeform, Dirty);

        for (int32 Idx : Dirty)
        {
            if (DirtyVertices.Num() >= MaxDirtyVerticesPerFrame) break;
            DirtyVertices.Add(Idx);
        }
    }

    PhysicsMesh.BuildSpatialHash(20.0f);

    const FBox UpdatedBounds = PhysicsMesh.GetLocalBounds();
    if (UpdatedBounds.IsValid)
    {
        ProxyHalfExtents = UpdatedBounds.GetExtent().GetAbs();
        ProxyLocalCenter = UpdatedBounds.GetCenter();
    }

    DeformationQueue.Reset();
}
