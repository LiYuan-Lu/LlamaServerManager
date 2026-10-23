#include "TrayIcon.h"
#include "core/LoginStartup.h"
#include "ipc/AgentClient.h"
#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QUrl>

namespace {
const QStringList ActiveStates { "RUNNING", "STARTING", "UNHEALTHY", "RESTARTING", "BACKOFF", "STOPPING" };
constexpr int MaxModelsInMenu = 20;
}

TrayIcon::TrayIcon(AgentClient* agentClient, LoginStartup* loginStartup, QString logsDirectory, QObject* parent)
    : QObject(parent)
    , agent(agentClient)
    , startup(loginStartup)
    , logs(std::move(logsDirectory))
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    menu = std::make_unique<QMenu>();
    tray = new QSystemTrayIcon(this);
    tray->setContextMenu(menu.get());
    connect(menu.get(), &QMenu::aboutToShow, this, &TrayIcon::rebuildMenu);
    connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
#ifdef Q_OS_MACOS
        // On macOS a click opens the menu, as for every menu bar item.
        Q_UNUSED(reason);
#else
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (visible && reason == QSystemTrayIcon::Trigger) {
                emit hideWindowRequested();
            } else {
                emit showWindowRequested();
            }
        }
#endif
    });
    connect(agent, &AgentClient::instancesChanged, this, &TrayIcon::updateStatus);
    connect(agent, &AgentClient::connectedChanged, this, &TrayIcon::updateStatus);
    connect(agent, &AgentClient::agentStoppedChanged, this, &TrayIcon::updateStatus);
    rebuildMenu();
    updateStatus();
    tray->show();
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::setWindowVisible(bool value)
{
    if (visible == value) {
        return;
    }
    visible = value;
    emit windowVisibleChanged();
}

QIcon TrayIcon::statusIcon(const QColor& dot) const
{
#ifdef Q_OS_MACOS
    // Menu bar icons are monochrome templates that macOS tints for light and dark mode.
    Q_UNUSED(dot);
    QIcon icon(":/icons/tray-mono.png");
    icon.setIsMask(true);
    return icon;
#else
    const QPixmap base(":/icons/app.png");
    QIcon icon;
    for (const int size : { 16, 20, 24, 32, 40, 48, 64 }) {
        QPixmap pixmap = base.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (dot.isValid()) {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            const qreal d = size * 0.46;
            const QRectF circle(size - d - 0.5, size - d - 0.5, d, d);
            painter.setPen(QPen(QColor("#111827"), std::max(1.0, size / 16.0)));
            painter.setBrush(dot);
            painter.drawEllipse(circle);
        }
        icon.addPixmap(pixmap);
    }
    return icon;
#endif
}

void TrayIcon::updateStatus()
{
    if (!tray) {
        return;
    }
    const auto ids = agent->instanceIds();
    const auto instances = agent->instances();
    int running = 0, failed = 0, busy = 0;
    for (const auto& id : ids) {
        const auto info = instances.value(id).toMap();
        const auto state = info.value("state").toString();
        running += state == "RUNNING";
        failed += state == "FAILED";
        busy += state == "STARTING" || state == "UNHEALTHY" || state == "RESTARTING" || state == "BACKOFF";
        // Notify when a model fails after the first snapshot (not for failures found on connect).
        if (synced && state == "FAILED" && lastStates.value(id) != "FAILED" && lastStates.contains(id)) {
            tray->showMessage(info.value("name").toString(),
                tr("The model stopped: %1").arg(info.value("error").toString()), QSystemTrayIcon::Warning, 10000);
        }
        lastStates.insert(id, state);
    }
    synced = agent->synchronized();
    QString status;
    QColor dot;
    if (!agent->connected()) {
        status = agent->agentStopped() ? tr("Agent stopped") : tr("Connecting to the Agent…");
        dot = QColor("#94a3b8");
    } else {
        status = tr("%1 of %2 models running").arg(running).arg(ids.size());
        dot = failed > 0 ? QColor("#f87171") : busy > 0 ? QColor("#fbbf24")
            : running > 0                               ? QColor("#34d399")
                                                        : QColor();
        if (failed > 0) {
            status += "\n" + tr("%n model(s) failed", "", failed);
        }
    }
    tray->setToolTip("Llama Server Manager\n" + status);
    const QString key = dot.isValid() ? dot.name() : QString("none");
    if (tray->property("iconKey").toString() != key) {
        tray->setIcon(statusIcon(dot));
        tray->setProperty("iconKey", key);
    }
}

