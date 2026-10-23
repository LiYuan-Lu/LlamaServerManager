#include "LoginStartup.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamReader>

namespace {
const QString AppName = QStringLiteral("Llama Server Manager");
const QString LaunchAgentLabel = QStringLiteral("llama-server-manager.login");
const QString DesktopFileName = QStringLiteral("llama-server-manager.desktop");

QString xml(const QString& text)
{
    return text.toHtmlEscaped();
}

// Joins arguments for a Windows command line (CommandLineToArgvW rules).
QString windowsArguments(const QStringList& arguments)
{
    QStringList quoted;
    for (const auto& argument : arguments) {
        if (!argument.isEmpty() && !argument.contains(QRegularExpression(R"([\s"])"))) {
            quoted << argument;
            continue;
        }
        QString result = "\"";
        int backslashes = 0;
        for (const QChar c : argument) {
            if (c == '\\') {
                ++backslashes;
                continue;
            }
            if (c == '"') {
                result += QString(backslashes * 2 + 1, '\\') + '"';
            } else {
                result += QString(backslashes, '\\') + c;
            }
            backslashes = 0;
        }
        result += QString(backslashes * 2, '\\') + '"';
        quoted << result;
    }
    return quoted.join(' ');
}

// Splits an XDG Exec value (quoted per the Desktop Entry specification).
QStringList splitDesktopExec(const QString& exec)
{
    QStringList parts;
    QString current;
    bool quoted = false, started = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec[i];
        if (quoted) {
            if (c == '\\' && i + 1 < exec.size()) {
                current += exec[++i];
            } else if (c == '"') {
                quoted = false;
            } else {
                current += c;
            }
        } else if (c == '"') {
            quoted = started = true;
        } else if (c.isSpace()) {
            if (started) {
                parts << current;
                current.clear();
                started = false;
            }
        } else {
            current += c;
            started = true;
        }
    }
    if (started) {
        parts << current;
    }
    for (auto& part : parts) {
        part.replace("%%", "%");
    }
    return parts;
}

// Linux and macOS: one file per user, written atomically.
class FileBackend : public LoginStartup::Backend {
public:
    FileBackend(LoginStartup::FileFormat format, QString directory)
        : format(format)
        , path(QDir(directory).filePath(format == LoginStartup::FileFormat::DesktopEntry ? DesktopFileName : LaunchAgentLabel + ".plist"))
    {
    }
    void query(Found done) override
    {
        QFile file(path);
        if (!file.exists()) {
            done(false, { }, { });
            return;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            done(false, { }, "Cannot read " + path + ": " + file.errorString());
            return;
        }
        LoginStartup::Entry entry;
        bool enabled = true;
        const auto bytes = file.readAll();
        if (format == LoginStartup::FileFormat::DesktopEntry) {
            for (const auto& raw : QString::fromUtf8(bytes).split('\n')) {
                const auto line = raw.trimmed();
                if (line.startsWith("Exec=")) {
                    auto parts = splitDesktopExec(line.mid(5));
                    entry.program = parts.value(0);
                    entry.arguments = parts.mid(1);
                } else if (line == "Hidden=true" || line == "X-GNOME-Autostart-enabled=false") {
                    enabled = false; // turned off in the desktop's own startup settings
                }
            }
        } else {
            // <key>ProgramArguments</key><array><string>…</string>…</array>
            QXmlStreamReader reader(bytes);
            bool inArguments = false, nextIsArguments = false, nextIsDisabled = false;
            QStringList arguments;
            while (!reader.atEnd()) {
                reader.readNext();
                if (reader.isStartElement()) {
                    const auto name = reader.name();
                    if (name == QLatin1String("key")) {
                        const auto key = reader.readElementText();
                        nextIsArguments = key == "ProgramArguments";
                        nextIsDisabled = key == "Disabled";
                    } else if (name == QLatin1String("array") && nextIsArguments) {
                        inArguments = true;
                    } else if (name == QLatin1String("string") && inArguments) {
                        arguments << reader.readElementText();
                    } else if (name == QLatin1String("true") && nextIsDisabled) {
                        enabled = false;
                    }
                } else if (reader.isEndElement() && reader.name() == QLatin1String("array")) {
                    inArguments = nextIsArguments = false;
                }
            }
            entry.program = arguments.value(0);
            entry.arguments = arguments.mid(1);
        }
        done(enabled, entry, { });
    }
    void apply(bool enable, const LoginStartup::Entry& entry, Done done) override
    {
        if (!enable) {
            done(!QFile::exists(path) || QFile::remove(path) ? QString() : "Cannot remove " + path);
            return;
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            done("Cannot create " + QFileInfo(path).absolutePath());
            return;
        }
        const auto bytes = format == LoginStartup::FileFormat::DesktopEntry ? LoginStartup::linuxDesktopEntry(entry)
                                                                            : LoginStartup::macLaunchAgentPlist(entry, LaunchAgentLabel);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            done("Cannot write " + path + ": " + file.errorString());
            return;
        }
        done({ });
    }

