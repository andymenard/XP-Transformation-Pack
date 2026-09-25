// xplasma: the command line.
//
//   xplasma --version
//   xplasma list [--json]      the library: visual styles, themes, Classic schemes
//   xplasma apply LOOK [--no-widgets] [--no-taskbar] [--no-icons] [--no-sounds] [--no-wallpaper]
//                              put a look on the running desktop
//   xplasma widgets LOOK       only Qt apps' widgets
//   xplasma reset              Plasma's default look back, XPlasma's gone
//   xplasma setup [--media PATH | --download-media] [--no-packs] [--all-packs]
//                              XP's media and the official theme packs
//   xplasma pointers [SCHEME [--no-shadow] [--out DIR]]
//                              XP's pointer schemes; one applied (or, --out,
//                              written as a cursor theme there)
//   xplasma check-forks        (at login) XPlasma's builds of Plasma's widgets
//                              matched to the Plasma there is, after an upgrade
//   xplasma build LOOK [--out DIR]
//                              generate the Plasma packages for a look without
//                              applying them (DIR: ~/.cache/xplasma/build)
//
// A LOOK is a .msstyles (optionally :VARIANT_INI), a .theme, or
// classic:SCHEME -- or its name in `list` ("Windows XP", "Silver", "Brick").
#include "app/adopt.h"
#include "app/apply.h"
#include "app/colors.h"
#include "app/controls.h"
#include "app/decoration.h"
#include "app/desktop.h"
#include "app/pointers.h"
#include "app/icons.h"
#include "app/media.h"
#include "app/forks.h"
#include "app/gtk.h"
#include "app/panel.h"
#include "app/qttarget.h"
#include "app/startbutton.h"
#include "app/startmenu.h"
#include "app/startpanel.h"
#include "app/library.h"
#include "app/paths.h"
#include "app/setup.h"
#include "app/sounds.h"
#include "app/state.h"
#include "app/xptext.h"
#include "core/themefile.h"

#include <QCommandLineParser>
#include <QApplication>
#include <QGuiApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using namespace xpl;

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

void adopt()
{
    for (const QString &line : adoptLegacy())
        err() << line << Qt::endl;
}

int list(bool json)
{
    adopt();
    const Library lib = Library::scan();
    const auto schemeList = schemes::list();
    if (json) {
        QJsonArray styles, themes, classic;
        for (const StyleInfo &s : lib.styles)
            styles.append(s.toJson());
        for (const ThemeBundle &t : lib.themes)
            themes.append(t.toJson());
        for (const schemes::Entry &e : schemeList)
            classic.append(QJsonObject{{QStringLiteral("name"), e.name}, {QStringLiteral("path"), e.path},
                                       {QStringLiteral("stock"), e.stock}});
        out() << QJsonDocument(QJsonObject{{QStringLiteral("styles"), styles},
                                           {QStringLiteral("themes"), themes},
                                           {QStringLiteral("schemes"), classic}})
                     .toJson();
        return 0;
    }
    out() << "Visual styles (" << paths::themes() << "):\n";
    for (const StyleInfo &s : lib.styles) {
        out() << "  " << s.label;
        if (!s.readable()) {
            out() << "  -- can't use: " << s.error << '\n';
            continue;
        }
        QStringList colours;
        for (const StyleOption &c : s.colours)
            colours << c.label;
        out() << ": " << colours.join(QStringLiteral(", ")) << '\n';
    }
    out() << "Themes:\n";
    for (const ThemeBundle &t : lib.themes) {
        out() << "  " << t.label;
        if (!t.styleMissing.isEmpty())
            out() << "  -- its style isn't here (" << t.styleMissing << ')';
        else if (t.classic())
            out() << "  (Windows Classic)";
        out() << '\n';
    }
    QStringList names;
    for (const schemes::Entry &e : schemeList)
        names << e.name + (e.stock ? QString() : QStringLiteral(" (yours)"));
    out() << "Windows Classic schemes: " << names.join(QStringLiteral(", ")) << Qt::endl;
    return 0;
}

