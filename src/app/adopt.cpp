#include "app/adopt.h"

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
            return;
        QDir().mkpath(QFileInfo(to).absolutePath());
        // A link is left behind: the running desktop still names files in
        // there (the wallpaper, xplasmarc's Source=, the Start menu's art)
        // until XPLasma next applies.
        if (QDir().rename(from, to)) {
            QFile::link(to, from);
            log << QStringLiteral("adopted xp2plasma's %1 (%2)").arg(what, to);
        } else {
            log << QStringLiteral("couldn't move xp2plasma's %1 from %2").arg(what, from);
        }
    };
    move(paths::legacyData() + QLatin1String("/assets"), paths::assets(), QStringLiteral("asset library"));
    move(paths::legacyData() + QLatin1String("/schemes"), paths::schemes(), QStringLiteral("saved schemes"));
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