void TrayIcon::rebuildMenu()
{
    if (!menu) {
        return;
    }
    menu->clear();
    if (visible) {
        connect(menu->addAction(tr("Hide window")), &QAction::triggered, this, &TrayIcon::hideWindowRequested);
    } else {
        connect(menu->addAction(tr("Show window")), &QAction::triggered, this, &TrayIcon::showWindowRequested);
    }
    menu->addSeparator();
    const bool connected = agent->connected();
    const auto ids = agent->instanceIds();
    const auto instances = agent->instances();
    int running = 0;
    for (const auto& id : ids) {
        running += instances.value(id).toMap().value("state").toString() == "RUNNING";
    }
    auto* header = menu->addAction(!connected ? (agent->agentStopped() ? tr("Agent stopped") : tr("Connecting to the Agent…"))
                                              : tr("%1 of %2 models running").arg(running).arg(ids.size()));
    header->setEnabled(false);
    if (connected) {
        for (int i = 0; i < ids.size() && i < MaxModelsInMenu; ++i) {
            const auto id = ids[i];
            const auto info = instances.value(id).toMap();
            const auto state = info.value("state").toString();
            const bool idle = !ActiveStates.contains(state);
            const bool busy = info.value("busy").toBool();
            auto* models = menu->addMenu(QString("%1  —  %2").arg(info.value("name").toString(), state));
            auto* start = models->addAction(idle ? tr("Start") : tr("Stop"));
            start->setEnabled(!busy && state != "STOPPING");
            connect(start, &QAction::triggered, this, [this, id, idle] { idle ? agent->start(id) : agent->stop(id); });
            auto* restart = models->addAction(tr("Restart"));
            restart->setEnabled(!busy && QStringList { "RUNNING", "UNHEALTHY", "BACKOFF", "RESTARTING" }.contains(state));
            connect(restart, &QAction::triggered, this, [this, id] { agent->restart(id); });
        }
        if (ids.size() > MaxModelsInMenu) {
            auto* more = menu->addAction(tr("%n more in the window…", "", int(ids.size()) - MaxModelsInMenu));
            connect(more, &QAction::triggered, this, &TrayIcon::showWindowRequested);
        }
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Open logs folder")), &QAction::triggered, this, &TrayIcon::openLogsFolder);
    if (startup && startup->supported()) {
        auto* login = menu->addAction(tr("Start at sign-in"));
        login->setCheckable(true);
        login->setChecked(startup->enabled());
        login->setEnabled(!startup->busy());
        connect(login, &QAction::triggered, startup, &LoginStartup::setEnabled);
    }
    if (connected) {
        auto* stop = menu->addAction(tr("Stop Agent…"));
        connect(stop, &QAction::triggered, this, [this] {
            emit showWindowRequested();
            emit stopAgentRequested();
        });
    } else if (agent->agentStopped()) {
        connect(menu->addAction(tr("Start Agent")), &QAction::triggered, agent, &AgentClient::launchAgent);
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Exit (models keep running)")), &QAction::triggered, this, &TrayIcon::quitRequested);
}

void TrayIcon::windowHidden()
{
    setWindowVisible(false);
    if (!tray) {
        return;
    }
    // Same store as the language choice (LanguageManager).
    QSettings settings(QSettings::NativeFormat, QSettings::UserScope, "LlamaServerManager", "llama-manager");
    if (settings.value("trayHintShown").toBool()) {
        return;
    }
    settings.setValue("trayHintShown", true);
    tray->showMessage("Llama Server Manager",
        tr("Still running here. The Agent and your models keep running; use this icon to open the window or exit."),
        QSystemTrayIcon::Information, 8000);
}

void TrayIcon::openLogsFolder()
{
    QDir().mkpath(logs);
    QDesktopServices::openUrl(QUrl::fromLocalFile(logs));
}
