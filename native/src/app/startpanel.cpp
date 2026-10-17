// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/startpanel.h"

#include "app/media.h"
#include "core/schema.h"
#include "core/crispfont.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

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
    return QJsonObject{{QStringLiteral("family"), osUiFamily(f.value(0).trimmed())},
                       {QStringLiteral("pixels"), int(std::lround(f.value(1).trimmed().toInt() * 96.0 / 72.0))},
                       {QStringLiteral("bold"), lower.contains(QLatin1String("bold"))},
                       {QStringLiteral("italic"), lower.contains(QLatin1String("italic"))}};
}

QJsonObject part(const QString &url, const QImage &image, const QList<int> &sizing, const QList<int> &content,
                 const QJsonValue &pane = QJsonValue())
{
    QJsonArray s, c;
    for (int v : sizing)
        s.append(v);
    for (int v : content)
        c.append(v);
    return QJsonObject{{QStringLiteral("url"), url}, {QStringLiteral("width"), image.width()},
                       {QStringLiteral("height"), image.height()}, {QStringLiteral("sizing"), s},
                       {QStringLiteral("content"), c}, {QStringLiteral("pane"), pane},
                       {QStringLiteral("properties"), QJsonObject()}};
}

bool save(const QImage &image, const QDir &dir, const QString &name)
{
    return image.convertToFormat(QImage::Format_ARGB32).save(dir.filePath(name));
}

