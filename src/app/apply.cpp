#include "app/apply.h"

#include "app/colors.h"
#include "app/decoration.h"
#include "app/icons.h"
#include "app/overrides.h"
#include "app/panel.h"
#include "app/pointers.h"
#include "app/paths.h"
#include "app/qttarget.h"
#include "app/startmenu.h"
#include "app/state.h"
#include "app/xptext.h"
#include "core/schema.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
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
        // Long enough for KWin to drop every Aurorae border: only then does
        // it let go of its QML engine, and so of the aurorae.qml it compiled
        // (a new title-shadow override isn't read otherwise). 500 ms wasn't.
        session.reconfigureKWin();
        QThread::msleep(2000);
    }
    session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("library"), QStringLiteral("org.kde.kwin.aurorae"));
    session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("theme"), QStringLiteral("__aurorae__svg__") + id);
    session.reconfigureKWin();
}

// KWin rules for XPLasma's own windows that XP gave no minimize or maximize
// box: the copy dialog (plasmashell's, titled "Copying..." and the like) and
// the editor (Display Properties and its dialogs, property sheets on XP).
// Wayland has no way for a window to say so itself. With neither box, the
// decoration shows Close alone, as XP does. Other rules are left as they are.
const QString kRulesFile = QStringLiteral("kwinrulesrc");
using Rule = QList<std::pair<QString, QString>>;
const Rule kNoBoxes{
    {QStringLiteral("types"), QStringLiteral("33")},            // normal, dialog
    {QStringLiteral("minimize"), QStringLiteral("false")},
    {QStringLiteral("minimizerule"), QStringLiteral("2")},      // force
    {QStringLiteral("maximizehoriz"), QStringLiteral("false")},
    {QStringLiteral("maximizehorizrule"), QStringLiteral("2")},
    {QStringLiteral("maximizevert"), QStringLiteral("false")},
    {QStringLiteral("maximizevertrule"), QStringLiteral("2")},
};
const QList<std::pair<QString, Rule>> kRules{
    {QStringLiteral("xplasma-copy-dialog"),
     Rule{{QStringLiteral("Description"), QStringLiteral("XPLasma: XP's copy dialog (Close only)")},
          {QStringLiteral("wmclass"), QStringLiteral("org.kde.plasmashell")},
          {QStringLiteral("wmclassmatch"), QStringLiteral("1")},      // exact
          {QStringLiteral("title"), QStringLiteral("\\.\\.\\.$")},
          {QStringLiteral("titlematch"), QStringLiteral("3")}}        // regular expression
         + kNoBoxes},
    {QStringLiteral("xplasma-display-properties"),
     Rule{{QStringLiteral("Description"), QStringLiteral("XPLasma: Display Properties (Close only)")},
          {QStringLiteral("wmclass"), QStringLiteral("fyi.hotsocket.xplasma")},
          {QStringLiteral("wmclassmatch"), QStringLiteral("1")}}
         + kNoBoxes},
};

QStringList ruleIds(Session &session)
{
    const QString list = session.readConfig(kRulesFile, {QStringLiteral("General")}, QStringLiteral("rules"));
    return list.isEmpty() ? QStringList() : list.split(QLatin1Char(','));
}

void setRuleIds(Session &session, const QStringList &ids)
{
    session.writeConfig(kRulesFile, {QStringLiteral("General")}, QStringLiteral("rules"), ids.join(QLatin1Char(',')), false);
    session.writeConfig(kRulesFile, {QStringLiteral("General")}, QStringLiteral("count"), QString::number(ids.size()), false);
}

void installWindowRules(Session &session)
{
    QStringList ids = ruleIds(session);
    for (const auto &[id, rule] : kRules) {
        for (const auto &[key, value] : rule)
            session.writeConfig(kRulesFile, {id}, key, value, false);
        if (!ids.contains(id))
            ids << id;
    }
    setRuleIds(session, ids);
}

