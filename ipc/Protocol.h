#pragma once
#include <QJsonObject>
#include <QLocalSocket>
namespace Protocol {
constexpr int Version = 1;
constexpr qsizetype MaxFrame = 1024 * 1024;
QString dataDirectory();
QString endpoint(const QString& directory);
QByteArray encode(const QJsonObject& message);
bool take(QByteArray& buffer, QJsonObject& message, QString& error);
bool send(QLocalSocket* socket, const QJsonObject& message);
}