// Windows Classic: XP keeps the same two-column Start menu with visual
// styles off, drawn in the scheme's colours (measured, Windows Standard):
// a raised frame with a face-coloured pixel inside it; a 40px header in the
// caption gradient with the user's name (no picture); etched separators
// (shadow over highlight) between the columns, the groups and above the
// footer; Log Off and Turn Off Computer as explorer.exe's bitmaps 7010 and
// 7011 (normal, hot).
QJsonObject classicPanel(const Look &look, const QDir &dir, const QString &base, QString *error)
{
    const ClassicScheme &s = look.system;
    auto menuScheme=s;
    if(s.shellMenuTextMode>=0)menuScheme.shellTextModes[1]=s.shellMenuTextMode;
    const QPalette pal = menuScheme.shellPalette(1);
    const QColor face = pal.color(QPalette::Button), shadow = pal.color(QPalette::Dark), light = pal.color(QPalette::Light);
    auto url = [&](const QString &name) -> QString { return base + name; };
    auto filled = [&](QSize size) {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        img.fill(face);
        return img;
    };
    QJsonObject parts;

    QImage frame = filled(QSize(8, 8));
    {
        QPainter p(&frame);
        classic::raisedEdge(&p, frame.rect(), pal);
    }
    save(frame, dir, QStringLiteral("frame.png"));
    parts.insert(QStringLiteral("frame"), part(url(QStringLiteral("frame.png")), frame, {3, 3, 3, 3}, {3, 3, 3, 3}));

    const int inner = 380 - 6;
    QImage header(inner, 40, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&header);
        classic::gradientFill(&p, header.rect(), s.shellColors ? (*s.shellColors)[1] : s.colour(u"ActiveTitle"),
                              s.shellColors ? (*s.shellColors)[1].lighter(140) : s.colour(u"GradientActiveTitle"));
    }
    save(header, dir, QStringLiteral("userpane.png"));
    parts.insert(QStringLiteral("userpane"), part(url(QStringLiteral("userpane.png")), header, {0, 0, 0, 0}, {0, 0, 0, 0},
                                                  QJsonArray{inner, 40}));
    QImage none(1, 1, QImage::Format_ARGB32_Premultiplied);
    none.fill(Qt::transparent);
    save(none, dir, QStringLiteral("userpicture.png"));
    parts.insert(QStringLiteral("userpicture"), part(url(QStringLiteral("userpicture.png")), none, {0, 0, 0, 0}, {0, 0, 0, 0}));

    // The left column ends in the etched line between the columns.
    QImage left = filled(QSize(192, 8));
    {
        QPainter p(&left);
        p.fillRect(QRect(190, 0, 1, 8), shadow);
        p.fillRect(QRect(191, 0, 1, 8), light);
    }
    save(left, dir, QStringLiteral("proglist.png"));
    parts.insert(QStringLiteral("proglist"), part(url(QStringLiteral("proglist.png")), left, {0, 2, 0, 0}, {6, 6, 5, 5},
                                                  QJsonArray{192, 306}));
    save(left, dir, QStringLiteral("moreprograms.png"));
    parts.insert(QStringLiteral("moreprograms"), part(url(QStringLiteral("moreprograms.png")), left, {0, 2, 0, 0}, {6, 8, 0, 6},
                                                      QJsonArray{192, 30}));
    QImage right = filled(QSize(8, 8));
    save(right, dir, QStringLiteral("placeslist.png"));
    parts.insert(QStringLiteral("placeslist"), part(url(QStringLiteral("placeslist.png")), right, {0, 0, 0, 0}, {4, 6, 5, 5},
                                                    QJsonArray{182, 336}));
    auto etched = [&](int width) {
        QImage img = filled(QSize(width, 2));
        QPainter p(&img);
        p.fillRect(QRect(0, 0, width, 1), shadow);
        p.fillRect(QRect(0, 1, width, 1), light);
        return img;
    };
    const QImage progSep = etched(180), placesSep = etched(170);
    save(progSep, dir, QStringLiteral("proglistseparator.png"));
    save(placesSep, dir, QStringLiteral("placeslistseparator.png"));
    parts.insert(QStringLiteral("proglistseparator"), part(url(QStringLiteral("proglistseparator.png")), progSep, {0, 0, 0, 0}, {0, 0, 0, 0}));
    parts.insert(QStringLiteral("placeslistseparator"), part(url(QStringLiteral("placeslistseparator.png")), placesSep, {0, 0, 0, 0}, {0, 0, 0, 0}));

    // All Programs' arrow: the menus' own, a solid triangle.
    QImage arrow(8, 8, QImage::Format_ARGB32_Premultiplied);
    arrow.fill(Qt::transparent);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x <= 3 - qAbs(3 - y); ++x)
            arrow.setPixel(2 + x, y, (s.shellColors ? pal.color(QPalette::ButtonText) : s.colour(u"MenuText")).rgba());
    save(arrow, dir, QStringLiteral("moreprogramsarrow.png"));
    parts.insert(QStringLiteral("moreprogramsarrow"), part(url(QStringLiteral("moreprogramsarrow.png")), arrow, {0, 0, 0, 0}, {0, 0, 0, 0}));

    QImage footer = filled(QSize(inner, 32));
    {
        QPainter p(&footer);
        p.fillRect(QRect(0, 0, inner, 1), shadow);
        p.fillRect(QRect(0, 1, inner, 1), light);
    }
    save(footer, dir, QStringLiteral("logoff.png"));
    parts.insert(QStringLiteral("logoff"), part(url(QStringLiteral("logoff.png")), footer, {0, 0, 2, 0}, {0, 0, 4, 2}, QJsonArray{inner, 32}));

    for (const auto &[id, suffix] : {std::pair(7010, ""), std::pair(7011, "-hot")}) {
        const auto strip = media::bitmap(QStringLiteral("explorer.exe"), id, error);
        if (!strip)
            return QJsonObject();
        const QString name = QStringLiteral("logoffbuttons%1.png").arg(QLatin1String(suffix));
        save(*strip, dir, name);
        QJsonObject spec = part(url(name), *strip, {0, 0, 0, 0}, {0, 0, 0, 0});
        static const char *const buttons[] = {"undock", "logoff", "shutdown"};
        for (int i = 0; i < 3; ++i) {
            const QString file = QStringLiteral("button-%1%2.png").arg(QLatin1String(buttons[i]), QLatin1String(suffix));
            save(strip->copy(i * 24, 0, 24, 24), dir, file);
            spec.insert(QLatin1String(buttons[i]), url(file));
        }
        parts.insert(QStringLiteral("logoffbuttons") + QLatin1String(suffix), spec);
    }

    // All Programs' flyouts: menus, the face in a raised frame.
    QImage submenu(8, 8, QImage::Format_ARGB32_Premultiplied);
    submenu.fill(s.colour(u"Menu"));
    {
        QPainter p(&submenu);
        classic::raisedEdge(&p, submenu.rect(), pal);
    }
    save(submenu, dir, QStringLiteral("submenu.png"));
    parts.insert(QStringLiteral("submenu"), QJsonObject{{QStringLiteral("url"), url(QStringLiteral("submenu.png"))},
                                                        {QStringLiteral("sizing"), QJsonArray{3, 3, 3, 3}}});

    auto hex = [&](const char *name) { return s.shellColors && QString::fromLatin1(name) == QLatin1String("MenuText") ? pal.color(QPalette::ButtonText).name() : s.colour(QString::fromLatin1(name)).name(); };
    const LogFont &menuFont = s.metrics.menuFont;
    auto fontJson = [](const LogFont &f, bool bold, bool italic, int pixels) {
        return QJsonObject{{QStringLiteral("family"), f.face}, {QStringLiteral("pixels"), pixels},
                           {QStringLiteral("bold"), bold}, {QStringLiteral("italic"), italic}};
    };
    return QJsonObject{
        {QStringLiteral("parts"), parts},
        {QStringLiteral("palette"), QJsonObject{{QStringLiteral("programText"), hex("MenuText")},
                                                {QStringLiteral("submenuText"), hex("MenuText")},
                                                {QStringLiteral("submenuHighlightText"), s.shellColors && s.shellMenuTextMode>=0 ? pal.color(QPalette::ButtonText).name() : hex("HilightText")},
                                                {QStringLiteral("secondaryText"), hex("MenuText")},
                                                {QStringLiteral("placesText"), hex("MenuText")},
                                                // (On the caption gradient: the titlebars' text.)
                                                {QStringLiteral("userText"), hex("TitleText")},
                                                {QStringLiteral("userShadow"), QStringLiteral("#00000000")},
                                                {QStringLiteral("footerText"), hex("MenuText")},
                                                {QStringLiteral("highlight"), hex("Hilight")},
                                                {QStringLiteral("highlightText"), s.shellColors && s.shellMenuTextMode>=0 ? pal.color(QPalette::ButtonText).name() : hex("HilightText")}}},
        {QStringLiteral("font"), fontJson(menuFont, menuFont.bold(), menuFont.italic, -menuFont.height)},
        {QStringLiteral("userFont"), fontJson(s.metrics.captionFont, true, true, 18)},
        {QStringLiteral("moreFont"), fontJson(menuFont, false, false, -menuFont.height)},
        {QStringLiteral("userShadowOffset"), QJsonArray{0, 0}},
        {QStringLiteral("layout"), QJsonObject{{QStringLiteral("width"), 380}, {QStringLiteral("height"), 478},
                                               {QStringLiteral("headerHeight"), 40}, {QStringLiteral("footerHeight"), 32},
                                               {QStringLiteral("leftWidth"), 192}, {QStringLiteral("moreHeight"), 30 + 14}}},
        {QStringLiteral("provisional"), QJsonObject{{QStringLiteral("pinnedRowHeight"), 38}, {QStringLiteral("recentRowHeight"), 38},
                                                    {QStringLiteral("moreExtraHeight"), 14}, {QStringLiteral("moreTopGap"), 2},
                                                    {QStringLiteral("moreBottomExtra"), 6}, {QStringLiteral("placesRowHeight"), 30},
                                                    {QStringLiteral("separatorGap"), 8}, {QStringLiteral("userPictureX"), 12},
                                                    {QStringLiteral("userPictureY"), 0}, {QStringLiteral("userNameGap"), 0}}},
    };
}

} // namespace

