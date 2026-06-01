#include "CustomConvexMesh.h"

void FCustomConvexMesh::Initialize(const TArray<FVector>& InVertices, const TArray<int32>& InTriangles)
{
    Vertices = InVertices;
    Triangles = InTriangles;
    RebuildCollisionPlanes();
}

void FCustomConvexMesh::BuildSpatialHash(float CellSize)
{
    SpatialHash.Empty();
    HashCellSize = FMath::Max(1.0f, CellSize);

    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        SpatialHash.FindOrAdd(HashKey(Vertices[i])).Add(i);
    }
}


void FCustomConvexMesh::RebuildCollisionPlanes()
{
    CollisionPlanes.Reset();
    CollisionPlanes.Reserve(Triangles.Num() / 3);

    for (int32 i = 0; i + 2 < Triangles.Num(); i += 3)
    {
        const int32 A = Triangles[i];
        const int32 B = Triangles[i + 1];
        const int32 C = Triangles[i + 2];
        if (!Vertices.IsValidIndex(A) || !Vertices.IsValidIndex(B) || !Vertices.IsValidIndex(C))
        {
            continue;
        }

        const FVector Normal = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            continue;
        }

        FDynamicCollisionPlane Plane;
        Plane.A = A;
        Plane.B = B;
        Plane.C = C;
        Plane.Normal = Normal;
        Plane.Distance = FVector::DotProduct(Normal, Vertices[A]);
        CollisionPlanes.Add(Plane);
    }
}

void FCustomConvexMesh::ApplyDeformationEvent(const FDeformationEvent& Event, float MaxDisplacement, TArray<int32>& OutDirtyVertices)
{
    TArray<int32> Candidates;
    GatherVerticesInRadius(Event.LocalPoint, Event.Radius, Candidates);

    for (const int32 Idx : Candidates)
    {
        FVector& V = Vertices[Idx];
        const float Dist = FVector::Distance(V, Event.LocalPoint);
        if (Dist > Event.Radius) continue;

        const float T = 1.0f - (Dist / FMath::Max(Event.Radius, 1.0f));
        const float Falloff = T * T;
        const float Offset = FMath::Clamp(Event.Force * 0.02f * Falloff, -MaxDisplacement, MaxDisplacement);
        V += Event.LocalNormal * Offset;
        OutDirtyVertices.Add(Idx);
    }

    // Упрощённый no-recook: после изменения вершин сразу обновляем plane cache.
    RebuildCollisionPlanes();
}

void FCustomConvexMesh::RecalculateNormalsPartial(const TSet<int32>& DirtyVertices, TArray<FVector>& InOutNormals) const
{
    if (InOutNormals.Num() != Vertices.Num())
    {
        InOutNormals.Init(FVector::ZeroVector, Vertices.Num());
    }

    TSet<int32> VerticesToReset = DirtyVertices;
    for (int32 v : VerticesToReset) InOutNormals[v] = FVector::ZeroVector;

    for (int32 i = 0; i + 2 < Triangles.Num(); i += 3)
    {
        const int32 A = Triangles[i], B = Triangles[i + 1], C = Triangles[i + 2];
        if (!Vertices.IsValidIndex(A) || !Vertices.IsValidIndex(B) || !Vertices.IsValidIndex(C)) continue;

        if (!(DirtyVertices.Contains(A) || DirtyVertices.Contains(B) || DirtyVertices.Contains(C))) continue;

        const FVector N = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).GetSafeNormal();
        InOutNormals[A] += N; InOutNormals[B] += N; InOutNormals[C] += N;
    }

    for (int32 v : VerticesToReset)
    {
        InOutNormals[v] = InOutNormals[v].GetSafeNormal();
    }
}

