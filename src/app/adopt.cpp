#include "app/adopt.h"

#include "app/library.h"
#include "app/paths.h"
#include "app/state.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace xpl {

QStringList adoptLegacy()
{
    QStringList log;
    auto move = [&](const QString &from, const QString &to, const QString &what) {
        if (!QFileInfo::exists(from) || QFileInfo(from).isSymLink() || QFileInfo::exists(to))
            return false;
        QDir().mkpath(QFileInfo(to).absolutePath());
        // A link is left behind: the running desktop still names files in
        // there (the wallpaper, xplasmarc's Source=, the Start menu's art)
        // until XPlasma next applies.
        if (QDir().rename(from, to)) {
            QFile::link(to, from);
            log << QStringLiteral("adopted xp2plasma's %1 (%2)").arg(what, to);
            return true;
        }
        log << QStringLiteral("couldn't move xp2plasma's %1 from %2").arg(what, from);
        return false;
    };
    move(paths::legacyData() + QLatin1String("/assets"), paths::assets(), QStringLiteral("asset library"));
    if (move(paths::legacyData() + QLatin1String("/schemes"), paths::schemes(), QStringLiteral("saved schemes"))) {
        // xp2plasma kept them as JSON in schemes/user/; XPlasma's are .theme
        // files. (The JSON stays, for the Python app until it's retired.)
        const QDir user(paths::schemes() + QLatin1String("/user"));
        for (const QString &f : user.entryList({QStringLiteral("*.json")}, QDir::Files)) {
            QFile json(user.filePath(f));
            if (!json.open(QIODevice::ReadOnly))
                continue;
            for (const ClassicScheme &s : schemes::fromLegacyJson(json.readAll())) {
                QString error;
                if (schemes::save(s, nullptr, &error))
                    log << QStringLiteral("converted saved scheme %1").arg(s.name);
                else
                    log << QStringLiteral("couldn't convert saved scheme %1: %2").arg(s.name, error);
            }
        }
    }
    move(paths::legacyCache() + QLatin1String("/downloads"), paths::downloads(), QStringLiteral("downloads"));

    // The XP media: xp2plasma recorded an unpacked copy, which may be gone;
    // the ISO it came from is the dependable source.
    State state = State::load();
    if (state.media.isEmpty() || !QFileInfo::exists(state.media)) {
        const QFileInfoList isos = QDir(paths::downloads()).entryInfoList({QStringLiteral("*.iso")}, QDir::Files, QDir::Size);
        if (!isos.isEmpty()) {
            state.media = isos.first().absoluteFilePath();
            if (state.save())
                log << QStringLiteral("XP media: %1").arg(state.media);
        }
    }
    return log;
}

} // namespace xpl
