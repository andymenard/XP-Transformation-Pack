#include "app/startpanel.h"

#include "core/schema.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <cmath>

namespace xpl {

namespace {

QJsonArray numbers(const QString &value)
{
    QJsonArray out;
    for (const QString &n : value.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts))
        out.append(n.toInt());
    return out;
}

QString hex(const QString &colour, QRgb fallback)
{
    QRgb c = fallback;
    parseColour(colour, &c);
    return QColor(c).name();
}

QJsonObject font(const QString &spec)
{
    const QStringList f = spec.split(QLatin1Char(','));
    const QString lower = spec.toLower();
    return QJsonObject{{QStringLiteral("family"), f.value(0).trimmed()},
                       {QStringLiteral("pixels"), int(std::lround(f.value(1).trimmed().toInt() * 96.0 / 72.0))},
                       {QStringLiteral("bold"), lower.contains(QLatin1String("bold"))},
                       {QStringLiteral("italic"), lower.contains(QLatin1String("italic"))}};
}

} // namespace

QJsonObject writeStartPanel(const Look &look, const QString &directory, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return QJsonObject();
    };
    if (look.classic())
        return fail(QStringLiteral("Windows Classic has no Start panel art"));
    const Renderer &r = *look.renderer;
    const Ini &ini = r.theme().ini();
    QDir().mkpath(directory);
    const QDir dir(directory);

    QJsonObject parts;
    static const char *const kParts[] = {"userpane", "moreprograms", "moreprogramsarrow", "proglist", "proglistseparator",
                                         "placeslist", "placeslistseparator", "logoff", "logoffbuttons", "userpicture"};
    for (const char *name : kParts) {
        const int part = schema::partId(u"startpanel", QString::fromLatin1(name));
        for (const bool hot : {false, true}) {
            // Hot art only where the style has a hot section of its own.
            if (hot && !ini.section({QString(), QStringLiteral("startpanel"), QLatin1String(name), QStringLiteral("hot")}))
                continue;
            const PartRef ref{QStringLiteral("startpanel"), part, hot ? 2 : 1, {}};
            const QImage image = r.stateImage(ref);
            if (image.isNull())
                continue;
            const QString key = QLatin1String(name) + (hot ? QLatin1String("-hot") : QLatin1String(""));
            image.convertToFormat(QImage::Format_ARGB32).save(dir.filePath(key + QLatin1String(".png")));
            QJsonObject properties;
            for (const char *p : {"textcolor", "font", "textshadowcolor", "textshadowoffset", "captiontext", "imagefile"})
                if (const auto v = r.property(ref, QString::fromLatin1(p)))
                    properties.insert(QLatin1String(p), *v);
            QJsonObject spec{{QStringLiteral("url"), QString(key + QLatin1String(".png"))},
                             {QStringLiteral("width"), image.width()},
                             {QStringLiteral("height"), image.height()},
                             {QStringLiteral("sizing"), numbers(r.property(ref, u"sizingmargins").value_or(QStringLiteral("0 0 0 0")))},
                             {QStringLiteral("content"), numbers(r.property(ref, u"contentmargins").value_or(QStringLiteral("0 0 0 0")))},
                             {QStringLiteral("source"), r.property(ref, u"imagefile").value_or(QString())},
                             {QStringLiteral("properties"), properties}};
            // DefaultPaneSize cascades from [StartPanel] (380 x 440), which
            // isn't a part's size: only a part's own counts.
            const Properties *own = ini.section({QString(), QStringLiteral("startpanel"), QLatin1String(name), hot ? QStringLiteral("hot") : QString()});
            if (own && own->contains(QStringLiteral("defaultpanesize"))) {
                const QJsonArray pane = numbers(own->value(QStringLiteral("defaultpanesize")));
                spec.insert(QStringLiteral("pane"), QJsonArray{pane.at(2), pane.at(3)});
            } else {
                spec.insert(QStringLiteral("pane"), QJsonValue());
            }
            // The footer's three buttons are one strip of squares.
            if (QLatin1String(name) == QLatin1String("logoffbuttons")) {
                if (image.width() != 3 * image.height())
                    return fail(QStringLiteral("%1: an unexpected Log Off / Turn Off button strip").arg(look.name));
                static const char *const buttons[] = {"undock", "logoff", "shutdown"};
                for (int i = 0; i < 3; ++i) {
                    const QString file = QStringLiteral("button-%1%2.png").arg(QLatin1String(buttons[i]), hot ? QStringLiteral("-hot") : QString());
                    image.copy(i * image.height(), 0, image.height(), image.height()).convertToFormat(QImage::Format_ARGB32).save(dir.filePath(file));
                    spec.insert(QLatin1String(buttons[i]), file);
                }
            }
            parts.insert(key, spec);
        }
    }
    const QStringList required{QStringLiteral("userpane"), QStringLiteral("userpicture"), QStringLiteral("proglist"),
                               QStringLiteral("proglistseparator"), QStringLiteral("moreprograms"), QStringLiteral("moreprogramsarrow"),
                               QStringLiteral("placeslist"), QStringLiteral("placeslistseparator"), QStringLiteral("logoff"),
                               QStringLiteral("logoffbuttons")};
    QStringList missing;
    for (const QString &k : required)
        if (!parts.contains(k))
            missing << k;
    if (!missing.isEmpty())
        return fail(QStringLiteral("%1 is missing Start panel art: %2").arg(look.name, missing.join(QStringLiteral(", "))));

    // All Programs' flyouts: StartMenu::Toolbar's background.
    const PartRef submenu{QStringLiteral("toolbar"), 0, 0, QStringLiteral("startmenu")};
    const QImage submenuImage = r.stateImage(submenu);
    if (!submenuImage.isNull()) {
        submenuImage.convertToFormat(QImage::Format_ARGB32).save(dir.filePath(QStringLiteral("submenu.png")));
        parts.insert(QStringLiteral("submenu"), QJsonObject{{QStringLiteral("url"), QStringLiteral("submenu.png")},
                                                            {QStringLiteral("sizing"), numbers(r.property(submenu, u"sizingmargins").value_or(QStringLiteral("0 0 0 0")))}});
    }
    // A measured Luna correction: the blue footer's fixed ends are 48 / 48
    // around a 1px stretch, not the INI's 49 / 47. (Not imposed elsewhere.)
    QJsonObject logoff = parts.value(QStringLiteral("logoff")).toObject();
    if (logoff.value(QStringLiteral("source")).toString().replace(QLatin1Char('\\'), QLatin1Char('/')).toLower()
        == QLatin1String("blue/startpanellogoffbackground.bmp")) {
        QJsonArray sizing = logoff.value(QStringLiteral("sizing")).toArray();
        sizing[0] = 48;
        sizing[1] = 48;
        logoff.insert(QStringLiteral("sizing"), sizing);
        parts.insert(QStringLiteral("logoff"), logoff);
    }

    auto prop = [&](const char *part, const char *name, const QString &fallback = QString()) {
        return parts.value(QLatin1String(part)).toObject().value(QStringLiteral("properties")).toObject()
            .value(QLatin1String(name)).toString(fallback);
    };
    auto sys = [&](const char *name, const QString &fallback) {
        return r.theme().property(u"sysmetrics", u"", u"", QString::fromLatin1(name)).value_or(fallback);
    };
    const Properties *submenuHot = ini.section({QStringLiteral("startmenu"), QStringLiteral("toolbar"), QString(), QStringLiteral("hot")});
    const QString submenuHotText = submenuHot && submenuHot->contains(QStringLiteral("textcolor"))
                                       ? submenuHot->value(QStringLiteral("textcolor"))
                                       : sys("highlighttext", QStringLiteral("255 255 255"));
    const QJsonObject palette{
        {QStringLiteral("programText"), hex(prop("proglist", "textcolor"), qRgb(0, 0, 0))},
        // A dark Start panel can still have white All Programs flyouts: their
        // colours are StartMenu's.
        {QStringLiteral("submenuText"), hex(r.property(submenu, u"textcolor").value_or(sys("menutext", QStringLiteral("0 0 0"))), qRgb(0, 0, 0))},
        {QStringLiteral("submenuHighlightText"), hex(submenuHotText, qRgb(255, 255, 255))},
        {QStringLiteral("secondaryText"), hex(prop("proglist", "captiontext", QStringLiteral("128 128 128")), qRgb(128, 128, 128))},
        {QStringLiteral("placesText"), hex(prop("placeslist", "textcolor"), qRgb(0, 0, 0))},
        {QStringLiteral("userText"), hex(prop("userpane", "textcolor"), qRgb(255, 255, 255))},
        {QStringLiteral("userShadow"), hex(prop("userpane", "textshadowcolor", QStringLiteral("0 0 0")), qRgb(0, 0, 0))},
        {QStringLiteral("footerText"), hex(prop("logoff", "textcolor"), qRgb(255, 255, 255))},
        {QStringLiteral("highlight"), hex(sys("highlight", QStringLiteral("49 106 197")), qRgb(49, 106, 197))},
        {QStringLiteral("highlightText"), hex(sys("highlighttext", QStringLiteral("255 255 255")), qRgb(255, 255, 255))},
    };
    auto pane = [&](const char *part, int index) {
        return parts.value(QLatin1String(part)).toObject().value(QStringLiteral("pane")).toArray().at(index).toInt();
    };
    QJsonObject result{
        {QStringLiteral("source"), look.stylePath},
        {QStringLiteral("variant"), look.variant},
        {QStringLiteral("parts"), parts},
        {QStringLiteral("palette"), palette},
        {QStringLiteral("font"), font(sys("menufont", QStringLiteral("Tahoma, 8")))},
        {QStringLiteral("userFont"), font(prop("userpane", "font", QStringLiteral("Tahoma, 14")))},
        {QStringLiteral("moreFont"), font(prop("moreprograms", "font", QStringLiteral("Tahoma, 8, Bold")))},
        {QStringLiteral("userShadowOffset"), numbers(prop("userpane", "textshadowoffset", QStringLiteral("0 0")))},
        {QStringLiteral("layout"), QJsonObject{{QStringLiteral("width"), 380}, {QStringLiteral("height"), 478},
                                               {QStringLiteral("headerHeight"), pane("userpane", 1)},
                                               {QStringLiteral("footerHeight"), pane("logoff", 1)},
                                               {QStringLiteral("leftWidth"), pane("proglist", 0)},
                                               {QStringLiteral("moreHeight"), pane("moreprograms", 1) + 14}}},
        // Measured by hand on XP, not read from the style.
        {QStringLiteral("provisional"), QJsonObject{{QStringLiteral("pinnedRowHeight"), 38}, {QStringLiteral("recentRowHeight"), 38},
                                                    {QStringLiteral("moreExtraHeight"), 14}, {QStringLiteral("moreTopGap"), 2},
                                                    {QStringLiteral("moreBottomExtra"), 6}, {QStringLiteral("placesRowHeight"), 30},
                                                    {QStringLiteral("separatorGap"), 8}, {QStringLiteral("userPictureX"), 7},
                                                    {QStringLiteral("userPictureY"), 4}, {QStringLiteral("userNameGap"), 8}}},
    };
    QSaveFile out(dir.filePath(QStringLiteral("theme.json")));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(result).toJson()) < 0 || !out.commit())
        return fail(out.errorString());
    return result;
}

} // namespace xpl
