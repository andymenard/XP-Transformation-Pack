// xplasma-edit: Display Properties, Date and Time Properties, and Sounds
// and Audio Devices Properties.
//
//   xplasma-edit                    Display Properties
//   xplasma-edit --date-time        Date and Time Properties
//   xplasma-edit --sounds           Sounds and Audio Devices Properties
//   ... --screenshots DIR           every tab drawn to DIR/*.png, then exit
//                                   (tests; with QT_QPA_PLATFORM=offscreen)
#include "app/adopt.h"
#include "app/desktop.h"
#include "app/editor/datetime.h"
#include "app/editor/displayproperties.h"
#include "app/editor/soundsproperties.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QWidget>

namespace {

// One of each, as XP's: opened again, the one already open comes to the
// front. The second launch hands its activation token (Wayland's leave to
// raise a window) to the first, over a socket in the user's runtime folder.
QString instanceName(const QString &which)
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QLatin1String("/xplasma-edit-") + which;
}

bool raiseRunning(const QString &which)
{
    QLocalSocket socket;
    socket.connectToServer(instanceName(which));
    if (!socket.waitForConnected(500))
        return false;
    socket.write(qgetenv("XDG_ACTIVATION_TOKEN") + '\n');
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return true;
}

void serveInstance(const QString &which, QWidget *window)
{
    auto *server = new QLocalServer(window);
    if (!server->listen(instanceName(which))) {
        QLocalServer::removeServer(instanceName(which));   // (left by one that crashed)
        server->listen(instanceName(which));
    }
    QObject::connect(server, &QLocalServer::newConnection, window, [server, window] {
        QLocalSocket *peer = server->nextPendingConnection();
        QObject::connect(peer, &QLocalSocket::disconnected, peer, &QObject::deleteLater);
        QObject::connect(peer, &QLocalSocket::readyRead, window, [peer, window] {
            if (!peer->canReadLine())
                return;
            const QByteArray token = peer->readLine().trimmed();
            if (!token.isEmpty())
                qputenv("XDG_ACTIVATION_TOKEN", token);   // (Qt's Wayland activation takes it from here)
            window->showNormal();
            window->raise();
            window->activateWindow();
        });
    });
}

// A Plasma session XPlasma can dress; else a word on why not, and false.
bool requirePlasma()
{
    xpl::LiveSession session;
    const xpl::desktop::Report r = xpl::desktop::check(session);
    if (r.ok())
        return true;
    QMessageBox::critical(nullptr, QApplication::applicationDisplayName(),
                          QStringLiteral("XPlasma is for the KDE Plasma desktop, and it can't dress this one:\n\n%1").arg(r.problems()));
    return false;
}

} // namespace

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
    const QCommandLineOption dateTime(QStringLiteral("date-time"), QStringLiteral("Date and Time Properties."));
    parser.addOption(dateTime);
    const QCommandLineOption soundsOption(QStringLiteral("sounds"), QStringLiteral("Sounds and Audio Devices Properties."));
    parser.addOption(soundsOption);
    parser.process(app);
    // (the screenshots, drawn offscreen for the tests, need no desktop)
    const bool screenshotsOnly = parser.isSet(screenshots);

    if (parser.isSet(soundsOption)) {
        QApplication::setApplicationDisplayName(QStringLiteral("Sounds and Audio Devices Properties"));
        QApplication::setDesktopFileName(QStringLiteral("fyi.hotsocket.xplasma.sounds"));
        if (!screenshotsOnly && !requirePlasma())
            return 1;
        xpl::editor::SoundsProperties dialog;
        if (parser.isSet(screenshots))
            return dialog.saveScreenshots(parser.value(screenshots)) ? 0 : 1;
        if (raiseRunning(QStringLiteral("sounds")))
            return 0;
        serveInstance(QStringLiteral("sounds"), &dialog);
        dialog.show();
        return app.exec();
    }

    if (parser.isSet(dateTime)) {
        QApplication::setApplicationDisplayName(QStringLiteral("Date and Time Properties"));
        // (its own desktop entry: its own name and icon on the taskbar)
        QApplication::setDesktopFileName(QStringLiteral("fyi.hotsocket.xplasma.datetime"));
        if (!screenshotsOnly && !requirePlasma())
            return 1;
        xpl::editor::DateTimeProperties dialog;
        if (parser.isSet(screenshots))
            return dialog.saveScreenshots(parser.value(screenshots)) ? 0 : 1;
        if (raiseRunning(QStringLiteral("date-time")))
            return 0;
        serveInstance(QStringLiteral("date-time"), &dialog);
        dialog.show();
        return app.exec();
    }

    if (!screenshotsOnly && !requirePlasma())
        return 1;
    for (const QString &line : xpl::adoptLegacy())
        QTextStream(stderr) << line << '\n';
    if (!parser.isSet(screenshots) && raiseRunning(QStringLiteral("display")))
        return 0;
    xpl::editor::DisplayProperties dialog;
    if (parser.isSet(screenshots))
        return dialog.saveScreenshots(parser.value(screenshots)) ? 0 : 1;
    serveInstance(QStringLiteral("display"), &dialog);
    dialog.show();
    return app.exec();
}
