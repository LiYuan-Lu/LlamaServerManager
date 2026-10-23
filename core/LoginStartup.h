#pragma once
#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>

// Starts a program when the current user signs in, using each platform's own mechanism:
//   Windows  a per-user Task Scheduler task (schtasks.exe, logon trigger)
//   macOS    a LaunchAgent plist in ~/Library/LaunchAgents
//   Linux    an XDG autostart .desktop file in ~/.config/autostart
// No administrator rights are needed. If the program moved since the entry was created
// (e.g. after an update to another folder), refresh() rewrites it with the current path.
class LoginStartup : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    struct Entry {
        QString program; // absolute path of the executable
        QStringList arguments;
        QString workingDirectory;
        bool operator==(const Entry& other) const
        {
            return program == other.program && arguments == other.arguments;
        }
    };
    class Backend {
    public:
        using Done = std::function<void(const QString& error)>;
        using Found = std::function<void(bool enabled, const Entry& current, const QString& error)>;
        virtual ~Backend() = default;
        virtual void query(Found done) = 0;
        virtual void apply(bool enable, const Entry& entry, Done done) = 0;
    };
    // `entry` is what should start at sign-in, normally this GUI with --minimized.
    explicit LoginStartup(Entry entry, QObject* parent = nullptr);
    // Uses `backend` instead of the platform's own (tests).
    LoginStartup(Entry entry, std::unique_ptr<Backend> backend, QObject* parent = nullptr);
    ~LoginStartup() override;
    static bool platformSupported();
    bool supported() const { return backend != nullptr; }
    bool enabled() const { return on; }
    bool busy() const { return pending > 0; }
    QString error() const { return lastError; }
    // Reads the current state (and repairs a stale program path).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setEnabled(bool enable);
    // Blocking variant for command-line use (e.g. the installer). Returns an error or "".
    QString applyBlocking(bool enable);

    // Platform documents, public for tests.
    static QString windowsTaskName(const QString& userName);
    static QByteArray windowsTaskXml(const Entry& entry, const QString& userId);
    static QByteArray macLaunchAgentPlist(const Entry& entry, const QString& label);
    static QByteArray linuxDesktopEntry(const Entry& entry);
    static QString desktopExecQuote(const QString& argument);
    // The Linux (desktop entry) or macOS (plist) backend writing into `directory`.
    enum class FileFormat { DesktopEntry,
        LaunchAgent };
    static std::unique_ptr<Backend> fileBackend(FileFormat format, const QString& directory);
signals:
    void changed();

private:
    void finish(const QString& error);
    Entry desired;
    std::unique_ptr<Backend> backend;
    bool on = false;
    int pending = 0;
    QString lastError;
};
