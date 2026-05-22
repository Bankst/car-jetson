#pragma once

class QQuickWindow;

namespace ImGuiOverlay {
    // Install render hooks + input event filter on the main window.
    // Toggle visibility with F12 or env BANKS_DEBUG_OVERLAY=1.
    void installOn(QQuickWindow* window);
}
