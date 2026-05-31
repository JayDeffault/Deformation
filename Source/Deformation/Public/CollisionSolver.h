#pragma once

#include "CoreMinimal.h"

struct FCollisionContact
{
    bool bHasContact = false;
    FVector Normal = FVector::UpVector;
    FVector Point = FVector::ZeroVector;
    float PenetrationDepth = 0.0f;
};

struct FProxyConvex
{
    FTransform WorldTransform;
    TArray<FVector> LocalVertices;
    TArray<int32> LocalTriangles;
};

class DEFORMATION_API FCollisionSolver
{
public:
    // SAT для динамических convex meshes: оси пересчитываются из актуальных деформированных треугольников.
    static FCollisionContact SolveConvexSAT(const FProxyConvex& A, const FProxyConvex& B);

    static void ResolveImpulse(
        FVector& InOutVelocity,
        FVector& InOutAngularVelocity,
        float Mass,
        const FVector& InertiaDiagonal,
        const FVector& ContactPointWS,
        const FVector& ContactNormalWS,
        float PenetrationDepth,
        float Restitution);
};
