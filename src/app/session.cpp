#include "app/session.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QThread>

namespace xpl {

namespace {
QStringList groupArgs(const QStringList &groups)
{
    QStringList args;
    for (const QString &g : groups)
        args << QStringLiteral("--group") << g;
    return args;
}
} // namespace

bool Session::writeConfig(const QString &file, const QStringList &groups, const QString &key, const QString &value, bool notify)
{
    QStringList argv{QStringLiteral("kwriteconfig6")};
    if (notify)
        argv << QStringLiteral("--notify");
    argv << QStringLiteral("--file") << file << groupArgs(groups) << QStringLiteral("--key") << key << value;
    return run(argv).ok();
}

bool Session::deleteConfig(const QString &file, const QStringList &groups, const QString &key, bool notify)
{
    QStringList argv{QStringLiteral("kwriteconfig6")};
    if (notify)
        argv << QStringLiteral("--notify");
    argv << QStringLiteral("--file") << file << groupArgs(groups) << QStringLiteral("--key") << key << QStringLiteral("--delete");
    return run(argv).ok();
}

QString Session::readConfig(const QString &file, const QStringList &groups, const QString &key)
{
    return QString::fromUtf8(run(QStringList{QStringLiteral("kreadconfig6"), QStringLiteral("--file"), file}
                                 + groupArgs(groups) + QStringList{QStringLiteral("--key"), key})
                                 .out)
        .trimmed();
}

void Session::reconfigureKWin()
{
    call(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure"));
}

void Session::restartPlasmaShell(const std::function<void()> &whileStopped)
{
    run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("stop"), QStringLiteral("plasma-plasmashell.service")});
    if (whileStopped)
        whileStopped();
    run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("start"), QStringLiteral("plasma-plasmashell.service")});
    // Until its scripting interface answers (it takes a few seconds).
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 20000) {
        if (script(QStringLiteral("print('up')")).value_or(QString()) == QLatin1String("up"))
            return;
        QThread::msleep(500);
    }
}

host::Result LiveSession::run(const QStringList &argv)
{
    return host::run(argv);
}

std::optional<QString> LiveSession::script(const QString &js, QString *error)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                                          QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    message << js;
    const QDBusReply<QString> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 60000);
    if (!reply.isValid()) {
        if (error)
            *error = reply.error().message();
        return std::nullopt;
    }
    return reply.value().trimmed();
}

bool LiveSession::call(const QString &service, const QString &path, const QString &interface, const QString &method,
                       const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().call(message, QDBus::Block, 30000).type() != QDBusMessage::ErrorMessage;
}

void LiveSession::signal(const QString &path, const QString &interface, const QString &name, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createSignal(path, interface, name);
    message.setArguments(arguments);
    QDBusConnection::sessionBus().send(message);
}

} // namespace xpl
