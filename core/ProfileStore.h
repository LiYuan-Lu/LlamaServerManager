#pragma once
#include "Profile.h"
#include <QList>
// Persists all model profiles in profiles.json (version 2). A version 1 file holding a
// single profile is migrated on load: it becomes one profile with a generated ID.
class ProfileStore {
public:
    static constexpr int MaxProfiles = 64;
    explicit ProfileStore(QString directory);
    // `changed` is set when IDs were assigned or the file was migrated and should be saved.
    QList<Profile> load(QString& warning, bool* changed = nullptr) const;
    bool save(const QList<Profile>&, QString& error);

private:
    QString path;
};
