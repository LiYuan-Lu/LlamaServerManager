#include "ipc/AgentClient.h"
#include "ipc/Protocol.h"

#include "LanguageManager.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv); app.setApplicationName("LlamaServerManager");
    LanguageManager languageManager;
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"data-dir", "Agent data directory", "directory"});
    parser.addOption({"smoke-test", "Exit after QML startup (test only)"}); parser.process(app);
    QQuickStyle::setStyle("Fusion");
    AgentClient client(parser.isSet("data-dir") ? parser.value("data-dir") : Protocol::dataDirectory());
    QQmlApplicationEngine engine;
    languageManager.setEngine(&engine);
    engine.rootContext()->setContextProperty("agent", &client);
    engine.rootContext()->setContextProperty("languageManager", &languageManager);
    engine.load(QUrl("qrc:/apps/gui/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    client.connectAgent();
    if (parser.isSet("smoke-test")) {
        const auto screenshot = qEnvironmentVariable("LSM_SCREENSHOT");
        if (!screenshot.isEmpty()) QTimer::singleShot(700, &app, [&engine, screenshot] {
            if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) window->grabWindow().save(screenshot);
        });
        QTimer::singleShot(4000, &app, [&app, &client] {
            const auto expected = qEnvironmentVariable("LSM_EXPECTED_PID");
            bool found = expected.isEmpty();
            const auto instances = client.instances();
            for (const auto &value : instances) found = found || value.toMap().value("pid").toString() == expected;
            const bool synchronized = client.connected() && client.synchronized() && found;
            if (!synchronized) qWarning() << "Smoke sync failed" << client.connected() << instances << client.error() << "expected" << expected;
            app.exit(synchronized ? 0 : 2);
        });
    }
    return app.exec();
}
