#include "AgentServer.h"
#include "Protocol.h"
#include <QDir>
#include <QHostAddress>
#include <QLocalSocket>
AgentServer::AgentServer(const QString& dir, QObject* parent)
    : QObject(parent)
    , directory(dir)
    , store(dir)
    , settingsStore(dir)
{
    bool changed = false;
    const auto loaded = store.load(loadWarning, &changed);
    QString settingsWarning;
    settings = settingsStore.load(settingsWarning);
    if (!settingsWarning.isEmpty()) {
        loadWarning += (loadWarning.isEmpty() ? "" : "; ") + settingsWarning;
    }
    for (const auto& p : loaded) {
        add(p);
    }
    if (changed) {
        // Stable IDs are required for per-model log directories; save migrated/assigned IDs once.
        QString error;
        if (!persist(loaded, error)) {
            loadWarning = "Cannot save migrated profiles: " + error;
        }
    }
    connect(&server, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = server.nextPendingConnection()) {
            if (clients.size() >= 16) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            socket->setReadBufferSize(Protocol::MaxFrame + 1);
            clients.insert(socket, { });
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] { receive(socket); });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] { clients.remove(socket); socket->deleteLater(); });
        }
    });
    ticker.setInterval(1000);
    connect(&ticker, &QTimer::timeout, this, [this] { broadcast("status", snapshot()); });
    ticker.start();
    // LSM_NVML_LIBRARY overrides the NVML location (tests use a fake library).
    metrics = std::make_unique<MetricsCollector>([this] {
        QList<MetricsCollector::Target> targets;
        for (const auto* instance : instances) {
            const auto pid = instance->manager->status()["pid"].toInteger();
            if (pid > 0) {
                targets.append({ instance->profile.id, pid });
            }
        }
        return targets;
    },
        qEnvironmentVariable("LSM_NVML_LIBRARY"));
    metrics->setIntervalSeconds(settings.metricsIntervalSeconds);
    connect(metrics.get(), &MetricsCollector::sampled, this, [this](const QJsonObject& sample) { broadcast("metrics", sample); });
    metrics->start();
}
bool AgentServer::listen(QString& error)
{
    server.setSocketOptions(QLocalServer::UserAccessOption);
    // Caller must hold the per-data-directory QLockFile before stale endpoint removal.
    QLocalServer::removeServer(Protocol::endpoint(directory));
    if (!server.listen(Protocol::endpoint(directory))) {
        error = server.errorString();
        return false;
    }
    return true;
}
AgentServer::~AgentServer()
{
    // Stops the sampling thread before the instances it reads are destroyed.
    metrics.reset();
    disconnect(&server, nullptr, this, nullptr);
    for (auto* socket : clients.keys()) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
    }
    for (auto* instance : instances) {
        delete instance->manager;
        delete instance->logs;
        delete instance;
    }
    instances.clear();
}
AgentServer::Instance* AgentServer::find(const QString& id) const
{
    for (auto* instance : instances) {
        if (instance->profile.id == id) {
            return instance;
        }
    }
    return nullptr;
}
AgentServer::Instance* AgentServer::add(const Profile& profile)
{
    const QString id = profile.id, logDir = directory + "/logs/" + id;
    auto* instance = new Instance { profile, new ServerSupervisor(id, logDir + "/restart-history.json", this), new LogStore(logDir, this) };
    connect(instance->manager, &ServerSupervisor::log, instance->logs, &LogStore::append);
    connect(instance->manager, &ServerSupervisor::changed, this, [this] { broadcast("status", snapshot()); });
    connect(instance->manager, &ServerSupervisor::historyChanged, this, [this, id] {
        if (auto* i = find(id)) {
            broadcast("history", { { "profileId", id }, { "entries", i->manager->history() } });
        }
    });
    connect(instance->logs, &LogStore::entry, this, [this, id](const QJsonObject& entry) {
        auto record = entry;
        record["profileId"] = id;
        broadcast("log", record);
    });
    connect(instance->logs, &LogStore::storageError, this, [this, id](const QString& error) {
        broadcast("warning", { { "profileId", id }, { "message", error } });
    });
    instances.append(instance);
    return instance;
}
void AgentServer::remove(Instance* instance)
{
    instances.removeOne(instance);
    disconnect(instance->manager, nullptr, this, nullptr);
    disconnect(instance->logs, nullptr, this, nullptr);
    delete instance->manager;
    delete instance->logs;
    QDir(directory + "/logs/" + instance->profile.id).removeRecursively();
    delete instance;
}
QList<Profile> AgentServer::profiles() const
{
    QList<Profile> list;
    for (const auto* instance : instances) {
        list << instance->profile;
    }
    return list;
}
bool AgentServer::persist(const QList<Profile>& list, QString& error) { return store.save(list, error); }
QString AgentServer::portConflict(const Instance* self) const
{
    const QHostAddress host(self->profile.host);
    for (const auto* other : instances) {
        if (other == self || !other->manager->active() || other->profile.port != self->profile.port) {
            continue;
        }
        const QHostAddress otherHost(other->profile.host);
        const bool wildcard = host == QHostAddress::AnyIPv4 || host == QHostAddress::AnyIPv6
            || otherHost == QHostAddress::AnyIPv4 || otherHost == QHostAddress::AnyIPv6;
        if (wildcard || host.isEqual(otherHost)) {
            return QString("Port %1 is already used by model \"%2\"").arg(self->profile.port).arg(other->profile.displayName());
        }
    }
    return { };
}
QJsonObject AgentServer::snapshot() const
{
    QJsonArray list;
    for (const auto* instance : instances) {
        auto s = instance->manager->status();
        s["id"] = instance->profile.id;
        s["name"] = instance->profile.displayName();
        s["profile"] = instance->profile.toJson();
        list.append(s);
    }
    return { { "instances", list }, { "warning", loadWarning } };
}
void AgentServer::broadcast(const QString& event, const QJsonObject& data)
{
    const auto sockets = clients.keys();
    for (auto* socket : sockets) {
        Protocol::send(socket, { { "version", 1 }, { "event", event }, { "data", data } });
    }
}
void AgentServer::receive(QLocalSocket* socket)
{
    QByteArray buffer = clients.value(socket);
    buffer += socket->readAll();
    while (socket->state() == QLocalSocket::ConnectedState) {
        QJsonObject message;
        QString error;
        if (!Protocol::take(buffer, message, error)) {
            if (!error.isEmpty()) {
                socket->abort();
            }
            break;
        }
        command(socket, message);
    }
    if (clients.contains(socket)) {
        clients[socket] = buffer;
    }
}
const QHash<QString, AgentServer::CommandSpec>& AgentServer::commands()
{
    static const QHash<QString, CommandSpec> table = {
        { "getStatus", { Command::GetStatus, false } },
        { "listProfiles", { Command::ListProfiles, false } },
        { "getLogs", { Command::GetLogs, true } },
        { "getRestartHistory", { Command::GetRestartHistory, true } },
        { "createProfile", { Command::CreateProfile, false } },
        { "updateProfile", { Command::UpdateProfile, true } },
        { "deleteProfile", { Command::DeleteProfile, true } },
        { "startServer", { Command::StartServer, true } },
        { "stopServer", { Command::StopServer, true } },
        { "restartServer", { Command::RestartServer, true } },
        { "getMetrics", { Command::GetMetrics, false } },
        { "getSettings", { Command::GetSettings, false } },
        { "updateSettings", { Command::UpdateSettings, false } },
    };
    return table;
}
void AgentServer::command(QLocalSocket* socket, const QJsonObject& m)
{
    const QString id = m["id"].toString();
    if (id.isEmpty() || id.size() > 128) {
        socket->abort();
        return;
    }
    const auto reply = execute(m);
    Protocol::send(socket, { { "version", 1 }, { "id", id }, { "ok", reply.error.isEmpty() }, { "error", reply.error }, { "data", reply.data } });
}
AgentServer::Reply AgentServer::execute(const QJsonObject& m)
{
    const QString name = m["command"].toString();
    const auto spec = commands().constFind(name);
    if (spec == commands().cend()) {
        return Reply::failure("Unknown or unavailable command: " + name);
    }
    Instance* instance = nullptr;
    if (spec->targetsProfile) {
        const QString profileId = m["profileId"].toString();
        instance = find(profileId);
        if (!instance) {
            return Reply::failure(profileId.isEmpty() ? "Missing profileId" : "Unknown profile: " + profileId);
        }
    }
    switch (spec->command) {
    case Command::GetStatus:
        return Reply::success(snapshot());
    case Command::ListProfiles:
        return listProfiles();
    case Command::GetLogs:
        return Reply::success({ { "profileId", instance->profile.id }, { "entries", instance->logs->snapshot() } });
    case Command::GetRestartHistory:
        return Reply::success({ { "profileId", instance->profile.id }, { "entries", instance->manager->history() } });
    case Command::CreateProfile:
        return createProfile(m);
    case Command::UpdateProfile:
        return updateProfile(instance, m);
    case Command::DeleteProfile:
        return deleteProfile(instance);
    case Command::StartServer:
        return startServer(instance, false);
    case Command::RestartServer:
        return startServer(instance, true);
    case Command::StopServer:
        return Reply { instance->manager->stop(), { } };
    case Command::GetMetrics:
        return getMetrics();
    case Command::GetSettings:
        return Reply::success({ { "settings", settings.toJson() } });
    case Command::UpdateSettings:
        return updateSettings(m);
    }
    return Reply::failure("Unhandled command: " + name); // every enum value is handled above
}
QJsonObject AgentServer::publishSnapshot()
{
    const auto data = snapshot();
    broadcast("status", data);
    return data;
}
bool AgentServer::parseProfile(const QJsonObject& m, Profile& profile, QString& error)
{
    if (!m["profile"].isObject()) {
        error = "Missing profile object";
        return false;
    }
    if (!Profile::fromJson(m["profile"].toObject(), profile, error)) {
        return false;
    }
    if (profile.name.trimmed().isEmpty()) {
        profile.name = profile.displayName();
    }
    return true;
}
AgentServer::Reply AgentServer::listProfiles() const
{
    QJsonArray list;
    for (const auto& p : profiles()) {
        list.append(p.toJson());
    }
    return Reply::success({ { "profiles", list } });
}
AgentServer::Reply AgentServer::createProfile(const QJsonObject& m)
{
    Profile candidate;
    QString error;
    if (!parseProfile(m, candidate, error)) {
        return Reply::failure(error);
    }
    candidate.id = Profile::newId(); // client-supplied IDs are ignored
    auto list = profiles();
    list << candidate;
    if (!persist(list, error)) {
        return Reply::failure(error);
    }
    add(candidate);
    auto data = publishSnapshot();
    data["id"] = candidate.id;
    return Reply::success(data);
}
AgentServer::Reply AgentServer::updateProfile(Instance* instance, const QJsonObject& m)
{
    if (instance->manager->active()) {
        return Reply::failure("Stop the server before changing its profile");
    }
    Profile candidate;
    QString error;
    if (!parseProfile(m, candidate, error)) {
        return Reply::failure(error);
    }
    candidate.id = instance->profile.id;
    auto list = profiles();
    list[instances.indexOf(instance)] = candidate;
    if (!persist(list, error)) {
        return Reply::failure(error);
    }
    instance->profile = candidate;
    return Reply::success(publishSnapshot());
}
AgentServer::Reply AgentServer::deleteProfile(Instance* instance)
{
    if (instance->manager->active()) {
        return Reply::failure("Stop the server before deleting this model");
    }
    QString error;
    auto list = profiles();
    list.removeAt(instances.indexOf(instance));
    if (!persist(list, error)) {
        return Reply::failure(error);
    }
    remove(instance);
    return Reply::success(publishSnapshot());
}
AgentServer::Reply AgentServer::startServer(Instance* instance, bool restart)
{
    // Restarting an active model keeps its own port, so only a fresh start can conflict.
    if (!restart || !instance->manager->active()) {
        if (const auto conflict = portConflict(instance); !conflict.isEmpty()) {
            return Reply::failure(conflict);
        }
    }
    auto* supervisor = instance->manager;
    return Reply { restart ? supervisor->restart(instance->profile) : supervisor->start(instance->profile), { } };
}
AgentServer::Reply AgentServer::getMetrics() const
{
    return Reply::success({
        { "intervalSeconds", metrics->intervalSeconds() },
        { "historySeconds", MetricsCollector::HistorySeconds },
        { "latest", metrics->latest() },
        { "history", metrics->history() },
    });
}
AgentServer::Reply AgentServer::updateSettings(const QJsonObject& m)
{
    if (!m["settings"].isObject()) {
        return Reply::failure("Missing settings object");
    }
    // Fields that are not sent keep their current values.
    auto merged = settings.toJson();
    const auto changes = m["settings"].toObject();
    for (auto i = changes.begin(); i != changes.end(); ++i) {
        if (!merged.contains(i.key())) {
            return Reply::failure("Unknown setting: " + i.key());
        }
        merged[i.key()] = i.value();
    }
    AppSettings candidate;
    QString error;
    if (!AppSettings::fromJson(merged, candidate, error) || !settingsStore.save(candidate, error)) {
        return Reply::failure(error);
    }
    settings = candidate;
    metrics->setIntervalSeconds(settings.metricsIntervalSeconds);
    const QJsonObject data { { "settings", settings.toJson() } };
    broadcast("settings", data);
    return Reply::success(data);
}
