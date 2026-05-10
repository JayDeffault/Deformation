#pragma once

#include "CoreMinimal.h"

class AActor;

struct FBroadPhaseBody
{
    TWeakObjectPtr<AActor> Owner;
    FBox WorldAABB;
};

class DEFORMATION_API FCollisionBroadPhase
{
public:
    void Build(const TArray<FBroadPhaseBody>& Bodies);
    void QueryPotentialPairs(TArray<TPair<int32,int32>>& OutPairs) const;

private:
    TArray<FBroadPhaseBody> Cached;
};
