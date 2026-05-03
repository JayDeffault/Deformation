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
};

class DEFORMATION_API FCollisionSolver
{
public:
    // SAT для convex-convex: тестируем оси нормалей граней двух proxy convex hull.
    static FCollisionContact SolveConvexSAT(const FProxyConvex& A, const FProxyConvex& B);

    // Impulse response для одной машины (вторая сторона — внешний мир или отдельно решается в другом компоненте).
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
