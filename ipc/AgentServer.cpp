#include "AgentServer.h"
#include "Protocol.h"
#include <QDir>
#include <QHostAddress>
#include <QLocalSocket>
AgentServer::AgentServer(const QString &dir, QObject *parent)
    : QObject(parent), directory(dir), store(dir) {
    bool changed = false;
    const auto loaded = store.load(loadWarning, &changed);
    for (const auto &p : loaded) add(p);
    if (changed) {
        // Stable IDs are required for per-model log directories; save migrated/assigned IDs once.
        QString error;
        if (!persist(loaded, error)) loadWarning = "Cannot save migrated profiles: " + error;
    }
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
    for (auto *instance : instances) { delete instance->manager; delete instance->logs; delete instance; }
    instances.clear();
}
AgentServer::Instance *AgentServer::find(const QString &id) const {
    for (auto *instance : instances) if (instance->profile.id == id) return instance;
    return nullptr;
}
AgentServer::Instance *AgentServer::add(const Profile &profile) {
    const QString id = profile.id, logDir = directory + "/logs/" + id;
    auto *instance = new Instance{profile, new ServerSupervisor(id, logDir + "/restart-history.json", this), new LogStore(logDir, this)};
    connect(instance->manager, &ServerSupervisor::log, instance->logs, &LogStore::append);
    connect(instance->manager, &ServerSupervisor::changed, this, [this] { broadcast("status", snapshot()); });
    connect(instance->manager, &ServerSupervisor::historyChanged, this, [this, id] {
        if (auto *i = find(id)) broadcast("history", {{"profileId", id}, {"entries", i->manager->history()}});
    });
    connect(instance->logs, &LogStore::entry, this, [this, id](const QJsonObject &entry) {
        auto record = entry; record["profileId"] = id; broadcast("log", record);
    });
    connect(instance->logs, &LogStore::storageError, this, [this, id](const QString &error) {
        broadcast("warning", {{"profileId", id}, {"message", error}});
    });
    instances.append(instance);
    return instance;
}
void AgentServer::remove(Instance *instance) {
    instances.removeOne(instance);
    disconnect(instance->manager, nullptr, this, nullptr);
    disconnect(instance->logs, nullptr, this, nullptr);
    delete instance->manager; delete instance->logs;
    QDir(directory + "/logs/" + instance->profile.id).removeRecursively();
    delete instance;
}
QList<Profile> AgentServer::profiles() const {
    QList<Profile> list;
    for (const auto *instance : instances) list << instance->profile;
    return list;
}
bool AgentServer::persist(const QList<Profile> &list, QString &error) { return store.save(list, error); }
QString AgentServer::portConflict(const Instance *self) const {
    const QHostAddress host(self->profile.host);
    for (const auto *other : instances) {
        if (other == self || !other->manager->active() || other->profile.port != self->profile.port) continue;
        const QHostAddress otherHost(other->profile.host);
        const bool wildcard = host == QHostAddress::AnyIPv4 || host == QHostAddress::AnyIPv6
            || otherHost == QHostAddress::AnyIPv4 || otherHost == QHostAddress::AnyIPv6;
        if (wildcard || host.isEqual(otherHost))
            return QString("Port %1 is already used by model \"%2\"").arg(self->profile.port).arg(other->profile.displayName());
    }
    return {};
}
QJsonObject AgentServer::snapshot() const {
    QJsonArray list;
    for (const auto *instance : instances) {
        auto s = instance->manager->status();
        s["id"] = instance->profile.id; s["name"] = instance->profile.displayName(); s["profile"] = instance->profile.toJson();
        list.append(s);
    }
    return {{"instances", list}, {"warning", loadWarning}};
}
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
    const QString id = m["id"].toString(), cmd = m["command"].toString(), profileId = m["profileId"].toString();
    QString error; QJsonObject data;
    if (id.isEmpty() || id.size() > 128) { socket->abort(); return; }
    const bool needsInstance = cmd == "getLogs" || cmd == "getRestartHistory" || cmd == "updateProfile" || cmd == "deleteProfile"
        || cmd == "startServer" || cmd == "stopServer" || cmd == "restartServer";
    Instance *instance = needsInstance ? find(profileId) : nullptr;
    if (needsInstance && !instance) error = profileId.isEmpty() ? "Missing profileId" : "Unknown profile: " + profileId;
    else if (cmd == "getStatus") data = snapshot();
    else if (cmd == "listProfiles") {
        QJsonArray list; for (const auto &p : profiles()) list.append(p.toJson());
        data = {{"profiles", list}};
    } else if (cmd == "getLogs") data = {{"profileId", profileId}, {"entries", instance->logs->snapshot()}};
    else if (cmd == "getRestartHistory") data = {{"profileId", profileId}, {"entries", instance->manager->history()}};
    else if (cmd == "createProfile") {
        Profile candidate;
        if (!m["profile"].isObject()) error = "Missing profile object";
        else if (Profile::fromJson(m["profile"].toObject(), candidate, error)) {
            candidate.id = Profile::newId();
            if (candidate.name.trimmed().isEmpty()) candidate.name = candidate.displayName();
            auto list = profiles(); list << candidate;
            if (persist(list, error)) {
                add(candidate); data = snapshot(); broadcast("status", data); data["id"] = candidate.id;
            }
        }
    } else if (cmd == "updateProfile") {
        Profile candidate;
        if (instance->manager->active()) error = "Stop the server before changing its profile";
        else if (!m["profile"].isObject()) error = "Missing profile object";
        else if (Profile::fromJson(m["profile"].toObject(), candidate, error)) {
            candidate.id = profileId;
            if (candidate.name.trimmed().isEmpty()) candidate.name = candidate.displayName();
            auto list = profiles(); list[instances.indexOf(instance)] = candidate;
            if (persist(list, error)) { instance->profile = candidate; data = snapshot(); broadcast("status", data); }
        }
    } else if (cmd == "deleteProfile") {
        if (instance->manager->active()) error = "Stop the server before deleting this model";
        else {
            auto list = profiles(); list.removeAt(instances.indexOf(instance));
            if (persist(list, error)) { remove(instance); data = snapshot(); broadcast("status", data); }
        }
    } else if (cmd == "startServer" || cmd == "restartServer") {
        error = instance->manager->active() && cmd == "restartServer" ? QString() : portConflict(instance);
        if (error.isEmpty())
            error = cmd == "startServer" ? instance->manager->start(instance->profile) : instance->manager->restart(instance->profile);
    } else if (cmd == "stopServer") error = instance->manager->stop();
    else error = "Unknown or unavailable command: " + cmd;
    Protocol::send(socket, {{"version", 1}, {"id", id}, {"ok", error.isEmpty()}, {"error", error}, {"data", data}});
}
