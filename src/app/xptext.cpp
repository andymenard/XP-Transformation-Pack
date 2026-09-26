// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/xptext.h"

#include "app/host.h"
#include "app/paths.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRawFont>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>

#include <climits>
#include <cmath>

namespace xpl::xptext {

namespace {

constexpr const char *kRulesName = "xplasma-xp-text.conf";
const char *const kXftKeys[] = {"XftHintStyle", "XftSubPixel", "XftAntialias"};

QString fontconfigDir()
{
    return paths::config() + QLatin1String("/fontconfig");
}

QString include()
{
    return QStringLiteral("<include ignore_missing=\"yes\" prefix=\"xdg\">fontconfig/%1</include>").arg(QLatin1String(kRulesName));
}

bool write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

QString escape(const QString &text)
{
    return text.toHtmlEscaped();
}

// A size's test: ppem as GDI rounds it (10.67 px -> 11), on a display at
// `scale` (fontconfig's pixelsize the unscaled one).
QString ppemTests(int from, int to, double scale)
{
    QString out;
    if (from > 0)
        out += QStringLiteral("  <test name=\"pixelsize\" compare=\"more_eq\"><double>%1</double></test>\n").arg((from - 0.5) / scale, 0, 'f', 4);
    if (to < 0xFFFF)
        out += QStringLiteral("  <test name=\"pixelsize\" compare=\"less\"><double>%1</double></test>\n").arg((to + 0.5) / scale, 0, 'f', 4);
    return out;
}

} // namespace

QByteArray rules(const QString &fontsDir, double scale)
{
    if (!(scale > 0))
        scale = 1.0;
    // Each family's gasp ranges (its regular face's, found first).
    QMap<QString, QList<std::pair<int, int>>> families;
    QDir dir(fontsDir);
    for (const QString &name : dir.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.TTF")}, QDir::Files, QDir::Name)) {
        const QRawFont font(dir.filePath(name), 11);
        if (!font.isValid() || families.contains(font.familyName()))
            continue;
        const QByteArray gasp = font.fontTable("gasp");
        QList<std::pair<int, int>> ranges;
        const auto *p = reinterpret_cast<const uchar *>(gasp.constData());
        const int count = gasp.size() >= 4 ? qFromBigEndian<quint16>(p + 2) : 0;
        for (int i = 0; i < count && 4 + 4 * i + 4 <= gasp.size(); ++i)
            ranges.append({qFromBigEndian<quint16>(p + 4 + 4 * i), qFromBigEndian<quint16>(p + 6 + 4 * i)});
        families.insert(font.familyName(), ranges);
    }

    QString out = QStringLiteral(
        "<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">\n"
        "<!-- Written by XPlasma: text as Windows XP's \"Standard\" font smoothing drew it.\n"
        "     Every font fully hinted, without colour; XP's own fonts unkerned, and smoothed\n"
        "     only where their gasp tables allow, at the sizes drawn on a display at %1x.\n"
        "     `xplasma reset` takes it away. -->\n"
        "<fontconfig>\n"
        " <match target=\"font\">\n"
        "  <edit mode=\"assign\" name=\"hinting\"><bool>true</bool></edit>\n"
        "  <edit mode=\"assign\" name=\"hintstyle\"><const>hintfull</const></edit>\n"
        "  <edit mode=\"assign\" name=\"autohint\"><bool>false</bool></edit>\n"
        "  <edit mode=\"assign\" name=\"rgba\"><const>none</const></edit>\n"
        "  <edit mode=\"assign\" name=\"lcdfilter\"><const>lcdnone</const></edit>\n"
        " </match>\n").arg(scale);
    for (auto it = families.cbegin(); it != families.cend(); ++it) {
        const QString family = QStringLiteral("  <test name=\"family\"><string>%1</string></test>\n").arg(escape(it.key()));
        out += QStringLiteral(" <match target=\"font\">\n") + family
               + QStringLiteral("  <edit mode=\"append\" name=\"fontfeatures\"><string>kern off</string></edit>\n </match>\n");
        int from = 0;
        for (const auto &[maxPpem, behaviour] : it.value()) {
            if (!(behaviour & 0x2))   // GASP_DOGRAY
                out += QStringLiteral(" <match target=\"font\">\n") + family + ppemTests(from, maxPpem, scale)
                       + QStringLiteral("  <edit mode=\"assign\" name=\"antialias\"><bool>false</bool></edit>\n </match>\n");
            from = maxPpem + 1;
        }
    }
    out += QStringLiteral("</fontconfig>\n");
    return out.toUtf8();
}

bool apply(Session &session, State &state, QString *error)
{
    const QString dir = fontconfigDir();
    const double scale = displayScale();
    if (!write(dir + QLatin1Char('/') + QLatin1String(kRulesName), rules(paths::xpFonts(), scale))) {
        if (error)
            *error = QStringLiteral("can't write %1/%2").arg(dir, QLatin1String(kRulesName));
        return false;
    }
    // Read after KDE's fonts.conf, whose settings would otherwise win.
    const QString user = dir + QLatin1String("/fonts.conf");
    QFile f(user);
    QString text = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    f.close();
    if (!text.contains(QLatin1String(kRulesName))) {
        if (text.isEmpty() || !text.contains(QLatin1String("</fontconfig>")))
            text = QStringLiteral("<?xml version='1.0'?>\n<!DOCTYPE fontconfig SYSTEM 'urn:fontconfig:fonts.dtd'>\n<fontconfig>\n</fontconfig>\n");
        text.insert(text.lastIndexOf(QLatin1String("</fontconfig>")), QLatin1Char(' ') + include() + QLatin1Char('\n'));
        if (!write(user, text.toUtf8())) {
            if (error)
                *error = QStringLiteral("can't write %1").arg(user);
            return false;
        }
    }
    // KDE's (and, through it, GTK's) own record, the user's kept for reset.
    const QStringList general{QStringLiteral("General")};
    if (!state.settings.contains(QStringLiteral("previousXft"))) {
        QJsonObject previous;
        for (const char *key : kXftKeys)
            previous.insert(QLatin1String(key), session.readConfig(QStringLiteral("kdeglobals"), general, QLatin1String(key)));
        state.settings.insert(QStringLiteral("previousXft"), previous);
    }
    session.writeConfig(QStringLiteral("kdeglobals"), general, QStringLiteral("XftHintStyle"), QStringLiteral("hintfull"));
    session.writeConfig(QStringLiteral("kdeglobals"), general, QStringLiteral("XftSubPixel"), QStringLiteral("none"));
    session.writeConfig(QStringLiteral("kdeglobals"), general, QStringLiteral("XftAntialias"), QStringLiteral("true"));
    state.settings.insert(QStringLiteral("xpTextScale"), scale);
    setGtk4Rendering(session, state, scale);
    return true;
}

bool refresh(Session &session, State &state)
{
    const QString file = fontconfigDir() + QLatin1Char('/') + QLatin1String(kRulesName);
    if (!QFileInfo::exists(file))
        return false;   // (not applied)
    const double scale = displayScale();
    if (qFuzzyCompare(state.settings.value(QStringLiteral("xpTextScale")).toDouble(1.0), scale))
        return false;
    if (!write(file, rules(paths::xpFonts(), scale)))
        return false;
    state.settings.insert(QStringLiteral("xpTextScale"), scale);
    setGtk4Rendering(session, state, scale);
    return true;
}

void setGtk4Rendering(Session &session, State &state, double scale)
{
    const QStringList key{QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("font-rendering")};
    const QString previousKey = QStringLiteral("previousGtkFontRendering");
    const bool fractional = std::abs(scale - std::round(scale)) > 0.01;
    if (fractional) {
        if (!state.settings.contains(previousKey)) {
            const host::Result was = session.run(QStringList{QStringLiteral("gsettings"), QStringLiteral("get")} + key);
            if (!was.ok())
                return;   // (no GSettings, or a GTK older than the key: nothing to do)
            state.settings.insert(previousKey, QString::fromUtf8(was.out).trimmed().remove(QLatin1Char('\'')));
        }
        session.run(QStringList{QStringLiteral("gsettings"), QStringLiteral("set")} + key + QStringList{QStringLiteral("manual")});
    } else if (state.settings.contains(previousKey)) {
        const QString previous = state.settings.value(previousKey).toString();
        session.run(QStringList{QStringLiteral("gsettings"), QStringLiteral("set")} + key
                    + QStringList{previous.isEmpty() ? QStringLiteral("automatic") : previous});
        state.settings.remove(previousKey);
    }
}

double displayScale(const QByteArray &kwinOutputConfig, const QStringList &connected)
{
    // [{"name": "outputs", "data": [{connectorName, scale...}]},
    //  {"name": "setups", "data": [{outputs: [{outputIndex, enabled, priority}]}]}]
    QJsonArray outputs, setups;
    for (const QJsonValue &section : QJsonDocument::fromJson(kwinOutputConfig).array()) {
        const QJsonObject o = section.toObject();
        if (o.value(QLatin1String("name")).toString() == QLatin1String("outputs"))
            outputs = o.value(QLatin1String("data")).toArray();
        else if (o.value(QLatin1String("name")).toString() == QLatin1String("setups"))
            setups = o.value(QLatin1String("data")).toArray();
    }
    auto connector = [&](int index) { return outputs.at(index).toObject().value(QLatin1String("connectorName")).toString(); };
    const QSet<QString> now(connected.cbegin(), connected.cend());
    for (const QJsonValue &setup : std::as_const(setups)) {
        // The setup for exactly the screens connected now...
        const QJsonArray members = setup.toObject().value(QLatin1String("outputs")).toArray();
        QSet<QString> theirs;
        for (const QJsonValue &m : members)
            theirs.insert(connector(m.toObject().value(QLatin1String("outputIndex")).toInt(-1)));
        if (theirs != now)
            continue;
        // ...its main screen: enabled, first in priority.
        int best = -1, bestPriority = INT_MAX;
        for (const QJsonValue &m : members) {
            const QJsonObject mo = m.toObject();
            const int priority = mo.value(QLatin1String("priority")).toInt(INT_MAX);
            if (mo.value(QLatin1String("enabled")).toBool() && priority < bestPriority) {
                best = mo.value(QLatin1String("outputIndex")).toInt(-1);
                bestPriority = priority;
            }
        }
        const double scale = outputs.at(best).toObject().value(QLatin1String("scale")).toDouble(1.0);
        return scale > 0 ? scale : 1.0;
    }
    return 1.0;
}

double displayScale()
{
    QFile config(paths::config() + QLatin1String("/kwinoutputconfig.json"));
    if (!config.open(QIODevice::ReadOnly))
        return 1.0;
    // The kernel's connectors that have a screen on them (card0-eDP-1 -> eDP-1).
    QStringList connected;
    for (const QFileInfo &c : QDir(QStringLiteral("/sys/class/drm")).entryInfoList({QStringLiteral("card*-*")}, QDir::Dirs | QDir::System)) {
        QFile status(c.absoluteFilePath() + QLatin1String("/status"));
        if (status.open(QIODevice::ReadOnly) && status.readAll().trimmed() == "connected")
            connected << c.fileName().section(QLatin1Char('-'), 1);
    }
    return displayScale(config.readAll(), connected);
}

void revert(Session &session, State &state)
{
    const QString dir = fontconfigDir();
    QFile::remove(dir + QLatin1Char('/') + QLatin1String(kRulesName));
    const QString user = dir + QLatin1String("/fonts.conf");
    QFile f(user);
    if (f.open(QIODevice::ReadOnly)) {
        QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        f.close();
        const qsizetype before = lines.size();
        lines.removeIf([](const QString &line) { return line.contains(QLatin1String(kRulesName)); });
        if (lines.size() != before)
            write(user, lines.join(QLatin1Char('\n')).toUtf8());
    }
    if (state.settings.contains(QStringLiteral("previousXft"))) {
        const QJsonObject previous = state.settings.value(QStringLiteral("previousXft")).toObject();
        const QStringList general{QStringLiteral("General")};
        for (const char *key : kXftKeys) {
            const QString value = previous.value(QLatin1String(key)).toString();
            if (value.isEmpty())
                session.deleteConfig(QStringLiteral("kdeglobals"), general, QLatin1String(key));
            else
                session.writeConfig(QStringLiteral("kdeglobals"), general, QLatin1String(key), value);
        }
        state.settings.remove(QStringLiteral("previousXft"));
    }
    setGtk4Rendering(session, state, 1.0);   // (the user's own back)
    state.settings.remove(QStringLiteral("xpTextScale"));
}

} // namespace xpl::xptext
