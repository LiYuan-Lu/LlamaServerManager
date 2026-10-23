#include "core/LoginStartup.h"
#include "ipc/AgentClient.h"
#include "ipc/Protocol.h"

#include "LanguageManager.h"
#include "SingleInstance.h"
#include "TrayIcon.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QIcon>
#include <QJsonObject>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cstring>

namespace {
void addOptions(QCommandLineParser& parser)
{
    parser.addHelpOption();
    parser.addOption({ "data-dir", "Agent data directory", "directory" });
    parser.addOption({ "minimized", "Start hidden in the system tray (used when starting at sign-in)" });
    parser.addOption({ "quit", "Close the running window and stop the Agent and all models, then exit (used by installers)" });
    parser.addOption({ "set-autostart", "Turn starting at sign-in on or off, then exit", "on|off" });
    parser.addOption({ "smoke-test", "Exit after QML startup (test only)" });
}

QString dataDirectory(const QCommandLineParser& parser)
{
    return parser.isSet("data-dir") ? QDir(parser.value("data-dir")).absolutePath() : Protocol::dataDirectory();
}

// What starts at sign-in: this executable, hidden in the tray, with the same data directory.
LoginStartup::Entry loginEntry(const QCommandLineParser& parser)
{
    LoginStartup::Entry entry;
    entry.program = QCoreApplication::applicationFilePath();
    entry.workingDirectory = QCoreApplication::applicationDirPath();
    entry.arguments << "--minimized";
    if (parser.isSet("data-dir")) {
        entry.arguments << "--data-dir" << dataDirectory(parser);
    }
    return entry;
}

// Asks the Agent to stop every model and exit; waits until it is gone.
bool shutdownAgent(const QString& directory, QString& error)
{
    QLocalSocket socket;
    socket.connectToServer(Protocol::endpoint(directory));
    if (!socket.waitForConnected(1500)) {
        return true; // no Agent running
    }
    Protocol::send(&socket, { { "version", Protocol::Version }, { "id", "quit" }, { "command", "shutdownAgent" } });
    socket.flush();
    QElapsedTimer timer;
    timer.start();
    while (socket.state() == QLocalSocket::ConnectedState && timer.elapsed() < 30000) {
        socket.waitForReadyRead(200);
        socket.readAll();
    }
    if (socket.state() == QLocalSocket::ConnectedState) {
        error = "The Agent did not stop within 30 seconds";
        return false;
    }
    return true;
}

// --quit and --set-autostart run without a window.
int runCommand(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("LlamaServerManager");
    QCommandLineParser parser;
    addOptions(parser);
    parser.process(app);
    QTextStream err(stderr);
    if (parser.isSet("set-autostart")) {
        const auto value = parser.value("set-autostart");
        if (value != "on" && value != "off") {
            err << "--set-autostart expects on or off" << Qt::endl;
            return 2;
        }
        LoginStartup startup(loginEntry(parser));
        const auto error = startup.applyBlocking(value == "on");
        if (!error.isEmpty()) {
            err << error << Qt::endl;
            return 1;
        }
        return 0;
    }
    const auto directory = dataDirectory(parser);
    SingleInstance instance(directory);
    if (!instance.claim()) {
        instance.send("quit", 5000);
    }
    QString error;
    if (!shutdownAgent(directory, error)) {
        err << error << Qt::endl;
        return 1;
    }
    return 0;
}

bool isCommand(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quit") == 0 || std::strncmp(argv[i], "--set-autostart", 15) == 0) {
            return true;
        }
    }
    return false;
}
}

