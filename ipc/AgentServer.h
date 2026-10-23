#pragma once
#include "core/ServerSupervisor.h"
#include "core/ProfileStore.h"
#include "core/LogStore.h"
#include <QLocalServer>
#include <QHash>
#include <QList>
class AgentServer : public QObject {
    Q_OBJECT
public:
    explicit AgentServer(const QString &directory, QObject *parent = nullptr);
    ~AgentServer() override;
    bool listen(QString &error);
private:
    // One managed llama-server per profile; each has its own watchdog supervisor and log.
    struct Instance {
        Profile profile;
        ServerSupervisor *manager = nullptr;
        LogStore *logs = nullptr;
    };
    void receive(QLocalSocket *socket);
    void command(QLocalSocket *socket, const QJsonObject &message);
    void broadcast(const QString &event, const QJsonObject &data);
    QJsonObject snapshot() const;
    Instance *find(const QString &id) const;
    Instance *add(const Profile &profile);
    void remove(Instance *instance);
    bool persist(const QList<Profile> &profiles, QString &error);
    QList<Profile> profiles() const;
    QString portConflict(const Instance *instance) const;
    QString directory;
    QLocalServer server;
    QHash<QLocalSocket *, QByteArray> clients;
    ProfileStore store;
    QList<Instance *> instances;
    QString loadWarning;
    QTimer ticker;
};
