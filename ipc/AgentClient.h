#pragma once
#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
class AgentClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    // True once a status snapshot has been received on the current connection.
    Q_PROPERTY(bool synchronized READ synchronized NOTIFY instancesChanged)
    // Ordered model IDs; changes only when models are added or removed.
    Q_PROPERTY(QStringList instanceIds READ instanceIds NOTIFY instanceIdsChanged)
    // id -> {id, name, state, pid, uptimeSeconds, error, profile, busy, ...}
    Q_PROPERTY(QVariantMap instances READ instances NOTIFY instancesChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY instancesChanged)
    // The model whose console is shown in logText.
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logsChanged)
    // Watchdog restart history of the selected model, newest last.
    Q_PROPERTY(QVariantList restartHistory READ restartHistory NOTIFY restartHistoryChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit AgentClient(QString directory, bool autoLaunch = true, QObject* parent = nullptr);
    ~AgentClient() override;
    bool connected() const { return socket.state() == QLocalSocket::ConnectedState; }
    bool synchronized() const { return synced; }
    QStringList instanceIds() const { return ids; }
    QVariantMap instances() const { return view; }
    QString warning() const { return agentWarning; }
    QString selectedId() const { return selected; }
    void setSelectedId(const QString& id);
    QString logText() const { return logs.value(selected).text; }
    QVariantList restartHistory() const { return history; }
    QString error() const { return lastError; }
    bool busy() const { return !pending.isEmpty(); }
    Q_INVOKABLE void connectAgent();
    Q_INVOKABLE void start(const QString& profileId);
    Q_INVOKABLE void stop(const QString& profileId);
    Q_INVOKABLE void restart(const QString& profileId);
    Q_INVOKABLE void createProfile(const QVariantMap& profile);
    Q_INVOKABLE void saveProfile(const QString& profileId, const QVariantMap& profile);
    Q_INVOKABLE void deleteProfile(const QString& profileId);
    Q_INVOKABLE QString localPath(const QUrl& url) const;
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void clearError() { setError({ }); }
signals:
    void connectedChanged();
    void instancesChanged();
    void instanceIdsChanged();
    void selectedIdChanged();
    void profileChanged(const QString& profileId);
    void profileCreated(const QString& profileId);
    void logsChanged();
    void restartHistoryChanged();
    void errorChanged();
    void busyChanged();

private:
    struct Pending {
        QString command, profileId;
        qint64 time = 0;
    };
    struct Log {
        QString text;
        qint64 sequence = 0;
    };
    void send(const QString& command, const QJsonObject& extra = { });
    void receive();
    void applyStatus(const QJsonObject& data);
    void rebuild();
    void appendLog(const QString& profileId, const QJsonObject& data, bool notify = true);
    void setError(const QString& error);
    QString directory;
    bool autoLaunch;
    QLocalSocket socket;
    QTimer retry, expiry;
    QByteArray buffer;
    bool synced = false;
    QStringList ids;
    QVariantMap raw, view;
    QHash<QString, QVariantMap> profiles;
    QHash<QString, Log> logs;
    QVariantList history;
    QString selected, agentWarning, lastError;
    qint64 lastLaunch = 0;
    QHash<QString, Pending> pending;
};
