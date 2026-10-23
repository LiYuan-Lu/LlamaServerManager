#include "ipc/AgentClient.h"
#include "ipc/Protocol.h"

#include "LanguageManager.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("LlamaServerManager");
    LanguageManager languageManager;
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({ "data-dir", "Agent data directory", "directory" });
    parser.addOption({ "smoke-test", "Exit after QML startup (test only)" });
    parser.process(app);
    QQuickStyle::setStyle("Fusion");
    AgentClient client(parser.isSet("data-dir") ? parser.value("data-dir") : Protocol::dataDirectory());
    QQmlApplicationEngine engine;
    languageManager.setEngine(&engine);
    engine.rootContext()->setContextProperty("agent", &client);
    engine.rootContext()->setContextProperty("languageManager", &languageManager);
    engine.load(QUrl("qrc:/apps/gui/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    client.connectAgent();
    if (parser.isSet("smoke-test")) {
        // Test hooks: LSM_SCREENSHOT saves the window after LSM_SCREENSHOT_DELAY_MS (default 700);
        // LSM_SMOKE_OPEN opens that model's detail page first.
        const auto screenshot = qEnvironmentVariable("LSM_SCREENSHOT");
        const int delay = qEnvironmentVariableIntValue("LSM_SCREENSHOT_DELAY_MS") > 0 ? qEnvironmentVariableIntValue("LSM_SCREENSHOT_DELAY_MS") : 700;
        const auto open = qEnvironmentVariable("LSM_SMOKE_OPEN");
        if (!open.isEmpty()) {
            QTimer::singleShot(delay / 2, &app, [&engine, open] {
                QMetaObject::invokeMethod(engine.rootObjects().first(), "openModel", Q_ARG(QVariant, open));
            });
        }
        if (!screenshot.isEmpty()) {
            QTimer::singleShot(delay, &app, [&engine, screenshot] {
                if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
                    window->grabWindow().save(screenshot);
                }
            });
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
