#include "app/volume.h"

#include "app/host.h"

#include <QRegularExpression>

namespace xpl::volume {

namespace {

bool havePactl()
{
    return !host::which(QStringLiteral("pactl")).isEmpty();
}

QString out(const QStringList &argv)
{
    host::Options options;
    options.timeoutMs = 5000;
    const host::Result r = host::run(argv, options);
    return r.ok() ? QString::fromUtf8(r.out) : QString();
}

// The tray's volume item, read or (`set`: "true", "false") changed, over
// plasmashell's scripting: every panel's system tray, its enabled items.
QString trayScript(const QString &set)
{
    return QStringLiteral(R"JS(
var set = %1, shown = null, id = "org.kde.plasma.volume";
panels().forEach(function (p) {
    p.widgets("org.kde.plasma.systemtray").forEach(function (w) {
        w.currentConfigGroup = ["General"];
        var items = w.readConfig("extraItems", []);
        if (typeof items === "string")
            items = items ? items.split(",") : [];
        var has = items.indexOf(id) >= 0;
        if (set !== null && has !== set) {
            if (set)
                items.push(id);
            else
                items.splice(items.indexOf(id), 1);
            w.writeConfig("extraItems", items);
            w.reloadConfig();
        }
        shown = set !== null ? set : has;
    });
});
print(shown === null ? "none" : String(shown));
)JS")
        .arg(set);
}

} // namespace

std::optional<Device> defaultOutput()
{
    Device d;
    if (havePactl()) {
        const QString sink = out({QStringLiteral("pactl"), QStringLiteral("get-default-sink")}).trimmed();
        if (sink.isEmpty() || sink == QLatin1String("auto_null"))
            return std::nullopt;
        // (its description, from the sink list)
        const QStringList lines = out({QStringLiteral("pactl"), QStringLiteral("list"), QStringLiteral("sinks")}).split(QLatin1Char('\n'));
        bool ours = false;
        for (const QString &line : lines) {
            const QString t = line.trimmed();
            if (t.startsWith(QLatin1String("Name: ")))
                ours = t.mid(6) == sink;
            else if (ours && t.startsWith(QLatin1String("Description: ")))
                d.name = t.mid(13);
        }
        static const QRegularExpression percent(QStringLiteral("(\\d+)%"));
        auto it = percent.globalMatch(out({QStringLiteral("pactl"), QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")}));
        while (it.hasNext())
            d.percent = qMax(d.percent, it.next().captured(1).toInt());
        d.muted = out({QStringLiteral("pactl"), QStringLiteral("get-sink-mute"), QStringLiteral("@DEFAULT_SINK@")}).contains(QLatin1String("yes"));
    } else if (!host::which(QStringLiteral("wpctl")).isEmpty()) {
        const QString info = out({QStringLiteral("wpctl"), QStringLiteral("inspect"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
        static const QRegularExpression description(QStringLiteral("node\\.description = \"([^\"]*)\""));
        static const QRegularExpression nodeName(QStringLiteral("node\\.name = \"([^\"]*)\""));
        if (info.isEmpty() || nodeName.match(info).captured(1) == QLatin1String("auto_null"))
            return std::nullopt;
        d.name = description.match(info).captured(1);
        const QString v = out({QStringLiteral("wpctl"), QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
        static const QRegularExpression level(QStringLiteral("Volume: ([0-9.]+)"));
        d.percent = qRound(level.match(v).captured(1).toDouble() * 100);
        d.muted = v.contains(QLatin1String("MUTED"));
    } else {
        return std::nullopt;
    }
    d.percent = qBound(0, d.percent, 100);
    if (d.name.isEmpty())
        d.name = QStringLiteral("Sound output");
    return d;
}

bool setPercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (havePactl())
        return host::run({QStringLiteral("pactl"), QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), QString::number(percent) + QLatin1Char('%')}).ok();
    return host::run({QStringLiteral("wpctl"), QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QString::number(percent / 100.0, 'f', 2)}).ok();
}

bool setMuted(bool muted)
{
    const QString flag = muted ? QStringLiteral("1") : QStringLiteral("0");
    if (havePactl())
        return host::run({QStringLiteral("pactl"), QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), flag}).ok();
    return host::run({QStringLiteral("wpctl"), QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), flag}).ok();
}

std::optional<bool> trayIcon(Session &session)
{
    const auto r = session.script(trayScript(QStringLiteral("null")));
    if (!r || r->trimmed() == QLatin1String("none") || r->trimmed().isEmpty())
        return std::nullopt;
    return r->trimmed() == QLatin1String("true");
}

bool setTrayIcon(Session &session, bool shown)
{
    const auto r = session.script(trayScript(shown ? QStringLiteral("true") : QStringLiteral("false")));
    return r && r->trimmed() != QLatin1String("none");
}

} // namespace xpl::volume
