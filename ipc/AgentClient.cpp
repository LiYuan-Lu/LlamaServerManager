#include "AgentClient.h"
#include "Protocol.h"
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#include <QJsonArray>
#include <QUrl>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
AgentClient::AgentClient(QString dir, bool launch, QObject *parent) : QObject(parent), directory(dir), autoLaunch(launch) {
    socket.setReadBufferSize(Protocol::MaxFrame + 1);
    retry.setInterval(1500);
    connect(&retry, &QTimer::timeout, this, &AgentClient::connectAgent); retry.start();
    connect(&socket, &QLocalSocket::connected, this, [this] {
        buffer.clear(); sequence = 0; text.clear(); emit logsChanged(); setError({}); emit connectedChanged();
        send("getStatus"); send("getLogs");
    });
    connect(&socket, &QLocalSocket::disconnected, this, [this] {
        pending.clear(); buffer.clear(); emit busyChanged(); emit connectedChanged();
        setError("Agent disconnected; displayed status is stale. Reconnecting...");
    });
    connect(&socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        setError(socket.errorString());
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (autoLaunch && now - lastLaunch > 10000) {
            lastLaunch = now;
            const QString executable = QCoreApplication::applicationDirPath() + "/llama-manager-agent.exe";
            QProcess detached;
            detached.setProgram(executable);
            detached.setArguments({"--data-dir", directory});
            detached.setWorkingDirectory(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
            detached.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
                args->flags &= ~CREATE_NEW_CONSOLE;
                args->flags |= CREATE_NO_WINDOW;
            });
#endif
            if (!detached.startDetached())
                setError("Cannot launch Agent: " + executable);
        }
    });
    connect(&socket, &QLocalSocket::readyRead, this, &AgentClient::receive);
    expiry.setInterval(1000);
    connect(&expiry, &QTimer::timeout, this, [this] {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        for (auto i = pending.begin(); i != pending.end();) {
            if (now - i->second > 15000) { setError("Request timed out: " + i->first + ". State will be resynchronized; command is not retried."); i = pending.erase(i); emit busyChanged(); }
            else ++i;
        }
    }); expiry.start();
}
void AgentClient::setError(const QString &e) { lastError = e; emit errorChanged(); }
AgentClient::~AgentClient() {
    // QLocalSocket destruction can emit disconnected after later members died.
    disconnect(&socket, nullptr, this, nullptr);
    socket.abort();
}
void AgentClient::connectAgent() {
    if (socket.state() == QLocalSocket::UnconnectedState) socket.connectToServer(Protocol::endpoint(directory));
}
void AgentClient::send(const QString &command, const QJsonObject &extra) {
    if (!connected()) { setError("Agent is not connected"); return; }
    if (pending.size() >= 32) { setError("Too many pending requests"); return; }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject m = extra; m["version"] = 1; m["id"] = id; m["command"] = command;
    pending[id] = {command, QDateTime::currentMSecsSinceEpoch()}; emit busyChanged();
    if (!Protocol::send(&socket, m)) { pending.remove(id); emit busyChanged(); setError("Could not send request"); }
}
void AgentClient::request(const QString &command) { setError({}); send(command); }
void AgentClient::saveProfile(const QVariantMap &p) { setError({}); send("updateProfile", {{"profile", QJsonObject::fromVariantMap(p)}}); }
QString AgentClient::localPath(const QUrl &url) const { return url.toLocalFile(); }
void AgentClient::clearLogs() { text.clear(); emit logsChanged(); }
void AgentClient::applyStatus(const QJsonObject &data) {
    state = data.toVariantMap(); emit statusChanged();
    const auto p = data["profile"].toObject().toVariantMap();
    if (p != savedProfile) { savedProfile = p; emit profileChanged(); }
}
void AgentClient::appendLog(const QJsonObject &record) {
    const auto n = record["sequence"].toInteger();
    if (n <= sequence) return;
    sequence = n;
    text += QString("[%1] [%2] %3").arg(record["time"].toString(), record["channel"].toString(), record["text"].toString());
    if (!text.endsWith('\n')) text += '\n';
    if (text.size() > 256 * 1024) text = text.right(192 * 1024);
    emit logsChanged();
}
void AgentClient::receive() {
    buffer += socket.readAll();
    while (connected()) {
        QJsonObject m; QString error;
        if (!Protocol::take(buffer, m, error)) { if (!error.isEmpty()) { setError(error); socket.abort(); } return; }
        const auto data = m["data"].toObject();
        if (m.contains("event")) {
            const auto event = m["event"].toString();
            if (event == "status") applyStatus(data);
            else if (event == "log") appendLog(data);
            else if (event == "warning") setError(data["message"].toString());
        } else {
            const auto id = m["id"].toString();
            if (!pending.contains(id)) continue;
            const auto command = pending.take(id).first; emit busyChanged();
            if (!m["ok"].toBool()) { setError(m["error"].toString()); continue; }
            if (command == "getStatus" || command == "updateProfile") {
                applyStatus(data);
                if (command == "updateProfile") emit profileChanged();
            }
            else if (command == "getLogs") {
                text.clear(); sequence = 0;
                for (const auto &entry : data["entries"].toArray()) appendLog(entry.toObject());
                emit logsChanged();
            }
        }
    }
}
