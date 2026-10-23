#pragma once
#include "core/LlamaProcessManager.h"
#include "core/ProfileStore.h"
#include "core/LogStore.h"
#include <QLocalServer>
#include <QHash>
class AgentServer : public QObject {
    Q_OBJECT
public:
    explicit AgentServer(const QString &directory, QObject *parent = nullptr);
    ~AgentServer() override;
    bool listen(QString &error);
private:
    void receive(QLocalSocket *socket);
    void command(QLocalSocket *socket, const QJsonObject &message);
    void broadcast(const QString &event, const QJsonObject &data);
    QJsonObject snapshot() const;
    QString directory;
    QLocalServer server;
    QHash<QLocalSocket *, QByteArray> clients;
    ProfileStore store;
    Profile profile;
    LlamaProcessManager manager;
    LogStore logs;
    QTimer ticker;
};
