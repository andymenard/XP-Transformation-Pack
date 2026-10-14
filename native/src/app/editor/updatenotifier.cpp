// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/updatenotifier.h"

#include "app/media.h"
#include "app/project.h"
#include "app/state.h"
#include "app/updates.h"

#include <QApplication>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

#include <functional>

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

// A receiver for org.freedesktop.Notifications' ActionInvoked signal.
class ActionListener : public QObject
{
    Q_OBJECT
public:
    explicit ActionListener(std::function<void(uint, const QString &)> onInvoked)
        : m_onInvoked(std::move(onInvoked))
    {
    }
public Q_SLOTS:
    void invoked(uint id, const QString &key) { m_onInvoked(id, key); }

private:
    std::function<void(uint, const QString &)> m_onInvoked;
};

// What the globe or its balloon opens: a message box -- the release, its
// notes, Download or Remind Me Later, and a box to say no to that version.
class UpdateBox : public QMessageBox
{
public:
    enum Choice { Later, Download, Decline };

    UpdateBox(const updates::Release &release, const QIcon &icon, QWidget *parent = nullptr)
        : QMessageBox(parent)
    {
        setWindowTitle(QStringLiteral("XPlasma Updates"));
        setWindowIcon(icon);
        setIconPixmap(icon.pixmap(32, 32));
        setText(QStringLiteral("XPlasma %1 is ready to download.").arg(release.version));
        setInformativeText(release.notes.isEmpty() ? QStringLiteral("You have XPlasma %1.").arg(project::kVersion)
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
    UpdateBox box({2, QStringLiteral("https://example.org/xplasma"),
                   QStringLiteral("XP's sounds on by default, and text that holds up on scaled screens.")},
                  globe());
    box.setAttribute(Qt::WA_DontShowOnScreen);
    box.show();
    QApplication::processEvents();
    return box.grab().save(QDir(dir).filePath(QStringLiteral("updates.png")));
}

void checkUpdatesNow(QWidget *parent)
{
    const QString title = QStringLiteral("XPlasma Updates");
    const QString url = updates::url();
    if (url.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("This XPlasma has nowhere to look for updates."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const auto release = updates::fetch(url, &error);
    QApplication::restoreOverrideCursor();
    if (!release) {
        QMessageBox::warning(parent, title, QStringLiteral("XPlasma couldn't check for updates: %1").arg(error));
        return;
    }
    State state = State::load();
    updates::checked(state);
    state.save();
    if (release->version <= project::kVersion) {
        QMessageBox::information(parent, title, QStringLiteral("You have the latest XPlasma, release %1.").arg(project::kVersion));
        return;
    }
    UpdateBox box(*release, globe(), parent);
    const UpdateBox::Choice choice = box.ask();
    if (choice == UpdateBox::Download) {
        QDesktopServices::openUrl(QUrl(release->page));
    } else if (choice == UpdateBox::Decline) {
        State s = State::load();
        updates::decline(s, *release);
        s.save();
    }
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

    // (the balloon's click, from the notification server)
    uint balloon = 0;
    ActionListener clicks([&](uint id, const QString &key) {
        if (id == balloon && key == QLatin1String("default"))
            QTimer::singleShot(0, &tray, showWindow);
    });
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
        // The balloon as a notification of our own, with a default action:
        // Plasma doesn't tell a tray icon its balloon was clicked
        // (QSystemTrayIcon::messageClicked never comes), and the balloon
        // says "Click here".
        // (no icon of its own: a picture there shows as an attachment; XP's
        // balloon has its own)
        QDBusInterface notifications(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                     QStringLiteral("org.freedesktop.Notifications"));
        const QDBusReply<uint> id = notifications.call(
            QStringLiteral("Notify"), QStringLiteral("XPlasma"), 0u, QString(), QStringLiteral("Updates are ready for your computer"), text,
            QStringList{QStringLiteral("default"), QStringLiteral("View Updates")},
            QVariantMap{{QStringLiteral("desktop-entry"), QStringLiteral("fyi.hotsocket.xplasma")}}, 30000);
        if (id.isValid()) {
            balloon = id.value();
        } else {
            tray.showMessage(QStringLiteral("Updates are ready for your computer"), text, icon, 30000);
        }
    });
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                          QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("ActionInvoked"), &clicks,
                                          SLOT(invoked(uint, QString)));
    wait.start(1000);
    return QApplication::exec();
}

} // namespace xpl::editor

#include "updatenotifier.moc"
