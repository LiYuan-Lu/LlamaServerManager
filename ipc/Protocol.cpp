#include "Protocol.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>
QString Protocol::dataDirectory() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
QString Protocol::endpoint(const QString& directory)
{
    return "LlamaServerManager-v1-" + QString::fromLatin1(QCryptographicHash::hash(QDir(directory).absolutePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}
QByteArray Protocol::encode(const QJsonObject& message) { return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'; }
bool Protocol::take(QByteArray& buffer, QJsonObject& message, QString& error)
{
    const auto end = buffer.indexOf('\n');
    if (end > MaxFrame || (end < 0 && buffer.size() > MaxFrame)) {
        error = "Frame exceeds 1 MiB";
        return false;
    }
    if (end < 0) {
        return false;
    }
    const auto bytes = buffer.left(end);
    buffer.remove(0, end + 1);
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Invalid JSON object";
        return false;
    }
    message = doc.object();
    if (message["version"].toInt() != Version) {
        error = "Unsupported protocol version";
        return false;
    }
    return true;
}
bool Protocol::send(QLocalSocket* socket, const QJsonObject& message)
{
    if (socket->state() != QLocalSocket::ConnectedState) {
        return false;
    }
    const auto bytes = encode(message);
    if (bytes.size() > MaxFrame || socket->bytesToWrite() + bytes.size() > 2 * MaxFrame) {
        socket->abort();
        return false;
    }
    return socket->write(bytes) == bytes.size();
}
