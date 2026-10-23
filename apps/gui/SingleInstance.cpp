#include "SingleInstance.h"
#include "ipc/Protocol.h"
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QThread>
#include <algorithm>

SingleInstance::SingleInstance(const QString& dataDirectory, QObject* parent)
    : QObject(parent)
    , name(Protocol::endpoint(dataDirectory).replace("LlamaServerManager-v1-", "LlamaServerManager-gui-v1-"))
    , lock(QDir(dataDirectory).filePath("gui.lock"))
{
    QDir().mkpath(dataDirectory);
    lock.setStaleLockTime(0); // a crashed GUI's lock is detected by its PID, not its age
    connect(&server, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                while (socket->canReadLine()) {
                    const auto message = socket->readLine().trimmed();
                    socket->write("ok\n");
                    socket->flush();
                    emit messageReceived(message);
                }
                if (socket->bytesAvailable() > 256) {
                    socket->abort(); // not one of our messages
                }
            });
        }
    });
}

bool SingleInstance::claim()
{
    if (!lock.tryLock(0)) {
        return false;
    }
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(name); // we hold the lock, so any endpoint left is stale
    server.listen(name);
    return true;
}

bool SingleInstance::send(const QByteArray& message, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    // The running GUI may still be starting up, so retry until the timeout.
    while (timer.elapsed() < timeoutMs) {
        QLocalSocket socket;
        socket.connectToServer(name);
        if (socket.waitForConnected(500)) {
            socket.write(message + '\n');
            socket.flush();
            if (socket.waitForReadyRead(int(std::max<qint64>(100, timeoutMs - timer.elapsed()))) && socket.readLine().trimmed() == "ok") {
                return true;
            }
        }
        QThread::msleep(100);
    }
    return false;
}
