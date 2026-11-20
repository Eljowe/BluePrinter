#pragma once

#include <algorithm>

// Standalone-window fill math (0060/0061). The editor asks for the content
// size that makes the window fill the display's work area once the window
// chrome (title bar + borders) is subtracted. The window may be any aspect —
// the UI's Fit mode stretches its design surface to the available width.
// Pure — the display, window and constrainer access stays in WebViewEditor.
// Unit-tested directly (Tests/test_EditorWindowMath.cpp).
namespace EditorWindowMath
{
    struct ContentSize
    {
        int width = 0;
        int height = 0;
    };

    // Returns the content (editor) size, not the window size. `chromeWidth`
    // and `chromeHeight` are the current window-minus-content deltas; they
    // are subtracted from the work area. A screen too small for the minimum
    // still returns the minimum size (the editor's size limits win over the
    // display).
    inline ContentSize computeFillContentSize (int workWidth,
                                               int workHeight,
                                               int chromeWidth,
                                               int chromeHeight,
                                               int minWidth,
                                               int minHeight)
    {
        if (workWidth <= 0 || workHeight <= 0 || minWidth <= 0 || minHeight <= 0)
            return {};

        return { std::max (minWidth,  workWidth  - std::max (0, chromeWidth)),
                 std::max (minHeight, workHeight - std::max (0, chromeHeight)) };
    }
} // namespace EditorWindowMath
