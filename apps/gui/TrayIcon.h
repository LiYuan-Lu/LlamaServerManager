#pragma once
#include <QHash>
#include <QObject>
#include <QSystemTrayIcon>
#include <memory>

class AgentClient;
class LoginStartup;
class QMenu;

// The icon in the Windows notification area, the macOS menu bar, or a Linux system tray.
// Shows overall status, offers Start/Stop/Restart per model and the window/Agent actions,
// and notifies when a model fails. Built on QSystemTrayIcon, so it is the same on every
// platform; `available` is false where the desktop has no tray (the GUI then behaves as a
// normal window).
class TrayIcon : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool windowVisible READ windowVisible WRITE setWindowVisible NOTIFY windowVisibleChanged)
public:
    TrayIcon(AgentClient* agent, LoginStartup* startup, QString logsDirectory, QObject* parent = nullptr);
    ~TrayIcon() override;
    bool available() const { return tray != nullptr; }
    bool windowVisible() const { return visible; }
    void setWindowVisible(bool value);
    // Called when the close button hid the window; explains once that the app is still running.
    Q_INVOKABLE void windowHidden();
    Q_INVOKABLE void openLogsFolder();
signals:
    void showWindowRequested();
    void hideWindowRequested();
    void stopAgentRequested(); // asks the window to confirm
    void quitRequested();
    void windowVisibleChanged();

private:
    void rebuildMenu();
    void updateStatus();
    QIcon statusIcon(const QColor& dot) const;
    AgentClient* agent;
    LoginStartup* startup;
    QString logs;
    std::unique_ptr<QMenu> menu;
    QSystemTrayIcon* tray = nullptr;
    QHash<QString, QString> lastStates;
    bool visible = true, synced = false;
};
