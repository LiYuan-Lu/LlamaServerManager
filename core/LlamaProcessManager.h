#pragma once
#include "Profile.h"
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QStringConverter>
#include <QTimer>

class LlamaProcessManager : public QObject {
    Q_OBJECT
public:
    explicit LlamaProcessManager(QObject* parent = nullptr);
    ~LlamaProcessManager() override;
    QString start(const Profile& profile);
    QString stop();
    QString restart(const Profile& profile);
    QJsonObject status() const;
    bool active() const { return state == "STARTING" || state == "RUNNING" || state == "STOPPING"; }
signals:
    void changed();
    void log(const QString& channel, const QString& text);

private:
    void setState(const QString& value, const QString& error = { });
    void launch();
    void drain(bool stderrChannel);
    QProcess process, probe;
    QTimer stopTimer, probeTimer, startupTimer;
    QElapsedTimer uptime;
    Profile current, restartProfile;
    QString state = "STOPPED", lastError;
    QByteArray help;
    bool manualStop = false, restartPending = false, probeFailed = false;
    QStringDecoder stdoutDecoder { QStringDecoder::Utf8 }, stderrDecoder { QStringDecoder::Utf8 };
    void* job = nullptr;
};
