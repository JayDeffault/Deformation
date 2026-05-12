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
    PrimaryComponentTick.bRunOnAnyThread = false; // Явно отключаем async tick.
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

    SimulateFixedStep(DeltaTime);

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

void UCustomCarPhysicsComponent::SimulateFixedStep(float Dt)
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
    PhysicsState.AngularVelocity *= 0.97f;
    PhysicsState.AngularVelocity = PhysicsState.AngularVelocity.GetClampedToMaxSize(2.5f);

    const FVector Position = GetOwner()->GetActorLocation() + PhysicsState.Velocity * Dt;
    const FRotator Rotation = GetOwner()->GetActorRotation() + FRotator::MakeFromEuler(PhysicsState.AngularVelocity * Dt);
    GetOwner()->SetActorLocationAndRotation(Position, Rotation);
}

void UCustomCarPhysicsComponent::HandleWorldCollision()
{
    if (!GetOwner()) return;

    FHitResult Hit;
    const FVector CenterWS = GetOwner()->GetActorTransform().TransformPosition(ProxyLocalCenter);
    const FVector Start = CenterWS;
    const FVector End = Start + PhysicsState.Velocity * 0.05f;
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CustomCarWorldSweep), false, GetOwner());
    const FCollisionObjectQueryParams ObjMask(
        ECC_TO_BITFIELD(ECC_WorldStatic) |
        ECC_TO_BITFIELD(ECC_WorldDynamic) |
        ECC_TO_BITFIELD(ECC_Pawn) |
        ECC_TO_BITFIELD(ECC_PhysicsBody) |
        ECC_TO_BITFIELD(ECC_Vehicle) |
        ECC_TO_BITFIELD(ECC_Destructible));

    const FQuat ProxyRotation = GetOwner()->GetActorQuat();
    if (GetWorld()->SweepSingleByObjectType(Hit, Start, End, ProxyRotation, ObjMask, FCollisionShape::MakeBox(ProxyHalfExtents), QueryParams))
    {
        const float ImpactForce = FMath::Max(Hit.PenetrationDepth * 700.0f, PhysicsState.Velocity.Size());
        if (PhysicsState.Velocity.Size() > MinImpactSpeedForDeformation || Hit.PenetrationDepth > 3.0f)
        {
            // Для контакта с землёй избегаем крутящего импульса, чтобы не было неконтролируемого спина.
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

        // Стабилизация контакта: мягкая коррекция позиции + подавление отскока на малых скоростях.

        // Если опора смещена от центра масс (край платформы), добавляем опрокидывающий момент.
        const FVector Lever = Hit.ImpactPoint - GetOwner()->GetActorLocation();
        const FVector GravityForce = FVector(0, 0, -980.0f * Mass);
        const FVector GravityTorque = FVector::CrossProduct(Lever, GravityForce) * GravityTorqueScale;
        PhysicsState.AngularVelocity += GravityTorque;

        const float VN = FVector::DotProduct(PhysicsState.Velocity, Hit.ImpactNormal);

        // Корректируем позицию только при реальном проникновении в поверхность.
        const float PenDepth = FMath::Max(0.0f, Hit.PenetrationDepth - GroundSnapTolerance);
        if (PenDepth > 0.01f)
        {
            GetOwner()->AddActorWorldOffset(Hit.ImpactNormal * PenDepth * PositionalCorrectionFactor, false);
        }


        const FVector ActorUp = GetOwner()->GetActorUpVector();
        const float UpDot = FVector::DotProduct(ActorUp, FVector::UpVector);
        const bool bContactBelowCOM = Hit.ImpactPoint.Z < (GetOwner()->GetActorLocation().Z - 5.0f);

        if (Hit.ImpactNormal.Z > 0.65f && UpDot > MinUpDotForGrounded && bContactBelowCOM)
        {
            bGrounded = true;
            FVector Horizontal = FVector(PhysicsState.Velocity.X, PhysicsState.Velocity.Y, 0.0f);
            const float StepDt = FMath::Max(GetWorld()->GetDeltaSeconds(), 1.0f / 240.0f);
            Horizontal *= FMath::Clamp(1.0f - GroundFriction * StepDt, 0.0f, 1.0f);
            PhysicsState.Velocity.X = Horizontal.X;
            PhysicsState.Velocity.Y = Horizontal.Y;

            if (PhysicsState.Velocity.Size() < SleepSpeedThreshold)
            {
                PhysicsState.Velocity = FVector::ZeroVector;
                PhysicsState.AngularVelocity *= 0.8f;
            }
        }

        if (VN < -1.0f)
        {
            // Убираем скорость в поверхность, оставляем касательную составляющую.
            PhysicsState.Velocity -= Hit.ImpactNormal * VN;

            // На земле (почти вертикальная нормаль) гасим остаточный Z, чтобы не было "дребезга".
            if (Hit.ImpactNormal.Z > 0.6f && FMath::Abs(PhysicsState.Velocity.Z) < 120.0f)
            {
                PhysicsState.Velocity.Z = 0.0f;
            }

            PhysicsState.Velocity *= 0.98f;

            if (Hit.ImpactNormal.Z > 0.6f)
            {
                PhysicsState.AngularVelocity *= 0.5f;
            }
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
