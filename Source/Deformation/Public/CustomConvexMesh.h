#pragma once

#include "CoreMinimal.h"

struct FDeformationEvent
{
    FVector LocalPoint = FVector::ZeroVector;
    FVector LocalNormal = FVector::UpVector;
    float Force = 0.0f;
    float Radius = 50.0f;
};

struct FDynamicCollisionPlane
{
    int32 A = INDEX_NONE;
    int32 B = INDEX_NONE;
    int32 C = INDEX_NONE;
    FVector Normal = FVector::UpVector;
    float Distance = 0.0f;
};

class DEFORMATION_API FCustomConvexMesh
{
public:
    TArray<FVector> Vertices;
    TArray<int32> Triangles;

    void Initialize(const TArray<FVector>& InVertices, const TArray<int32>& InTriangles);
    void BuildSpatialHash(float CellSize = 20.0f);
    void RebuildCollisionPlanes();
    void ApplyDeformationEvent(const FDeformationEvent& Event, float MaxDisplacement, TArray<int32>& OutDirtyVertices);
    void RecalculateNormalsPartial(const TSet<int32>& DirtyVertices, TArray<FVector>& InOutNormals) const;
    void RecalculateNormalsAll(TArray<FVector>& OutNormals) const;
    FBox GetLocalBounds() const;

    const TArray<FDynamicCollisionPlane>& GetCollisionPlanes() const;
    FVector GetSupportPoint(const FVector& Direction) const;
    void GetWorldVertices(const FTransform& LocalToWorld, TArray<FVector>& OutVertices) const;
    void GetWorldCollisionPlanes(const FTransform& LocalToWorld, TArray<FDynamicCollisionPlane>& OutPlanes) const;
    void TranslateVertices(const FVector& Delta);

private:
    TArray<FDynamicCollisionPlane> CollisionPlanes;
    TMap<FIntVector, TArray<int32>> SpatialHash;
    float HashCellSize = 20.0f;

    FIntVector HashKey(const FVector& P) const;
    void GatherVerticesInRadius(const FVector& Center, float Radius, TArray<int32>& OutIndices) const;
};
