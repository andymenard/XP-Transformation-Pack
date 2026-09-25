#include "app/editor/updatenotifier.h"

#include "app/media.h"
#include "app/state.h"
#include "app/updates.h"

#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

namespace xpl::editor {

namespace {

// XP's globe: wuauclt.exe's one icon, off the media; a theme's otherwise.
QIcon globe()
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48})
        if (const auto image = media::icon(QStringLiteral("wuauclt.exe"), 1, size))
            icon.addPixmap(QPixmap::fromImage(*image));
    return icon.isNull() ? QIcon::fromTheme(QStringLiteral("system-software-update")) : icon;
}

} // namespace

int checkUpdates(bool now)
{
    const QString url = updates::url();
    if (url.isEmpty())
        return 0;
    State state = State::load();
    if (!now && !updates::due(state))
        return 0;
    const auto release = updates::fetch(url);
    if (!release)
        return 0;   // (not recorded: tried again at the next login)
    updates::checked(state);
    state.save();
    if (!updates::worthTelling(state, *release))
        return 0;

    const QIcon icon = globe();
    QSystemTrayIcon tray(icon);
    tray.setToolTip(QStringLiteral("Updates are ready for your computer"));
    const auto download = [&] {
        QDesktopServices::openUrl(QUrl(release->page));
        QApplication::quit();
    };
    QMenu menu;
    menu.addAction(QStringLiteral("&Download XPlasma %1").arg(release->version), download);
    menu.addAction(QStringLiteral("&Remind Me Later"), &QApplication::quit);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Do&n't Tell Me About This Version Again"), [&] {
        State s = State::load();
        updates::decline(s, *release);
        s.save();
        QApplication::quit();
    });
    tray.setContextMenu(&menu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &tray, [&](QSystemTrayIcon::ActivationReason why) {
        if (why == QSystemTrayIcon::Trigger || why == QSystemTrayIcon::DoubleClick)
            download();
    });
    QObject::connect(&tray, &QSystemTrayIcon::messageClicked, &tray, download);

    // At login the tray may not be up yet: waited for, a minute at most.
    int tries = 0;
    QTimer wait;
    QObject::connect(&wait, &QTimer::timeout, &tray, [&] {
        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            if (++tries > 60)
                QApplication::quit();
            return;
        }
        wait.stop();
        tray.show();
        QString text = QStringLiteral("Click here to download these updates.");
        if (!release->notes.isEmpty())
            text = release->notes + QStringLiteral("\n\n") + text;
        tray.showMessage(QStringLiteral("Updates are ready for your computer"), text, icon, 30000);
    });
    wait.start(1000);
    return QApplication::exec();
}

} // namespace xpl::editor
