#include "app/xptext.h"

#include "app/paths.h"

#include <QDir>
#include <QJsonObject>
#include <QMap>
#include <QRawFont>
#include <QSaveFile>
#include <QtEndian>

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

// A size's test: ppem as GDI rounds it (10.67 px -> 11).
QString ppemTests(int from, int to)
{
    QString out;
    if (from > 0)
        out += QStringLiteral("  <test name=\"pixelsize\" compare=\"more_eq\"><double>%1.5</double></test>\n").arg(from - 1);
    if (to < 0xFFFF)
        out += QStringLiteral("  <test name=\"pixelsize\" compare=\"less\"><double>%1.5</double></test>\n").arg(to);
    return out;
}

} // namespace

QByteArray rules(const QString &fontsDir)
{
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
        "<!-- Written by XPLasma: text as Windows XP's \"Standard\" font smoothing drew it.\n"
        "     Every font fully hinted, without colour; XP's own fonts unkerned, and smoothed\n"
        "     only where their gasp tables allow. `xplasma reset` takes it away. -->\n"
        "<fontconfig>\n"
        " <match target=\"font\">\n"
        "  <edit mode=\"assign\" name=\"hinting\"><bool>true</bool></edit>\n"
        "  <edit mode=\"assign\" name=\"hintstyle\"><const>hintfull</const></edit>\n"
        "  <edit mode=\"assign\" name=\"autohint\"><bool>false</bool></edit>\n"
        "  <edit mode=\"assign\" name=\"rgba\"><const>none</const></edit>\n"
        "  <edit mode=\"assign\" name=\"lcdfilter\"><const>lcdnone</const></edit>\n"
        " </match>\n");
    for (auto it = families.cbegin(); it != families.cend(); ++it) {
        const QString family = QStringLiteral("  <test name=\"family\"><string>%1</string></test>\n").arg(escape(it.key()));
        out += QStringLiteral(" <match target=\"font\">\n") + family
               + QStringLiteral("  <edit mode=\"append\" name=\"fontfeatures\"><string>kern off</string></edit>\n </match>\n");
        int from = 0;
        for (const auto &[maxPpem, behaviour] : it.value()) {
            if (!(behaviour & 0x2))   // GASP_DOGRAY
                out += QStringLiteral(" <match target=\"font\">\n") + family + ppemTests(from, maxPpem)
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
    if (!write(dir + QLatin1Char('/') + QLatin1String(kRulesName), rules(paths::xpFonts()))) {
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
    return true;
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
}

} // namespace xpl::xptext