// A look from the command line: STYLE[:VARIANT_INI], a .theme, or classic:SCHEME.
// A look as `list` names it -- a theme ("Windows XP"), a visual style
// ("Windows XP style") or one of its colours ("Silver", "Windows XP style:
// Silver"), a Windows Classic scheme ("Brick") -- as a LOOK; a LOOK as it is.
std::optional<QString> named(const QString &name, QString *error)
{
    const QString file = name.contains(QLatin1Char('/')) ? name : name.section(QLatin1Char(':'), 0, 0);
    if (name.startsWith(QLatin1String("classic:"), Qt::CaseInsensitive) || QFileInfo::exists(name) || QFileInfo::exists(file))
        return name;
    const auto is = [&](const QString &label) { return label.compare(name, Qt::CaseInsensitive) == 0; };
    const Library lib = Library::scan();
    for (const ThemeBundle &t : lib.themes)
        if (is(t.label))
            return t.path;
    QStringList found;
    for (const StyleInfo &s : lib.styles) {
        if (!s.readable())
            continue;
        if (is(s.label))
            found << s.path;
        const StyleVariant *standard = s.variant(s.defaultVariant);
        for (const StyleOption &c : s.colours) {
            if (!is(c.label) && !is(s.label + QStringLiteral(": ") + c.label))
                continue;
            QString ini = s.ini(c.key, standard ? standard->size : QString());
            if (ini.isEmpty() && !s.sizesFor(c.key).isEmpty())
                ini = s.ini(c.key, s.sizesFor(c.key).first().key);
            if (!ini.isEmpty())
                found << s.path + QLatin1Char(':') + ini;
        }
    }
    for (const schemes::Entry &e : schemes::list())
        if (is(e.name))
            found << QStringLiteral("classic:") + e.name;
    found.removeDuplicates();
    if (found.size() == 1)
        return found.first();
    *error = found.isEmpty()
        ? QStringLiteral("no look called \"%1\" (`xplasma list` shows them; or name a .theme or .msstyles file)").arg(name)
        : QStringLiteral("\"%1\" could be any of: %2").arg(name, found.join(QStringLiteral(", ")));
    return std::nullopt;
}

std::optional<Look> lookFrom(const QString &spec, QString *error)
{
    if (spec.startsWith(QLatin1String("classic:"), Qt::CaseInsensitive)) {
        const auto scheme = schemes::load(spec.mid(8), error);
        return scheme ? std::optional(Look::fromScheme(*scheme)) : std::nullopt;
    }
    if (spec.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive)) {
        const ThemeBundle b = Library::bundle(spec);
        if (!b.styleMissing.isEmpty()) {
            *error = QStringLiteral("%1 names %2, which isn't here").arg(spec, b.styleMissing);
            return std::nullopt;
        }
        if (b.classic()) {
            const auto file = ThemeFile::load(spec, error);
            if (!file)
                return std::nullopt;
            ClassicScheme scheme = ClassicScheme::fromThemeFile(*file);
            scheme.name = b.label;
            return Look::fromScheme(scheme);
        }
        return Look::fromStyle(b.style, b.variant, error);
    }
    QString path = spec, variant;
    if (const int colon = int(spec.lastIndexOf(QLatin1Char(':'))); colon > 0 && spec.mid(colon + 1).endsWith(QLatin1String("_INI"), Qt::CaseInsensitive)) {
        path = spec.left(colon);
        variant = spec.mid(colon + 1).toUpper();
    }
    return Look::fromStyle(path, variant, error);
}

void say(const QString &line)
{
    out() << line << Qt::endl;
}