void FCustomConvexMesh::RecalculateNormalsAll(TArray<FVector>& OutNormals) const
{
    OutNormals.Init(FVector::ZeroVector, Vertices.Num());
    for (int32 i = 0; i + 2 < Triangles.Num(); i += 3)
    {
        const int32 A = Triangles[i], B = Triangles[i + 1], C = Triangles[i + 2];
        if (!Vertices.IsValidIndex(A) || !Vertices.IsValidIndex(B) || !Vertices.IsValidIndex(C)) continue;
        const FVector N = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).GetSafeNormal();
        OutNormals[A] += N; OutNormals[B] += N; OutNormals[C] += N;
    }
    for (FVector& N : OutNormals) N = N.GetSafeNormal();
}

FBox FCustomConvexMesh::GetLocalBounds() const
{
    FBox B(EForceInit::ForceInit);
    for (const FVector& V : Vertices) B += V;
    return B;
}

FIntVector FCustomConvexMesh::HashKey(const FVector& P) const
{
    return FIntVector(
        FMath::FloorToInt(P.X / HashCellSize),
        FMath::FloorToInt(P.Y / HashCellSize),
        FMath::FloorToInt(P.Z / HashCellSize));
}

void FCustomConvexMesh::GatherVerticesInRadius(const FVector& Center, float Radius, TArray<int32>& OutIndices) const
{
    const int32 R = FMath::CeilToInt(Radius / HashCellSize);
    const FIntVector C = HashKey(Center);
    for (int32 x = -R; x <= R; ++x)
    for (int32 y = -R; y <= R; ++y)
    for (int32 z = -R; z <= R; ++z)
    {
        const FIntVector K = C + FIntVector(x,y,z);
        if (const TArray<int32>* Bucket = SpatialHash.Find(K))
        {
            OutIndices.Append(*Bucket);
        }
    }
}



const TArray<FDynamicCollisionPlane>& FCustomConvexMesh::GetCollisionPlanes() const
{
    return CollisionPlanes;
}

FVector FCustomConvexMesh::GetSupportPoint(const FVector& Direction) const
{
    if (Vertices.Num() == 0) return FVector::ZeroVector;

    const FVector Dir = Direction.GetSafeNormal();
    float BestDot = -TNumericLimits<float>::Max();
    FVector Best = Vertices[0];

    for (const FVector& V : Vertices)
    {
        const float D = FVector::DotProduct(V, Dir);
        if (D > BestDot)
        {
            BestDot = D;
            Best = V;
        }
    }

    return Best;
}

void FCustomConvexMesh::GetWorldVertices(const FTransform& LocalToWorld, TArray<FVector>& OutVertices) const
{
    OutVertices.Reset();
    OutVertices.Reserve(Vertices.Num());
    for (const FVector& V : Vertices)
    {
        OutVertices.Add(LocalToWorld.TransformPosition(V));
    }
}


void FCustomConvexMesh::GetWorldCollisionPlanes(const FTransform& LocalToWorld, TArray<FDynamicCollisionPlane>& OutPlanes) const
{
    OutPlanes.Reset();
    OutPlanes.Reserve(CollisionPlanes.Num());

    for (const FDynamicCollisionPlane& LocalPlane : CollisionPlanes)
    {
        if (!Vertices.IsValidIndex(LocalPlane.A))
        {
            continue;
        }

        FDynamicCollisionPlane WorldPlane = LocalPlane;
        WorldPlane.Normal = LocalToWorld.TransformVectorNoScale(LocalPlane.Normal).GetSafeNormal();
        const FVector WorldPoint = LocalToWorld.TransformPosition(Vertices[LocalPlane.A]);
        WorldPlane.Distance = FVector::DotProduct(WorldPlane.Normal, WorldPoint);
        OutPlanes.Add(WorldPlane);
    }
}

void FCustomConvexMesh::TranslateVertices(const FVector& Delta)
{
    for (FVector& V : Vertices)
    {
        V += Delta;
    }
    BuildSpatialHash(HashCellSize);
    RebuildCollisionPlanes();
}
