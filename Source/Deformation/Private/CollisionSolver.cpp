#include "CollisionSolver.h"

namespace
{
static void BuildAxesFromConvex(const FProxyConvex& Convex, TArray<FVector>& OutAxes)
{
    const int32 Num = Convex.LocalVertices.Num();
    if (Num < 3) return;

    for (int32 i = 0; i + 2 < Num; i += 3)
    {
        const FVector A = Convex.WorldTransform.TransformPosition(Convex.LocalVertices[i]);
        const FVector B = Convex.WorldTransform.TransformPosition(Convex.LocalVertices[i + 1]);
        const FVector C = Convex.WorldTransform.TransformPosition(Convex.LocalVertices[i + 2]);
        const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
        if (!N.IsNearlyZero())
        {
            OutAxes.Add(N);
        }
    }

    OutAxes.Add(Convex.WorldTransform.GetUnitAxis(EAxis::X));
    OutAxes.Add(Convex.WorldTransform.GetUnitAxis(EAxis::Y));
    OutAxes.Add(Convex.WorldTransform.GetUnitAxis(EAxis::Z));
}

static void ProjectConvex(const FProxyConvex& Convex, const FVector& Axis, float& OutMin, float& OutMax)
{
    OutMin = TNumericLimits<float>::Max();
    OutMax = -TNumericLimits<float>::Max();

    for (const FVector& V : Convex.LocalVertices)
    {
        const FVector W = Convex.WorldTransform.TransformPosition(V);
        const float D = FVector::DotProduct(W, Axis);
        OutMin = FMath::Min(OutMin, D);
        OutMax = FMath::Max(OutMax, D);
    }
}
}

FCollisionContact FCollisionSolver::SolveConvexSAT(const FProxyConvex& A, const FProxyConvex& B)
{
    FCollisionContact Contact;
    if (A.LocalVertices.Num() < 4 || B.LocalVertices.Num() < 4)
    {
        return Contact;
    }

    TArray<FVector> Axes;
    Axes.Reserve(32);
    BuildAxesFromConvex(A, Axes);
    BuildAxesFromConvex(B, Axes);

    float MinPenetration = TNumericLimits<float>::Max();
    FVector BestAxis = FVector::UpVector;

    for (const FVector& RawAxis : Axes)
    {
        const FVector Axis = RawAxis.GetSafeNormal();
        if (Axis.IsNearlyZero()) continue;

        float MinA, MaxA, MinB, MaxB;
        ProjectConvex(A, Axis, MinA, MaxA);
        ProjectConvex(B, Axis, MinB, MaxB);

        if (MaxA < MinB || MaxB < MinA)
        {
            return Contact;
        }

        const float Pen = FMath::Min(MaxA, MaxB) - FMath::Max(MinA, MinB);
        if (Pen < MinPenetration)
        {
            MinPenetration = Pen;
            BestAxis = Axis;
        }
    }

    Contact.bHasContact = true;
    Contact.PenetrationDepth = MinPenetration;
    const FVector Delta = B.WorldTransform.GetLocation() - A.WorldTransform.GetLocation();
    Contact.Normal = FVector::DotProduct(Delta, BestAxis) > 0.0f ? BestAxis : -BestAxis;
    Contact.Point = (A.WorldTransform.GetLocation() + B.WorldTransform.GetLocation()) * 0.5f;
    return Contact;
}

void FCollisionSolver::ResolveImpulse(FVector& InOutVelocity, FVector& InOutAngularVelocity, float Mass, const FVector& InertiaDiagonal, const FVector& ContactPointWS, const FVector& ContactNormalWS, float PenetrationDepth, float Restitution)
{
    if (Mass <= KINDA_SMALL_NUMBER) return;

    const float InvMass = 1.0f / Mass;
    const FVector RelativeVel = InOutVelocity + FVector::CrossProduct(InOutAngularVelocity, ContactPointWS);
    const float AlongN = FVector::DotProduct(RelativeVel, ContactNormalWS);
    if (AlongN > 0.0f) return;

    const float J = (-(1.0f + Restitution) * AlongN) / InvMass;
    const FVector Impulse = J * ContactNormalWS;

    InOutVelocity += Impulse * InvMass;
    InOutAngularVelocity += FVector(
        Impulse.X / FMath::Max(InertiaDiagonal.X, 1.0f),
        Impulse.Y / FMath::Max(InertiaDiagonal.Y, 1.0f),
        Impulse.Z / FMath::Max(InertiaDiagonal.Z, 1.0f));

    InOutVelocity += ContactNormalWS * PenetrationDepth * 0.35f;
}
