#include "LlamaProcessManager.h"
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpServer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

LlamaProcessManager::LlamaProcessManager(QObject* parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    // Loader/crash dialogs must never block an unattended Agent.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    stopTimer.setSingleShot(true);
    stopTimer.setInterval(3000);
    probeTimer.setSingleShot(true);
    probeTimer.setInterval(10000);
    startupTimer.setSingleShot(true);
    startupTimer.setInterval(10000);
    connect(&startupTimer, &QTimer::timeout, this, [this] { lastError = "Process launch timed out"; process.kill(); });
    connect(&stopTimer, &QTimer::timeout, this, [this] {
        emit log("agent", "Stop timeout: force terminating owned process\n");
#ifdef Q_OS_WIN
        if (job) {
            TerminateJobObject(static_cast<HANDLE>(job), 1);
        }
#endif
        process.kill();
    });
    connect(&probeTimer, &QTimer::timeout, this, [this] { probeFailed = true; lastError = "--help timed out after 10 seconds"; probe.kill(); });
    auto readHelp = [this] {
        help += probe.readAllStandardOutput();
        help += probe.readAllStandardError();
        if (help.size() > 1024 * 1024) {
            probeFailed = true;
            lastError = "--help output exceeds 1 MiB";
            probe.kill();
        }
    };
    connect(&probe, &QProcess::readyReadStandardOutput, this, readHelp);
    connect(&probe, &QProcess::readyReadStandardError, this, readHelp);
    connect(&probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            probeTimer.stop();
            setState("FAILED", "Cannot inspect executable: " + probe.errorString());
        }
    });
    connect(&probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, readHelp](int code, QProcess::ExitStatus exit) {
        probeTimer.stop();
        readHelp();
        if (manualStop) {
            setState("STOPPED");
            return;
        }
        if (probeFailed || code != 0 || exit != QProcess::NormalExit) {
            setState("FAILED", lastError.isEmpty() ? "Executable --help failed" : lastError);
            return;
        }
        const QString output = QString::fromUtf8(help);
        QStringList required { "-m", "-c", "-ngl", "-t", "--host", "--port", "--parallel" };
        if (current.metrics) {
            required << "--metrics";
        }
        if (current.aliasEnabled) {
            required << "--alias";
        }
        for (const auto& flag : required) {
            QRegularExpression re("(?<![\\w-])" + QRegularExpression::escape(flag) + "(?![\\w-])");
            if (!output.contains(re)) {
                setState("FAILED", "Executable --help does not advertise required option: " + flag);
                return;
            }
        }
        launch();
    });
    connect(&process, &QProcess::started, this, [this] {
        startupTimer.stop();
        uptime.start();
#ifdef Q_OS_WIN
        HANDLE handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process.processId()));
        if (job && (!handle || !AssignProcessToJobObject(static_cast<HANDLE>(job), handle))) {
            emit log("agent", "Job assignment unavailable; fallback manages direct child only\n");
        }
        if (handle) {
            CloseHandle(handle);
        }
#endif
        if (manualStop) {
            process.terminate();
            stopTimer.start();
        } else {
            setState("RUNNING");
        }
    });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] { drain(false); });
    connect(&process, &QProcess::readyReadStandardError, this, [this] { drain(true); });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            startupTimer.stop();
            stopTimer.stop();
            setState("FAILED", process.errorString());
        }
    });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
        startupTimer.stop();
        stopTimer.stop();
        drain(false);
        drain(true);
#ifdef Q_OS_WIN
        if (job) {
            CloseHandle(static_cast<HANDLE>(job));
            job = nullptr;
        }