private:
    LoginStartup::FileFormat format;
    QString path;
};

#ifdef Q_OS_WIN
// Windows: a Task Scheduler task with a logon trigger for this user only, created through
// schtasks.exe so no COM code or administrator rights are needed.
class TaskSchedulerBackend : public LoginStartup::Backend {
public:
    explicit TaskSchedulerBackend(QObject* context)
        : context(context)
        , user(qEnvironmentVariable("USERNAME"))
        , userId(qEnvironmentVariable("USERDOMAIN").isEmpty() ? user : qEnvironmentVariable("USERDOMAIN") + "\\" + user)
        , name(LoginStartup::windowsTaskName(user))
    {
    }
    void query(Found done) override
    {
        run({ "/Query", "/TN", name, "/XML" }, [done](int code, const QByteArray& output) {
            if (code != 0) {
                done(false, { }, { }); // not registered
                return;
            }
            // schtasks writes the XML as UTF-16 or in the console code page.
            const QString text = output.contains('\0') ? QString::fromUtf16(reinterpret_cast<const char16_t*>(output.constData()), output.size() / 2)
                                                       : QString::fromLocal8Bit(output);
            LoginStartup::Entry entry;
            static const QRegularExpression command("<Command>(.*?)</Command>", QRegularExpression::DotMatchesEverythingOption);
            static const QRegularExpression arguments("<Arguments>(.*?)</Arguments>", QRegularExpression::DotMatchesEverythingOption);
            const auto unescape = [](QString s) {
                return s.replace("&quot;", "\"").replace("&apos;", "'").replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
            };
            entry.program = QDir::fromNativeSeparators(unescape(command.match(text).captured(1)).remove('"'));
            entry.arguments = QProcess::splitCommand(unescape(arguments.match(text).captured(1)));
            done(true, entry, { });
        });
    }
    void apply(bool enable, const LoginStartup::Entry& entry, Done done) override
    {
        if (!enable) {
            run({ "/Delete", "/TN", name, "/F" }, [this, done](int code, const QByteArray& output) {
                if (code == 0) {
                    done({ });
                    return;
                }
                // Deleting a task that does not exist is not an error.
                query([done, output](bool enabled, const LoginStartup::Entry&, const QString&) {
                    done(enabled ? "Cannot remove the sign-in task: " + QString::fromLocal8Bit(output).trimmed() : QString());
                });
            });
            return;
        }
        // A plain file, fully closed before schtasks.exe reads it: on Windows QTemporaryFile
        // keeps its file open exclusively until destroyed, so schtasks would get a sharing
        // violation ("the file is being used by another process").
        const QString path = QDir::temp().filePath("llama-server-manager-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".xml");
        const auto bytes = LoginStartup::windowsTaskXml(entry, userId);
        // Task Scheduler expects UTF-16 for an XML document that declares it.
        QByteArray utf16("\xFF\xFE", 2);
        const auto text = QString::fromUtf8(bytes);
        utf16.append(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
        QFile file(path);
        const bool written = file.open(QIODevice::WriteOnly) && file.write(utf16) == utf16.size();
        file.close();
        if (!written) {
            QFile::remove(path);
            done("Cannot write a temporary file for the sign-in task: " + file.errorString());
            return;
        }
        run({ "/Create", "/TN", name, "/XML", QDir::toNativeSeparators(path), "/F" }, [path, done](int code, const QByteArray& output) {
            QFile::remove(path);
            done(code == 0 ? QString() : "Cannot create the sign-in task: " + QString::fromLocal8Bit(output).trimmed());
        });
    }

private:
    void run(const QStringList& arguments, std::function<void(int, const QByteArray&)> finished)
    {
        auto* process = new QProcess(context);
        process->setProgram(QDir(qEnvironmentVariable("SystemRoot", "C:\\Windows")).filePath("System32/schtasks.exe"));
        process->setArguments(arguments);
        process->setProcessChannelMode(QProcess::MergedChannels);
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
        QObject::connect(process, &QProcess::finished, context, [process, finished](int code, QProcess::ExitStatus status) {
            finished(status == QProcess::NormalExit ? code : -1, process->readAll());
            process->deleteLater();
        });
        QObject::connect(process, &QProcess::errorOccurred, context, [process, finished](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                finished(-1, "schtasks.exe could not be started");
                process->deleteLater();
            }
        });
        process->start();
    }
    QObject* context;
    QString user, userId, name;
};
#endif

std::unique_ptr<LoginStartup::Backend> platformBackend(QObject* context)
{
    Q_UNUSED(context);
#if defined(Q_OS_WIN)
    return std::make_unique<TaskSchedulerBackend>(context);
#elif defined(Q_OS_MACOS)
    return LoginStartup::fileBackend(LoginStartup::FileFormat::LaunchAgent, QDir::home().filePath("Library/LaunchAgents"));
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    return LoginStartup::fileBackend(LoginStartup::FileFormat::DesktopEntry,
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath("autostart"));
#else
    return nullptr;
#endif
}
}

