#include "AgentClient.h"
#include "Protocol.h"
#include "core/MetricsCollector.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QProcess>
#include <QUrl>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
AgentClient::AgentClient(QString dir, bool launch, QObject* parent)
    : QObject(parent)
    , directory(dir)
    , autoLaunch(launch)
{
    socket.setReadBufferSize(Protocol::MaxFrame + 1);
    retry.setInterval(1500);
    connect(&retry, &QTimer::timeout, this, &AgentClient::connectAgent);
    retry.start();
    connect(&socket, &QLocalSocket::connected, this, [this] {
        setStopped(false); // another GUI or the user started the Agent again
        buffer.clear();
        logs.clear();
        console.reset({ });
        synced = false;
        setError({ });
        emit connectedChanged();
        send("getStatus");
        send("getMetrics");
        if (!selected.isEmpty()) {
            send("getLogs", { { "profileId", selected } });
            send("getRestartHistory", { { "profileId", selected } });
        }
    });
    connect(&socket, &QLocalSocket::disconnected, this, [this] {
        pending.clear();
        buffer.clear();
        synced = false;
        clearMetrics();
        rebuild();
        emit busyChanged();
        emit connectedChanged();
        setError(stoppedByUser ? QString() : QString("Agent disconnected; displayed status is stale. Reconnecting..."));
    });
    connect(&socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (stoppedByUser) {
            return; // keep quietly retrying the connection, but never relaunch on our own
        }
        setError(socket.errorString());
        if (autoLaunch && QDateTime::currentMSecsSinceEpoch() - lastLaunch > 10000) {
            startAgentProcess();
        }
    });
    connect(&socket, &QLocalSocket::readyRead, this, &AgentClient::receive);
    expiry.setInterval(1000);
    connect(&expiry, &QTimer::timeout, this, [this] {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        bool changed = false;
        for (auto i = pending.begin(); i != pending.end();) {
            if (now - i->time > 15000) {
                setError("Request timed out: " + i->command + ". State will be resynchronized; command is not retried.");
                i = pending.erase(i);
                changed = true;
            } else {
                ++i;
            }
        }
        if (changed) {
            rebuild();
            emit busyChanged();
        }
    });
    expiry.start();
}
void AgentClient::setError(const QString& e)
{
    if (lastError == e) {
        return;
    }
    lastError = e;
    emit errorChanged();
}
AgentClient::~AgentClient()
{
    // QLocalSocket destruction can emit disconnected after later members died.
    disconnect(&socket, nullptr, this, nullptr);
    socket.abort();
}
void AgentClient::connectAgent()
{
    if (socket.state() == QLocalSocket::UnconnectedState) {
        socket.connectToServer(Protocol::endpoint(directory));
    }
}
void AgentClient::send(const QString& command, const QJsonObject& extra)
{
    if (!connected()) {
        setError("Agent is not connected");
        return;
    }
    if (pending.size() >= 32) {
        setError("Too many pending requests");
        return;
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject m = extra;
    m["version"] = 1;
    m["id"] = id;
    m["command"] = command;
    pending[id] = { command, extra["profileId"].toString(), QDateTime::currentMSecsSinceEpoch() };
    rebuild();
    emit busyChanged();
    if (!Protocol::send(&socket, m)) {
        pending.remove(id);
        rebuild();
        emit busyChanged();
        setError("Could not send request");
    }
}
void AgentClient::start(const QString& id)
{
    setError({ });
    send("startServer", { { "profileId", id } });
}
void AgentClient::stop(const QString& id)
{
    setError({ });
    send("stopServer", { { "profileId", id } });
}
void AgentClient::restart(const QString& id)
{
    setError({ });
    send("restartServer", { { "profileId", id } });
}
void AgentClient::createProfile(const QVariantMap& p)
{
    setError({ });
    send("createProfile", { { "profile", QJsonObject::fromVariantMap(p) } });
}
void AgentClient::saveProfile(const QString& id, const QVariantMap& p)
{
    setError({ });
    send("updateProfile", { { "profileId", id }, { "profile", QJsonObject::fromVariantMap(p) } });
}
void AgentClient::deleteProfile(const QString& id)
{
    setError({ });
    send("deleteProfile", { { "profileId", id } });
}
QString AgentClient::localPath(const QUrl& url) const { return url.toLocalFile(); }
void AgentClient::clearLogs()
{
    if (logs.contains(selected)) {
        logs[selected].lines.clear();
    }
    console.reset({ });
}
void AgentClient::setMetricsInterval(int seconds)
{
    setError({ });
    send("updateSettings", { { "settings", QJsonObject { { "metricsIntervalSeconds", seconds } } } });
}
void AgentClient::startAgentProcess()
{
    lastLaunch = QDateTime::currentMSecsSinceEpoch();
#ifdef Q_OS_WIN
    const QString executable = QCoreApplication::applicationDirPath() + "/llama-manager-agent.exe";
#else
    const QString executable = QCoreApplication::applicationDirPath() + "/llama-manager-agent";
#endif
    QProcess detached;
    detached.setProgram(executable);
    detached.setArguments({ "--data-dir", directory });
    detached.setWorkingDirectory(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    detached.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags &= ~CREATE_NEW_CONSOLE;
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    if (!detached.startDetached()) {
        setError("Cannot launch Agent: " + executable);
    }
}
void AgentClient::setStopped(bool stopped)
{
    if (stoppedByUser == stopped) {
        return;
    }
    stoppedByUser = stopped;
    emit agentStoppedChanged();
}
void AgentClient::shutdownAgent()
{
    setError({ });
    send("shutdownAgent");
}
void AgentClient::launchAgent()
{
    setStopped(false);
    setError({ });
    if (!connected()) {
        startAgentProcess();
        connectAgent();
    }
}
void AgentClient::clearMetrics()
{
    latestMetrics.clear();
    history5m.clear();
    emit metricsChanged();
}
void AgentClient::applyMetrics(const QJsonObject& sample)
{
    latestMetrics = sample.toVariantMap();
    history5m.append(MetricsCollector::compact(sample).toVariantMap());
    MetricsCollector::prune(history5m, sample["timeMs"].toInteger(), [](const QVariant& v) { return v.toMap().value("time").toLongLong(); });
    const int next = sample["intervalSeconds"].toInt();
    if (next > 0 && next != interval) {
        interval = next;
        emit metricsIntervalChanged();
    }
    emit metricsChanged();
}
void AgentClient::resetMetrics(const QJsonObject& data)
{
    history5m = data["history"].toArray().toVariantList();
    latestMetrics = data["latest"].toObject().toVariantMap();
    const int next = data["intervalSeconds"].toInt();
    if (next > 0 && next != interval) {
        interval = next;
        emit metricsIntervalChanged();
    }
    emit metricsChanged();
}
void AgentClient::setSelectedId(const QString& id)
{
    if (selected == id) {
        return;
    }
    selected = id;
    history.clear();
    console.reset(logs.value(id).lines);
    emit selectedIdChanged();
    emit restartHistoryChanged();
    if (!id.isEmpty() && connected() && ids.contains(id)) {
        send("getLogs", { { "profileId", id } });
        send("getRestartHistory", { { "profileId", id } });
    }
}
void AgentClient::rebuild()
{
    view.clear();
    for (auto i = raw.begin(); i != raw.end(); ++i) {
        auto entry = i.value().toMap();
        bool busy = false;
        for (const auto& p : pending) {
            if (p.profileId == i.key()) {
                busy = true;
                break;
            }
        }
        entry["busy"] = busy;
        view.insert(i.key(), entry);
    }
    emit instancesChanged();
}
void AgentClient::applyStatus(const QJsonObject& data)
{
    QStringList nextIds;
    QVariantMap next;
    QStringList changedProfiles;
    for (const auto& value : data["instances"].toArray()) {
        const auto o = value.toObject();
        const auto id = o["id"].toString();
        if (id.isEmpty()) {
            continue;
        }
        nextIds << id;
        next.insert(id, o.toVariantMap());
        const auto p = o["profile"].toObject().toVariantMap();
        if (profiles.value(id) != p) {
            profiles[id] = p;
            changedProfiles << id;
        }
    }
    for (const auto& id : ids) {
        if (!nextIds.contains(id)) {
            profiles.remove(id);
            logs.remove(id);
            if (id == selected) {
                console.reset({ });
            }
        }
    }
    raw = next;
    agentWarning = data["warning"].toString();
    synced = true;
    const bool idsChanged = nextIds != ids;
    ids = nextIds;
    rebuild();
    if (idsChanged) {
        emit instanceIdsChanged();
    }
    for (const auto& id : changedProfiles) {
        emit profileChanged(id);
    }
}
void AgentClient::appendLog(const QString& profileId, const QJsonObject& record, bool notify)
{
    auto& log = logs[profileId];
    const auto n = record["sequence"].toInteger();
    if (n <= log.sequence) {
        return;
    }
    log.sequence = n;
    const auto lines = LogModel::parse(record);
    log.lines += lines;
    if (log.lines.size() > LogModel::MaxLines) {
        log.lines.remove(0, log.lines.size() - LogModel::MaxLines);
    }
    if (notify && profileId == selected) {
        console.append(lines);
    }
}
void AgentClient::receive()
{
    buffer += socket.readAll();
    while (connected()) {
        QJsonObject m;
        QString error;
        if (!Protocol::take(buffer, m, error)) {
            if (!error.isEmpty()) {
                setError(error);
                socket.abort();
            }
            return;
        }
        const auto data = m["data"].toObject();
        if (m.contains("event")) {
            const auto event = m["event"].toString();
            if (event == "status") {
                applyStatus(data);
            } else if (event == "log") {
                appendLog(data["profileId"].toString(), data);
            } else if (event == "metrics") {
                applyMetrics(data);
            } else if (event == "settings") {
                const int next = data["settings"].toObject()["metricsIntervalSeconds"].toInt();
                if (next > 0 && next != interval) {
                    interval = next;
                    emit metricsIntervalChanged();
                }
            } else if (event == "shutdown") {
                setStopped(true);
            } else if (event == "warning") {
                setError(data["message"].toString());
            } else if (event == "history" && data["profileId"].toString() == selected) {
                history = data["entries"].toArray().toVariantList();
                emit restartHistoryChanged();
            }
        } else {
            const auto id = m["id"].toString();
            if (!pending.contains(id)) {
                continue;
            }
            const auto request = pending.take(id);
            rebuild();
            emit busyChanged();
            if (!m["ok"].toBool()) {
                setError(m["error"].toString());
                continue;
            }
            if (data.contains("instances")) {
                applyStatus(data);
            }
            if (request.command == "updateProfile") {
                emit profileChanged(request.profileId);
            } else if (request.command == "createProfile") {
                emit profileCreated(data["id"].toString());
            } else if (request.command == "getRestartHistory") {
                if (data["profileId"].toString() == selected) {
                    history = data["entries"].toArray().toVariantList();
                    emit restartHistoryChanged();
                }
            } else if (request.command == "getLogs") {
                const auto profileId = data["profileId"].toString();
                logs[profileId] = { };
                for (const auto& entry : data["entries"].toArray()) {
                    appendLog(profileId, entry.toObject(), false);
                }
                if (profileId == selected) {
                    console.reset(logs.value(profileId).lines);
                }
            } else if (request.command == "getMetrics") {
                resetMetrics(data);
            }
        }
    }
}