int apply(const QString &name, const QCommandLineParser &parser)
{
    adopt();
    QString error;
    const QString spec = named(name, &error).value_or(QString());
    const auto look = spec.isEmpty() ? std::nullopt : lookFrom(spec, &error);
    if (!look) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    ApplyOptions o;
    o.widgets = !parser.isSet(QStringLiteral("no-widgets"));
    o.taskbar = !parser.isSet(QStringLiteral("no-taskbar"));
    o.icons = !parser.isSet(QStringLiteral("no-icons"));
    o.sounds = !parser.isSet(QStringLiteral("no-sounds"));
    o.progress = say;
    // The wallpaper: a .theme's own, or Windows Classic's desktop colour.
    if (!parser.isSet(QStringLiteral("no-wallpaper"))) {
        if (spec.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive)) {
            const ThemeBundle b = Library::bundle(spec);
            o.themeFile = QFileInfo(spec).absoluteFilePath();
            const auto file = ThemeFile::load(spec);
            const ClassicScheme colours = file ? ClassicScheme::fromThemeFile(*file) : ClassicScheme();
            if (!b.wallpaper.isEmpty() || look->classic())
                o.wallpaper = Wallpaper{b.wallpaper, b.fill, colours.colour(u"Background")};
        } else if (look->classic()) {
            o.wallpaper = Wallpaper{QString(), std::nullopt, look->system.colour(u"Background")};
        }
    }
    LiveSession session;
    if (!applyLook(session, *look, o, &error)) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    return 0;
}

int widgets(const QString &name)
{
    adopt();
    QString error;
    const QString spec = named(name, &error).value_or(QString());
    const auto look = spec.isEmpty() ? std::nullopt : lookFrom(spec, &error);
    LiveSession session;
    if (!look || !applyWidgets(session, *look, say, &error)) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    return 0;
}

int pointerSchemes(const QString &name, bool shadow, const QString &out)
{
    QString error;
    const QList<pointers::Scheme> all = pointers::schemes(&error);
    if (all.isEmpty()) {
        err() << "xplasma: no XP pointer schemes (" << (error.isEmpty() ? QStringLiteral("run setup with XP's media") : error) << ")" << Qt::endl;
        return 1;
    }
    if (name.isEmpty()) {
        for (const pointers::Scheme &s : all)
            say(s.name);
        return 0;
    }
    const auto it = std::find_if(all.begin(), all.end(), [&](const pointers::Scheme &s) { return s.name.compare(name, Qt::CaseInsensitive) == 0; });
    if (it == all.end()) {
        err() << "xplasma: no pointer scheme " << name << Qt::endl;
        return 1;
    }
    if (!out.isEmpty()) {
        int size = 0;
        if (!pointers::writeTheme(*it, shadow, out, &size, &error)) {
            err() << "xplasma: " << error << Qt::endl;
            return 1;
        }
        say(QStringLiteral("%1: %2 (%3 px)").arg(it->name, out).arg(size));
        return 0;
    }
    LiveSession session;
    State state = State::load();
    if (!pointers::apply(session, state, *it, shadow, &error)) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    state.save();
    say(QStringLiteral("Pointers: %1%2").arg(it->name, shadow ? QStringLiteral(", with shadows") : QString()));
    return 0;
}

// A sound scheme: XP's (Windows Default, No Sounds) or one saved in Sounds
// and Audio Devices; "off", the user's own sounds back; none, the list.
int soundSchemes(const QString &name, const QString &out)
{
    State state = State::load();
    const QList<sounds::Scheme> all = sounds::builtinSchemes() + sounds::savedSchemes(state);
    if (name.isEmpty()) {
        for (const sounds::Scheme &s : all)
            say(s.name);
        return 0;
    }
    LiveSession session;
    if (name == QLatin1String("off")) {
        sounds::turnOff(session, state);
        state.save();
        say(QStringLiteral("Sounds: the desktop's own (looks applied from now on leave them be)"));
        return 0;
    }
    const auto it = std::find_if(all.begin(), all.end(), [&](const sounds::Scheme &s) { return s.name.compare(name, Qt::CaseInsensitive) == 0; });
    if (it == all.end()) {
        err() << "xplasma: no sound scheme " << name << Qt::endl;
        return 1;
    }
    QString error;
    if (!out.isEmpty()) {
        if (!sounds::writeTheme(*it, out, &error)) {
            err() << "xplasma: " << error << Qt::endl;
            return 1;
        }
        say(QStringLiteral("%1: %2").arg(it->name, out));
        return 0;
    }
    if (!sounds::apply(session, state, *it, &error)) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    state.save();
    say(QStringLiteral("Sounds: %1").arg(it->name));
    return 0;
}

