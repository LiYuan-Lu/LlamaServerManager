#include "ProfileStore.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
namespace {
bool read(const QString& path, QList<Profile>& profiles, bool& changed, QByteArray* raw = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        return false;
    }
    const auto bytes = file.readAll();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        return false;
    }
    const auto root = doc.object();
    QList<Profile> result;
    QString error;
    changed = false;
    if (root["version"].toInt() == 1) {
        // Phase 1 format: {"version":1,"profile":{...}}
        Profile p;
        if (!root["profile"].isObject() || !Profile::fromJson(root["profile"].toObject(), p, error)) {
            return false;
        }
        result << p;
        changed = true;
    } else if (root["version"].toInt() == 2 && root["profiles"].isArray()) {
        const auto list = root["profiles"].toArray();
        if (list.size() > ProfileStore::MaxProfiles) {
            return false;
        }
        for (const auto& v : list) {
            Profile p;
            if (!v.isObject() || !Profile::fromJson(v.toObject(), p, error)) {
                return false;
            }
            result << p;
        }
    } else {
        return false;
    }
    QSet<QString> seen;
    for (auto& p : result) {
        if (p.id.isEmpty() || seen.contains(p.id)) {
            p.id = Profile::newId();
            changed = true;
        }
        if (p.name.trimmed().isEmpty()) {
            p.name = p.displayName();
            changed = true;
        }
        seen.insert(p.id);
    }
    profiles = result;
    if (raw) {
        *raw = bytes;
    }
    return true;
}
bool write(const QString& path, const QByteArray& bytes, QString& error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
}
ProfileStore::ProfileStore(QString directory)
    : path(directory + "/profiles.json")
{
    QDir().mkpath(directory);
}
QList<Profile> ProfileStore::load(QString& warning, bool* changed) const
{
    QList<Profile> profiles;
    bool assigned = false;
    if (changed) {
        *changed = false;
    }
    if (!QFile::exists(path)) {
        return profiles;
    }
    if (read(path, profiles, assigned)) {
        if (changed) {
            *changed = assigned;
        }
        return profiles;
    }
    if (read(path + ".bak", profiles, assigned)) {
        warning = "Invalid profiles.json; recovered backup";
    } else {
        warning = "Invalid profiles and backup; starting with no models. Original files preserved until Save.";
    }
    return profiles;
}
bool ProfileStore::save(const QList<Profile>& profiles, QString& error)
{
    if (profiles.size() > MaxProfiles) {
        error = QString("At most %1 models are supported").arg(MaxProfiles);
        return false;
    }
    QJsonArray list;
    QSet<QString> ids;
    for (const auto& p : profiles) {
        error = p.validate(false);
        if (error.isEmpty() && (p.id.isEmpty() || ids.contains(p.id))) {
            error = "Every profile needs a unique ID";
        }
        if (!error.isEmpty()) {
            return false;
        }
        ids.insert(p.id);
        list.append(p.toJson());
    }
    QList<Profile> old;
    QByteArray raw;
    bool ignored = false;
    if (read(path, old, ignored, &raw) && !write(path + ".bak", raw, error)) {
        return false;
    }
    return write(path, QJsonDocument(QJsonObject { { "version", 2 }, { "profiles", list } }).toJson(), error);
}