LoginStartup::LoginStartup(Entry entry, QObject* parent)
    : QObject(parent)
    , desired(std::move(entry))
    , backend(platformBackend(this))
{
}

LoginStartup::LoginStartup(Entry entry, std::unique_ptr<Backend> custom, QObject* parent)
    : QObject(parent)
    , desired(std::move(entry))
    , backend(std::move(custom))
{
}

LoginStartup::~LoginStartup() = default;

bool LoginStartup::platformSupported()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    return true;
#else
    return false;
#endif
}

std::unique_ptr<LoginStartup::Backend> LoginStartup::fileBackend(FileFormat format, const QString& directory)
{
    return std::make_unique<FileBackend>(format, directory);
}

void LoginStartup::finish(const QString& error)
{
    --pending;
    lastError = error;
    emit changed();
}

void LoginStartup::refresh()
{
    if (!backend) {
        return;
    }
    ++pending;
    emit changed();
    backend->query([this](bool enabled, const Entry& current, const QString& error) {
        on = enabled;
        if (enabled && !(current == desired)) {
            // The program moved or its arguments changed: point the entry at this copy.
            backend->apply(true, desired, [this](const QString& error) { finish(error); });
            return;
        }
        finish(error);
    });
}

void LoginStartup::setEnabled(bool enable)
{
    if (!backend) {
        lastError = "Starting at sign-in is not supported on this platform";
        emit changed();
        return;
    }
    ++pending;
    emit changed();
    backend->apply(enable, desired, [this, enable](const QString& error) {
        if (error.isEmpty()) {
            on = enable;
        }
        finish(error);
    });
}

QString LoginStartup::applyBlocking(bool enable)
{
    if (!backend) {
        return "Starting at sign-in is not supported on this platform";
    }
    QEventLoop loop;
    QString result;
    bool finished = false;
    backend->apply(enable, desired, [&](const QString& error) {
        result = error;
        finished = true;
        loop.quit();
    });
    if (!finished) {
        QTimer::singleShot(30000, &loop, [&] {
            result = "Timed out";
            loop.quit();
        });
        loop.exec();
    }
    if (result.isEmpty()) {
        on = enable;
    }
    return result;
}