// A Plasma session XPlasma can dress, for the commands that change the
// desktop; else why not.
bool requirePlasma()
{
    LiveSession session;
    const desktop::Report r = desktop::check(session);
    if (r.ok())
        return true;
    err() << "xplasma: this needs a KDE Plasma session it can dress; not here:\n  " << QString(r.problems()).replace(QLatin1Char('\n'), QStringLiteral("\n  "))
          << Qt::endl;
    return false;
}

// Whether this desktop can take XPlasma's whole look -- what apply checks
// before changing anything -- and, --json, the details (Setup's check).
int check(bool json)
{
    LiveSession session;
    const desktop::Report r = desktop::check(session);
    const QStringList cannot = r.ok() ? cannotTake(session) : QStringList();
    const bool ok = r.ok() && cannot.isEmpty();
    if (json) {
        QJsonArray unmet;
        for (const desktop::Requirement &q : r.requirements)
            if (!q.met)
                unmet.append(QJsonObject{{QStringLiteral("what"), q.what}, {QStringLiteral("found"), q.found}});
        const QString failure = cannot.join(QLatin1Char('\n'));   // (unmet requirements are in the diagnostics already)
        const QJsonObject o{{QStringLiteral("ok"), ok},
                            {QStringLiteral("plasma"), r.plasma},
                            {QStringLiteral("newestKnownPlasma"), desktop::newestKnownPlasma()},
                            {QStringLiteral("newer"), r.newer},
                            {QStringLiteral("unmet"), unmet},
                            {QStringLiteral("cannot"), QJsonArray::fromStringList(cannot)},
                            {QStringLiteral("diagnostics"), desktop::diagnostics(session, ok ? QString() : failure)}};
        out() << QJsonDocument(o).toJson(QJsonDocument::Indented);
        return ok ? 0 : 1;
    }
    for (const desktop::Requirement &q : r.requirements)
        say(QStringLiteral("%1 %2%3").arg(q.met ? QStringLiteral("ok  ") : QStringLiteral("NO  "), q.what,
                                          q.found.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(q.found)));
    if (r.newer)
        say(QStringLiteral("note: Plasma %1 is newer than XPlasma's builds (up to %2)").arg(r.plasma, desktop::newestKnownPlasma()));
    for (const QString &c : cannot)
        say(QStringLiteral("NO  %1").arg(c));
    say(ok ? QStringLiteral("This desktop can take the whole XP look.") : QStringLiteral("This desktop can't take the whole XP look."));
    return ok ? 0 : 1;
}

int reset()
{
    LiveSession session;
    QString error;
    if (!resetDesktop(session, say, &error)) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    return 0;
}

int runSetup(const QCommandLineParser &parser)
{
    adopt();
    QString detail;
    if (!setup::plasmaSession(&detail))
        err() << "xplasma: this isn't a Plasma 6 session (" << detail << "); setup goes on, but applying needs one" << Qt::endl;
    bool missing = false;
    for (const setup::Check &c : setup::preflight())
        if (!c.ok() && c.neededToApply) {
            err() << "missing: " << c.tool << " (" << c.purpose << ")" << Qt::endl;
            missing = true;
        }
    if (missing)
        err() << "Install the programs above to apply looks." << Qt::endl;
    QString error;
    auto bytes = [](qint64 done, qint64 total) {
        if (total > 0)
            err() << "\r  " << done * 100 / total << "% of " << total / 1048576 << " MB" << Qt::flush;
    };
    if (parser.isSet(QStringLiteral("media"))) {
        if (!setup::importMedia(parser.value(QStringLiteral("media")), nullptr, say, nullptr, &error)) {
            err() << "xplasma: " << error << Qt::endl;
            return 1;
        }
    }
    for (const setup::Download &d : setup::downloads()) {
        if (d.media ? !parser.isSet(QStringLiteral("download-media")) : parser.isSet(QStringLiteral("no-packs")))
            continue;
        if (d.optional && !parser.isSet(QStringLiteral("all-packs"))) {
            say(QStringLiteral("Skipping %1 (--all-packs fetches it)").arg(d.label));
            continue;
        }
        if (!setup::fetch(d, say, bytes, nullptr, &error)) {
            err() << "\nxplasma: " << error << Qt::endl;
            return 1;
        }
    }
    say(QStringLiteral("Setup finished."));
    return 0;
}