void removeWindowRules(Session &session)
{
    QStringList ids = ruleIds(session);
    for (const auto &[id, rule] : kRules) {
        ids.removeAll(id);
        for (const auto &[key, value] : rule)
            session.deleteConfig(kRulesFile, {id}, key, false);
    }
    setRuleIds(session, ids);
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

// Plasma's helper that switches the icon theme: in its libexec, wherever the
// distribution keeps that -- found beside Plasma's own tools first.
QString iconHelper()
{
    QStringList candidates;
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("plasma-apply-colorscheme"));
    if (!tool.isEmpty()) {
        const QDir prefix(QFileInfo(tool).canonicalPath() + QLatin1String("/.."));
        candidates << prefix.filePath(QStringLiteral("libexec/plasma-changeicons"))
                   << prefix.filePath(QStringLiteral("lib/libexec/plasma-changeicons"))
                   << prefix.filePath(QStringLiteral("lib64/libexec/plasma-changeicons"));
        // (Debian's multiarch: lib/x86_64-linux-gnu/libexec)
        for (const QString &triplet : QDir(prefix.filePath(QStringLiteral("lib"))).entryList({QStringLiteral("*-linux-*")}, QDir::Dirs))
            candidates << prefix.filePath(QStringLiteral("lib/%1/libexec/plasma-changeicons").arg(triplet));
    }
    candidates << QStringLiteral("/usr/lib/plasma-changeicons") << QStringLiteral("/usr/lib64/plasma-changeicons")
               << QStringLiteral("/usr/libexec/plasma-changeicons");
    for (const QString &p : std::as_const(candidates))
        if (QFileInfo(p).isExecutable())
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

// "[A][B C][D]" -> {"A", "B C", "D"}
static QStringList configGroups(const QString &header)
{
    return header.trimmed().mid(1).chopped(1).split(QLatin1String("]["));
}

void settleTaskbars(Session &session, int height)
{
    auto lines = [](const QString &name) {
        QFile f(paths::config() + QLatin1Char('/') + name);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')) : QStringList();
    };
    // The bottom panels (location 4), from the layout.
    QSet<QString> bottom;
    QString containment;
    bool panel = false, atBottom = false;
    auto finish = [&] {
        if (!containment.isEmpty() && panel && atBottom)
            bottom.insert(containment);
    };
    for (const QString &line : lines(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"))) {
        if (line.startsWith(QLatin1Char('['))) {
            finish();
            const QStringList g = configGroups(line);
            containment = g.size() == 2 && g.at(0) == QLatin1String("Containments") ? g.at(1) : QString();
            panel = atBottom = false;
        } else if (!containment.isEmpty()) {
            panel |= line.trimmed() == QLatin1String("plugin=org.kde.panel");
            atBottom |= line.trimmed() == QLatin1String("location=4");
        }
    }
    finish();
    // Their views, as the shell keeps them: per panel, its defaults, and per
    // screen size (HorizontalNNNN).
    const QString file = QStringLiteral("plasmashellrc");
    QSet<QString> seen;
    for (const QString &line : lines(file)) {
        if (!line.startsWith(QLatin1String("[PlasmaViews][Panel ")))
            continue;
        const QStringList g = configGroups(line);
        if (g.size() < 2 || !bottom.contains(g.at(1).mid(6)) || seen.contains(g.join(QLatin1Char('/'))))
            continue;
        seen.insert(g.join(QLatin1Char('/')));
        const QString sub = g.value(2);
        if (sub == QLatin1String("Defaults")) {
            session.writeConfig(file, g, QStringLiteral("thickness"), QString::number(height), false);
            continue;
        }
        if (!sub.isEmpty() && !sub.startsWith(QLatin1String("Horizontal")))
            continue;
        session.writeConfig(file, g, QStringLiteral("floating"), QStringLiteral("0"), false);
        session.writeConfig(file, g, QStringLiteral("alignment"), QStringLiteral("1"), false);   // Qt::AlignLeft
        if (!sub.isEmpty()) {
            session.writeConfig(file, g, QStringLiteral("panelLengthMode"), QStringLiteral("0"), false);   // fill
            session.writeConfig(file, g, QStringLiteral("panelOpacity"), QStringLiteral("1"), false);      // opaque
        }
    }
}

bool applyLook(Session &session, const Look &look, const ApplyOptions &o, QString *error)
{
    // The whole XP look or none of it: what this Plasma can't take is known
    // before anything changes.
    {
        QStringList cannot;
        QString why;
        if (o.taskbar && !overrides::checkPanelWidgets(share(QStringLiteral("plasma/plasmoids")), &why))
            cannot << why.split(QLatin1Char('\n'));
        if (!overrides::checkDesktopMenu(share(QStringLiteral("plasma/plasmoids")), &why))
            cannot << why.split(QLatin1Char('\n'));
        if (!overrides::checkTitleShadows(share(QStringLiteral("kwin/aurorae/aurorae.qml")), &why))
            cannot << why;
        if (!cannot.isEmpty())
            return fail(error, QStringLiteral("this Plasma can't take the whole XP look yet, so nothing was changed:\n  ")
                                   + cannot.join(QStringLiteral("\n  ")));
    }
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
        // (the tray's icons XP has no art for, in the tray's text colour)
        iconTheme = writeIconTheme(build + QLatin1String("/icons"), previous, panelArt(look).trayTextColour, &why);
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
        const QString editor = QCoreApplication::applicationDirPath() + QLatin1String("/xplasma-edit");
        if (!overrides::installPanelWidgets(share(QStringLiteral("plasma/plasmoids")), QFileInfo(editor).isExecutable() ? editor : QString(), &why))
            for (const QString &line : why.split(QLatin1Char('\n')))
                report(o, QStringLiteral("  left as Plasma's own: %1").arg(line));
    }
    if (!iconTheme.isEmpty() && !install(iconTheme, iconsDest, error))
        return false;
    QString why;
    // XP's Properties on the desktop's menu: this XPLasma's editor.
    const QString editor = QCoreApplication::applicationDirPath() + QLatin1String("/xplasma-edit");
    if (!QFileInfo(editor).isExecutable())
        report(o, QStringLiteral("  no XP Properties on the desktop: %1 isn't there").arg(editor));
    else if (!overrides::installDesktopMenu(share(QStringLiteral("plasma/plasmoids")), editor, &why))
        report(o, QStringLiteral("  no XP Properties on the desktop: %1").arg(why));
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
    // XP's text first: the shell restarts below, and reads fontconfig anew.
    if (!xptext::apply(session, state, &why))
        report(o, QStringLiteral("  text stays as it was: %1").arg(why));
    installWindowRules(session);
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
            settleTaskbars(session, panelArt(look).height);
        });
        // (Again through the running shell, for panels it hadn't saved yet.)
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
    if (o.wallpaper)
        applyWallpaper(session, *o.wallpaper, o.progress);
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

void applyWallpaper(Session &session, const Wallpaper &w, const std::function<void(const QString &)> &progress)
{
    session.script(scriptFrom(QStringLiteral("wallpaper"),
                              {{QStringLiteral("IMAGE"), w.image.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(QUrl::fromLocalFile(w.image).toString())},
                               {QStringLiteral("FILL"), w.fill ? QVariant(*w.fill) : QVariant::fromValue(nullptr)},
                               {QStringLiteral("COLOUR"), w.colour.name()}}));
    if (progress)
        progress(w.image.isEmpty() ? QStringLiteral("Desktop: %1").arg(w.colour.name()) : QStringLiteral("Wallpaper: %1").arg(QFileInfo(w.image).fileName()));
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
    removeWindowRules(session);
    overrides::removePanelWidgets(share(QStringLiteral("plasma/plasmoids")));
    overrides::removeDesktopMenu(share(QStringLiteral("plasma/plasmoids")));

    say(QStringLiteral("Clearing the XP fonts ..."));
    xptext::revert(session, state);
    pointers::revert(session, state);
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
    for (const QString &root : paths::systemData()) {
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
