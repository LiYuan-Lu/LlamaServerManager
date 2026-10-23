#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
// Test double for llama-server. Environment switches:
//   FAKE_HELP_HANG            --help never returns
//   FAKE_EXIT                 exit with code 42 after 250 ms
//   FAKE_HEALTH_LOADING_MS    GET /health returns 503 for this long after start, then 200
//   FAKE_HEALTH_HANG_AFTER_MS /health stops answering (connection stays open) after this long
//   FAKE_ONCE_FILE            misbehave (FAKE_EXIT / FAKE_HEALTH_HANG_AFTER_MS) only on the run
//                             that creates this file; later runs behave normally
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains("--help")) {
        if (qEnvironmentVariableIsSet("FAKE_HELP_HANG")) {
            return app.exec();
        }
        QTextStream(stdout) << "-m -c -ngl -t --host --port --parallel --metrics\n";
        return 0;
    }
    bool misbehave = true;
    if (qEnvironmentVariableIsSet("FAKE_ONCE_FILE")) {
        QFile marker(qEnvironmentVariable("FAKE_ONCE_FILE"));
        if (marker.exists()) {
            misbehave = false;
        } else if (marker.open(QIODevice::WriteOnly)) {
            marker.close();
        }
    }
    const int loadingMs = qEnvironmentVariableIntValue("FAKE_HEALTH_LOADING_MS");
    const int hangAfterMs = misbehave && qEnvironmentVariableIsSet("FAKE_HEALTH_HANG_AFTER_MS")
        ? qEnvironmentVariableIntValue("FAKE_HEALTH_HANG_AFTER_MS")
        : -1;
    QElapsedTimer started;
    started.start();
    QTcpServer listener;
    const int index = args.indexOf("--port");
    if (index < 0 || !listener.listen(QHostAddress::LocalHost, args.value(index + 1).toUShort())) {
        return 2;
    }
    QObject::connect(&listener, &QTcpServer::newConnection, &app, [&] {
        while (QTcpSocket* socket = listener.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &started, loadingMs, hangAfterMs] {
                const auto request = socket->property("buffer").toByteArray() + socket->readAll();
                if (!request.contains("\r\n\r\n")) {
                    socket->setProperty("buffer", request);
                    return;
                }
                socket->setProperty("buffer", QByteArray());
                const auto elapsed = started.elapsed();
                if (hangAfterMs >= 0 && elapsed >= hangAfterMs) {
                    return; // simulate a stuck server
                }
                QByteArray status = "200 OK", body = R"({"status":"ok"})";
                if (!request.startsWith("GET /health ")) {
                    status = "404 Not Found";
                    body = "{}";
                } else if (elapsed < loadingMs) {
                    status = "503 Service Unavailable";
                    body = R"({"error":{"code":503,"message":"Loading model"}})";
                }
                socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    QTextStream(stdout) << "args=" << QJsonDocument(QJsonArray::fromStringList(args.mid(1))).toJson(QJsonDocument::Compact) << "\n"
                        << "cwd=" << QDir::currentPath() << Qt::endl;
    QTextStream(stderr) << "fake stderr ready\n"
                        << Qt::flush;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [] { QTextStream(stdout) << "heartbeat 模型\n"
                                                                              << Qt::flush; });
    timer.start(100);
    if (misbehave && qEnvironmentVariableIsSet("FAKE_EXIT")) {
        QTimer::singleShot(250, &app, [] { QCoreApplication::exit(42); });
    }
    return app.exec();
}
