#include "AgentServer.h"
#include "Protocol.h"
#include <QLocalSocket>
AgentServer::AgentServer(const QString &dir, QObject *parent)
    : QObject(parent), directory(dir), store(dir), logs(dir + "/logs") {
    QString warning; profile = store.load(warning);
    connect(&manager, &LlamaProcessManager::log, &logs, &LogStore::append);
    connect(&manager, &LlamaProcessManager::changed, this, [this] { broadcast("status", snapshot()); });
    connect(&logs, &LogStore::entry, this, [this](const QJsonObject &entry) { broadcast("log", entry); });
    connect(&logs, &LogStore::storageError, this, [this](const QString &error) { broadcast("warning", {{"message", error}}); });
    if (!warning.isEmpty()) logs.append("agent", warning + "\n");
    connect(&server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = server.nextPendingConnection()) {
            if (clients.size() >= 16) { socket->abort(); socket->deleteLater(); continue; }
            socket->setReadBufferSize(Protocol::MaxFrame + 1);
            clients.insert(socket, {});
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] { receive(socket); });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] { clients.remove(socket); socket->deleteLater(); });
        }
    });
    ticker.setInterval(1000);
    connect(&ticker, &QTimer::timeout, this, [this] { broadcast("status", snapshot()); }); ticker.start();
}
bool AgentServer::listen(QString &error) {
    server.setSocketOptions(QLocalServer::UserAccessOption);
    // Caller must hold the per-data-directory QLockFile before stale endpoint removal.
    QLocalServer::removeServer(Protocol::endpoint(directory));
    if (!server.listen(Protocol::endpoint(directory))) { error = server.errorString(); return false; }
    return true;
}
AgentServer::~AgentServer() {
    disconnect(&server, nullptr, this, nullptr);
    for (auto *socket : clients.keys()) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
    }
}
QJsonObject AgentServer::snapshot() const { auto s = manager.status(); s["profile"] = profile.toJson(); return s; }
void AgentServer::broadcast(const QString &event, const QJsonObject &data) {
    const auto sockets = clients.keys();
    for (auto *socket : sockets) Protocol::send(socket, {{"version", 1}, {"event", event}, {"data", data}});
}
void AgentServer::receive(QLocalSocket *socket) {
    QByteArray buffer = clients.value(socket); buffer += socket->readAll();
    while (socket->state() == QLocalSocket::ConnectedState) {
        QJsonObject message; QString error;
        if (!Protocol::take(buffer, message, error)) {
            if (!error.isEmpty()) socket->abort();
            break;
        }
        command(socket, message);
    }
    if (clients.contains(socket)) clients[socket] = buffer;
}
void AgentServer::command(QLocalSocket *socket, const QJsonObject &m) {
    const QString id = m["id"].toString(), cmd = m["command"].toString();
    QString error; QJsonObject data;
    if (id.isEmpty() || id.size() > 128) { socket->abort(); return; }
    if (cmd == "getStatus") data = snapshot();
    else if (cmd == "getLogs") data = {{"entries", logs.snapshot()}};
    else if (cmd == "updateProfile") {
        Profile candidate;
        if (manager.active()) error = "Stop the server before changing its profile";
        else if (!m["profile"].isObject()) error = "Missing profile object";
        else if (Profile::fromJson(m["profile"].toObject(), candidate, error) && store.save(candidate, error)) {
            profile = candidate; data = snapshot(); broadcast("status", data);
        }
    } else if (cmd == "startServer") error = manager.start(profile);
    else if (cmd == "stopServer") error = manager.stop();
    else if (cmd == "restartServer") error = manager.restart(profile);
    else error = "Unknown or unavailable Phase 1 command: " + cmd;
    Protocol::send(socket, {{"version", 1}, {"id", id}, {"ok", error.isEmpty()}, {"error", error}, {"data", data}});
}
