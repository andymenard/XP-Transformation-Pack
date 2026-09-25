#include "app/flatpak.h"

#include "app/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace xpl::flatpak {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// The global override's [Context] filesystems, with `add` in and `drop` out:
// the file edited as text (its other groups and keys, the user's own, kept
// as they are), since `flatpak override` can only add a path or deny one,
// never take one back out.
bool editFilesystems(const QStringList &add, const QStringList &drop, QString *error)
{
    const QString path = overrideFile();
    QStringList lines;
    if (QFile f(path); f.open(QIODevice::ReadOnly))
        lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    int context = -1, entry = -1, end = int(lines.size());
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines.at(i).trimmed();
        if (t.startsWith(QLatin1Char('['))) {
            if (context >= 0 && end == lines.size())
                end = i;
            if (t == QLatin1String("[Context]"))
                context = i, end = int(lines.size());
        } else if (context >= 0 && end == lines.size() && t.startsWith(QLatin1String("filesystems="))) {
            entry = i;
        }
    }
    QStringList paths;
    if (entry >= 0)
        paths = lines.at(entry).trimmed().mid(int(qstrlen("filesystems="))).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    const QStringList before = paths;
    for (const QString &p : drop)
        paths.removeAll(p);
    for (const QString &p : add)
        if (!paths.contains(p))
            paths << p;
    if (paths == before && (entry >= 0 || paths.isEmpty()))
        return true;
    const QString line = QStringLiteral("filesystems=") + paths.join(QLatin1Char(';')) + QLatin1Char(';');
    if (entry >= 0) {
        if (paths.isEmpty())
            lines.removeAt(entry);
        else
            lines[entry] = line;
    } else if (context >= 0) {
        lines.insert(end, line);
    } else {
        if (!lines.isEmpty())
            lines << QString();
        lines << QStringLiteral("[Context]") << line;
    }
    // (A [Context] left empty, and a file with nothing in it, go.)
    QStringList kept;
    for (int i = 0; i < lines.size(); ++i) {
        const bool header = lines.at(i).trimmed() == QLatin1String("[Context]");
        const bool empty = header && (i + 1 >= lines.size() || lines.at(i + 1).trimmed().isEmpty() || lines.at(i + 1).trimmed().startsWith(QLatin1Char('[')));
        if (!empty)
            kept << lines.at(i);
    }
    while (!kept.isEmpty() && kept.last().trimmed().isEmpty())
        kept.removeLast();
    if (kept.isEmpty()) {
        QFile::remove(path);
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write((kept.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8()) < 0 || !out.commit())
        return fail(error, QStringLiteral("%1: %2").arg(path, out.errorString()));
    return true;
}

} // namespace

const QStringList &readOnlyPaths()
{
    static const QStringList paths{QStringLiteral("xdg-data/xplasma/active:ro")};
    return paths;
}

// What earlier XPlasmas let apps read, taken back out.
static const QStringList kRetiredPaths{QStringLiteral("xdg-config/xplasmarc:ro")};

// The empty stand-ins Flatpak made in each app's own config folder
// (~/.var/app/APP/config/xplasmarc) while ~/.config/xplasmarc was shared
// into sandboxes: left there, the plugin found them first and no look.
static void removeStandIns()
{
    const QDir apps(paths::home() + QLatin1String("/.var/app"));
    for (const QString &app : apps.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfo rc(apps.filePath(app) + QLatin1String("/config/xplasmarc"));
        if (rc.isFile() && rc.size() == 0)
            QFile::remove(rc.absoluteFilePath());
    }
}

QString extensionRoot()
{
    return paths::localShare() + QLatin1String("/flatpak/extension/") + QLatin1String(kExtensionId);
}

QString overrideFile()
{
    return paths::localShare() + QLatin1String("/flatpak/overrides/global");
}

QStringList qtRuntimes(Session &session)
{
    const host::Result r = session.run({QStringLiteral("flatpak"), QStringLiteral("list"), QStringLiteral("--runtime"),
                                        QStringLiteral("--columns=application,branch")});
    QStringList branches;
    if (!r.ok())
        return branches;
    for (const QString &line : QString::fromUtf8(r.out).split(QLatin1Char('\n'))) {
        const QStringList cols = line.split(QLatin1Char('\t'));
        if (cols.size() >= 2 && cols.at(0).trimmed() == QLatin1String("org.kde.Platform") && cols.at(1).trimmed().startsWith(QLatin1String("6."))
            && !branches.contains(cols.at(1).trimmed()))
            branches << cols.at(1).trimmed();
    }
    return branches;
}

