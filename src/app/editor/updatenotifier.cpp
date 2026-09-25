#include "app/editor/updatenotifier.h"

#include "app/media.h"
#include "app/project.h"
#include "app/state.h"
#include "app/updates.h"

#include <QApplication>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
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

// What the globe or its balloon opens: a message box -- the release, its
// notes, Download or Remind Me Later, and a box to say no to that version.
class UpdateBox : public QMessageBox
{
public:
    enum Choice { Later, Download, Decline };

    UpdateBox(const updates::Release &release, const QIcon &icon)
    {
        setWindowTitle(QStringLiteral("XPlasma Updates"));
        setWindowIcon(icon);
        setIconPixmap(icon.pixmap(32, 32));
        setText(QStringLiteral("XPlasma %1 is ready to download.").arg(release.version));
        setInformativeText(release.notes.isEmpty() ? QStringLiteral("You have XPlasma %1.").arg(QLatin1String(project::kVersion))
                                                   : release.notes);
        m_download = addButton(QStringLiteral("&Download"), QMessageBox::AcceptRole);
        addButton(QStringLiteral("&Remind Me Later"), QMessageBox::RejectRole);
        setDefaultButton(m_download);
        setCheckBox(new QCheckBox(QStringLiteral("Do&n't tell me about this version again")));
    }

    Choice ask()
    {
        exec();
        if (clickedButton() == m_download)
            return Download;
        return checkBox()->isChecked() ? Decline : Later;
    }

private:
    QPushButton *m_download;
};

} // namespace

bool saveUpdateScreenshot(const QString &dir)
{
    QDir().mkpath(dir);
    UpdateBox box({QStringLiteral("0.2.0"), QStringLiteral("https://example.org/xplasma"),
                   QStringLiteral("XP's sounds on by default, and text that holds up on scaled screens.")},
                  globe());
    box.setAttribute(Qt::WA_DontShowOnScreen);
    box.show();
    QApplication::processEvents();
    return box.grab().save(QDir(dir).filePath(QStringLiteral("updates.png")));
}

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
    const auto decline = [&] {
        State s = State::load();
        updates::decline(s, *release);
        s.save();
        QApplication::quit();
    };
    // The box: the globe or the balloon clicked. Remind Me Later leaves the
    // globe in the tray, as XP did, till the session ends.
    bool open = false;
    const auto showWindow = [&] {
        if (open)
            return;
        open = true;
        UpdateBox box(*release, icon);
        const UpdateBox::Choice choice = box.ask();
        open = false;
        if (choice == UpdateBox::Download)
            download();
        else if (choice == UpdateBox::Decline)
            decline();
    };
    QMenu menu;
    QAction *view = menu.addAction(QStringLiteral("&View Updates..."), showWindow);
    menu.setDefaultAction(view);
    menu.addAction(QStringLiteral("&Download XPlasma %1").arg(release->version), download);
    menu.addSeparator();
    menu.addAction(QStringLiteral("&Remind Me Later"), &QApplication::quit);
    menu.addAction(QStringLiteral("Do&n't Tell Me About This Version Again"), decline);
    tray.setContextMenu(&menu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &tray, [&](QSystemTrayIcon::ActivationReason why) {
        if (why == QSystemTrayIcon::Trigger || why == QSystemTrayIcon::DoubleClick)
            showWindow();
    });
    QObject::connect(&tray, &QSystemTrayIcon::messageClicked, &tray, showWindow);

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
