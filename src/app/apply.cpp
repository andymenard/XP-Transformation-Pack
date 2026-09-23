#include "app/apply.h"

#include "app/colors.h"
#include "app/decoration.h"
#include "app/icons.h"
#include "app/overrides.h"
#include "app/panel.h"
#include "app/paths.h"
#include "app/qttarget.h"
#include "app/startmenu.h"
#include "app/state.h"
#include "core/schema.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QUrl>

namespace xpl {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString share(const QString &relative)
{
    return paths::localShare() + QLatin1Char('/') + relative;
}

// A package directory built under `from` moved into place at `to`.
bool install(const QString &from, const QString &to, QString *error)
{
    QDir(to).removeRecursively();
    QDir().mkpath(QFileInfo(to).absolutePath());
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + file.mid(from.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(file, target))
            return fail(error, QStringLiteral("can't install %1").arg(target));
    }
    return true;
}

bool ours(const QString &metadata)
{
    QFile f(metadata);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray text = f.readAll().toLower();
    return text.contains("xplasma") || text.contains("xp2plasma");   // (X-Xp2plasma-Icon-Theme, too)
}

QString resourceText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

void report(const ApplyOptions &o, const QString &line)
{
    if (o.progress)
        o.progress(line);
}

// KWin: Aurorae, with the look's theme. (Aurorae keeps one QML renderer for
// every SVG theme, with its shadow palette cached: switching straight from
// one of ours to another kept the old one's, so it goes by Breeze first.)
void applyDecoration(Session &session, const QString &id)
{
    const QStringList group{QStringLiteral("org.kde.kdecoration2")};
    if (session.readConfig(QStringLiteral("kwinrc"), group, QStringLiteral("theme")).startsWith(QLatin1String("__aurorae__svg__"))) {
        session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("library"), QStringLiteral("org.kde.breeze"));
        session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("theme"), QStringLiteral("Breeze"));
        session.reconfigureKWin();
        QThread::msleep(500);
    }
    session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("library"), QStringLiteral("org.kde.kwin.aurorae"));
    session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("theme"), QStringLiteral("__aurorae__svg__") + id);
    session.reconfigureKWin();
}

// The fonts, and the running session told: the platform theme only re-reads
// them on refreshFonts, the signal the Fonts settings send.
void applyFonts(Session &session, const QList<ConfigValue> &fonts)
{
    for (const ConfigValue &v : fonts)
        session.writeConfig(v.file, v.group.split(QLatin1Char('/')), v.key, v.value);
    session.signal(QStringLiteral("/KDEPlatformTheme"), QStringLiteral("org.kde.KDEPlatformTheme"), QStringLiteral("refreshFonts"));
    session.reconfigureKWin();
}

QString iconHelper()
{
    for (const QString &p : {QStringLiteral("/usr/lib/plasma-changeicons"), QStringLiteral("/usr/lib64/plasma-changeicons"),
                             QStringLiteral("/usr/libexec/plasma-changeicons")})
        if (QFileInfo::exists(p))
            return p;
    return QString();
}

} // namespace

QString scriptFrom(const QString &name, const QVariantMap &values)
{
    QString text = resourceText(QStringLiteral(":/scripts/") + name + QLatin1String(".js"));
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QJsonValue v = QJsonValue::fromVariant(it.value());
        QString json;
        if (v.isObject())
            json = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        else if (v.isArray())
            json = QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
        else   // (a scalar, as JSON: wrap it in an array and take it out again)
            json = QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
        text.replace(QLatin1String("${") + it.key() + QLatin1Char('}'), json);
    }
    return text;
}

