// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/startbutton.h"

#include "app/gdifont.h"
#include "app/panel.h"
#include "core/schema.h"

#include <QFontMetrics>
#include <QPainter>

#include <cmath>

namespace xpl {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// "Franklin Gothic Medium, 14, italic" at 96 dpi, as GDI sizes it
// (MulDiv(points, 96, 72) pixels).
QFont fontFrom(const QString &spec, const QFont &fallback)
{
    const QStringList parts = spec.split(QLatin1Char(','));
    if (parts.size() < 2)
        return fallback;
    QFont font(parts.at(0).trimmed());
    font.setPixelSize(int(std::lround(parts.at(1).trimmed().toInt() * 96.0 / 72.0)));
    for (int i = 2; i < parts.size(); ++i) {
        const QString flag = parts.at(i).trimmed().toLower();
        if (flag == QLatin1String("bold"))
            font.setBold(true);
        else if (flag == QLatin1String("italic"))
            font.setItalic(true);
    }
    return gdiFont(font);
}

// Drawn `scale` times over, for a HiDPI screen: smoothed, as no font is
// bi-level that large.
QFont scaledFont(QFont font, int scale)
{
    if (scale > 1)
        font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

// The art at `scale`: drawn on in the bar's pixels (devicePixelRatio), its
// bitmaps -- XP's button and flag -- their own pixels, made bigger.
QImage scaledUp(const QImage &image, int scale)
{
    QImage out = scale == 1 ? image : image.scaled(image.size() * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    out.setDevicePixelRatio(scale);
    return out;
}

QImage blank(QSize size, int scale)
{
    QImage out(size * scale, QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(scale);
    out.fill(Qt::transparent);
    return out;
}

QSize logical(const QImage &image)
{
    return image.deviceIndependentSize().toSize();
}

// The bar's height of it (the button is 2px taller).
QImage barHeight(const QImage &image, int width)
{
    const int scale = qRound(image.devicePixelRatio());
    return image.copy(0, 0, width * scale, kTaskbarHeight * scale);
}

// A box blur of an alpha mask, `radius` wide, twice: uxtheme's "continuous"
// text shadow is the text's shape softened, not an outline.
QImage blurred(const QImage &mask, int radius)
{
    QImage out = mask;
    for (int pass = 0; pass < 2; ++pass)
        for (const bool horizontal : {true, false}) {
            const QImage in = out;
            for (int y = 0; y < in.height(); ++y)
                for (int x = 0; x < in.width(); ++x) {
                    int sum = 0, n = 0;
                    for (int d = -radius; d <= radius; ++d) {
                        const int sx = horizontal ? x + d : x, sy = horizontal ? y : y + d;
                        if (sx < 0 || sy < 0 || sx >= in.width() || sy >= in.height())
                            continue;
                        sum += qAlpha(in.pixel(sx, sy));
                        ++n;
                    }
                    out.setPixel(x, y, qRgba(0, 0, 0, n ? sum / n : 0));
                }
        }
    return out;
}

// DrawText(DT_CENTER | DT_VCENTER | DT_SINGLELINE) by GDI's measures.
void drawLabel(QPainter &p, const QRect &rect, const QString &label, const QFont &font, const QColor &colour,
               const QString &shadowType, const QColor &shadowColour, QPoint shadowOffset)
{
    p.setFont(font);
    const GdiMetrics gm = gdiMetrics(font);
    const QPoint origin(rect.left() + (rect.width() - gdiTextWidth(font, label)) / 2,
                        rect.top() + (rect.height() - gm.height()) / 2 + gm.ascent);
    if (shadowType.compare(QLatin1String("single"), Qt::CaseInsensitive) == 0) {
        p.setPen(shadowColour);
        p.drawText(origin + shadowOffset, label);
    } else if (shadowType.compare(QLatin1String("continuous"), Qt::CaseInsensitive) == 0) {
        const qreal dpr = p.device()->devicePixelRatioF();
        QImage mask(p.device()->width(), p.device()->height(), QImage::Format_ARGB32_Premultiplied);
        mask.setDevicePixelRatio(dpr);
        mask.fill(Qt::transparent);
        {
            QPainter m(&mask);
            m.setFont(font);
            m.setPen(Qt::black);
            m.drawText(origin + shadowOffset, label);
        }
        QImage shadow = blurred(mask, qRound(dpr));
        QPainter s(&shadow);
        s.setCompositionMode(QPainter::CompositionMode_SourceIn);
        s.fillRect(shadow.rect(), shadowColour);
        s.end();
        p.drawImage(0, 0, shadow);
    }
    p.setPen(colour);
    p.drawText(origin, label);
}

// Characters as a logo, `height` tall: drawn large and scaled down, in
// `colour` -- the label's -- or their own colours where the font has them
// (an emoji). At `scale`, like the rest.
QImage characters(const QString &text, int height, const QColor &colour, int scale)
{
    QFont font;
    font.setPixelSize(height * 4);
    const QFontMetrics fm(font);
    QImage big(fm.horizontalAdvance(text) + height * 2, fm.height() + height, QImage::Format_ARGB32_Premultiplied);
    big.fill(Qt::transparent);
    {
        QPainter p(&big);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        p.setFont(font);
        p.setPen(colour);
        p.drawText(QPoint(height, height / 2 + fm.ascent()), text);
    }
    QRect box;
    for (int y = 0; y < big.height(); ++y)
        for (int x = 0; x < big.width(); ++x)
            if (qAlpha(big.pixel(x, y)))
                box |= QRect(x, y, 1, 1);
    if (box.isEmpty())
        return QImage();
    QImage out = big.copy(box).scaledToHeight(height * scale, Qt::SmoothTransformation);
    out.setDevicePixelRatio(scale);
    return out;
}

// A picture as a logo: `height` tall (no wider than three times that), at
// `scale`.
QImage picture(const QString &file, int height, int scale)
{
    const QImage image(file);
    if (image.isNull())
        return QImage();
    height *= scale;
    const QImage scaled = image.scaledToHeight(height, Qt::SmoothTransformation);
    QImage out = (scaled.width() > 3 * height ? image.scaled(3 * height, height, Qt::KeepAspectRatio, Qt::SmoothTransformation) : scaled)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(scale);
    return out;
}

std::optional<StartButtonArt> styleButton(const Look &look, QImage flag, const QString &label, const QString &logoText,
                                          int scale, QString *error)
{
    const Renderer &r = *look.renderer;
    const PartRef normal{QStringLiteral("button"), schema::partId(u"button", u"pushbutton"), 1, QStringLiteral("start")};
    if (!r.hasPart(normal)) {
        fail(error, QStringLiteral("%1 has no Start button").arg(look.name));
        return std::nullopt;
    }
    const QFont font = scaledFont(fontFrom(r.property(normal, u"font").value_or(QString()),
                                           QFont(QStringLiteral("Franklin Gothic Medium"))),
                                  scale);
    QRgb text = qRgb(255, 255, 255), shadow = qRgb(0, 0, 0);
    parseColour(r.property(normal, u"textcolor").value_or(QString()), &text);
    parseColour(r.property(normal, u"textshadowcolor").value_or(QString()), &shadow);
    const QStringList off = r.property(normal, u"textshadowoffset").value_or(QStringLiteral("1, 1")).split(QLatin1Char(','));
    const QPoint shadowOffset(off.value(0).trimmed().toInt(), off.value(1, off.value(0)).trimmed().toInt());
    const QString shadowType = r.property(normal, u"textshadowtype").value_or(QString());
    if (const QImage logo = characters(logoText, logical(flag).height(), QColor(text), scale); !logoText.isEmpty() && !logo.isNull())
        flag = logo;
    const QSize flagSize = logical(flag);

    // Measured (Luna, 800 x 600): the button is its content margins round
    // the flag and the label with a pixel either side of it, 2px taller than
    // the bar (the rest is off the screen's bottom); the flag at the
    // content's left, centred.
    const QMargins cm = r.contentMargins(normal);
    const int textWidth = gdiTextWidth(font, label) + 2;
    const QSize size(cm.left() + flagSize.width() + textWidth + cm.right(), kTaskbarHeight + 2);
    const QRect content = QRect(QPoint(), size).marginsRemoved(cm);

    StartButtonArt art;
    art.scale = scale;
    QImage *out[] = {&art.normal, &art.hover, &art.pressed};
    for (int state = 1; state <= 3; ++state) {
        QImage img = scaledUp(r.background({normal.cls, normal.part, state, normal.sub}, size), scale);
        art.backgrounds[state-1] = barHeight(img,size.width());
        art.textColor = QColor(text);
        QImage foreground=blank(size,scale);
        QImage mask=blank(size,scale);
        const QRect labelRect(content.left()+flagSize.width(),content.top(),textWidth,content.height());
        {
            QPainter p(&foreground);p.setRenderHint(QPainter::TextAntialiasing);
            p.drawImage(content.left(),content.top()+(content.height()-flagSize.height())/2,flag);
            QImage shadowMask=blank(size,scale);
            {QPainter shadowPainter(&shadowMask);shadowPainter.setRenderHint(QPainter::TextAntialiasing);
             drawLabel(shadowPainter,labelRect,label,font,Qt::transparent,shadowType,Qt::white,shadowOffset);}
            art.shadowMasks[state-1]=barHeight(shadowMask,size.width());
            QPainter glyph(&mask);glyph.setRenderHint(QPainter::TextAntialiasing);
            drawLabel(glyph,labelRect,label,font,Qt::white,QString(),QColor(),QPoint());
        }
        art.foregrounds[state-1]=barHeight(foreground,size.width());
        art.textMasks[state-1]=barHeight(mask,size.width());
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.drawImage(content.left(), content.top() + (content.height() - flagSize.height()) / 2, flag);
            drawLabel(p, labelRect, label, font, QColor(text), shadowType, QColor(shadow), shadowOffset);
        }
        *out[state - 1] = barHeight(img, size.width());
    }
    return art;
}

// Windows Classic (measured, Windows Standard, 800 x 600): a push button
// 22px tall, 2px in and 4px down the 30px bar; the small flag (bitmap 176)
// 2px into it, centred; a pixel after it "Start" in bold Tahoma, centred in
// a slot 2px wider than the text; 2px after that. Pushed, the contents move 1px down and right.
std::optional<StartButtonArt> classicButton(const Look &look, QImage flag, const QString &label, const QString &logoText,
                                            int scale)
{
    const ClassicScheme &s = look.system;
    const QPalette pal = s.shellPalette(0);
    if (const QImage logo = characters(logoText, logical(flag).height(), s.colour(u"ButtonText"), scale); !logoText.isEmpty() && !logo.isNull())
        flag = logo;
    const QSize flagSize = logical(flag);
    QFont font(s.metrics.messageFont.face);
    font.setPixelSize(-s.metrics.messageFont.height);
    font.setBold(true);
    font = scaledFont(gdiFont(font), scale);
    const int textWidth = gdiTextWidth(font, label) + 2;
    const QRect button(2, 4, 2 + flagSize.width() + 1 + textWidth + 2, 22);
    const QSize size(button.right() + 1, kTaskbarHeight);
    StartButtonArt art;
    art.scale = scale;
    QImage *out[] = {&art.normal, &art.hover, &art.pressed};
    for (int state = 0; state < 3; ++state) {
        const bool down = state == 2;
        QImage edge(size, QImage::Format_ARGB32_Premultiplied);
        edge.fill(Qt::transparent);
        {
            QPainter e(&edge);
            classic::pushEdge(&e, button, pal, down);
        }
        QImage img = scaledUp(edge, scale);
        QPainter p(&img);
        art.backgrounds[state]=img.copy();
        art.textColor=pal.color(QPalette::ButtonText);
        const QPoint shift = down ? QPoint(1, 1) : QPoint();
        QImage fg=blank(size,scale);
        QImage mask=blank(size,scale);
        {
            QPainter fp(&fg);fp.drawImage(button.topLeft()+QPoint(2,(button.height()-flagSize.height())/2)+shift,flag);
            QPainter mp(&mask);mp.setRenderHint(QPainter::TextAntialiasing);
            drawLabel(mp,QRect(button.left()+2+flagSize.width()+1,button.top(),textWidth,button.height()).translated(shift),
                label,font,Qt::white,QString(),QColor(),QPoint());
        }
        art.foregrounds[state]=fg;art.textMasks[state]=mask;art.shadowMasks[state]=blank(size,scale);
        p.drawImage(button.topLeft() + QPoint(2, (button.height() - flagSize.height()) / 2) + shift, flag);
        p.setRenderHint(QPainter::TextAntialiasing);
        drawLabel(p, QRect(button.left() + 2 + flagSize.width() + 1, button.top(), textWidth, button.height()).translated(shift),
                  label, font, pal.color(QPalette::ButtonText), QString(), QColor(), QPoint());
        p.end();
        *out[state] = img;
    }
    return art;
}

} // namespace

std::optional<StartButtonArt> startButton(const Look &look, const QImage &flag, const QString &label, QString *error,
                                          int scale)
{
    if (flag.isNull()) {
        fail(error, QStringLiteral("no Start flag: XPlasma needs the Windows XP media for it"));
        return std::nullopt;
    }
    const QImage premultiplied = scaledUp(flag.convertToFormat(QImage::Format_ARGB32_Premultiplied), scale);
    return look.classic() ? classicButton(look, premultiplied, label, QString(), scale)
                          : styleButton(look, premultiplied, label, QString(), scale, error);
}

std::optional<StartButtonArt> startButton(const Look &look, const QImage &flag, const StartButtonExtras &extras, QString *error,
                                          int scale)
{
    if (flag.isNull()) {
        fail(error, QStringLiteral("no Start flag: XPlasma needs the Windows XP media for it"));
        return std::nullopt;
    }
    QImage logo = scaledUp(flag.convertToFormat(QImage::Format_ARGB32_Premultiplied), scale);
    QString logoText = extras.logoText;
    if (!extras.logoFile.isEmpty()) {
        // (A picture that's gone or unreadable: XP's flag, not no button.)
        if (const QImage pic = picture(extras.logoFile, flag.height(), scale); !pic.isNull())
            logo = pic;
        logoText.clear();
    }
    const QString label = !extras.label.isEmpty() ? extras.label : look.classic() ? QStringLiteral("Start") : QStringLiteral("start");
    return look.classic() ? classicButton(look, logo, label, logoText, scale) : styleButton(look, logo, label, logoText, scale, error);
}

StartButtonExtras StartButtonExtras::fromJson(const QJsonObject &o)
{
    return {o.value(QStringLiteral("label")).toString(), o.value(QStringLiteral("logoText")).toString(),
            o.value(QStringLiteral("logoFile")).toString()};
}

QJsonObject StartButtonExtras::toJson() const
{
    QJsonObject o;
    if (!label.isEmpty())
        o.insert(QStringLiteral("label"), label);
    if (!logoText.isEmpty())
        o.insert(QStringLiteral("logoText"), logoText);
    if (!logoFile.isEmpty())
        o.insert(QStringLiteral("logoFile"), logoFile);
    return o;
}

} // namespace xpl
