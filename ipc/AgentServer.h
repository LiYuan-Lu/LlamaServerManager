#pragma once
#include "core/AppSettings.h"
#include "core/LogStore.h"
#include "core/MetricsCollector.h"
#include "core/ProfileStore.h"
#include "core/ServerSupervisor.h"
#include <QHash>
#include <QList>
#include <QLocalServer>
#include <memory>
class AgentServer : public QObject {
    Q_OBJECT
public:
    explicit AgentServer(const QString& directory, QObject* parent = nullptr);
    ~AgentServer() override;
    bool listen(QString& error);

private:
    // One managed llama-server per profile; each has its own watchdog supervisor and log.
    struct Instance {
        Profile profile;
        ServerSupervisor* manager = nullptr;
        LogStore* logs = nullptr;
    };
    enum class Command {
        GetStatus,
        ListProfiles,
        GetLogs,
        GetRestartHistory,
        CreateProfile,
        UpdateProfile,
        DeleteProfile,
        StartServer,
        StopServer,
        RestartServer,
        GetMetrics,
        GetSettings,
        UpdateSettings,
    };
    struct CommandSpec {
        Command command;
        bool targetsProfile; // requires a valid "profileId"
    };
    struct Reply {
        QString error;
        QJsonObject data;
        static Reply success(const QJsonObject& data = { }) { return { { }, data }; }
        static Reply failure(const QString& error) { return { error, { } }; }
    };
    static const QHash<QString, CommandSpec>& commands();
    void receive(QLocalSocket* socket);
    void command(QLocalSocket* socket, const QJsonObject& message);
    Reply execute(const QJsonObject& message);
    Reply listProfiles() const;
    Reply createProfile(const QJsonObject& message);
    Reply updateProfile(Instance* instance, const QJsonObject& message);
    Reply deleteProfile(Instance* instance);
    Reply startServer(Instance* instance, bool restart);
    Reply getMetrics() const;
    Reply updateSettings(const QJsonObject& message);
    static bool parseProfile(const QJsonObject& message, Profile& profile, QString& error);
    // Broadcasts the new state after a profile change and returns it for the reply.
    QJsonObject publishSnapshot();
    void broadcast(const QString& event, const QJsonObject& data);
    QJsonObject snapshot() const;
    Instance* find(const QString& id) const;
    Instance* add(const Profile& profile);
    void remove(Instance* instance);
    bool persist(const QList<Profile>& profiles, QString& error);
    QList<Profile> profiles() const;
    QString portConflict(const Instance* instance) const;
    QString directory;
    QLocalServer server;
    QHash<QLocalSocket*, QByteArray> clients;
    ProfileStore store;
    QList<Instance*> instances;
    QString loadWarning;
    QTimer ticker;
    SettingsStore settingsStore;
    AppSettings settings;
    std::unique_ptr<MetricsCollector> metrics;
};
