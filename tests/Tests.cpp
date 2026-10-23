#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTcpServer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include "core/LlamaProcessManager.h"
#include "core/ProfileStore.h"
#include "core/LogStore.h"
#include "ipc/Protocol.h"
#include <QLockFile>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class Tests : public QObject {
    Q_OBJECT
    QString binary(const QString &name) { return QCoreApplication::applicationDirPath() + "/" + name + ".exe"; }
    int freePort() { QTcpServer s; if (!s.listen(QHostAddress::LocalHost, 0)) return 0; return s.serverPort(); }
    Profile profile(const QString &dir) {
        Profile p; p.executable = binary("fake-llama-server"); p.model = dir + "/測試 model.gguf";
        QFile f(p.model); if (!f.open(QIODevice::WriteOnly)) qFatal("Cannot create test model"); f.write("GGUF"); f.close();
        p.port = freePort(); p.workingDirectory = dir; return p;
    }
    QJsonObject rpc(QLocalSocket &socket, const QString &cmd, QJsonObject extra = {}) {
        static int serial = 0;
        const auto id = QString::number(++serial);
        extra["version"] = 1; extra["id"] = id; extra["command"] = cmd;
        Protocol::send(&socket, extra); socket.flush();
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 7000) {
            if (!socket.canReadLine()) socket.waitForReadyRead(100);
            while (socket.canReadLine()) {
                const auto m = QJsonDocument::fromJson(socket.readLine()).object();
                if (m["id"].toString() == id) return m;
            }
        }
        return {};
    }
private slots:
    void guiDetachedAgent() {
#ifdef Q_OS_WIN
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX"); QVERIFY(dir.isValid());
        QProcess gui; gui.start(binary("llama-manager"), {"--data-dir", dir.path(), "--smoke-test"});
        QVERIFY(gui.waitForFinished(15000));
        QLockFile lock(dir.path() + "/agent.lock"); qint64 pid = 0; QString host, name;
        QVERIFY(lock.getLockInfo(&pid, &host, &name));
        HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(pid));
        QVERIFY(handle);
        struct Cleanup { HANDLE h; ~Cleanup() { TerminateProcess(h, 0); WaitForSingleObject(h, 5000); CloseHandle(h); } } cleanup{handle};
        const auto errors = gui.readAllStandardError();
        QVERIFY2(gui.exitCode() == 0, errors.constData());
        QLocalSocket socket; socket.connectToServer(Protocol::endpoint(dir.path())); QVERIFY(socket.waitForConnected(3000));
        QCOMPARE(rpc(socket, "getStatus")["data"].toObject()["state"].toString(), "STOPPED");
#else
        QSKIP("Windows detached Agent test");
