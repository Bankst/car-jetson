#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QVariant>

#include <QLoggingCategory>

#include "AudioCapture.h"
#include "Visualizer.h"
#include "SpectrumWidget.h"
#include "ImGuiOverlay.h"
#include "Log.h"
#include "aa/AASessionController.h"
#include "aa/AAVideoItem.h"
#include "aa/AudioTestController.h"
#include "aa/SystemInfo.h"
#include "aa/BluetoothPairingAgent.h"
#include "aa/BluetoothManager.h"
#include "aa/LogCapture.h"

int main(int argc, char** argv) {
    // Verbose Qt scene-graph info to stderr by default in dev builds.
    qputenv("QSG_INFO", "1");
    QLoggingCategory::setFilterRules(
        "qt.scenegraph.general=true\n"
        "banks.*=true\n"
    );

    // Install log capture early so all messages (including pre-app) are buffered.
    LogCapture::install();

    qInfo("[banks-frontend] starting, pid=%lld", (long long)QCoreApplication::applicationPid());

    // Force GLES rendering — projectM built ENABLE_GLES=ON, must match.
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    fmt.setVersion(3, 0);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);
    qInfo("[banks-frontend] requested GL: OpenGLES 3.0, depth=24, stencil=8");

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    qInfo("[banks-frontend] RHI backend pinned: OpenGL");

    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");

    // Process-wide audio capture singleton. Lives for full app lifetime so
    // both Visualizer and SpectrumWidget can borrow it as they enter/leave
    // the scene.
    auto* audio = new AudioCapture();
    audio->start();
    // QObject parent for cleanup
    app.setProperty("audioCapturePtr", QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(audio)));
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [audio]{
        audio->stop();
        delete audio;
    });
    qInfo("[banks-frontend] audio capture singleton started, consumers=%d", audio->consumerCount());

    qmlRegisterType<Visualizer>("BanksFrontend", 1, 0, "Visualizer");
    qmlRegisterType<SpectrumWidget>("BanksFrontend", 1, 0, "SpectrumWidget");
    qmlRegisterType<AASessionController>("BanksFrontend", 1, 0, "AASessionController");
    qmlRegisterType<AAVideoItem>("BanksFrontend", 1, 0, "AAVideoItem");
    qmlRegisterType<AudioTestController>("BanksFrontend", 1, 0, "AudioTestController");
    qmlRegisterType<SystemInfo>("BanksFrontend", 1, 0, "SystemInfo");
    qmlRegisterType<BluetoothPairingAgent>("BanksFrontend", 1, 0, "BluetoothPairingAgent");
    qmlRegisterType<BluetoothManager>("BanksFrontend", 1, 0, "BluetoothManager");
    qmlRegisterSingletonInstance("BanksFrontend", 1, 0, "LogCapture", LogCapture::instance());

    QQmlApplicationEngine engine;
    engine.loadFromModule("BanksFrontend", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    ImGuiOverlay::installOn(win);

    return app.exec();
}
