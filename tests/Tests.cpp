#include "core/AppSettings.h"
#include "core/GpuMonitor.h"
#include "core/HealthMonitor.h"
#include "core/LlamaProcessManager.h"
#include "core/LogStore.h"
#include "core/MetricsCollector.h"
#include "core/ProfileStore.h"
#include "core/RestartPolicy.h"
#include "core/ServerSupervisor.h"
#include "core/SystemMonitor.h"
#include "ipc/LogModel.h"
#include "ipc/Protocol.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class Tests : public QObject {
    Q_OBJECT
    QString binary(const QString& name)
    {
#ifdef Q_OS_WIN
        return QCoreApplication::applicationDirPath() + "/" + name + ".exe";
#else
        return QCoreApplication::applicationDirPath() + "/" + name;
#endif
    }
    QJsonObject instance(QLocalSocket& socket, const QString& profileId)
    {
        for (const auto& v : rpc(socket, "getStatus")["data"].toObject()["instances"].toArray()) {
            if (v.toObject()["id"].toString() == profileId) {
                return v.toObject();
            }
        }
        return { };
    }
    int freePort()
    {
        QTcpServer s;
        if (!s.listen(QHostAddress::LocalHost, 0)) {
            return 0;
        }
        return s.serverPort();
    }
    Profile profile(const QString& dir)
    {
        Profile p;
        p.executable = binary("fake-llama-server");
        p.model = dir + "/測試 model.gguf";
        QFile f(p.model);
        if (!f.open(QIODevice::WriteOnly)) {
            qFatal("Cannot create test model");
        }
        f.write("GGUF");
        f.close();
        p.port = freePort();
        p.workingDirectory = dir;
        return p;
    }
    QJsonObject rpc(QLocalSocket& socket, const QString& cmd, QJsonObject extra = { })
    {
        static int serial = 0;
        const auto id = QString::number(++serial);
        extra["version"] = 1;
        extra["id"] = id;
        extra["command"] = cmd;
        Protocol::send(&socket, extra);
        socket.flush();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 7000) {
            if (!socket.canReadLine()) {
                socket.waitForReadyRead(100);
            }
            while (socket.canReadLine()) {
                const auto m = QJsonDocument::fromJson(socket.readLine()).object();
                if (m["id"].toString() == id) {
                    return m;
                }
            }
        }
        return { };
    }
    // Watchdog tests run with 1 configured "second" = 100 ms.
    Profile watchdogProfile(const QString& dir, const QJsonObject& environment = { })
    {
        auto p = profile(dir);
        p.environment = environment;
        auto& w = p.watchdog;
        w.intervalSeconds = 1;
        w.timeoutSeconds = 2;
        w.failureThreshold = 2;
        w.startupGraceSeconds = 30;
        w.cooldownSeconds = 1;
        w.maxBackoffSeconds = 4;
        w.maxRestarts = 3;
        w.windowSeconds = 600;
        return p;
    }
    struct TimeScale {
        TimeScale() { ServerSupervisor::setTimeScale(100); }
        ~TimeScale() { ServerSupervisor::setTimeScale(1000); }
    };
    // Minimal HTTP endpoint for HealthMonitor tests: replies with `status` after `delayMs`, or never if status == 0.
    struct FakeHttp : QObject {
        QTcpServer server;
        int status = 200, delayMs = 0, requests = 0, open = 0, maxOpen = 0;
        FakeHttp()
        {
            server.listen(QHostAddress::LocalHost, 0);
            connect(&server, &QTcpServer::newConnection, this, [this] {
                while (auto* socket = server.nextPendingConnection()) {
                    ++open;
                    maxOpen = qMax(maxOpen, open);
                    connect(socket, &QTcpSocket::disconnected, this, [this, socket] { --open; socket->deleteLater(); });
                    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                        if (!socket->readAll().contains("\r\n\r\n")) {
                            return;
                        }
                        ++requests;
                        if (status == 0) {
                            return;
                        }
                        const int code = status;
                        QTimer::singleShot(delayMs, socket, [socket, code] {
                            socket->write(QString("HTTP/1.1 %1 X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").arg(code).toLatin1());
                            socket->disconnectFromHost();
                        });
                    });
                }
            });
        }
        int port() const { return server.serverPort(); }
    };
    // Keeps the CPU busy so CPU usage differences are measurable.
    static void spin(int ms)
    {
        QElapsedTimer t;
        t.start();
        volatile double x = 0;
        while (t.elapsed() < ms) {
            x = x + 1.0;
        }
    }
    struct EnvironmentVariable {
        QByteArray name;
        EnvironmentVariable(const char* key, const QByteArray& value)
            : name(key)
        {
            qputenv(key, value);
        }
        ~EnvironmentVariable() { qunsetenv(name.constData()); }
    };
    QJsonObject waitForEvent(QLocalSocket& socket, const QString& event, int timeoutMs)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (!socket.canReadLine()) {
                socket.waitForReadyRead(100);
            }
            while (socket.canReadLine()) {
                const auto m = QJsonDocument::fromJson(socket.readLine()).object();
                if (m["event"].toString() == event) {
                    return m["data"].toObject();
                }
            }
        }
        return { };
    }
    QStringList outcomes(const QJsonArray& history)
    {
        QStringList list;
        for (const auto& e : history) {
            list << e.toObject()["outcome"].toString();
        }
        return list;
    }
private slots:
    void guiDetachedAgent()
    {
#ifdef Q_OS_WIN
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        QVERIFY(dir.isValid());
        QProcess gui;
        gui.start(binary("llama-manager"), { "--data-dir", dir.path(), "--smoke-test" });
        QVERIFY(gui.waitForFinished(15000));
        QLockFile lock(dir.path() + "/agent.lock");
        qint64 pid = 0;
        QString host, name;
        QVERIFY(lock.getLockInfo(&pid, &host, &name));
        HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(pid));
        QVERIFY(handle);
        struct Cleanup {
            HANDLE h;
            ~Cleanup()
            {
                TerminateProcess(h, 0);
                WaitForSingleObject(h, 5000);
                CloseHandle(h);
            }
        } cleanup { handle };
        const auto errors = gui.readAllStandardError();
        QVERIFY2(gui.exitCode() == 0, errors.constData());
        QLocalSocket socket;
        socket.connectToServer(Protocol::endpoint(dir.path()));
        QVERIFY(socket.waitForConnected(3000));
        const auto data = rpc(socket, "getStatus")["data"].toObject();
        QVERIFY(data["instances"].isArray());
        QCOMPARE(data["instances"].toArray().size(), 0);
