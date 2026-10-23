#include "LogStore.h"
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
LogStore::LogStore(const QString &directory, QObject *parent, qint64 limit)
    : QObject(parent), path(directory + "/server.jsonl"), file(path), limit(limit) { QDir().mkpath(directory); }
void LogStore::append(const QString &channel, const QString &text) {
    if (text.isEmpty()) return;
    QJsonObject record{{"sequence", ++sequence}, {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"channel", channel}, {"text", text}};
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    records.append(record); memoryBytes += bytes.size();
    while (records.size() > 1000 || memoryBytes > 256 * 1024) {
        memoryBytes -= QJsonDocument(records.first().toObject()).toJson(QJsonDocument::Compact).size() + 1;
        records.removeFirst();
    }
    if (!file.isOpen() && !file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        emit storageError("Cannot open log: " + file.errorString()); emit entry(record); return;
    }
    if (file.isOpen() && file.size() + bytes.size() > limit) {
        file.close();
        QFile::remove(path + ".3");
        for (int n = 2; n >= 1; --n) QFile::rename(path + "." + QString::number(n), path + "." + QString::number(n + 1));
        if (!QFile::rename(path, path + ".1")) {
            emit storageError("Log rotation failed; disk logging paused"); emit entry(record); return;
        }
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            emit storageError("Cannot open rotated log: " + file.errorString()); emit entry(record); return;
        }
    }
    if (!file.isOpen() || file.write(bytes) != bytes.size() || !file.flush()) emit storageError("Cannot write log: " + file.errorString());
    emit entry(record);
}
