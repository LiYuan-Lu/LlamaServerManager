#pragma once
#include <QJsonObject>
#include <QLocalSocket>
namespace Protocol {
constexpr int Version = 1;
// Agent feature level, sent as `revision` in status snapshots (absent before 2). A GUI that
// finds an older Agent asks the user to restart it, because an older Agent drops newer fields.
// 2: alias option, throughput status.
constexpr int Revision = 2;
constexpr qsizetype MaxFrame = 1024 * 1024;
QString dataDirectory();
QString endpoint(const QString& directory);
QByteArray encode(const QJsonObject& message);
bool take(QByteArray& buffer, QJsonObject& message, QString& error);
bool send(QLocalSocket* socket, const QJsonObject& message);
}
