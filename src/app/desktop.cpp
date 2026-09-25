#include "app/desktop.h"

#include "app/forks.h"
#include "app/paths.h"
#include "app/project.h"
#include "app/setup.h"

#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QSysInfo>
#include <QVersionNumber>

namespace xpl::desktop {

namespace {

// A library's version from its file name (libKF6CoreAddons.so.6.18.0), as
// forks::runningPlasma reads libPlasma's.
QString libraryVersion(const QString &library)
{
    QStringList dirs{QLibraryInfo::path(QLibraryInfo::LibrariesPath)};
    for (const char *d : {"/usr/lib64", "/usr/lib", "/usr/lib/x86_64-linux-gnu", "/usr/local/lib64", "/usr/local/lib"})
        dirs << QLatin1String(d);
    const QRegularExpression name(QStringLiteral("^%1\\.so\\.(\\d+\\.\\d+\\.\\d+)$").arg(QRegularExpression::escape(library)));
    for (const QString &dir : std::as_const(dirs))
        for (const QString &file : QDir(dir).entryList({library + QLatin1String(".so.*")}, QDir::Files))
            if (const auto m = name.match(file); m.hasMatch())
                return m.captured(1);
    return QString();
}

// /etc/os-release's PRETTY_NAME, else its NAME and VERSION_ID.
QString distribution()
{
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly))
        return QSysInfo::prettyProductName();
    QString pretty, name, version;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const QString value = line.section(QLatin1Char('='), 1).remove(QLatin1Char('"'));
        if (line.startsWith(QLatin1String("PRETTY_NAME=")))
            pretty = value;
        else if (line.startsWith(QLatin1String("NAME=")))
            name = value;
        else if (line.startsWith(QLatin1String("VERSION_ID=")))
            version = value;
    }
    return !pretty.isEmpty() ? pretty : QString(name + QLatin1Char(' ') + version).trimmed();
}

} // namespace

QString newestKnownPlasma()
{
    QString newest;
    for (const QString &s : forks::seriesNames())
        if (newest.isEmpty() || QVersionNumber::fromString(newest) < QVersionNumber::fromString(s))
            newest = s;
    return newest;
}

bool Report::ok() const
{
    return std::all_of(requirements.cbegin(), requirements.cend(), [](const Requirement &r) { return r.met; });
}

QString Report::problems() const
{
    QStringList lines;
    for (const Requirement &r : requirements)
        if (!r.met)
            lines << (r.found.isEmpty() ? r.what : QStringLiteral("%1 (found: %2)").arg(r.what, r.found));
    return lines.join(QLatin1Char('\n'));
}

Report check(Session &session)
{
    Report report;
    QString detail;
    const bool plasma = setup::plasmaSession(&detail);
    report.requirements.append({QStringLiteral("A KDE Plasma desktop session"), detail, plasma});

    report.plasma = forks::runningPlasma(session);
    const QString oldest = QLatin1String(kOldestPlasma);
    const QString newest = newestKnownPlasma();
    // (a series's point releases count as it -- 6.7.5 is 6.7 -- and its
    // next's betas as that: 6.7.91 is 6.8)
    const QVersionNumber series = forks::seriesOf(report.plasma);
    report.newer = !report.plasma.isEmpty() && !newest.isEmpty() && QVersionNumber::fromString(newest) < series;
    report.requirements.append({QStringLiteral("KDE Plasma %1 or newer").arg(oldest),
                                report.plasma.isEmpty() ? QString() : QStringLiteral("Plasma %1").arg(report.plasma),
                                !report.plasma.isEmpty() && !(series < QVersionNumber::fromString(oldest))});

    for (const setup::Check &c : setup::preflight())
        if (c.neededToApply)
            report.requirements.append({QStringLiteral("%1, to %2").arg(c.tool, c.purpose), c.found, c.ok()});
    return report;
}

QString diagnostics(Session &session, const QString &failure)
{
    const Report r = check(session);
    QStringList lines;
    lines << QStringLiteral("XPLasma: %1 (builds of Plasma's widgets for %2)")
                 .arg(QLatin1String(project::kVersion), forks::seriesNames().join(QStringLiteral(", ")));
    lines << QStringLiteral("Plasma: %1").arg(r.plasma.isEmpty() ? QStringLiteral("unknown") : r.plasma);
    const QString kf = libraryVersion(QStringLiteral("libKF6CoreAddons"));
    lines << QStringLiteral("KDE Frameworks: %1").arg(kf.isEmpty() ? QStringLiteral("unknown") : kf);
    lines << QStringLiteral("Qt: %1 (XPLasma built with %2)").arg(QLatin1String(qVersion()), QLatin1String(QT_VERSION_STR));
    lines << QStringLiteral("Distribution: %1").arg(distribution());
    lines << QStringLiteral("Session: %1, %2")
                 .arg(qEnvironmentVariable("XDG_CURRENT_DESKTOP", QStringLiteral("no desktop")),
                      qEnvironmentVariable("XDG_SESSION_TYPE", QStringLiteral("unknown type")));
    lines << QStringLiteral("Kernel: %1 %2 (%3)").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture());
    if (!r.ok())
        lines << QStringLiteral("Unmet: %1").arg(r.problems().replace(QLatin1Char('\n'), QStringLiteral("; ")));
    if (!failure.isEmpty())
        lines << QString() << QStringLiteral("What failed:") << failure.trimmed();
    return lines.join(QLatin1Char('\n'));
}

} // namespace xpl::desktop
