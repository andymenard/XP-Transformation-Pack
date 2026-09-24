#include "app/session.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QThread>

#include <unistd.h>

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

bool Session::waitForPlasmaShell(int ms)
{
    // Until its scripting interface answers (it takes a few seconds).
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        if (script(QStringLiteral("print('up')")).value_or(QString()) == QLatin1String("up"))
            return true;
        QThread::msleep(500);
    }
    return false;
}

void Session::restartPlasmaShell(const std::function<void()> &whileStopped)
{
    const QString unit = QStringLiteral("plasma-plasmashell.service");
    const bool systemd = QString::fromUtf8(run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("show"), unit,
                                                QStringLiteral("-p"), QStringLiteral("LoadState"), QStringLiteral("--value")}).out)
                             .trimmed() == QLatin1String("loaded");
    // A shell still starting -- restarted a moment ago by something else,
    // plasma-apply-lookandfeel for one -- can hang on its way out (Plasma
    // 6.6's Wayland clipboard) till systemd aborts it: up first, then down.
    const bool running = systemd
        ? QString::fromUtf8(run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("is-active"), unit}).out).trimmed().startsWith(QLatin1String("activ"))
        : run({QStringLiteral("pgrep"), QStringLiteral("-u"), QString::number(getuid()), QStringLiteral("-x"), QStringLiteral("plasmashell")}).ok();
    if (running)
        waitForPlasmaShell();
    // (Without systemd, the new shell is started from here -- in the old
    // one's environment, the session's, whatever this program was run from.)
    QStringList sessionEnv;
    if (systemd) {
        run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("stop"), unit});
    } else {
        const QString pid = QString::fromUtf8(run({QStringLiteral("pgrep"), QStringLiteral("-u"), QString::number(getuid()),
                                                   QStringLiteral("-x"), QStringLiteral("plasmashell")}).out).section(QLatin1Char('\n'), 0, 0).trimmed();
        if (QFile environ(QStringLiteral("/proc/%1/environ").arg(pid)); !pid.isEmpty() && environ.open(QIODevice::ReadOnly))
            for (const QByteArray &line : environ.readAll().split('\0'))
                if (line.contains('='))
                    sessionEnv << QString::fromLocal8Bit(line);
        // (kquitapp6 asks it to quit and waits till it has)
        run({QStringLiteral("kquitapp6"), QStringLiteral("plasmashell")});
    }
    if (whileStopped)
        whileStopped();
    if (systemd) {
        // (systemd allows the unit 3 starts a minute, and an apply and a reset
        // -- each restarting it, plasma-apply-lookandfeel too -- can need more)
        run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("reset-failed"), unit});
        run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("start"), unit});
    } else {
        startDetached({QStringLiteral("plasmashell")}, sessionEnv);
    }
    waitForPlasmaShell();
}

host::Result LiveSession::run(const QStringList &argv)
{
    return host::run(argv);
}

bool LiveSession::startDetached(const QStringList &argv, const QStringList &environment)
{
    if (argv.isEmpty())
        return false;
    QProcess process;
    process.setProgram(argv.first());
    process.setArguments(argv.mid(1));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!environment.isEmpty()) {
        env.clear();
        for (const QString &line : environment)
            env.insert(line.section(QLatin1Char('='), 0, 0), line.section(QLatin1Char('='), 1));
        const QByteArray plugins = qgetenv("QT_PLUGIN_PATH");
        if (plugins.isEmpty())
            env.remove(QStringLiteral("QT_PLUGIN_PATH"));
        else
            env.insert(QStringLiteral("QT_PLUGIN_PATH"), QString::fromLocal8Bit(plugins));
    }
    // (Never the offscreen platform the command line runs in, main.cpp:
    // not for a program started for the desktop.)
    if (env.value(QStringLiteral("QT_QPA_PLATFORM")) == QLatin1String("offscreen"))
        env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    process.setProcessEnvironment(env);
    // (and none of this program's output: a detached program holding it
    // open keeps whatever reads it -- a terminal's pipe, SSH -- waiting)
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    return process.startDetached();
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
