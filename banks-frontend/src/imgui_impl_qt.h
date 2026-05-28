#pragma once

class QQuickWindow;
class QEvent;

namespace ImGuiQt {
    void init();
    void shutdown();
    void newFrame(QQuickWindow* win);   // sets DisplaySize, DeltaTime, etc.
    bool handleEvent(QEvent* ev);       // returns true if ImGui consumed event.
}