bool install(Session &session, const QString &plugin, QStringList *log, QString *error)
{
    const QStringList branches = qtRuntimes(session);
    if (branches.isEmpty()) {
        // (No Flatpak, or no Qt 6 app from it: none of ours left behind.)
        remove();
        return true;
    }
    const host::Result a = session.run({QStringLiteral("flatpak"), QStringLiteral("--default-arch")});
    const QString arch = a.ok() && !a.out.trimmed().isEmpty() ? QString::fromUtf8(a.out.trimmed()) : QStringLiteral("x86_64");
    QFile source(plugin);
    if (!source.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("%1: %2").arg(plugin, source.errorString()));
    const QByteArray bytes = source.readAll();
    const QString root = extensionRoot() + QLatin1Char('/') + arch;
    for (const QString &branch : branches) {
        const QString dir = root + QLatin1Char('/') + branch + QLatin1String("/styles");
        QDir().mkpath(dir);
        QSaveFile out(dir + QLatin1String("/libxplasma.so"));
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
            return fail(error, QStringLiteral("%1: %2").arg(out.fileName(), out.errorString()));
    }
    for (const QString &gone : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!branches.contains(gone))
            QDir(root + QLatin1Char('/') + gone).removeRecursively();
    if (!editFilesystems(readOnlyPaths(), kRetiredPaths, error))
        return false;
    removeStandIns();
    if (log)
        *log << QStringLiteral("Flatpak apps: the style for KDE runtime %1").arg(branches.join(QStringLiteral(", ")));
    return true;
}

void remove(QStringList *log)
{
    const bool had = QFileInfo::exists(extensionRoot());
    QDir(extensionRoot()).removeRecursively();
    editFilesystems({}, readOnlyPaths() + kRetiredPaths, nullptr);
    removeStandIns();
    if (had && log)
        *log << QStringLiteral("Flatpak apps: the style gone");
}

const QStringList &gtkReadOnlyPaths()
{
    static const QStringList paths{QStringLiteral("xdg-config/gtk-4.0:ro"), QStringLiteral("xdg-data/themes:ro")};
    return paths;
}

QString gtkExtensionRoot(const QString &name)
{
    return paths::localShare() + QLatin1String("/flatpak/extension/org.gtk.Gtk3theme.") + name;
}

static void removeGtkExtensions(const QString &keep)
{
    const QDir root(paths::localShare() + QLatin1String("/flatpak/extension"));
    for (const QString &dir : root.entryList({QStringLiteral("org.gtk.Gtk3theme.XPlasma-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        if (dir != QLatin1String("org.gtk.Gtk3theme.") + keep)
            QDir(root.filePath(dir)).removeRecursively();
}

bool installGtk(Session &session, const QString &themeRoot, const QString &name, QStringList *log, QString *error)
{
    if (!session.run({QStringLiteral("flatpak"), QStringLiteral("--version")}).ok())
        return true;
    const host::Result a = session.run({QStringLiteral("flatpak"), QStringLiteral("--default-arch")});
    const QString arch = a.ok() && !a.out.trimmed().isEmpty() ? QString::fromUtf8(a.out.trimmed()) : QStringLiteral("x86_64");
    const QString dir = gtkExtensionRoot(name) + QLatin1Char('/') + arch + QLatin1String("/3.22");
    QDir(gtkExtensionRoot(name)).removeRecursively();
    QDir().mkpath(dir + QLatin1String("/assets"));
    QFile css(themeRoot + QLatin1String("/gtk-3.0/gtk.css"));
    if (!css.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("%1: %2").arg(css.fileName(), css.errorString()));
    const QByteArray text = css.readAll().replace("url(\"../assets/", "url(\"assets/");
    QSaveFile out(dir + QLatin1String("/gtk.css"));
    if (!out.open(QIODevice::WriteOnly) || out.write(text) < 0 || !out.commit())
        return fail(error, QStringLiteral("%1: %2").arg(out.fileName(), out.errorString()));
    for (const QFileInfo &f : QDir(themeRoot + QLatin1String("/assets")).entryInfoList(QDir::Files))
        if (!QFile::copy(f.absoluteFilePath(), dir + QLatin1String("/assets/") + f.fileName()))
            return fail(error, QStringLiteral("can't copy %1 into %2").arg(f.fileName(), dir));
    removeGtkExtensions(name);
    if (!editFilesystems(gtkReadOnlyPaths(), {}, error))
        return false;
    if (log)
        *log << QStringLiteral("Flatpak apps: the GTK theme too (org.gtk.Gtk3theme.%1)").arg(name);
    return true;
}

void removeGtk(QStringList *log)
{
    const QDir root(paths::localShare() + QLatin1String("/flatpak/extension"));
    const bool had = !root.entryList({QStringLiteral("org.gtk.Gtk3theme.XPlasma-*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
    removeGtkExtensions(QString());
    editFilesystems({}, gtkReadOnlyPaths(), nullptr);
    if (had && log)
        *log << QStringLiteral("Flatpak apps: the GTK theme gone");
}

} // namespace xpl::flatpak
