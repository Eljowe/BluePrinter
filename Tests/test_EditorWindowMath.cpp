#include "TestRunner.h"
#include "EditorWindowMath.h"

BP_TEST (EditorWindowMath_fillsWorkArea)
{
    const auto size = EditorWindowMath::computeFillContentSize (1920, 1040, 0, 0, 672, 490);
    BP_CHECK_EQ (size.width, 1920);
    BP_CHECK_EQ (size.height, 1040);
}

BP_TEST (EditorWindowMath_subtractsWindowChrome)
{
    const auto size = EditorWindowMath::computeFillContentSize (1920, 1040, 16, 40, 672, 490);
    BP_CHECK_EQ (size.width, 1904);
    BP_CHECK_EQ (size.height, 1000);
}

BP_TEST (EditorWindowMath_tinyScreenKeepsMinimum)
{
    const auto size = EditorWindowMath::computeFillContentSize (500, 300, 0, 0, 672, 490);
    BP_CHECK_EQ (size.width, 672);
    BP_CHECK_EQ (size.height, 490);
}

BP_TEST (EditorWindowMath_handlesUltrawide)
{
    const auto size = EditorWindowMath::computeFillContentSize (3440, 1440, 16, 40, 672, 490);
    BP_CHECK_EQ (size.width, 3424);
    BP_CHECK_EQ (size.height, 1400);
}

BP_TEST (EditorWindowMath_rejectsInvalidInputs)
{
    BP_CHECK_EQ (EditorWindowMath::computeFillContentSize (0, 1080, 0, 0, 672, 490).width, 0);
    BP_CHECK_EQ (EditorWindowMath::computeFillContentSize (1920, -1, 0, 0, 672, 490).width, 0);
    BP_CHECK_EQ (EditorWindowMath::computeFillContentSize (1920, 1080, 0, 0, 0, 490).width, 0);
    BP_CHECK_EQ (EditorWindowMath::computeFillContentSize (1920, 1080, 0, 0, 672, 0).width, 0);
}