#endif
        const bool again = restartPending;
        restartPending = false;
        if (manualStop) {
            setState("STOPPED");
        } else {
            setState("FAILED", QString("Process exited unexpectedly (code %1, %2)").arg(code).arg(exit == QProcess::CrashExit ? "crash" : "normal exit"));
        }
        if (again) {
            const auto error = start(restartProfile);
            if (!error.isEmpty()) {
                setState("FAILED", error);
            }
        }
    });
}
LlamaProcessManager::~LlamaProcessManager()
{
    // Only Agent destruction owns this shutdown; GUI never owns this object.
    restartPending = false;
    disconnect(&probe, nullptr, this, nullptr);
    disconnect(&process, nullptr, this, nullptr);
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished(3000);
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
#ifdef Q_OS_WIN
    if (job) {
        CloseHandle(static_cast<HANDLE>(job));
    }
#endif
}
void LlamaProcessManager::setState(const QString& value, const QString& error)
{
    state = value;
    lastError = error;
    emit log("agent", value + (error.isEmpty() ? "\n" : ": " + error + "\n"));
    emit changed();
}
QString LlamaProcessManager::start(const Profile& profile)
{
    if (active()) {
        return "Server is already active";
    }
    const QString error = profile.validate();
    if (!error.isEmpty()) {
        return error;
    }
    QTcpServer portCheck;
    if (!portCheck.listen(QHostAddress(profile.host), quint16(profile.port))) {
        return "Cannot bind requested host/port: " + portCheck.errorString();
    }
    portCheck.close(); // Advisory check: the child must still handle a later bind race.
    current = profile;
    manualStop = false;
    restartPending = false;
    probeFailed = false;
    help.clear();
    stdoutDecoder.resetState();
    stderrDecoder.resetState();
    uptime.invalidate();
    setState("STARTING");
    auto environment = QProcessEnvironment::systemEnvironment();
    for (auto i = current.environment.begin(); i != current.environment.end(); ++i) {
        environment.insert(i.key(), i.value().toString());
    }
    probe.setProcessEnvironment(environment);
    probe.setWorkingDirectory(current.workingDirectory.isEmpty() ? QFileInfo(current.executable).absolutePath() : current.workingDirectory);
    probeTimer.start();
    probe.start(current.executable, { "--help" });
    return { };
}
void LlamaProcessManager::launch()
{
#ifdef Q_OS_WIN
    if (job) {
        CloseHandle(static_cast<HANDLE>(job));
        job = nullptr;
    }
    job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info { };
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(static_cast<HANDLE>(job), JobObjectExtendedLimitInformation, &info, sizeof(info))) {
            CloseHandle(static_cast<HANDLE>(job));
            job = nullptr;
        }
    }
#endif
    process.setProcessEnvironment(probe.processEnvironment());
    process.setWorkingDirectory(probe.workingDirectory());
    emit log("agent", "Arguments: " + QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(current.arguments())).toJson(QJsonDocument::Compact)) + "\n");
    // On Windows started()/errorOccurred() may be emitted inside start().
    startupTimer.start();
    process.start(current.executable, current.arguments());
}
QString LlamaProcessManager::stop()
{
    restartPending = false;
    if (!active()) {
        return { };
    }
    manualStop = true;
    setState("STOPPING");
    if (probe.state() != QProcess::NotRunning) {
        probeTimer.stop();
        probe.kill();
    } else if (process.state() != QProcess::NotRunning) {
        process.terminate();
        stopTimer.start();
    } else {
        setState("STOPPED");
    }
    return { };
}
QString LlamaProcessManager::restart(const Profile& profile)
{
    if (!active()) {
        return start(profile);
    }
    if (state != "RUNNING") {
        return "Wait until server is running or stopped";
    }
    const auto error = profile.validate();
    if (!error.isEmpty()) {
        return error;
    }
    stop();
    restartProfile = profile;
    restartPending = true;
    return { };
}
void LlamaProcessManager::drain(bool err)
{
    const auto bytes = err ? process.readAllStandardError() : process.readAllStandardOutput();
    // Bound each record, preserve UTF-8 sequences split across reads.
    for (qsizetype i = 0; i < bytes.size(); i += 4096) {
        emit log(err ? "stderr" : "stdout", err ? stderrDecoder(bytes.mid(i, 4096)) : stdoutDecoder(bytes.mid(i, 4096)));
    }
}
QJsonObject LlamaProcessManager::status() const
{
    return { { "state", state }, { "pid", process.processId() }, { "port", current.port }, { "model", current.model },
        { "uptimeSeconds", process.state() == QProcess::Running && uptime.isValid() ? uptime.elapsed() / 1000 : 0 },
        { "error", lastError }, { "arguments", QJsonArray::fromStringList(current.arguments()) }, { "health", "Not checked (Phase 1)" } };
}
