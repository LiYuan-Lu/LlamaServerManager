#pragma once
#include <QLocalServer>
#include <QLockFile>
#include <QObject>

// Keeps one GUI per data directory. The first instance holds a lock file and listens on a
// local socket; later launches pass their request ("show", "quit") to it and exit.
// Uses only Qt APIs, so it works the same on Windows, macOS, and Linux.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString& dataDirectory, QObject* parent = nullptr);
    // True if this process is the first GUI; it then starts listening for messages.
    bool claim();
    // Sends a message to the running GUI. Returns false if none answered within timeoutMs.
    bool send(const QByteArray& message, int timeoutMs = 3000);
signals:
    void messageReceived(const QByteArray& message);

private:
    QString name;
    QLockFile lock;
    QLocalServer server;
};
