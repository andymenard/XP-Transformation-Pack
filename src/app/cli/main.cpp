// xplasma: the command line.
//
//   xplasma --version
//   xplasma list [--json]      the library: visual styles, themes, Classic schemes
//   xplasma apply LOOK [--no-widgets] [--no-taskbar] [--no-icons] [--no-wallpaper]
//                              put a look on the running desktop
//   xplasma widgets LOOK       only Qt apps' widgets
//   xplasma reset              Plasma's default look back, XPLasma's gone
//   xplasma build LOOK [--out DIR]
//                              generate the Plasma packages for a look without
//                              applying them (DIR: ~/.cache/xplasma/build)
//
// A LOOK is a .msstyles (optionally :VARIANT_INI), a .theme, or
// classic:SCHEME.
//
// (apply, widgets, reset and setup arrive with the phases that build them.)
#include "app/adopt.h"
#include "app/apply.h"
#include "app/colors.h"
#include "app/decoration.h"
#include "app/icons.h"
#include "app/media.h"
#include "app/panel.h"
#include "app/startbutton.h"
#include "app/startmenu.h"
#include "app/startpanel.h"
#include "app/library.h"
#include "app/paths.h"
#include "app/state.h"
#include "core/themefile.h"

#include <QCommandLineParser>
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

int apply(const QString &spec, const QCommandLineParser &parser)
{
    adopt();
    QString error;
    const auto look = lookFrom(spec, &error);
    if (!look) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    ApplyOptions o;
    o.widgets = !parser.isSet(QStringLiteral("no-widgets"));
    o.taskbar = !parser.isSet(QStringLiteral("no-taskbar"));
    o.icons = !parser.isSet(QStringLiteral("no-icons"));
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

int widgets(const QString &spec)
{
    adopt();
    QString error;
    const auto look = lookFrom(spec, &error);
    LiveSession session;
    if (!look || !applyWidgets(session, *look, say, &error)) {
        err() << "xplasma: " << error << Qt::endl;
        return 1;
    }
    return 0;
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

int build(const QString &spec, QString dir)
{
    adopt();
    QString error;
    const auto look = lookFrom(spec, &error);
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
    const QString icons = writeIconTheme(dir + QLatin1String("/icons"), QStringLiteral("breeze"), &error);
    if (icons.isEmpty()) {
        err() << "xplasma: the icons: " << error << Qt::endl;
        return 1;
    }
    out() << "  icons       " << icons << '\n';
    for (const ConfigValue &v : fontSettings(look->system))
        out() << "  font        " << v.file << " [" << v.group << "] " << v.key << '=' << v.value << '\n';
    out().flush();
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    // (A GUI application for fonts and painting; it never shows anything.)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xplasma"));
    QCoreApplication::setApplicationVersion(QStringLiteral(XPLASMA_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows XP visual styles for KDE Plasma."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("list | apply LOOK | widgets LOOK | reset | build LOOK"));
    for (const char *name : {"no-widgets", "no-taskbar", "no-icons", "no-wallpaper"})
        parser.addOption(QCommandLineOption(QString::fromLatin1(name)));
    const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Machine-readable output."));
    parser.addOption(jsonOption);
    const QCommandLineOption outOption(QStringLiteral("out"), QStringLiteral("Where build writes."), QStringLiteral("DIR"));
    parser.addOption(outOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString command = args.value(0);
    if (command == QLatin1String("list"))
        return list(parser.isSet(jsonOption));
    if (command == QLatin1String("apply") && args.size() == 2)
        return apply(args.at(1), parser);
    if (command == QLatin1String("widgets") && args.size() == 2)
        return widgets(args.at(1));
    if (command == QLatin1String("reset") && args.size() == 1)
        return reset();
    if (command == QLatin1String("build") && args.size() == 2)
        return build(args.at(1), parser.value(outOption));
    if (command.isEmpty())
        parser.showHelp(2);
    err() << "xplasma: unknown command: " << command << Qt::endl;
    return 2;
}
