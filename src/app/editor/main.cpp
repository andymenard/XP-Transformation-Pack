// xplasma-edit: Display Properties.
//
//   xplasma-edit                    the dialog
//   xplasma-edit --screenshots DIR  every tab drawn to DIR/*.png, then exit
//                                   (tests; with QT_QPA_PLATFORM=offscreen)
#include "app/adopt.h"
#include "app/editor/displayproperties.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("xplasma-edit"));
    QApplication::setApplicationDisplayName(QStringLiteral("Display Properties"));
    QApplication::setApplicationVersion(QStringLiteral(XPLASMA_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("fyi.hotsocket.xplasma"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption screenshots(QStringLiteral("screenshots"), QStringLiteral("Draw every tab into DIR and exit."),
                                         QStringLiteral("DIR"));
    parser.addOption(screenshots);
    parser.process(app);

    for (const QString &line : xpl::adoptLegacy())
        QTextStream(stderr) << line << '\n';
    xpl::editor::DisplayProperties dialog;
    if (parser.isSet(screenshots))
        return dialog.saveScreenshots(parser.value(screenshots)) ? 0 : 1;
    dialog.show();
    return app.exec();
}
