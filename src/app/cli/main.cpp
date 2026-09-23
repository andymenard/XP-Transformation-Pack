// xplasma: the command line.
//
//   xplasma --version
//   xplasma list [--json]      the library: visual styles, themes, Classic schemes
//
// (apply, widgets, reset and setup arrive with the phases that build them.)
#include "app/adopt.h"
#include "app/library.h"
#include "app/paths.h"
#include "app/state.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDirIterator>
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xplasma"));
    QCoreApplication::setApplicationVersion(QStringLiteral(XPLASMA_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows XP visual styles for KDE Plasma."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("list"));
    const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Machine-readable output."));
    parser.addOption(jsonOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString command = args.value(0);
    if (command == QLatin1String("list"))
        return list(parser.isSet(jsonOption));
    if (command.isEmpty())
        parser.showHelp(2);
    err() << "xplasma: unknown command: " << command << Qt::endl;
    return 2;
}
