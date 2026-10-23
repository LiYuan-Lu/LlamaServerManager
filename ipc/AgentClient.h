#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QTimer>
#include <QVariantMap>
#include <QHash>
#include <QJsonObject>
#include <QUrl>
class AgentClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY profileChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit AgentClient(QString directory, bool autoLaunch = true, QObject *parent = nullptr);
    ~AgentClient() override;
    bool connected() const { return socket.state() == QLocalSocket::ConnectedState; }
    QVariantMap status() const { return state; }
    QVariantMap profile() const { return savedProfile; }
    QString logText() const { return text; }
    QString error() const { return lastError; }
    bool busy() const { return !pending.isEmpty(); }
    Q_INVOKABLE void connectAgent();
    Q_INVOKABLE void request(const QString &command);
    Q_INVOKABLE void saveProfile(const QVariantMap &profile);
    Q_INVOKABLE QString localPath(const QUrl &url) const;
    Q_INVOKABLE void clearLogs();
signals:
    void connectedChanged();
    void statusChanged();
    void profileChanged();
    void logsChanged();
    void errorChanged();
    void busyChanged();
private:
    void send(const QString &command, const QJsonObject &extra = {});
    void receive();
    void applyStatus(const QJsonObject &data);
    void appendLog(const QJsonObject &data);
    void setError(const QString &error);
    QString directory;
    bool autoLaunch;
    QLocalSocket socket;
    QTimer retry, expiry;
    QByteArray buffer;
    QVariantMap state, savedProfile;
    QString text, lastError;
    qint64 lastLaunch = 0, sequence = 0;
    QHash<QString, QPair<QString, qint64>> pending;
};