int build(const QString &name, QString dir)
{
    adopt();
    QString error;
    const QString spec = named(name, &error).value_or(QString());
    const auto look = spec.isEmpty() ? std::nullopt : lookFrom(spec, &error);
    if (!look) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    if (dir.isEmpty())
        dir = paths::build();
    out() << look->name << '\n';
    const QString colors = dir + QLatin1String("/color-schemes/") + look->id + QLatin1String(".colors");
    QDir().mkpath(QFileInfo(colors).absolutePath());
    QFile c(colors);
    if (!c.open(QIODevice::WriteOnly) || c.write(colorScheme(look->system, look->name)) < 0) {
        err() << "xplasma: " << colors << ": " << c.errorString() << Qt::endl;
        return 1;
    }
    out() << "  colours     " << colors << '\n';
    const QString decoration = writeDecoration(*look, dir + QLatin1String("/aurorae"), &error);
    if (decoration.isEmpty()) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    out() << "  decoration  " << decoration << '\n';
    const QString panel = writePanelTheme(*look, dir + QLatin1String("/desktoptheme"), &error);
    if (panel.isEmpty()) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    out() << "  taskbar     " << panel << '\n';
    const QString startDir = dir + QLatin1String("/start/") + look->id;
    if (writeStartTheme(*look, startDir, &error).isEmpty()) {
        err() << "xplasma: the Start button and menu: " << error << Qt::endl;
        return 1;
    }
    out() << "  start       " << startDir << "/theme.json\n";
    const QString icons = writeIconTheme(dir + QLatin1String("/icons"), QStringLiteral("breeze"), panelArt(*look).trayTextColour, &error, spinIcons(*look));
    if (icons.isEmpty()) {
        err() << "xplasma: the icons: " << error << Qt::endl;
        return 1;
    }
    out() << "  icons       " << icons << '\n';
    const QString gtkTheme = gtk::writeTheme(*look, dir + QLatin1String("/themes"), &error);
    if (gtkTheme.isEmpty()) {
        err() << "xplasma: the GTK theme: " << error << Qt::endl;
        return 1;
    }
    out() << "  gtk         " << gtkTheme << '\n';
    for (const ConfigValue &v : fontSettings(look->system))
        out() << "  font        " << v.file << " [" << v.group << "] " << v.key << '=' << v.value << '\n';
    out().flush();
    return 0;
}

// Building Plasma's widgets (build-forks.sh): a series' image, Qt floor,
// At login (XPlasma's plasma-workspace env script): XPlasma's builds of
// Plasma's widgets still right for the Plasma there is, after an upgrade.
// Says what it changed, if anything.
int checkForks()
{
    State state = State::load();
    const QJsonObject before = state.settings;
    LiveSession session;
    QString what;
    forks::check(session, state, &what);
    // (And the Flatpak style, for a runtime installed since.)
    QString flatpakWhat;
    qtstyle::refreshFlatpak(session, &flatpakWhat);
    if (!flatpakWhat.isEmpty())
        what += (what.isEmpty() ? QString() : QStringLiteral("\n")) + flatpakWhat;
    // (And XP's text for the display's scale now, if it changed last session.)
    if (xptext::refresh(session, state))
        what += (what.isEmpty() ? QString() : QStringLiteral("\n"))
                + QStringLiteral("XP's text: its smoothing for a display at %1x").arg(state.settings.value(QStringLiteral("xpTextScale")).toDouble());
    if (state.settings != before)
        state.save();
    if (!what.isEmpty())
        out() << what << Qt::endl;
    return 0;
}

