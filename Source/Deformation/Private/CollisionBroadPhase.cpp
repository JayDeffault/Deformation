#include "CollisionBroadPhase.h"

void FCollisionBroadPhase::Build(const TArray<FBroadPhaseBody>& Bodies)
{
    Cached = Bodies;
}

void FCollisionBroadPhase::QueryPotentialPairs(TArray<TPair<int32,int32>>& OutPairs) const
{
    OutPairs.Reset();
    for (int32 i = 0; i < Cached.Num(); ++i)
    {
        for (int32 j = i + 1; j < Cached.Num(); ++j)
        {
            if (Cached[i].WorldAABB.Intersect(Cached[j].WorldAABB))
            {
                OutPairs.Add(TPair<int32,int32>(i, j));
            }
        }
    }
}