#else
        QSKIP("Windows detached Agent test");
#endif
    }
    void restartPolicy()
    {
        RestartPolicy policy({ 3, 600000, 5000, 15000 });
        auto d = policy.onFailure(0);
        QVERIFY(d.restart);
        QCOMPARE(d.attempt, 1);
        QCOMPARE(d.delayMs, 5000);
        d = policy.onFailure(1000);
        QVERIFY(d.restart);
        QCOMPARE(d.attempt, 2);
        QCOMPARE(d.delayMs, 10000);
        d = policy.onFailure(2000);
        QVERIFY(d.restart);
        QCOMPARE(d.attempt, 3);
        QCOMPARE(d.delayMs, 15000); // capped
        d = policy.onFailure(3000);
        QVERIFY(!d.restart);
        QCOMPARE(d.attempt, 4);
        QCOMPARE(policy.recentRestarts(3000), 3);
        // Restarts older than the window no longer count.
        QCOMPARE(policy.recentRestarts(600500), 2);
        d = policy.onFailure(600500);
        QVERIFY(d.restart);
        QCOMPARE(d.attempt, 3);
        policy.reset();
        d = policy.onFailure(600600);
        QCOMPARE(d.attempt, 1);
        QCOMPARE(d.delayMs, 5000);
        RestartPolicy never({ 0, 600000, 1000, 1000 });
        QVERIFY(!never.onFailure(0).restart);
    }
    void healthMonitor()
    {
        qRegisterMetaType<HealthMonitor::Outcome>();
        FakeHttp http;
        HealthMonitor monitor;
        QSignalSpy results(&monitor, &HealthMonitor::result);
        monitor.start(1, "127.0.0.1", http.port(), 50, 1000);
        QTRY_VERIFY(results.size() >= 2);
        QCOMPARE(results.first()[0].toULongLong(), 1ULL);
        QCOMPARE(results.first()[1].value<HealthMonitor::Outcome>(), HealthMonitor::Outcome::Healthy);
        monitor.stop();
        results.clear();
        http.status = 503;
        monitor.start(2, "0.0.0.0", http.port(), 50, 1000); // wildcard host is checked via loopback
        QTRY_VERIFY(!results.isEmpty());
        QCOMPARE(results.first()[1].value<HealthMonitor::Outcome>(), HealthMonitor::Outcome::Loading);
        monitor.stop();
        results.clear();
        // A hung server: requests time out and never overlap.
        http.status = 0;
        http.requests = 0;
        http.maxOpen = 0;
        monitor.start(3, "127.0.0.1", http.port(), 10, 200);
        QTest::qWait(900);
        QVERIFY(!results.isEmpty());
        QVERIFY(http.requests <= 5);
        QVERIFY(http.maxOpen <= 1);
        for (const auto& r : results) {
            QCOMPARE(r[1].value<HealthMonitor::Outcome>(), HealthMonitor::Outcome::Failure);
            QVERIFY(r[3].toString().contains("timed out"));
        }
        monitor.stop();
        results.clear();
        // A slow reply from an older generation is never delivered after stop()/start().
        http.status = 200;
        http.delayMs = 300;
        monitor.start(4, "127.0.0.1", http.port(), 1000, 2000);
        QTest::qWait(100);
        monitor.stop();
        monitor.start(5, "127.0.0.1", http.port(), 1000, 2000);
        QTest::qWait(800);
        QVERIFY(!results.isEmpty());
        for (const auto& r : results) {
            QCOMPARE(r[0].toULongLong(), 5ULL);
        }
        monitor.stop();
        results.clear();
        // Connection refused.
        int closedPort = freePort();
        monitor.start(6, "127.0.0.1", closedPort, 1000, 1000);
        QTRY_VERIFY(!results.isEmpty());
        QCOMPARE(results.first()[1].value<HealthMonitor::Outcome>(), HealthMonitor::Outcome::Failure);
    }
    void watchdogStartupGrace()
    {
        TimeScale scale;
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        // Loading-time 503s inside the grace period are not failures.
        ServerSupervisor supervisor("grace", dir.path() + "/history.json");
        auto p = watchdogProfile(dir.path(), { { "FAKE_HEALTH_LOADING_MS", "1200" } });
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(supervisor.status()["health"].toString().contains("503"), 5000);
        QCOMPARE(supervisor.state(), QString("STARTING"));
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("RUNNING"), 8000);
        QVERIFY(supervisor.history().isEmpty());
        supervisor.stop();
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("STOPPED"), 6000);
        // A model that never becomes healthy fails once the grace period ends.
        p = watchdogProfile(dir.path(), { { "FAKE_HEALTH_LOADING_MS", "60000" } });
        p.watchdog.startupGraceSeconds = 8;
        p.watchdog.autoRestart = false;
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("FAILED"), 8000);
        QVERIFY(supervisor.status()["error"].toString().contains("did not become healthy"));
        QCOMPARE(supervisor.history().size(), 1);
        QCOMPARE(supervisor.history().last().toObject()["kind"].toString(), QString("startup"));
        QCOMPARE(supervisor.history().last().toObject()["outcome"].toString(), QString("auto restart disabled"));
        // FAILED after the grace period: the stuck process is not left running.
        QTRY_VERIFY_WITH_TIMEOUT(supervisor.status()["pid"].toInteger() == 0, 6000);
    }
    void watchdogCrashRecovery()
    {
        TimeScale scale;
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        ServerSupervisor supervisor("crash", dir.path() + "/history.json");
        // Crashes on the first run only; the automatic restart recovers.
        auto p = watchdogProfile(dir.path(), { { "FAKE_EXIT", "1" }, { "FAKE_ONCE_FILE", dir.path() + "/crashed" } });
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(supervisor.history().size() == 1, 5000);
        auto entry = supervisor.history().first().toObject();
        QCOMPARE(entry["kind"].toString(), QString("crash"));
        QCOMPARE(entry["attempt"].toInt(), 1);
        QVERIFY(entry["reason"].toString().contains("42"));
        QVERIFY(entry["pid"].toInteger() > 0);
        QTRY_VERIFY_WITH_TIMEOUT(supervisor.state() == "RUNNING" && outcomes(supervisor.history()) == QStringList { "recovered" }, 8000);
        QVERIFY(supervisor.status()["pid"].toInteger() != entry["pid"].toInteger());
        supervisor.stop();
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("STOPPED"), 6000);
        // History survives a new supervisor (Agent restart).
        ServerSupervisor reloaded("crash", dir.path() + "/history.json");
        QCOMPARE(outcomes(reloaded.history()), QStringList { "recovered" });
        // Auto restart disabled: a crash ends in FAILED without a restart.
        p.watchdog.autoRestart = false;
        p.environment = { { "FAKE_EXIT", "1" } };
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("FAILED"), 5000);
        QTest::qWait(500);
        QCOMPARE(supervisor.state(), QString("FAILED"));
        QCOMPARE(outcomes(supervisor.history()).last(), QString("auto restart disabled"));
    }
    void watchdogHealthRestart()
    {
        TimeScale scale;
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        ServerSupervisor supervisor("health", dir.path() + "/history.json");
        QSignalSpy states(&supervisor, &ServerSupervisor::changed);
        // /health stops answering on the first run; the watchdog restarts the process.
        auto p = watchdogProfile(dir.path(), { { "FAKE_HEALTH_HANG_AFTER_MS", "600" }, { "FAKE_ONCE_FILE", dir.path() + "/hung" } });
        QStringList seen;
        connect(&supervisor, &ServerSupervisor::changed, this, [&] {
            if (seen.isEmpty() || seen.last() != supervisor.state()) {
                seen << supervisor.state();
            }
        });
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("RUNNING"), 5000);
        const auto firstPid = supervisor.status()["pid"].toInteger();
        QTRY_VERIFY_WITH_TIMEOUT(supervisor.history().size() == 1, 10000);
        QCOMPARE(supervisor.history().first().toObject()["kind"].toString(), QString("health"));
        QTRY_VERIFY_WITH_TIMEOUT(supervisor.state() == "RUNNING" && outcomes(supervisor.history()) == QStringList { "recovered" }, 15000);
        QVERIFY(supervisor.status()["pid"].toInteger() != firstPid);
        QVERIFY2(seen.contains("RESTARTING"), qPrintable(seen.join(",")));
        supervisor.stop();
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("STOPPED"), 6000);
        // Without auto restart the model is only marked UNHEALTHY and keeps running.
        QFile::remove(dir.path() + "/hung");
        p.watchdog.autoRestart = false;
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("UNHEALTHY"), 10000);
        QVERIFY(supervisor.status()["pid"].toInteger() > 0);
        QCOMPARE(outcomes(supervisor.history()).last(), QString("auto restart disabled"));
        QVERIFY(supervisor.restart(p).isEmpty()); // manual restart is allowed from UNHEALTHY
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("RUNNING"), 10000);
        supervisor.stop();
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("STOPPED"), 6000);
    }
    void watchdogLimitsAndManualStop()
    {
        TimeScale scale;
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        ServerSupervisor supervisor("limits", dir.path() + "/history.json");
        QStringList seen;
        connect(&supervisor, &ServerSupervisor::changed, this, [&] {
            if (seen.isEmpty() || seen.last() != supervisor.state()) {
                seen << supervisor.state();
            }
        });
        // Always crashes while loading: three restarts with growing delays, then FAILED instead of looping.
        auto p = watchdogProfile(dir.path(), { { "FAKE_EXIT", "1" }, { "FAKE_HEALTH_LOADING_MS", "60000" } });
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("FAILED"), 15000);
        QCOMPARE(outcomes(supervisor.history()), (QStringList { "failed", "failed", "failed", "gave up: restart limit reached" }));
        QVERIFY(supervisor.status()["error"].toString().contains("Restart limit"));
        QVERIFY2(seen.contains("BACKOFF"), qPrintable(seen.join(",")));
        QTest::qWait(1000);
        QCOMPARE(supervisor.state(), QString("FAILED"));
        QCOMPARE(supervisor.history().size(), 4);
        // Manual Stop during a pending restart cancels it.
        p.watchdog.cooldownSeconds = 20;
        p.watchdog.maxBackoffSeconds = 20;
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("RESTARTING"), 5000);
        QVERIFY(supervisor.status()["nextRestartSeconds"].toInt() > 0);
        QVERIFY(supervisor.stop().isEmpty());
        QCOMPARE(supervisor.state(), QString("STOPPED"));
        QTest::qWait(2500);
        QCOMPARE(supervisor.state(), QString("STOPPED"));
        QCOMPARE(supervisor.status()["pid"].toInteger(), 0);
        QCOMPARE(outcomes(supervisor.history()).last(), QString("cancelled by manual stop"));
        // A manual start after giving up begins with a fresh restart budget.
        p.environment = { };
        QVERIFY(supervisor.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("RUNNING"), 5000);
        QCOMPARE(supervisor.status()["restartsInWindow"].toInt(), 0);
        supervisor.stop();
        QTRY_COMPARE_WITH_TIMEOUT(supervisor.state(), QString("STOPPED"), 6000);
    }
    void protocolFraming()
    {
        QByteArray buffer = "{\"version\":1";
        QJsonObject m;
        QString error;
        QVERIFY(!Protocol::take(buffer, m, error));
        QVERIFY(error.isEmpty());
        buffer += "}\n{\"version\":1,\"id\":\"two\"}\n";
        QVERIFY(Protocol::take(buffer, m, error));
        QVERIFY(Protocol::take(buffer, m, error));
        QCOMPARE(m["id"].toString(), "two");
        buffer = "{\"version\":2}\n";
        QVERIFY(!Protocol::take(buffer, m, error));
        QVERIFY(!error.isEmpty());
        error.clear();
        buffer = QByteArray(Protocol::MaxFrame + 1, 'x');
        QVERIFY(!Protocol::take(buffer, m, error));
        QVERIFY(!error.isEmpty());
    }
    void persistenceAndValidation()
    {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        auto p = profile(dir.path());
        QString error;
        p.id = "first";
        p.name = "First";
        auto second = p;
        second.id = "second";
        second.name = "Second";
        second.port = p.port + 1;
        QVERIFY(store.save({ p, second }, error));
        p.context = 2048;
        QVERIFY(store.save({ p, second }, error));
        bool changed = true;
        auto loaded = store.load(error, &changed);
        QCOMPARE(loaded.size(), 2);
        QVERIFY(!changed);
        QCOMPARE(loaded[0].id, QString("first"));
        QCOMPARE(loaded[0].context, 2048);
        QCOMPARE(loaded[1].name, QString("Second"));
        auto duplicate = second;
        duplicate.id = "first";
        QVERIFY(!store.save({ p, duplicate }, error));
        QFile corrupt(dir.path() + "/profiles.json");
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("broken");
        corrupt.close();
        error.clear();
        loaded = store.load(error);
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded[0].context, 4096);
        QVERIFY(!error.isEmpty());
        // Phase 1 single-profile files migrate to one model with a generated ID.
        QTemporaryDir legacyDir(QDir::currentPath() + "/lsm-test-XXXXXX");
        QVERIFY(legacyDir.isValid());
        Profile legacy = profile(legacyDir.path());
        legacy.context = 1024;
        auto legacyJson = legacy.toJson();
        legacyJson.remove("id");
        legacyJson.remove("name");
        QFile v1(legacyDir.path() + "/profiles.json");
        QVERIFY(v1.open(QIODevice::WriteOnly));
        v1.write(QJsonDocument(QJsonObject { { "version", 1 }, { "profile", legacyJson } }).toJson());
        v1.close();
        ProfileStore legacyStore(legacyDir.path());
        error.clear();
        changed = false;
        loaded = legacyStore.load(error, &changed);
        QVERIFY(error.isEmpty());
        QVERIFY(changed);
        QCOMPARE(loaded.size(), 1);
        QVERIFY(Profile::validId(loaded[0].id));
        QCOMPARE(loaded[0].context, 1024);
        QCOMPARE(loaded[0].name, QString("測試 model"));
        p.extraArguments = { "--port=1234" };
        QVERIFY(!p.validate().isEmpty());
        p.extraArguments = { };
        p.id = "../escape";
        QVERIFY(!p.validate(false).isEmpty());
        auto o = p.toJson();
        o["port"] = 1.5;
        QVERIFY(!Profile::fromJson(o, p, error));
        o = Profile().toJson();
        QCOMPARE(o["watchdog"].toObject()["intervalSeconds"].toInt(), 30);
        auto w = o["watchdog"].toObject();
        w["intervalSeconds"] = 0;
        o["watchdog"] = w;
        QVERIFY(!Profile::fromJson(o, p, error));
        w["intervalSeconds"] = 10;
        w["autoRestart"] = "yes";
        o["watchdog"] = w;
        QVERIFY(!Profile::fromJson(o, p, error));
        w["autoRestart"] = false;
        o["watchdog"] = w;
        QVERIFY(Profile::fromJson(o, p, error));
        QVERIFY(!p.watchdog.autoRestart);
        o.remove("watchdog");
        QVERIFY(Profile::fromJson(o, p, error));
        QVERIFY(p.watchdog.autoRestart); // older files get defaults
    }
    void boundedLogs()
    {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        LogStore log(dir.path(), nullptr, 4096);
        for (int i = 0; i < 2000; ++i) {
            log.append("stdout", QString(200, 'x'));
        }
        QVERIFY(log.snapshot().size() <= 1000);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 4);
        for (const auto& f : QDir(dir.path()).entryInfoList(QDir::Files)) {
            QVERIFY(f.size() <= 4096);
        }
    }
    void processLifecycle()
    {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        auto p = profile(dir.path());
        // Unicode + whitespace executable path, with DLL lookup supplied by test PATH.
        const auto copied = dir.path() + "/測試 server.exe";
        QVERIFY(QFile::copy(p.executable, copied));
        p.executable = copied;
        LlamaProcessManager manager;
        QSignalSpy logs(&manager, &LlamaProcessManager::log);
        QVERIFY(manager.start(p).isEmpty());
        QVERIFY(!manager.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "RUNNING", 10000);
        QVERIFY(manager.status()["pid"].toInteger() > 0);
        QTRY_VERIFY_WITH_TIMEOUT(logs.size() >= 5, 5000);
        QString all;
        for (const auto& entry : logs) {
            all += entry[0].toString() + entry[1].toString();
        }
        QVERIFY(all.contains("fake stderr ready"));
        QVERIFY(all.contains("測試 model.gguf"));
        QVERIFY(all.contains("cwd="));
        const auto oldPid = manager.status()["pid"].toInteger();
        QVERIFY(manager.restart(p).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(manager.status()["state"].toString() == "RUNNING" && manager.status()["pid"].toInteger() != oldPid, 10000);
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "STOPPED", 6000);
        QTest::qWait(300);
        QCOMPARE(manager.status()["pid"].toInteger(), 0);
    }
    void failuresAndCancellation()
    {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        auto p = profile(dir.path());
        LlamaProcessManager manager;
        QTcpServer occupied;
        QVERIFY(occupied.listen(QHostAddress::LocalHost, quint16(p.port)));
        QVERIFY(!manager.start(p).isEmpty());
        occupied.close();
        const auto goodExe = p.executable;
        p.executable = dir.path() + "/absent.exe";
        QVERIFY(!manager.start(p).isEmpty());
        QFile bad(dir.path() + "/bad.exe");
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("invalid executable");
        bad.close();
        p.executable = bad.fileName();
        QVERIFY(manager.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "FAILED", 5000);
        p.executable = goodExe;
        p.environment = { { "FAKE_EXIT", "1" } };
        QVERIFY(manager.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "FAILED", 5000);
        QVERIFY(manager.status()["error"].toString().contains("42"));
        p.environment = { { "FAKE_HELP_HANG", "1" } };
        QVERIFY(manager.start(p).isEmpty());
        QTest::qWait(100);
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "STOPPED", 5000);
        QVERIFY(manager.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "FAILED", 12000);
        QVERIFY(manager.status()["error"].toString().contains("timed out"));
    }
    void systemMonitor()
    {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        SystemMonitor monitor;
        const auto first = monitor.sampleSystem();
        QCOMPARE(first.cpuPercent, -1.0); // needs two samples
        QVERIFY(first.ramTotalBytes > 0);
        QVERIFY(first.ramUsedBytes > 0 && first.ramUsedBytes <= first.ramTotalBytes);
        const qint64 self = QCoreApplication::applicationPid();
        const qint64 missing = 0x7ffffff0;
        auto processes = monitor.sampleProcesses({ self, missing, 0 });
        QCOMPARE(processes.size(), 3);
        QVERIFY(processes[0].running);
        QVERIFY(processes[0].ramBytes > 1024 * 1024);
        QCOMPARE(processes[0].cpuPercent, -1.0);
        QVERIFY(!processes[1].running);
        QCOMPARE(processes[1].ramBytes, -1);
        QVERIFY(!processes[2].running);
        spin(300);
        const auto second = monitor.sampleSystem();
        QVERIFY(second.cpuPercent >= 0 && second.cpuPercent <= 100);
        processes = monitor.sampleProcesses({ self });
        QVERIFY(processes[0].cpuPercent > 0 && processes[0].cpuPercent <= 100);
        // One busy thread uses at most one logical processor's share of the machine.
        QVERIFY2(processes[0].cpuPercent <= 100.0 / SystemMonitor::logicalProcessors() * 1.5 + 1, qPrintable(QString::number(processes[0].cpuPercent)));
        // A process that is no longer requested loses its baseline.
        monitor.sampleProcesses({ });
        QCOMPARE(monitor.sampleProcesses({ self })[0].cpuPercent, -1.0);
#else
        QSKIP("Resource monitoring is implemented for Windows and Linux");
#endif
    }
    void gpuMonitor()
    {
        {
            GpuMonitor missing(QDir::currentPath() + "/no-such-nvml-library");
            QVERIFY(!missing.available());
            QVERIFY(!missing.unavailableReason().isEmpty());
            const auto sample = missing.sample();
            QVERIFY(!sample.available);
            QVERIFY(sample.devices.isEmpty());
            QCOMPARE(sample.reason, missing.unavailableReason());
        }
        const QString library = LSM_FAKE_NVML;
        const qint64 self = QCoreApplication::applicationPid();
        EnvironmentVariable pid("FAKE_NVML_PID", QByteArray::number(self));
        {
            EnvironmentVariable mode("FAKE_NVML_MODE", "ok");
            GpuMonitor gpu(library);
            QVERIFY2(gpu.available(), qPrintable(gpu.unavailableReason()));
            const auto sample = gpu.sample();
            QVERIFY(sample.available);
            QCOMPARE(sample.devices.size(), 2);
            const auto& d0 = sample.devices[0];
            const qint64 GiB = 1024LL * 1024 * 1024;
            QCOMPARE(d0.name, QString("Fake RTX 0"));
            QCOMPARE(d0.utilizationPercent, 75);
            QCOMPARE(d0.memoryUsedBytes, 6 * GiB);
            QCOMPARE(d0.memoryTotalBytes, 24 * GiB);
            QCOMPARE(d0.temperatureC, 67);
            QCOMPARE(d0.powerWatts, 215.5);
            QCOMPARE(sample.devices[1].index, 1);
            QCOMPARE(sample.devices[1].powerWatts, -1.0); // not supported by that GPU
            // Compute and graphics entries of one GPU are not double counted; GPUs add up.
            QCOMPARE(sample.processMemory.value(self), GiB + GiB / 2);
            // The driver did not report this process's memory: unknown, not zero.
            QCOMPARE(sample.processMemory.value(4242, 0), -1);
            QVERIFY(!sample.processMemory.contains(1));
        }
        {
            EnvironmentVariable mode("FAKE_NVML_MODE", "no-gpu");
            GpuMonitor gpu(library);
            QVERIFY(!gpu.available());
            QCOMPARE(gpu.unavailableReason(), QString("No NVIDIA GPU found"));
        }
        {
            EnvironmentVariable mode("FAKE_NVML_MODE", "init-fail");
            GpuMonitor gpu(library);
            QVERIFY(!gpu.available());
            QVERIFY(gpu.unavailableReason().contains("Driver Not Loaded"));
            QVERIFY(!gpu.sample().available);
        }
    }
    void metricsCollector()
    {
        const qint64 self = QCoreApplication::applicationPid();
        EnvironmentVariable pid("FAKE_NVML_PID", QByteArray::number(self));
        EnvironmentVariable mode("FAKE_NVML_MODE", "ok");
        MetricsCollector collector([self] { return QList<MetricsCollector::Target> { { "self", self }, { "gone", 0x7ffffff0 } }; }, LSM_FAKE_NVML);
        collector.setIntervalSeconds(0);
        QCOMPARE(collector.intervalSeconds(), MetricsCollector::MinIntervalSeconds);
        collector.setIntervalSeconds(99);
        QCOMPARE(collector.intervalSeconds(), MetricsCollector::MaxIntervalSeconds);
        collector.setIntervalSeconds(1);
        collector.setTimeScale(50); // 1 configured second = 50 ms
        QSignalSpy spy(&collector, &MetricsCollector::sampled);
        collector.start();
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() >= 3, 5000);
        const auto latest = collector.latest();
        QCOMPARE(latest["intervalSeconds"].toInt(), 1);
        const auto system = latest["system"].toObject();
        QVERIFY(system["cpuPercent"].toDouble() >= 0);
        QVERIFY(system["ramTotalBytes"].toInteger() > 0);
        const auto gpu = latest["gpu"].toObject();
        QVERIFY(gpu["available"].toBool());
        QCOMPARE(gpu["devices"].toArray().size(), 2);
        const auto processes = latest["processes"].toObject();
        const auto mine = processes["self"].toObject();
        QVERIFY(mine["running"].toBool());
        QCOMPARE(mine["pid"].toInteger(), self);
        QVERIFY(mine["cpuPercent"].toDouble() >= 0);
        QVERIFY(mine["ramBytes"].toInteger() > 0);
        QCOMPARE(mine["gpuMemoryBytes"].toInteger(), 1024LL * 1024 * 1024 * 3 / 2);
        const auto gone = processes["gone"].toObject();
        QVERIFY(!gone["running"].toBool());
        QCOMPARE(gone["gpuMemoryBytes"].toInteger(), -1);
        const auto history = collector.history();
        QVERIFY(history.size() >= 3);
        const auto point = history.last().toObject();
        QVERIFY(point["time"].toInteger() > 0);
        QCOMPARE(point["gpus"].toArray().size(), 2);
        QCOMPARE(point["gpus"].toArray()[0].toObject()["util"].toInt(), 75);
        // The history stays bounded however fast samples arrive.
        collector.setTimeScale(1);
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() > MetricsCollector::MaxHistoryPoints + 20, 20000);
        QVERIFY(collector.history().size() <= MetricsCollector::MaxHistoryPoints);
    }
    void agentMetricsAndSettings()
    {
        AppSettings parsed;
        QString error;
        QVERIFY(AppSettings::fromJson({ }, parsed, error));
        QCOMPARE(parsed.metricsIntervalSeconds, 2);
        QVERIFY(!AppSettings::fromJson({ { "metricsIntervalSeconds", 0 } }, parsed, error));
        QVERIFY(!AppSettings::fromJson({ { "metricsIntervalSeconds", 1.5 } }, parsed, error));
        QVERIFY(!AppSettings::fromJson({ { "metricsIntervalSeconds", "1" } }, parsed, error));
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        QVERIFY(dir.isValid());
        QProcess agent;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("LSM_NVML_LIBRARY", LSM_FAKE_NVML);
        environment.insert("FAKE_NVML_MODE", "ok");
        agent.setProcessEnvironment(environment);
        agent.start(binary("llama-manager-agent"), { "--data-dir", dir.path() });
        QVERIFY(agent.waitForStarted());
        struct Cleanup {
            QProcess& p;
            ~Cleanup()
            {
                p.kill();
                p.waitForFinished(5000);
            }
        } cleanup { agent };
        QLocalSocket socket;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            if (socket.state() == QLocalSocket::UnconnectedState) {
                socket.connectToServer(Protocol::endpoint(dir.path()));
            }
            return socket.waitForConnected(100);
        })(),
            10000);
        auto settings = rpc(socket, "getSettings");
        QVERIFY(settings["ok"].toBool());
        QCOMPARE(settings["data"].toObject()["settings"].toObject()["metricsIntervalSeconds"].toInt(), 2);
        QVERIFY(!rpc(socket, "updateSettings")["ok"].toBool());
        QVERIFY(!rpc(socket, "updateSettings", { { "settings", QJsonObject { { "metricsIntervalSeconds", 11 } } } })["ok"].toBool());
        QVERIFY(!rpc(socket, "updateSettings", { { "settings", QJsonObject { { "bogus", 1 } } } })["ok"].toBool());
        settings = rpc(socket, "updateSettings", { { "settings", QJsonObject { { "metricsIntervalSeconds", 1 } } } });
        QVERIFY2(settings["ok"].toBool(), qPrintable(settings["error"].toString()));
        QCOMPARE(settings["data"].toObject()["settings"].toObject()["metricsIntervalSeconds"].toInt(), 1);
        QFile saved(dir.path() + "/settings.json");
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object()["settings"].toObject()["metricsIntervalSeconds"].toInt(), 1);
        // A running model shows up in the per-process metrics under its profile ID.
        const auto created = rpc(socket, "createProfile", { { "profile", profile(dir.path()).toJson() } });
        const auto id = created["data"].toObject()["id"].toString();
        QVERIFY(rpc(socket, "startServer", { { "profileId", id } })["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(instance(socket, id)["state"].toString(), "RUNNING", 10000);
        const auto pid = instance(socket, id)["pid"].toInteger();
        QJsonObject event;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            event = waitForEvent(socket, "metrics", 1500);
            return event["processes"].toObject()[id].toObject()["cpuPercent"].toDouble() >= 0;
        })(),
            10000);
        QCOMPARE(event["intervalSeconds"].toInt(), 1);
        const auto process = event["processes"].toObject()[id].toObject();
        QCOMPARE(process["pid"].toInteger(), pid);
        QVERIFY(process["running"].toBool());
        QVERIFY(process["ramBytes"].toInteger() > 0);
        QCOMPARE(process["gpuMemoryBytes"].toInteger(), 0); // the fake GPU does not list this process
        QVERIFY(event["gpu"].toObject()["available"].toBool());
        const auto metrics = rpc(socket, "getMetrics");
        QVERIFY(metrics["ok"].toBool());
        const auto data = metrics["data"].toObject();
        QCOMPARE(data["historySeconds"].toInt(), MetricsCollector::HistorySeconds);
        QVERIFY(data["history"].toArray().size() >= 2);
        QVERIFY(data["latest"].toObject()["system"].isObject());
        QVERIFY(rpc(socket, "stopServer", { { "profileId", id } })["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(instance(socket, id)["state"].toString(), "STOPPED", 6000);
        // The setting survives an Agent restart.
        socket.abort();
        agent.kill();
        agent.waitForFinished(5000);
        agent.start(binary("llama-manager-agent"), { "--data-dir", dir.path() });
        QVERIFY(agent.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            if (socket.state() == QLocalSocket::UnconnectedState) {
                socket.connectToServer(Protocol::endpoint(dir.path()));
            }
            return socket.waitForConnected(100);
        })(),
            10000);
        QCOMPARE(rpc(socket, "getMetrics")["data"].toObject()["intervalSeconds"].toInt(), 1);
    }
    void logModel()
    {
        using Level = LogModel::Level;
        QCOMPARE(LogModel::classify("stderr", "llama_model_load: error loading model"), Level::Error);
        QCOMPARE(LogModel::classify("stderr", "E srv  load_model: cannot open"), Level::Error);
        QCOMPARE(LogModel::classify("agent", "FAILED: Process exited unexpectedly"), Level::Error);
        QCOMPARE(LogModel::classify("stderr", "W llama_context: n_ctx_per_seq < n_ctx_train"), Level::Warning);
        QCOMPARE(LogModel::classify("stderr", "warning: no usable GPU found"), Level::Warning);
        QCOMPARE(LogModel::classify("stderr", "load_tensors: offloaded 33/33 layers to GPU"), Level::Info);
        QCOMPARE(LogModel::classify("stdout", "terror is not a level"), Level::Info); // whole words only
        // One record may hold several lines; a trailing newline does not add an empty line.
        const auto lines = LogModel::parse({ { "sequence", 7 }, { "time", "2026-10-23T02:00:00.000Z" }, { "channel", "stderr" }, { "text", "one\r\nwarning: two\nthree" } });
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].text, QString("one"));
        QCOMPARE(lines[1].level, Level::Warning);
        QCOMPARE(lines[2].sequence, 7);
        QCOMPARE(LogModel::parse({ { "text", "only\n" } }).size(), 1);

        LogModel model;
        QAbstractItemModelTester tester(&model);
        auto line = [](const QString& text) { return LogModel::Line { 0, "2026-10-23T02:00:00.000Z", "stderr", text, LogModel::classify("stderr", text) }; };
        model.reset({ line("starting"), line("error: bad"), line("warning: slow"), line("Loaded MODEL") });
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(model.errorCount(), 1);
        QCOMPARE(model.warningCount(), 1);
        QCOMPARE(model.infoCount(), 2);
        QCOMPARE(model.data(model.index(1, 0), LogModel::LevelRole).toString(), QString("error"));
        model.setFilterText("model");
        QCOMPARE(model.rowCount(), 1); // case-insensitive search
        QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(), QString("Loaded MODEL"));
        model.setFilterText("");
        model.setShowInfo(false);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.text(true).count('\n'), 2);
        QCOMPARE(model.text(false).count('\n'), 4);
        // Paused: new lines are held back and not shown until resumed.
        model.setPaused(true);
        model.append({ line("error: during pause") });
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.heldCount(), 1);
        model.setPaused(false);
        QCOMPARE(model.heldCount(), 0);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.errorCount(), 2);
        // Bounded: the oldest lines are dropped, together with their rows and counts.
        model.setShowInfo(true);
        QVector<LogModel::Line> many;
        for (int i = 0; i < LogModel::MaxLines; ++i) {
            many << line(QString("line %1").arg(i));
        }
        model.append(many);
        QCOMPARE(model.totalCount(), LogModel::MaxLines);
        QCOMPARE(model.rowCount(), LogModel::MaxLines);
        QCOMPARE(model.errorCount() + model.warningCount() + model.infoCount(), LogModel::MaxLines);
        QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(), QString("line 0"));
        model.append({ line("error: newest") });
        QCOMPARE(model.totalCount(), LogModel::MaxLines);
        QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(), QString("line 1"));
        QCOMPARE(model.errorCount(), 1);
        // Export writes the matching lines as UTF-8.
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        model.setFilterText("newest");
        const auto file = QUrl::fromLocalFile(dir.path() + "/模型 log.txt");
        QCOMPARE(model.exportTo(file, true), QString());
        QFile exported(file.toLocalFile());
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(exported.readAll()), QString("2026-10-23T02:00:00.000Z [stderr] error: newest\n"));
        QVERIFY(!model.exportTo(QUrl::fromLocalFile(dir.path() + "/missing/dir/x.txt"), true).isEmpty());
        model.reset({ });
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.errorCount(), 0);
    }
    void agentAndGuiReconnect()
    {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX");
        QVERIFY(dir.isValid());
        QProcess agent;
        agent.start(binary("llama-manager-agent"), { "--data-dir", dir.path() });
        QVERIFY(agent.waitForStarted());
        struct Cleanup {
            QProcess& p;
            ~Cleanup()
            {
                p.kill();
                p.waitForFinished(5000);
            }
        } cleanup { agent };
        QLocalSocket socket;
        QElapsedTimer connecting;
        connecting.start();
        while (socket.state() != QLocalSocket::ConnectedState && connecting.elapsed() < 10000 && agent.state() != QProcess::NotRunning) {
            if (socket.state() == QLocalSocket::UnconnectedState) {
                socket.connectToServer(Protocol::endpoint(dir.path()));
            }
            socket.waitForConnected(100);
            QTest::qWait(50);
        }
        QVERIFY2(socket.state() == QLocalSocket::ConnectedState, qPrintable(QString("Agent exit=%1, stderr=%2, socket=%3").arg(agent.exitCode()).arg(QString::fromUtf8(agent.readAllStandardError()), socket.errorString())));
        QProcess duplicate;
        duplicate.start(binary("llama-manager-agent"), { "--data-dir", dir.path() });
        QVERIFY(duplicate.waitForFinished(5000));
        QCOMPARE(duplicate.exitCode(), 3);
        auto p = profile(dir.path());
        const bool realModel = qEnvironmentVariableIsSet("LSM_TEST_REAL_EXE");
        if (realModel) {
            p.executable = qEnvironmentVariable("LSM_TEST_REAL_EXE");
            p.model = qEnvironmentVariable("LSM_TEST_REAL_MODEL");
            p.context = 512;
            p.threads = 2;
            p.gpuLayers = 0;
        }
        const auto created = rpc(socket, "createProfile", { { "profile", p.toJson() } });
        QVERIFY2(created["ok"].toBool(), qPrintable(created["error"].toString()));
        const auto id = created["data"].toObject()["id"].toString();
        QVERIFY(!id.isEmpty());
        QVERIFY(!rpc(socket, "startServer")["ok"].toBool()); // profileId is required
        QVERIFY(rpc(socket, "startServer", { { "profileId", id } })["ok"].toBool());
        QJsonObject status;
        // RUNNING now means /health returned 200, so a real model needs its loading time.
        QTRY_VERIFY_WITH_TIMEOUT(([&] { status = instance(socket, id); return status["state"] == "RUNNING"; })(), realModel ? 120000 : 10000);
        const auto pid = status["pid"].toInteger();
        QVERIFY(pid > 0);
        if (realModel) {
            // Acceptance-only check, deliberately not a product HealthMonitor.
            QNetworkAccessManager http;
            bool healthy = false;
            QElapsedTimer loading;
            loading.start();
            while (!healthy && loading.elapsed() < 90000) {
                QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/health").arg(p.port)));
                request.setTransferTimeout(2000);
                auto* reply = http.get(request);
                QSignalSpy finished(reply, &QNetworkReply::finished);
                if (!reply->isFinished()) {
                    finished.wait(3000);
                }
                healthy = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
                reply->deleteLater();
                if (!healthy) {
                    QTest::qWait(500);
                }
            }
            QVERIFY2(healthy, "Real model failed to reach HTTP 200 within 90 seconds");
        }
        QVERIFY(!rpc(socket, "nonsense")["ok"].toBool());
        const auto history = rpc(socket, "getRestartHistory", { { "profileId", id } });
        QVERIFY(history["ok"].toBool());
        QVERIFY(history["data"].toObject()["entries"].isArray());
        QCOMPARE(status["health"].toString(), QString("Healthy"));
        QVERIFY(!rpc(socket, "deleteProfile", { { "profileId", id } })["ok"].toBool()); // active models cannot be deleted
        QVERIFY(!rpc(socket, "updateProfile", { { "profileId", id }, { "profile", p.toJson() } })["ok"].toBool()); // nor edited
        QVERIFY(!rpc(socket, "getLogs", { { "profileId", "missing" } })["error"].toString().isEmpty());
        // A second model on another port runs alongside the first; a third on the same port is refused.
        auto p2 = profile(dir.path());
        p2.name = "Second";
        const auto id2 = rpc(socket, "createProfile", { { "profile", p2.toJson() } })["data"].toObject()["id"].toString();
        QVERIFY(!id2.isEmpty());
        auto clash = p2;
        clash.port = p.port;
        clash.name = "Clash";
        const auto clashId = rpc(socket, "createProfile", { { "profile", clash.toJson() } })["data"].toObject()["id"].toString();
        const auto refused = rpc(socket, "startServer", { { "profileId", clashId } });
        QVERIFY(!refused["ok"].toBool());
        QVERIFY(refused["error"].toString().contains(QString::number(p.port)));
        // Stopped models can be edited; the ID is kept and the name defaults to the model file name.
        auto renamed = clash;
        renamed.name = "";
        renamed.context = 1024;
        const auto updated = rpc(socket, "updateProfile", { { "profileId", clashId }, { "profile", renamed.toJson() } });
        QVERIFY2(updated["ok"].toBool(), qPrintable(updated["error"].toString()));
        const auto listed = rpc(socket, "listProfiles")["data"].toObject()["profiles"].toArray();
        QCOMPARE(listed.size(), 3);
        QCOMPARE(listed[2].toObject()["id"].toString(), clashId);
        QCOMPARE(listed[2].toObject()["context"].toInt(), 1024);
        QCOMPARE(listed[2].toObject()["name"].toString(), QString("測試 model"));
        QVERIFY(!rpc(socket, "updateProfile", { { "profileId", clashId } })["ok"].toBool()); // missing profile object
        QVERIFY(rpc(socket, "deleteProfile", { { "profileId", clashId } })["ok"].toBool());
        QVERIFY(rpc(socket, "startServer", { { "profileId", id2 } })["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(instance(socket, id2)["state"].toString(), "RUNNING", 10000);
        const auto pid2 = instance(socket, id2)["pid"].toInteger();
        QVERIFY(pid2 > 0 && pid2 != pid);
        const auto logs2 = rpc(socket, "getLogs", { { "profileId", id2 } })["data"].toObject()["entries"].toArray();
        QVERIFY(!logs2.isEmpty());
        QVERIFY(rpc(socket, "stopServer", { { "profileId", id2 } })["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(instance(socket, id2)["state"].toString(), "STOPPED", 6000);
        QCOMPARE(instance(socket, id)["state"].toString(), "RUNNING"); // stopping one must not touch another
        QCOMPARE(instance(socket, id)["pid"].toInteger(), pid);
        // A real QML GUI connects to the running Agent, exits normally, then reopens.
        for (int i = 0; i < 2; ++i) {
            QProcess gui;
            auto guiEnvironment = QProcessEnvironment::systemEnvironment();
            guiEnvironment.insert("LSM_EXPECTED_PID", QString::number(pid));
            guiEnvironment.insert("QT_FORCE_STDERR_LOGGING", "1");
            guiEnvironment.insert("LSM_SCREENSHOT", QDir::currentPath() + "/gui-preview.png");
            gui.setProcessEnvironment(guiEnvironment);
            gui.start(binary("llama-manager"), { "--data-dir", dir.path(), "--smoke-test" });
            QVERIFY(gui.waitForFinished(10000));
            const auto errors = gui.readAllStandardError();
            QVERIFY2(gui.exitCode() == 0, qPrintable(QString("GUI exit=%1, stderr=%2").arg(gui.exitCode()).arg(QString::fromUtf8(errors))));
            QVERIFY2(!errors.contains("ReferenceError") && !errors.contains("TypeError") && !errors.contains("failed to load"), errors.constData());
            QCOMPARE(instance(socket, id)["pid"].toInteger(), pid);
        }
        socket.abort();
        QTest::qWait(300);
        socket.connectToServer(Protocol::endpoint(dir.path()));
        QVERIFY(socket.waitForConnected(5000));
        QTest::qWait(2500); // Cross the 10-second startup deadline: it must have been cancelled.
        QCOMPARE(instance(socket, id)["state"].toString(), "RUNNING");
        QCOMPARE(instance(socket, id)["pid"].toInteger(), pid);
        const auto entries = rpc(socket, "getLogs", { { "profileId", id } })["data"].toObject()["entries"].toArray();
        QVERIFY(!entries.isEmpty());
        QVERIFY(rpc(socket, "stopServer", { { "profileId", id } })["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(instance(socket, id)["state"].toString(), "STOPPED", 6000);
        QVERIFY(rpc(socket, "deleteProfile", { { "profileId", id2 } })["ok"].toBool());
        QVERIFY(instance(socket, id2).isEmpty());
        QVERIFY(!QDir(dir.path() + "/logs/" + id2).exists());
        // A malformed client must not take down the Agent.
        socket.write("garbage\n");
        socket.flush();
        QTRY_VERIFY(socket.state() == QLocalSocket::UnconnectedState);
        QCOMPARE(agent.state(), QProcess::Running);
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "Tests.moc"
