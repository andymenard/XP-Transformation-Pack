// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/colors.h"

#include <QFont>

namespace xpl {

namespace {
QString rgb(const QColor &c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}
} // namespace

QByteArray colorScheme(const ClassicScheme &s, const QString &name, const std::pair<QColor, QColor> &complementary)
{
    QString out;
    auto group = [&](const char *group, const char *bg, const char *fg) {
        const QString b = rgb(s.colour(QString::fromLatin1(bg))), f = rgb(s.colour(QString::fromLatin1(fg)));
        out += QStringLiteral("[Colors:%1]\n").arg(QLatin1String(group));
        out += QStringLiteral("BackgroundNormal=%1\nBackgroundAlternate=%1\n").arg(b);
        out += QStringLiteral("ForegroundNormal=%1\nForegroundActive=%1\n").arg(f);
        out += QStringLiteral("ForegroundInactive=%1\n").arg(rgb(s.colour(u"GrayText")));
        out += QStringLiteral("ForegroundLink=%1\n").arg(rgb(s.colour(u"HotTrackingColor")));
        out += QStringLiteral("DecorationFocus=%1\nDecorationHover=%1\n\n").arg(rgb(s.colour(u"Hilight")));
    };
    // XP greys disabled text to GrayText and nothing else: no KDE effects.
    out += QStringLiteral("[ColorEffects:Disabled]\nColorEffect=0\nContrastEffect=0\nIntensityEffect=0\n\n");
    out += QStringLiteral("[ColorEffects:Inactive]\nEnable=false\n\n");
    group("Window", "ButtonFace", "ButtonText");
    group("Button", "ButtonFace", "ButtonText");
    group("Header", "ButtonFace", "ButtonText");
    if (complementary.first.isValid() && complementary.second.isValid()) {
        out += QStringLiteral("[Colors:Complementary]\n");
        out += QStringLiteral("BackgroundNormal=%1\nBackgroundAlternate=%1\n").arg(rgb(complementary.first));
        out += QStringLiteral("ForegroundNormal=%1\nForegroundActive=%1\nForegroundInactive=%1\n").arg(rgb(complementary.second));
        out += QStringLiteral("DecorationFocus=%1\nDecorationHover=%1\n\n").arg(rgb(s.colour(u"Hilight")));
    } else {
        group("Complementary", "ButtonFace", "ButtonText");
    }
    group("View", "Window", "WindowText");
    group("Selection", "Hilight", "HilightText");
    group("Tooltip", "InfoWindow", "InfoText");
    out += QStringLiteral("[General]\nColorScheme=%1\nName=%1\nshadeSortColumn=false\n\n").arg(name);
    out += QStringLiteral("[KDE]\ncontrast=4\n\n");
    out += QStringLiteral("[WM]\nactiveBackground=%1\nactiveBlend=%2\nactiveForeground=%3\n")
               .arg(rgb(s.colour(u"ActiveTitle")), rgb(s.colour(u"GradientActiveTitle")), rgb(s.colour(u"TitleText")));
    out += QStringLiteral("inactiveBackground=%1\ninactiveBlend=%2\ninactiveForeground=%3\n\n")
               .arg(rgb(s.colour(u"InactiveTitle")), rgb(s.colour(u"GradientInactiveTitle")), rgb(s.colour(u"InactiveTitleText")));
    out += QStringLiteral("[%1]\nScheme=%2\n").arg(QLatin1String("X-XPlasma-Generated"), name);
    return out.toUtf8();
}

QString qtFontString(const LogFont &lf)
{
    QFont font(lf.face);
    font.setPointSize(lf.points());
    font.setBold(lf.bold());
    font.setItalic(lf.italic);
    font.setUnderline(lf.underline);
    return font.toString();
}

QList<ConfigValue> fontSettings(const ClassicScheme &s)
{
    const QString kdeglobals = QStringLiteral("kdeglobals");
    const QString ui = qtFontString(s.metrics.messageFont), caption = qtFontString(s.metrics.captionFont);
    return {
        {kdeglobals, QStringLiteral("General"), QStringLiteral("font"), ui},
        {kdeglobals, QStringLiteral("General"), QStringLiteral("menuFont"), qtFontString(s.metrics.menuFont)},
        {kdeglobals, QStringLiteral("General"), QStringLiteral("toolBarFont"), ui},
        {kdeglobals, QStringLiteral("General"), QStringLiteral("smallestReadableFont"), qtFontString(s.metrics.statusFont)},
        {kdeglobals, QStringLiteral("WM"), QStringLiteral("activeFont"), caption},
        {kdeglobals, QStringLiteral("WM"), QStringLiteral("inactiveFont"), caption},
    };
}

} // namespace xpl
