#include "ipc/AgentServer.h"
#include "ipc/Protocol.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QTextStream>
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("LlamaServerManager");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({ "data-dir", "Override data directory (isolated tests)", "directory" });
    parser.process(app);
    const auto dir = parser.isSet("data-dir") ? parser.value("data-dir") : Protocol::dataDirectory();
    if (!QDir().mkpath(dir)) {
        return 2;
    }
    QLockFile lock(dir + "/agent.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        return 3;
    }
    AgentServer server(dir);
    QString error;
    if (!server.listen(error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 4;
    }
    return app.exec();
}