int main(int argc, char** argv)
{
    if (isCommand(argc, argv)) {
        return runCommand(argc, argv);
    }
    QApplication app(argc, argv);
    app.setApplicationName("LlamaServerManager");
    app.setApplicationDisplayName("Llama Server Manager");
    app.setWindowIcon(QIcon(":/icons/app.png"));
    LanguageManager languageManager;
    QCommandLineParser parser;
    addOptions(parser);
    parser.process(app);
    const bool smokeTest = parser.isSet("smoke-test");
    const auto directory = dataDirectory(parser);

    // A second launch (e.g. from the Start menu while the GUI sits in the tray) shows the
    // existing window instead of opening another one.
    SingleInstance instance(directory);
    // The running GUI answers from its event loop, which starts only after it has loaded the
    // QML; on a busy or slow machine (just after signing in, no GPU) that can take a while.
    if (!instance.claim()) {
        return instance.send("show", 15000) ? 0 : 3;
    }

    QQuickStyle::setStyle("Fusion");
    // Software rendering: no GPU driver involved (some drivers made the desktop flicker on
    // hover), no VRAM taken from the models, and no stutter while inference keeps the GPU
    // busy. The UI is simple enough for the CPU. Setting QSG_RHI_BACKEND (for example
    // "d3d11" or "opengl") or QT_QUICK_BACKEND still chooses another renderer.
    if (qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND") && qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }
    AgentClient client(directory);
    LoginStartup startup(loginEntry(parser));
    TrayIcon tray(&client, &startup, QDir(directory).filePath("logs"));
    const bool startHidden = parser.isSet("minimized") && tray.available();
    app.setQuitOnLastWindowClosed(!tray.available());

    QQmlApplicationEngine engine;
    languageManager.setEngine(&engine);
    engine.rootContext()->setContextProperty("agent", &client);
    engine.rootContext()->setContextProperty("languageManager", &languageManager);
    engine.rootContext()->setContextProperty("tray", &tray);
    engine.rootContext()->setContextProperty("startup", &startup);
    engine.rootContext()->setContextProperty("startHidden", startHidden);
    engine.load(QUrl("qrc:/apps/gui/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto showWindow = [window] {
        if (window->windowState() & Qt::WindowMinimized) {
            window->showNormal();
        } else {
            window->show();
        }
        window->raise();
        window->requestActivate();
    };
    tray.setWindowVisible(window->isVisible());
    QObject::connect(window, &QWindow::visibleChanged, &tray, &TrayIcon::setWindowVisible);
    QObject::connect(&tray, &TrayIcon::showWindowRequested, window, showWindow);
    QObject::connect(&tray, &TrayIcon::hideWindowRequested, window, &QWindow::hide);
    QObject::connect(&tray, &TrayIcon::quitRequested, &app, &QCoreApplication::quit);
    QObject::connect(&instance, &SingleInstance::messageReceived, &app, [&app, showWindow](const QByteArray& message) {
        if (message == "show") {
            showWindow();
        } else if (message == "quit") {
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
        }
    });
    client.connectAgent();
    if (!smokeTest) {
        startup.refresh(); // also repairs the sign-in entry if the app was moved
    }
    if (smokeTest) {
        // Test hooks: LSM_SCREENSHOT saves the window after LSM_SCREENSHOT_DELAY_MS (default 700);
        // LSM_SMOKE_OPEN opens that model's detail page first ("duplicate:<id>" opens a copy).
        const auto screenshot = qEnvironmentVariable("LSM_SCREENSHOT");
        const int delay = qEnvironmentVariableIntValue("LSM_SCREENSHOT_DELAY_MS") > 0 ? qEnvironmentVariableIntValue("LSM_SCREENSHOT_DELAY_MS") : 700;
        const auto open = qEnvironmentVariable("LSM_SMOKE_OPEN");
        if (!open.isEmpty()) {
            QTimer::singleShot(delay / 2, &app, [&engine, &client, open] {
                auto* root = engine.rootObjects().first();
                if (open.startsWith("duplicate:")) {
                    const auto profile = client.instances().value(open.mid(10)).toMap().value("profile");
                    QMetaObject::invokeMethod(root, "duplicateModel", Q_ARG(QVariant, profile));
                } else {
                    QMetaObject::invokeMethod(root, "openModel", Q_ARG(QVariant, open));
                }
            });
        }
        if (!screenshot.isEmpty()) {
            QTimer::singleShot(delay, &app, [window, screenshot] { window->grabWindow().save(screenshot); });
        }
        QTimer::singleShot(std::max(4000, delay + 500), &app, [&app, &client] {
            const auto expected = qEnvironmentVariable("LSM_EXPECTED_PID");
            bool found = expected.isEmpty();
            const auto instances = client.instances();
            for (const auto& value : instances) {
                found = found || value.toMap().value("pid").toString() == expected;
            }
            const bool synchronized = client.connected() && client.synchronized() && found;
            if (!synchronized) {
                qWarning() << "Smoke sync failed" << client.connected() << instances << client.error() << "expected" << expected;
            }
            app.exit(synchronized ? 0 : 2);
        });
    }
    return app.exec();
}
