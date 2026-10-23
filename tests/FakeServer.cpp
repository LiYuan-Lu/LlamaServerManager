#include <QCoreApplication>
#include <QTimer>
#include <QTcpServer>
#include <QHostAddress>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains("--help")) {
        if (qEnvironmentVariableIsSet("FAKE_HELP_HANG")) return app.exec();
        QTextStream(stdout) << "-m -c -ngl -t --host --port --parallel --metrics\n";
        return 0;
    }
    QTcpServer listener;
    const int index = args.indexOf("--port");
    if (index < 0 || !listener.listen(QHostAddress::LocalHost, args.value(index + 1).toUShort())) return 2;
    QTextStream(stdout) << "args=" << QJsonDocument(QJsonArray::fromStringList(args.mid(1))).toJson(QJsonDocument::Compact) << "\n" << "cwd=" << QDir::currentPath() << Qt::endl;
    QTextStream(stderr) << "fake stderr ready\n" << Qt::flush;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [] { QTextStream(stdout) << "heartbeat 模型\n" << Qt::flush; });
    timer.start(100);
    if (qEnvironmentVariableIsSet("FAKE_EXIT")) QTimer::singleShot(250, &app, [] { QCoreApplication::exit(42); });
    return app.exec();
}