QStringList generatedPackages(const QStringList &keep)
{
    QStringList found;
    const struct { QString dir; QStringList meta; } kinds[] = {
        {share(QStringLiteral("aurorae/themes")), {QStringLiteral("metadata.json"), QStringLiteral("metadata.desktop")}},
        {share(QStringLiteral("plasma/desktoptheme")), {QStringLiteral("metadata.json"), QStringLiteral("metadata.desktop")}},
        {share(QStringLiteral("icons")), {QStringLiteral("xplasma-icons.json"), QStringLiteral("xp2plasma-icons.json")}},
    };
    for (const auto &k : kinds)
        for (const QFileInfo &pkg : QDir(k.dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (keep.contains(pkg.absoluteFilePath()) || pkg.isSymLink())
                continue;
            for (const QString &m : k.meta)
                if (ours(pkg.filePath() + QLatin1Char('/') + m)) {
                    found << pkg.absoluteFilePath();
                    break;
                }
        }
    for (const QFileInfo &f : QDir(share(QStringLiteral("color-schemes"))).entryInfoList({QStringLiteral("*.colors")}, QDir::Files))
        if (!keep.contains(f.absoluteFilePath()) && ours(f.absoluteFilePath()))
            found << f.absoluteFilePath();
    return found;
}

bool applyWidgets(Session &session, const Look &look, const std::function<void(const QString &)> &progress, QString *error)
{
    State state = State::load();
    QStringList log;
    const bool ok = qtstyle::apply(session, look, state, &log, error);
    for (const QString &line : std::as_const(log))
        if (progress)
            progress(QStringLiteral("  ") + line);
    state.save();
    return ok;
}

bool applyLook(Session &session, const Look &look, const ApplyOptions &o, QString *error)
{
    State state = State::load();
    const QString build = paths::build() + QLatin1Char('/') + look.id;
    QDir(build).removeRecursively();

    // --- generate ------------------------------------------------------------
    report(o, QStringLiteral("Generating %1 ...").arg(look.name));
    const QString decoration = writeDecoration(look, build + QLatin1String("/aurorae"), error);
    if (decoration.isEmpty())
        return false;
    const QString panel = o.taskbar ? writePanelTheme(look, build + QLatin1String("/desktoptheme"), error) : QString();
    if (o.taskbar && panel.isEmpty())
        return false;
    const QString startDir = paths::data() + QLatin1String("/start/") + look.id;
    QJsonObject startTheme;
    if (o.taskbar) {
        QDir(startDir).removeRecursively();
        startTheme = writeStartTheme(look, startDir, error);
        if (startTheme.isEmpty())
            return false;
    }
    QString iconTheme;
    if (o.icons) {
        QString previous = state.settings.value(QStringLiteral("previousIconTheme")).toString();
        const QString current = session.readConfig(QStringLiteral("kdeglobals"), {QStringLiteral("Icons")}, QStringLiteral("Theme"));
        if (previous.isEmpty() && current != QLatin1String(kIconThemeId) && current != QLatin1String("xp2plasma-icons"))
            previous = current.isEmpty() ? QStringLiteral("breeze") : current;
        if (!previous.isEmpty())
            state.settings.insert(QStringLiteral("previousIconTheme"), previous);
        QString why;
        iconTheme = writeIconTheme(build + QLatin1String("/icons"), previous, &why);
        if (iconTheme.isEmpty())
            report(o, QStringLiteral("  no XP icons: %1").arg(why));
    }

    // --- install ---------------------------------------------------------------
    report(o, QStringLiteral("Installing ..."));
    const QString auroraeDest = share(QStringLiteral("aurorae/themes/")) + look.id;
    const QString colorsDest = share(QStringLiteral("color-schemes/")) + look.id + QLatin1String(".colors");
    const QString panelDest = share(QStringLiteral("plasma/desktoptheme/")) + look.id + QLatin1String("-taskbar");
    const QString iconsDest = share(QStringLiteral("icons/")) + QLatin1String(kIconThemeId);
    if (!install(decoration, auroraeDest, error))
        return false;
    QDir().mkpath(QFileInfo(colorsDest).absolutePath());
    {
        QSaveFile f(colorsDest);
        if (!f.open(QIODevice::WriteOnly) || f.write(colorScheme(look.system, look.id)) < 0 || !f.commit())
            return fail(error, QStringLiteral("%1: %2").arg(colorsDest, f.errorString()));
    }
    if (o.taskbar) {
        if (!install(panel, panelDest, error) || !installStartPlasmoid(share(QStringLiteral("plasma/plasmoids")), error))
            return false;
        QString why;
        if (!overrides::installPanelWidgets(share(QStringLiteral("plasma/plasmoids")), &why))
            report(o, QStringLiteral("  the tray and clock stay KDE's: %1").arg(why));
    }
    if (!iconTheme.isEmpty() && !install(iconTheme, iconsDest, error))
        return false;
    QString why;
    if (!overrides::installTitleShadows(share(QStringLiteral("aurorae/themes")), share(QStringLiteral("kwin/aurorae/aurorae.qml")), &why))
        report(o, QStringLiteral("  no title shadows: %1").arg(why));
    // Other looks' packages go: one of each at a time.
    for (const QString &stale : generatedPackages({auroraeDest, colorsDest, panelDest, iconsDest})) {
        if (QFileInfo(stale).isDir())
            QDir(stale).removeRecursively();
        else
            QFile::remove(stale);
    }

    // --- apply -------------------------------------------------------------------
    report(o, QStringLiteral("Applying ..."));
    applyDecoration(session, look.id);
    if (!session.run({QStringLiteral("plasma-apply-colorscheme"), look.id}).ok())
        report(o, QStringLiteral("  plasma-apply-colorscheme failed"));
    applyFonts(session, fontSettings(look.system));
    if (o.widgets) {
        QStringList log;
        if (!qtstyle::apply(session, look, state, &log, &why))
            report(o, QStringLiteral("  Qt apps keep their style: %1").arg(why));
        for (const QString &line : std::as_const(log))
            report(o, QStringLiteral("  ") + line);
    }
    if (o.taskbar) {
        const QString panelId = look.id + QLatin1String("-taskbar");
        session.run({QStringLiteral("plasma-apply-desktoptheme"), panelId});
        // A running plasmashell neither finds a theme package that appeared
        // after it started nor re-reads the current one's SVGs, and caches
        // them by the theme's name (another colour of the same style came up
        // in the old one's art): it restarts, its caches gone while it's down.
        report(o, QStringLiteral("Restarting Plasma's shell for the taskbar ..."));
        session.restartPlasmaShell([&] {
            const QString cache = paths::home() + QLatin1String("/.cache/");
            QFile::remove(cache + QLatin1String("plasma_theme_") + panelId + QLatin1String(".kcache"));
            QFile::remove(cache + QLatin1String("ksvg-elements"));
        });
        session.script(scriptFrom(QStringLiteral("panel"), {{QStringLiteral("HEIGHT"), panelArt(look).height}}));
        // The clock in the style's clock font (Classic: the status font).
        LogFont clock = look.system.metrics.statusFont;
        if (!look.classic()) {
            const QString spec = look.renderer->property({QStringLiteral("clock"), 0, 0, QStringLiteral("traynotify")}, u"font")
                                     .value_or(QStringLiteral("Tahoma, 8"));
            const QStringList parts = spec.split(QLatin1Char(','));
            clock = LogFont::fromPoints(parts.value(0).trimmed(), parts.value(1).trimmed().toInt(), spec.contains(QLatin1String("bold"), Qt::CaseInsensitive));
        }
        const auto layout = session.script(scriptFrom(QStringLiteral("layout"),
                                                      {{QStringLiteral("START"), QLatin1String(kStartPlasmoidId)},
                                                       {QStringLiteral("THEME"), startTheme.toVariantMap()},
                                                       {QStringLiteral("CLOCK_FAMILY"), clock.face},
                                                       {QStringLiteral("CLOCK_POINTS"), clock.points()},
                                                       {QStringLiteral("CLOCK_WEIGHT"), clock.weight}}),
                                           &why);
        if (!layout) {
            report(o, QStringLiteral("  the panel wasn't rearranged: %1").arg(why));
        } else {
            const QJsonObject result = QJsonDocument::fromJson(layout->toUtf8()).object();
            for (const QJsonValue &line : result.value(QStringLiteral("log")).toArray())
                report(o, QStringLiteral("  ") + line.toString());
            // Plasma's scripting can't reorder a panel: its AppletOrder is
            // written with the shell down (it saves its own on the way out).
            QList<std::pair<QString, QString>> orders;
            for (const QJsonValue &p : result.value(QStringLiteral("panels")).toArray()) {
                QStringList ids;
                for (const QJsonValue &id : p.toObject().value(QStringLiteral("order")).toArray())
                    ids << QString::number(id.toInt());
                const QString panelNumber = QString::number(p.toObject().value(QStringLiteral("panel")).toInt());
                const QString wanted = ids.join(QLatin1Char(';'));
                if (session.readConfig(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"),
                                       {QStringLiteral("Containments"), panelNumber, QStringLiteral("General")}, QStringLiteral("AppletOrder"))
                    != wanted)
                    orders.append({panelNumber, wanted});
            }
            if (!orders.isEmpty()) {
                report(o, QStringLiteral("Restarting Plasma's shell for the taskbar's order ..."));
                session.restartPlasmaShell([&] {
                    for (const auto &[panelNumber, order] : orders)
                        session.writeConfig(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"),
                                            {QStringLiteral("Containments"), panelNumber, QStringLiteral("General")},
                                            QStringLiteral("AppletOrder"), order, false);
                });
            }
        }
    }
    if (o.wallpaper) {
        const Wallpaper &w = *o.wallpaper;
        session.script(scriptFrom(QStringLiteral("wallpaper"),
                                  {{QStringLiteral("IMAGE"), w.image.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(QUrl::fromLocalFile(w.image).toString())},
                                   {QStringLiteral("FILL"), w.fill ? QVariant(*w.fill) : QVariant::fromValue(nullptr)},
                                   {QStringLiteral("COLOUR"), w.colour.name()}}));
        report(o, w.image.isEmpty() ? QStringLiteral("Desktop: %1").arg(w.colour.name()) : QStringLiteral("Wallpaper: %1").arg(QFileInfo(w.image).fileName()));
    }
    if (!iconTheme.isEmpty()) {
        const QString helper = iconHelper();
        if (helper.isEmpty())
            report(o, QStringLiteral("  Plasma's icon helper isn't here; the XP icons are installed but not switched on"));
        else
            session.run({helper, QLatin1String(kIconThemeId)});
    }

    state.appliedKind = look.classic() ? QStringLiteral("classic") : QStringLiteral("style");
    state.style = look.stylePath;
    state.variant = look.variant;
    state.scheme = look.classic() ? look.system.name : QString();
    state.themeFile = o.themeFile;
    state.save();
    report(o, QStringLiteral("Done."));
    return true;
}

bool resetDesktop(Session &session, const std::function<void(const QString &)> &progress, QString *error)
{
    auto say = [&](const QString &line) {
        if (progress)
            progress(line);
    };
    State state = State::load();
    QStringList log;
    say(QStringLiteral("Taking XPLasma's widget style off ..."));
    qtstyle::revert(session, state, &log);
    for (const QString &line : std::as_const(log))
        say(QStringLiteral("  ") + line);
    overrides::removeTitleShadows(share(QStringLiteral("kwin/aurorae/aurorae.qml")));
    overrides::removePanelWidgets(share(QStringLiteral("plasma/plasmoids")));

    say(QStringLiteral("Clearing the XP fonts ..."));
    for (const ConfigValue &v : fontSettings(ClassicScheme()))
        session.deleteConfig(v.file, v.group.split(QLatin1Char('/')), v.key);
    session.signal(QStringLiteral("/KDEPlatformTheme"), QStringLiteral("org.kde.KDEPlatformTheme"), QStringLiteral("refreshFonts"));

    // Plasma's own tool does the heavy lifting: the global theme (borders,
    // colours, Plasma style, widget style, icons) and the default panel.
    const QString lookAndFeel = QStringLiteral("org.kde.breeze.desktop");
    say(QStringLiteral("Applying %1 with the default panel ...").arg(lookAndFeel));
    const host::Result r = session.run({QStringLiteral("plasma-apply-lookandfeel"), QStringLiteral("-a"), lookAndFeel, QStringLiteral("--resetLayout")});
    if (!r.ok())
        return fail(error, QStringLiteral("plasma-apply-lookandfeel failed: %1").arg(QString::fromUtf8(r.err).trimmed()));
    session.restartPlasmaShell();

    // The wallpaper from the global theme's own defaults, set outright.
    for (const QString &root : {QStringLiteral("/usr/share"), QStringLiteral("/usr/local/share")}) {
        QFile defaults(root + QLatin1String("/plasma/look-and-feel/") + lookAndFeel + QLatin1String("/contents/defaults"));
        if (!defaults.open(QIODevice::ReadOnly))
            continue;
        QString section, image;
        for (const QString &line : QString::fromUtf8(defaults.readAll()).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1Char('[')))
                section = line.trimmed();
            else if (section == QLatin1String("[Wallpaper]") && line.startsWith(QLatin1String("Image=")))
                image = line.mid(6).trimmed();
        }
        const QString path = image.startsWith(QLatin1Char('/')) ? image : root + QLatin1String("/wallpapers/") + (image.isEmpty() ? QStringLiteral("Next") : image);
        if (QFileInfo::exists(path)) {
            session.script(scriptFrom(QStringLiteral("wallpaper"), {{QStringLiteral("IMAGE"), path},
                                                                     {QStringLiteral("FILL"), QVariant::fromValue(nullptr)},
                                                                     {QStringLiteral("COLOUR"), QStringLiteral("#000000")}}));
            say(QStringLiteral("Wallpaper: %1").arg(QFileInfo(path).fileName()));
        }
        break;
    }

    // The icons the user had.
    const QString previousIcons = state.settings.value(QStringLiteral("previousIconTheme")).toString();
    if (const QString helper = iconHelper(); !helper.isEmpty() && !previousIcons.isEmpty())
        session.run({helper, previousIcons});
    state.settings.remove(QStringLiteral("previousIconTheme"));

    say(QStringLiteral("Removing XPLasma's packages ..."));
    const QStringList packages = generatedPackages();
    for (const QString &p : packages) {
        if (QFileInfo(p).isDir())
            QDir(p).removeRecursively();
        else
            QFile::remove(p);
    }
    const QString start = share(QStringLiteral("plasma/plasmoids/")) + QLatin1String(kStartPlasmoidId);
    QDir(start).removeRecursively();
    QDir(paths::data() + QLatin1String("/start")).removeRecursively();
    say(QStringLiteral("  %1 removed").arg(packages.size() + 1));

    state.appliedKind.clear();
    state.style.clear();
    state.variant.clear();
    state.scheme.clear();
    state.themeFile.clear();
    state.save();
    session.reconfigureKWin();
    say(QStringLiteral("The desktop is Plasma's default again."));
    return true;
}

} // namespace xpl
