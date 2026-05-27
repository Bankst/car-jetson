#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QVariant>
#include <QSocketNotifier>

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

#include <QCommandLineParser>
#include <QLockFile>
#include <QStandardPaths>

#include <csignal>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>

static int s_sigFd[2];

static void signalHandler(int) {
    char c = 1;
    write(s_sigFd[0], &c, 1);
}

static void installUnixSignalHandlers() {
    socketpair(AF_UNIX, SOCK_STREAM, 0, s_sigFd);

    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

int main(int argc, char** argv) {
    // Single instance lock
    QLockFile lockFile(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/banks-frontend.lock");
    if (!lockFile.tryLock(100)) {
        qint64 pid = 0;
        QString hostname, appname;
        lockFile.getLockInfo(&pid, &hostname, &appname);
        fprintf(stderr, "banks-frontend already running (pid %lld). Exiting.\n", (long long)pid);
        return 1;
    }

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

    installUnixSignalHandlers();

    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle("Basic");

    auto* sigNotifier = new QSocketNotifier(s_sigFd[1], QSocketNotifier::Read, &app);
    QObject::connect(sigNotifier, &QSocketNotifier::activated, &app, []{
        qInfo("[banks-frontend] caught SIGINT/SIGTERM, shutting down");
        std::thread([]{ std::this_thread::sleep_for(std::chrono::seconds(2)); _exit(1); }).detach();
        QCoreApplication::quit();
    });

    QCommandLineParser parser;
    parser.setApplicationDescription("Banks infotainment frontend");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

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
