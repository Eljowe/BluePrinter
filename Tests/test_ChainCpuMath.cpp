#include "TestRunner.h"
#include "ChainCpuMath.h"

BP_TEST (ChainCpuMath_blockFraction)
{
    // Half the block budget reads 0.5.
    BP_CHECK_NEAR (ChainCpuMath::blockFraction (0.005, 0.010), 0.5f, 0.0001f);
    // Overrun reads above 1.
    BP_CHECK_NEAR (ChainCpuMath::blockFraction (0.020, 0.010), 2.0f, 0.0001f);
    // Zero / negative budgets and negative elapsed are 0.
    BP_CHECK_NEAR (ChainCpuMath::blockFraction (0.005, 0.0), 0.0f, 0.0001f);
    BP_CHECK_NEAR (ChainCpuMath::blockFraction (0.005, -1.0), 0.0f, 0.0001f);
    BP_CHECK_NEAR (ChainCpuMath::blockFraction (-0.001, 0.010), 0.0f, 0.0001f);
}

BP_TEST (ChainCpuMath_smoothsTowardTarget)
{
    // A time constant of 1 block snaps to the target.
    BP_CHECK_NEAR (ChainCpuMath::smooth (0.0f, 1.0f, 1.0f), 1.0f, 0.0001f);
    // 2 blocks: halfway each step.
    BP_CHECK_NEAR (ChainCpuMath::smooth (0.0f, 1.0f, 2.0f), 0.5f, 0.0001f);
    BP_CHECK_NEAR (ChainCpuMath::smooth (0.5f, 1.0f, 2.0f), 0.75f, 0.0001f);
    // <= 0 snaps.
    BP_CHECK_NEAR (ChainCpuMath::smooth (0.3f, 1.0f, 0.0f), 1.0f, 0.0001f);
}

BP_TEST (ChainCpuMath_defaultSmoothingReactsGradually)
{
    // The default constant is 30 blocks, so a step from 0 to 1 only moves
    // 1/30 in the first block and a heavier chain therefore reads higher
    // than a lighter one at every step.
    const float first = ChainCpuMath::smooth (0.0f, 1.0f);
    BP_CHECK_NEAR (first, 1.0f / ChainCpuMath::smoothingBlocks, 0.0001f);

    float light = 0.0f;
    float heavy = 0.0f;
    for (int i = 0; i < 10; ++i)
    {
        light = ChainCpuMath::smooth (light, 0.2f);
        heavy = ChainCpuMath::smooth (heavy, 0.8f);
    }
    BP_CHECK (heavy > light);
}