QString LoginStartup::windowsTaskName(const QString& userName)
{
    // Tasks in the root folder are shared by all users, so the name includes the user.
    return AppName + " - " + (userName.isEmpty() ? QStringLiteral("user") : userName);
}

QByteArray LoginStartup::windowsTaskXml(const Entry& entry, const QString& userId)
{
    const auto program = QDir::toNativeSeparators(entry.program);
    const auto directory = QDir::toNativeSeparators(entry.workingDirectory.isEmpty() ? QFileInfo(entry.program).absolutePath() : entry.workingDirectory);
    // Priority 4 is normal: the default (7) would also slow down every model it starts.
    // ExecutionTimeLimit PT0S: never stop the GUI after the default three days.
    QString text = QString(R"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Author>%1</Author>
    <Description>Starts %1 in the notification area when you sign in.</Description>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Enabled>true</Enabled>
      <UserId>%2</UserId>
      <Delay>PT10S</Delay>
    </LogonTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>%2</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>LeastPrivilege</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>false</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <IdleSettings>
      <StopOnIdleEnd>false</StopOnIdleEnd>
      <RestartOnIdle>false</RestartOnIdle>
    </IdleSettings>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>false</Hidden>
    <RunOnlyIfIdle>false</RunOnlyIfIdle>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>4</Priority>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>%3</Command>
      <Arguments>%4</Arguments>
      <WorkingDirectory>%5</WorkingDirectory>
    </Exec>
  </Actions>
</Task>
)")
                       .arg(xml(AppName), xml(userId), xml(program), xml(windowsArguments(entry.arguments)), xml(directory));
    return text.toUtf8();
}

QByteArray LoginStartup::macLaunchAgentPlist(const Entry& entry, const QString& label)
{
    QString arguments = "    <string>" + xml(entry.program) + "</string>\n";
    for (const auto& argument : entry.arguments) {
        arguments += "    <string>" + xml(argument) + "</string>\n";
    }
    const auto directory = entry.workingDirectory.isEmpty() ? QFileInfo(entry.program).absolutePath() : entry.workingDirectory;
    return QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>%1</string>
  <key>ProgramArguments</key>
  <array>
%2  </array>
  <key>WorkingDirectory</key>
  <string>%3</string>
  <key>RunAtLoad</key>
  <true/>
  <key>ProcessType</key>
  <string>Interactive</string>
</dict>
</plist>
)")
        .arg(xml(label), arguments, xml(directory))
        .toUtf8();
}

QString LoginStartup::desktopExecQuote(const QString& argument)
{
    // Desktop Entry specification: quote arguments with reserved characters, escape
    // " ` $ \ inside quotes, and write a literal % as %%.
    static const QRegularExpression reserved(R"([\s"'\\><~|&;$*?#()`=%])");
    QString value = argument;
    value.replace('%', "%%");
    if (!value.isEmpty() && !value.contains(reserved)) {
        return value;
    }
    QString quoted;
    for (const QChar c : value) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') {
            quoted += '\\';
        }
        quoted += c;
    }
    return '"' + quoted + '"';
}

QByteArray LoginStartup::linuxDesktopEntry(const Entry& entry)
{
    QStringList exec { desktopExecQuote(entry.program) };
    for (const auto& argument : entry.arguments) {
        exec << desktopExecQuote(argument);
    }
    QString text = "[Desktop Entry]\n"
                   "Type=Application\n"
                   "Name="
        + AppName + "\n"
                    "Comment=Starts "
        + AppName + " in the system tray when you sign in\n"
                    "Exec="
        + exec.join(' ') + "\n";
    if (!entry.workingDirectory.isEmpty()) {
        text += "Path=" + entry.workingDirectory + "\n";
    }
    text += "Terminal=false\n"
            "X-GNOME-Autostart-enabled=true\n";
    return text.toUtf8();
}