QJsonObject writeStartPanel(const Look &look, const QString &directory, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return QJsonObject();
    };
    QDir().mkpath(directory);
    // (Absolute file URLs: the plasmoid gets this JSON as a setting.)
    const QString base = QUrl::fromLocalFile(QDir(directory).absolutePath() + QLatin1Char('/')).toString();
    if (look.classic()) {
        QJsonObject result = classicPanel(look, QDir(directory), base, error);
        if (result.isEmpty())
            return result;
        result.insert(QStringLiteral("variant"), look.system.name);
        QSaveFile out(QDir(directory).filePath(QStringLiteral("theme.json")));
        if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(result).toJson()) < 0 || !out.commit())
            return fail(out.errorString());
        return result;
    }
    const Renderer &r = *look.renderer;
    const Ini &ini = r.theme().ini();
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
            QJsonObject spec{{QStringLiteral("url"), QString(base + key + QLatin1String(".png"))},
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
                    spec.insert(QLatin1String(buttons[i]), QString(base + file));
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
        parts.insert(QStringLiteral("submenu"), QJsonObject{{QStringLiteral("url"), QString(base + QLatin1String("submenu.png"))},
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
        {QStringLiteral("highlightText"), hex(r.theme().property(u"xplasmacustomluna",u"",u"",u"menutext").value_or(sys("highlighttext", QStringLiteral("255 255 255"))), qRgb(255, 255, 255))},
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