// sources and targets, one per line ("image IMAGE", "qtfloor 6.10",
// "source PROJECT VERSION SHA256", "target PROJECT ID").
int forkInfo(const QString &name)
{
    QString error;
    const auto s = forks::series(name, &error);
    if (!s) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    out() << "image " << s->image << Qt::endl;
    out() << "qtfloor " << s->qtFloor << Qt::endl;
    for (const forks::Source &src : s->sources)
        out() << "source " << src.project << ' ' << src.version << ' ' << src.sha256 << Qt::endl;
    for (const forks::Widget &w : s->widgets)
        out() << "target " << w.project << ' ' << w.id << Qt::endl;
    return 0;
}

// ...and the series' patched source, synced from the pristine trees.
int forkSources(const QString &name, const QString &pristine, const QString &work)
{
    QString error;
    const auto s = forks::series(name, &error);
    const auto written = s ? forks::syncSources(*s, pristine, work, &error) : std::nullopt;
    if (!written) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    out() << *written << " file(s) changed" << Qt::endl;
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    // (A widgets application for fonts, painting and XPlasma's style, which
    // draws the Plasma theme's controls; it never shows anything.)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xplasma"));
    QCoreApplication::setApplicationVersion(QStringLiteral(XPLASMA_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows XP visual styles for KDE Plasma."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("list | check | apply LOOK | widgets LOOK | pointers [SCHEME] | sounds [SCHEME|off] | reset | setup | build LOOK"));
    parser.addOption(QCommandLineOption(QStringLiteral("media"), QStringLiteral("XP's install media (an ISO, or a folder with I386)."), QStringLiteral("PATH")));
    for (const char *name : {"no-widgets", "no-taskbar", "no-icons", "no-sounds", "no-wallpaper", "download-media", "no-packs", "all-packs", "no-shadow"})
        parser.addOption(QCommandLineOption(QString::fromLatin1(name)));
    const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Machine-readable output."));
    parser.addOption(jsonOption);
    const QCommandLineOption outOption(QStringLiteral("out"), QStringLiteral("Where build writes."), QStringLiteral("DIR"));
    parser.addOption(outOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString command = args.value(0);
    // (what changes the desktop needs one it can change; list, setup, build,
    // check and writing a theme --out don't)
    const bool changes = command == QLatin1String("apply") || command == QLatin1String("widgets") || command == QLatin1String("reset")
                         || ((command == QLatin1String("pointers") || command == QLatin1String("sounds")) && args.size() == 2
                             && !parser.isSet(outOption));
    if (changes && !requirePlasma())
        return 1;
    if (command == QLatin1String("check") && args.size() == 1)
        return check(parser.isSet(jsonOption));
    if (command == QLatin1String("list"))
        return list(parser.isSet(jsonOption));
    if (command == QLatin1String("apply") && args.size() == 2)
        return apply(args.at(1), parser);
    if (command == QLatin1String("widgets") && args.size() == 2)
        return widgets(args.at(1));
    if (command == QLatin1String("setup") && args.size() == 1)
        return runSetup(parser);
    if (command == QLatin1String("reset") && args.size() == 1)
        return reset();
    if (command == QLatin1String("pointers") && args.size() <= 2)
        return pointerSchemes(args.value(1), !parser.isSet(QStringLiteral("no-shadow")), parser.value(outOption));
    if (command == QLatin1String("sounds") && args.size() <= 2)
        return soundSchemes(args.value(1), parser.value(outOption));
    if (command == QLatin1String("build") && args.size() == 2)
        return build(args.at(1), parser.value(outOption));
    if (command == QLatin1String("check-forks") && args.size() == 1)
        return checkForks();
    if (command == QLatin1String("fork-info") && args.size() == 2)
        return forkInfo(args.at(1));
    if (command == QLatin1String("fork-sources") && args.size() == 4)
        return forkSources(args.at(1), args.at(2), args.at(3));
    if (command.isEmpty())
        parser.showHelp(2);
    err() << "xplasma: unknown command: " << command << Qt::endl;
    return 2;
}
