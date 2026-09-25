#pragma once
// The running Plasma session, as applying a look talks to it: its host
// programs (kwriteconfig6, the plasma-apply-* tools, systemctl), plasmashell's
// scripting interface and KWin, over D-Bus. One seam, so the tests can see
// what an apply would do without a desktop to do it to.
#include "app/host.h"

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

#include <functional>
#include <optional>

namespace xpl {

class Session
{
public:
    virtual ~Session() = default;

    // A host program, as host::run.
    virtual host::Result run(const QStringList &argv) = 0;
    // plasmashell's org.kde.PlasmaShell.evaluateScript; its print() output,
    // or nothing with *error.
    virtual std::optional<QString> script(const QString &js, QString *error = nullptr) = 0;
    // A D-Bus method call or signal on the session bus.
    virtual bool call(const QString &service, const QString &path, const QString &interface, const QString &method,
                      const QVariantList &arguments = {}) = 0;
    virtual void signal(const QString &path, const QString &interface, const QString &name, const QVariantList &arguments = {}) = 0;
    // A D-Bus method call's reply (its first value), or nothing: no such
    // service, an error -- or a session that can't ask (the tests').
    virtual std::optional<QVariant> query(const QString &service, const QString &path, const QString &interface, const QString &method,
                                          const QVariantList &arguments = {})
    {
        Q_UNUSED(service) Q_UNUSED(path) Q_UNUSED(interface) Q_UNUSED(method) Q_UNUSED(arguments)
        return std::nullopt;
    }
    // A host program left running on its own: in `environment` (KEY=VALUE
    // lines) if given, else this program's -- either way with this
    // program's QT_PLUGIN_PATH (XPlasma's, or none).
    virtual bool startDetached(const QStringList &argv, const QStringList &environment = {}) = 0;

    // Conveniences over those.
    bool writeConfig(const QString &file, const QStringList &groups, const QString &key, const QString &value,
                     bool notify = true);
    bool deleteConfig(const QString &file, const QStringList &groups, const QString &key, bool notify = true);
    QString readConfig(const QString &file, const QStringList &groups, const QString &key);
    void reconfigureKWin();
    // Stops plasmashell, runs `whileStopped` (plasmashell writes its layout
    // back when it quits, so the layout is edited with it down), starts it,
    // and waits for its scripting interface. Through systemd's user unit
    // where the session has it; otherwise (a session without systemd:
    // Slackware's, Gentoo's...) quit over D-Bus and started again.
    void restartPlasmaShell(const std::function<void()> &whileStopped = {});
    // Whether plasmashell's scripting interface answered within `ms`.
    bool waitForPlasmaShell(int ms = 20000);
};

// The real one: host::run, QtDBus.
class LiveSession : public Session
{
public:
    host::Result run(const QStringList &argv) override;
    std::optional<QString> script(const QString &js, QString *error = nullptr) override;
    bool call(const QString &service, const QString &path, const QString &interface, const QString &method,
              const QVariantList &arguments = {}) override;
    void signal(const QString &path, const QString &interface, const QString &name, const QVariantList &arguments = {}) override;
    std::optional<QVariant> query(const QString &service, const QString &path, const QString &interface, const QString &method,
                                  const QVariantList &arguments = {}) override;
    bool startDetached(const QStringList &argv, const QStringList &environment = {}) override;
};

} // namespace xpl
