#include "ProfileStore.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
namespace {
bool read(const QString &path, Profile &profile) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    QString error;
    return doc.isObject() && doc.object()["version"].toInt() == 1
        && doc.object()["profile"].isObject() && Profile::fromJson(doc.object()["profile"].toObject(), profile, error);
}
bool write(const QString &path, const QByteArray &bytes, QString &error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
}
ProfileStore::ProfileStore(QString directory) : path(directory + "/profiles.json") { QDir().mkpath(directory); }
Profile ProfileStore::load(QString &warning) const {
    Profile profile;
    if (!QFile::exists(path)) return profile;
    if (read(path, profile)) return profile;
    if (read(path + ".bak", profile)) warning = "Invalid profiles.json; recovered backup";
    else warning = "Invalid profile and backup; using defaults. Original files preserved until Save.";
    return profile;
}
bool ProfileStore::save(const Profile &p, QString &error) {
    error = p.validate(false);
    if (!error.isEmpty()) return false;
    Profile old;
    if (read(path, old)) {
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"profile", old.toJson()}}).toJson();
        if (!write(path + ".bak", bytes, error)) return false;
    }
    return write(path, QJsonDocument(QJsonObject{{"version", 1}, {"profile", p.toJson()}}).toJson(), error);
}
