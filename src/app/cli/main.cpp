// xplasma: the command line.
//
//   xplasma --version
//   xplasma list [--json]      the visual styles in the library, with their colours
//
// (apply, widgets, reset and setup arrive with the phases that build them.)
#include "app/adopt.h"
#include "app/paths.h"
#include "app/state.h"
#include "core/theme.h"

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
    QJsonArray styles;
    QDirIterator it(paths::themes(), {QStringLiteral("*.msstyles")}, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext())
        files << it.next();
    files.sort(Qt::CaseInsensitive);
    for (const QString &file : files) {
        QString error;
        const auto theme = Theme::load(file, QString(), &error);
        QJsonObject style{{QStringLiteral("path"), file}};
        if (!theme) {
            style[QStringLiteral("error")] = error;
        } else {
            style[QStringLiteral("colours")] = QJsonArray::fromStringList(theme->variants().colourNames);
            style[QStringLiteral("sizes")] = QJsonArray::fromStringList(theme->variants().sizeNames);
        }
        styles.append(style);
    }
    if (json) {
        out() << QJsonDocument(QJsonObject{{QStringLiteral("styles"), styles}}).toJson();
        return 0;
    }
    if (styles.isEmpty())
        out() << "no visual styles in " << paths::themes() << Qt::endl;
    for (const QJsonValue &v : styles) {
        const QJsonObject s = v.toObject();
        out() << QDir(paths::themes()).relativeFilePath(s[QStringLiteral("path")].toString());
        if (s.contains(QStringLiteral("error")))
            out() << "  (unreadable: " << s[QStringLiteral("error")].toString() << ')';
        else
            out() << "  " << s[QStringLiteral("colours")].toVariant().toStringList().join(QStringLiteral(", "));
        out() << Qt::endl;
    }
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
