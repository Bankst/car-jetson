#include "ImGuiOverlay.h"
#include "imgui_impl_qt.h"
#include "Log.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

#include <QQuickWindow>
#include <QKeyEvent>
#include <QObject>
#include <QOpenGLContext>
#include <QCoreApplication>

#include <atomic>

namespace {

class OverlayController : public QObject {
public:
    explicit OverlayController(QQuickWindow* win) : QObject(win), m_win(win) {
        if (qEnvironmentVariableIntValue("BANKS_DEBUG_OVERLAY") == 1) m_visible.store(true);

        QObject::connect(win, &QQuickWindow::beforeRenderPassRecording,
                         this, [this]{ render(); }, Qt::DirectConnection);
        QObject::connect(win, &QQuickWindow::sceneGraphInitialized,
                         this, [this]{ initGL(); }, Qt::DirectConnection);
        QObject::connect(win, &QQuickWindow::sceneGraphInvalidated,
                         this, [this]{ shutdownGL(); }, Qt::DirectConnection);
        win->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() == Qt::Key_F12) { m_visible.store(!m_visible.load()); return true; }
        }
        if (m_visible.load()) {
            return ImGuiQt::handleEvent(ev);
        }
        return QObject::eventFilter(obj, ev);
    }

private:
    void initGL() {
        if (m_ready) return;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGui_ImplOpenGL3_Init("#version 300 es");
        ImGuiQt::init();
        m_ready = true;
    }

    void shutdownGL() {
        if (!m_ready) return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGuiQt::shutdown();
        ImGui::DestroyContext();
        m_ready = false;
    }

    void render() {
        if (!m_ready || !m_visible.load()) return;

        ImGuiQt::newFrame(m_win);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Debug")) {
            ImGui::Text("Banks Frontend dev overlay");
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Separator();
            ImGui::TextDisabled("F12 toggles overlay");
        }
        ImGui::End();

        ImGui::Render();

        // Save/restore minimal GL state Qt scene graph cares about.
        m_win->beginExternalCommands();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        m_win->endExternalCommands();
    }

    QQuickWindow* m_win{};
    std::atomic<bool> m_visible{false};
    bool m_ready{false};
};

} // namespace

void ImGuiOverlay::installOn(QQuickWindow* win) {
    if (!win) { qCWarning(logOverlay) << "installOn(nullptr) — skipping"; return; }
    qCInfo(logOverlay) << "Installing ImGui overlay on" << win;
    new OverlayController(win);
}