#endif
    }
    void protocolFraming() {
        QByteArray buffer = "{\"version\":1"; QJsonObject m; QString error;
        QVERIFY(!Protocol::take(buffer, m, error)); QVERIFY(error.isEmpty());
        buffer += "}\n{\"version\":1,\"id\":\"two\"}\n";
        QVERIFY(Protocol::take(buffer, m, error)); QVERIFY(Protocol::take(buffer, m, error)); QCOMPARE(m["id"].toString(), "two");
        buffer = "{\"version\":2}\n"; QVERIFY(!Protocol::take(buffer, m, error)); QVERIFY(!error.isEmpty());
        error.clear(); buffer = QByteArray(Protocol::MaxFrame + 1, 'x'); QVERIFY(!Protocol::take(buffer, m, error)); QVERIFY(!error.isEmpty());
    }
    void persistenceAndValidation() {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX"); QVERIFY(dir.isValid()); ProfileStore store(dir.path()); auto p = profile(dir.path()); QString error;
        QVERIFY(store.save(p, error)); p.context = 2048; QVERIFY(store.save(p, error));
        QCOMPARE(store.load(error).context, 2048);
        QFile corrupt(dir.path() + "/profiles.json"); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("broken"); corrupt.close();
        QCOMPARE(store.load(error).context, 4096); QVERIFY(!error.isEmpty());
        p.extraArguments = {"--port=1234"}; QVERIFY(!p.validate().isEmpty());
        auto o = p.toJson(); o["port"] = 1.5; QVERIFY(!Profile::fromJson(o, p, error));
    }
    void boundedLogs() {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX"); LogStore log(dir.path(), nullptr, 4096);
        for (int i = 0; i < 2000; ++i) log.append("stdout", QString(200, 'x'));
        QVERIFY(log.snapshot().size() <= 1000);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 4);
        for (const auto &f : QDir(dir.path()).entryInfoList(QDir::Files)) QVERIFY(f.size() <= 4096);
    }
    void processLifecycle() {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX"); auto p = profile(dir.path());
        // Unicode + whitespace executable path, with DLL lookup supplied by test PATH.
        const auto copied = dir.path() + "/測試 server.exe"; QVERIFY(QFile::copy(p.executable, copied)); p.executable = copied;
        LlamaProcessManager manager; QSignalSpy logs(&manager, &LlamaProcessManager::log);
        QVERIFY(manager.start(p).isEmpty()); QVERIFY(!manager.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "RUNNING", 10000);
        QVERIFY(manager.status()["pid"].toInteger() > 0);
        QTRY_VERIFY_WITH_TIMEOUT(logs.size() >= 5, 5000);
        QString all; for (const auto &entry : logs) all += entry[0].toString() + entry[1].toString();
        QVERIFY(all.contains("fake stderr ready")); QVERIFY(all.contains("測試 model.gguf")); QVERIFY(all.contains("cwd="));
        const auto oldPid = manager.status()["pid"].toInteger();
        QVERIFY(manager.restart(p).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(manager.status()["state"].toString() == "RUNNING" && manager.status()["pid"].toInteger() != oldPid, 10000);
        manager.stop(); QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "STOPPED", 6000);
        QTest::qWait(300); QCOMPARE(manager.status()["pid"].toInteger(), 0);
    }
    void failuresAndCancellation() {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX"); auto p = profile(dir.path()); LlamaProcessManager manager;
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost, quint16(p.port))); QVERIFY(!manager.start(p).isEmpty()); occupied.close();
        const auto goodExe = p.executable; p.executable = dir.path() + "/absent.exe"; QVERIFY(!manager.start(p).isEmpty());
        QFile bad(dir.path() + "/bad.exe"); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("invalid executable"); bad.close();
        p.executable = bad.fileName(); QVERIFY(manager.start(p).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "FAILED", 5000);
        p.executable = goodExe; p.environment = {{"FAKE_EXIT", "1"}}; QVERIFY(manager.start(p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "FAILED", 5000);
        QVERIFY(manager.status()["error"].toString().contains("42"));
        p.environment = {{"FAKE_HELP_HANG", "1"}}; QVERIFY(manager.start(p).isEmpty()); QTest::qWait(100); manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "STOPPED", 5000);
        QVERIFY(manager.start(p).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(manager.status()["state"].toString(), "FAILED", 12000);
        QVERIFY(manager.status()["error"].toString().contains("timed out"));
    }
    void agentAndGuiReconnect() {
        QTemporaryDir dir(QDir::currentPath() + "/lsm-test-XXXXXX"); QVERIFY(dir.isValid());
        QProcess agent; agent.start(binary("llama-manager-agent"), {"--data-dir", dir.path()}); QVERIFY(agent.waitForStarted());
        struct Cleanup { QProcess &p; ~Cleanup() { p.kill(); p.waitForFinished(5000); } } cleanup{agent};
        QLocalSocket socket;
        QElapsedTimer connecting; connecting.start();
        while (socket.state() != QLocalSocket::ConnectedState && connecting.elapsed() < 10000 && agent.state() != QProcess::NotRunning) {
            if (socket.state() == QLocalSocket::UnconnectedState) socket.connectToServer(Protocol::endpoint(dir.path()));
            socket.waitForConnected(100); QTest::qWait(50);
        }
        QVERIFY2(socket.state() == QLocalSocket::ConnectedState, qPrintable(QString("Agent exit=%1, stderr=%2, socket=%3").arg(agent.exitCode()).arg(QString::fromUtf8(agent.readAllStandardError()), socket.errorString())));
        QProcess duplicate; duplicate.start(binary("llama-manager-agent"), {"--data-dir", dir.path()}); QVERIFY(duplicate.waitForFinished(5000)); QCOMPARE(duplicate.exitCode(), 3);
        auto p = profile(dir.path());
        const bool realModel = qEnvironmentVariableIsSet("LSM_TEST_REAL_EXE");
        if (realModel) {
            p.executable = qEnvironmentVariable("LSM_TEST_REAL_EXE");
            p.model = qEnvironmentVariable("LSM_TEST_REAL_MODEL");
            p.context = 512; p.threads = 2; p.gpuLayers = 0;
        }
        QVERIFY(rpc(socket, "updateProfile", {{"profile", p.toJson()}})["ok"].toBool());
        QVERIFY(rpc(socket, "startServer")["ok"].toBool());
        QJsonObject status;
        QTRY_VERIFY_WITH_TIMEOUT(([&] { status = rpc(socket, "getStatus")["data"].toObject(); return status["state"] == "RUNNING"; })(), 10000);
        const auto pid = status["pid"].toInteger(); QVERIFY(pid > 0);
        if (realModel) {
            // Acceptance-only check, deliberately not a product HealthMonitor.
            QNetworkAccessManager http; bool healthy = false; QElapsedTimer loading; loading.start();
            while (!healthy && loading.elapsed() < 90000) {
                QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/health").arg(p.port)));
                request.setTransferTimeout(2000);
                auto *reply = http.get(request);
                QSignalSpy finished(reply, &QNetworkReply::finished);
                if (!reply->isFinished()) finished.wait(3000);
                healthy = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
                reply->deleteLater();
                if (!healthy) QTest::qWait(500);
            }
            QVERIFY2(healthy, "Real model failed to reach HTTP 200 within 90 seconds");
        }
        QVERIFY(!rpc(socket, "nonsense")["ok"].toBool());
        // A real QML GUI connects to the running Agent, exits normally, then reopens.
        for (int i = 0; i < 2; ++i) {
            QProcess gui;
            auto guiEnvironment = QProcessEnvironment::systemEnvironment();
            guiEnvironment.insert("LSM_EXPECTED_PID", QString::number(pid));
            guiEnvironment.insert("QT_FORCE_STDERR_LOGGING", "1");
            guiEnvironment.insert("LSM_SCREENSHOT", QDir::currentPath() + "/gui-preview.png");
            gui.setProcessEnvironment(guiEnvironment);
            gui.start(binary("llama-manager"), {"--data-dir", dir.path(), "--smoke-test"});
            QVERIFY(gui.waitForFinished(10000));
            const auto errors = gui.readAllStandardError();
            QVERIFY2(gui.exitCode() == 0, qPrintable(QString("GUI exit=%1, stderr=%2").arg(gui.exitCode()).arg(QString::fromUtf8(errors))));
            QVERIFY2(!errors.contains("ReferenceError") && !errors.contains("TypeError") && !errors.contains("failed to load"), errors.constData());
            QCOMPARE(rpc(socket, "getStatus")["data"].toObject()["pid"].toInteger(), pid);
        }
        socket.abort(); QTest::qWait(300);
        socket.connectToServer(Protocol::endpoint(dir.path())); QVERIFY(socket.waitForConnected(5000));
        QTest::qWait(2500); // Cross the 10-second startup deadline: it must have been cancelled.
        QCOMPARE(rpc(socket, "getStatus")["data"].toObject()["state"].toString(), "RUNNING");
        QCOMPARE(rpc(socket, "getStatus")["data"].toObject()["pid"].toInteger(), pid);
        const auto entries = rpc(socket, "getLogs")["data"].toObject()["entries"].toArray();
        QVERIFY(!entries.isEmpty());
        QVERIFY(rpc(socket, "stopServer")["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(rpc(socket, "getStatus")["data"].toObject()["state"].toString(), "STOPPED", 6000);
        // A malformed client must not take down the Agent.
        socket.write("garbage\n"); socket.flush(); QTRY_VERIFY(socket.state() == QLocalSocket::UnconnectedState);
        QCOMPARE(agent.state(), QProcess::Running);
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "Tests.moc"
