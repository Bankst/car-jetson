#include "imgui_impl_qt.h"

#include <imgui.h>

#include <QQuickWindow>
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QElapsedTimer>

namespace {
QElapsedTimer s_timer;
qint64        s_lastNs = 0;

ImGuiKey toImGuiKey(int qtKey) {
    switch (qtKey) {
        case Qt::Key_Tab:        return ImGuiKey_Tab;
        case Qt::Key_Left:       return ImGuiKey_LeftArrow;
        case Qt::Key_Right:      return ImGuiKey_RightArrow;
        case Qt::Key_Up:         return ImGuiKey_UpArrow;
        case Qt::Key_Down:       return ImGuiKey_DownArrow;
        case Qt::Key_PageUp:     return ImGuiKey_PageUp;
        case Qt::Key_PageDown:   return ImGuiKey_PageDown;
        case Qt::Key_Home:       return ImGuiKey_Home;
        case Qt::Key_End:        return ImGuiKey_End;
        case Qt::Key_Insert:     return ImGuiKey_Insert;
        case Qt::Key_Delete:     return ImGuiKey_Delete;
        case Qt::Key_Backspace:  return ImGuiKey_Backspace;
        case Qt::Key_Space:      return ImGuiKey_Space;
        case Qt::Key_Return:     return ImGuiKey_Enter;
        case Qt::Key_Escape:     return ImGuiKey_Escape;
        case Qt::Key_A: return ImGuiKey_A; case Qt::Key_C: return ImGuiKey_C;
        case Qt::Key_V: return ImGuiKey_V; case Qt::Key_X: return ImGuiKey_X;
        case Qt::Key_Y: return ImGuiKey_Y; case Qt::Key_Z: return ImGuiKey_Z;
        default: return ImGuiKey_None;
    }
}

int toImGuiButton(Qt::MouseButton b) {
    switch (b) {
        case Qt::LeftButton:   return 0;
        case Qt::RightButton:  return 1;
        case Qt::MiddleButton: return 2;
        default: return -1;
    }
}
}

void ImGuiQt::init() {
    s_timer.start();
    s_lastNs = s_timer.nsecsElapsed();
}

void ImGuiQt::shutdown() {}

void ImGuiQt::newFrame(QQuickWindow* win) {
    ImGuiIO& io = ImGui::GetIO();
    if (win) {
        io.DisplaySize = ImVec2(static_cast<float>(win->width()),
                                static_cast<float>(win->height()));
        const float dpr = static_cast<float>(win->devicePixelRatio());
        io.DisplayFramebufferScale = ImVec2(dpr, dpr);
    }
    qint64 now = s_timer.nsecsElapsed();
    io.DeltaTime = (now - s_lastNs) / 1e9f;
    if (io.DeltaTime <= 0) io.DeltaTime = 1.0f / 60.0f;
    s_lastNs = now;
}

bool ImGuiQt::handleEvent(QEvent* ev) {
    ImGuiIO& io = ImGui::GetIO();
    switch (ev->type()) {
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(ev);
            io.AddMousePosEvent(static_cast<float>(me->position().x()),
                                static_cast<float>(me->position().y()));
            return io.WantCaptureMouse;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(ev);
            int b = toImGuiButton(me->button());
            if (b >= 0) io.AddMouseButtonEvent(b, ev->type() == QEvent::MouseButtonPress);
            return io.WantCaptureMouse;
        }
        case QEvent::Wheel: {
            auto* we = static_cast<QWheelEvent*>(ev);
            io.AddMouseWheelEvent(we->angleDelta().x() / 120.0f,
                                  we->angleDelta().y() / 120.0f);
            return io.WantCaptureMouse;
        }
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            auto* ke = static_cast<QKeyEvent*>(ev);
            ImGuiKey k = toImGuiKey(ke->key());
            if (k != ImGuiKey_None) io.AddKeyEvent(k, ev->type() == QEvent::KeyPress);
            io.AddKeyEvent(ImGuiMod_Ctrl,  ke->modifiers() & Qt::ControlModifier);
            io.AddKeyEvent(ImGuiMod_Shift, ke->modifiers() & Qt::ShiftModifier);
            io.AddKeyEvent(ImGuiMod_Alt,   ke->modifiers() & Qt::AltModifier);
            io.AddKeyEvent(ImGuiMod_Super, ke->modifiers() & Qt::MetaModifier);
            if (ev->type() == QEvent::KeyPress) {
                const QString& t = ke->text();
                for (QChar c : t) {
                    if (c.unicode() >= 32) io.AddInputCharacter(c.unicode());
                }
            }
            return io.WantCaptureKeyboard;
        }
        default: break;
    }
    return false;
}
