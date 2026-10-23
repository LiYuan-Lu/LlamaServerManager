#pragma once
#include <QFile>
#include <QJsonArray>
#include <QObject>
class LogStore : public QObject {
    Q_OBJECT
public:
    explicit LogStore(const QString& directory, QObject* parent = nullptr, qint64 limit = 4 * 1024 * 1024);
    void append(const QString& channel, const QString& text);
    QJsonArray snapshot() const { return records; }
signals:
    void entry(const QJsonObject& record);
    void storageError(const QString& message);

private:
    QString path;
    QFile file;
    qint64 limit, sequence = 0, memoryBytes = 0;
    QJsonArray records;
};
