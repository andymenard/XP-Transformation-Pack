// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/state.h"

#include "app/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace xpl {

static const char *const kKnown[] = {"version", "applied", "media", "settings"};

State State::fromJson(const QJsonObject &json)
{
    State s;
    const QJsonObject applied = json.value(QLatin1String("applied")).toObject();
    s.appliedKind = applied.value(QLatin1String("kind")).toString();
    s.style = applied.value(QLatin1String("style")).toString();
    s.variant = applied.value(QLatin1String("variant")).toString();
    s.themeFile = applied.value(QLatin1String("themeFile")).toString();
    s.scheme = applied.value(QLatin1String("scheme")).toString();
    s.media = json.value(QLatin1String("media")).toString();
    s.settings = json.value(QLatin1String("settings")).toObject();
    s.m_rest = json;
    for (const char *key : kKnown)
        s.m_rest.remove(QLatin1String(key));
    return s;
}

QJsonObject State::toJson() const
{
    QJsonObject json = m_rest;
    json.insert(QLatin1String("version"), kVersion);
    QJsonObject applied;
    if (!appliedKind.isEmpty()) {
        applied.insert(QLatin1String("kind"), appliedKind);
        auto put = [&](const char *key, const QString &value) {
            if (!value.isEmpty())
                applied.insert(QLatin1String(key), value);
        };
        put("style", style);
        put("variant", variant);
        put("themeFile", themeFile);
        put("scheme", scheme);
    }
    json.insert(QLatin1String("applied"), applied);
    if (!media.isEmpty())
        json.insert(QLatin1String("media"), media);
    json.insert(QLatin1String("settings"), settings);
    return json;
}

State State::load()
{
    QFile file(paths::statePath());
    if (!file.open(QIODevice::ReadOnly))
        return State();
    return fromJson(QJsonDocument::fromJson(file.readAll()).object());
}

bool State::save(QString *error) const
{
    QDir().mkpath(QFileInfo(paths::statePath()).absolutePath());
    QSaveFile file(paths::statePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace xpl
