#pragma once
#include "Profile.h"
class ProfileStore {
public:
    explicit ProfileStore(QString directory);
    Profile load(QString &warning) const;
    bool save(const Profile &, QString &error);
private:
    QString path;
};
