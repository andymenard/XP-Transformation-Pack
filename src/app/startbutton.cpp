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
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
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
        QImage mask(p.device()->width(), p.device()->height(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        {
            QPainter m(&mask);
            m.setFont(font);
            m.setPen(Qt::black);
            m.drawText(origin + shadowOffset, label);
        }
        QImage shadow = blurred(mask, 1);
        QPainter s(&shadow);
        s.setCompositionMode(QPainter::CompositionMode_SourceIn);
        s.fillRect(shadow.rect(), shadowColour);
        s.end();
        p.drawImage(0, 0, shadow);
    }
    p.setPen(colour);
    p.drawText(origin, label);
}

std::optional<StartButtonArt> styleButton(const Look &look, const QImage &flag, const QString &label, QString *error)
{
    const Renderer &r = *look.renderer;
    const PartRef normal{QStringLiteral("button"), schema::partId(u"button", u"pushbutton"), 1, QStringLiteral("start")};
    if (!r.hasPart(normal)) {
        fail(error, QStringLiteral("%1 has no Start button").arg(look.name));
        return std::nullopt;
    }
    const QFont font = fontFrom(r.property(normal, u"font").value_or(QString()),
                                QFont(QStringLiteral("Franklin Gothic Medium")));
    QRgb text = qRgb(255, 255, 255), shadow = qRgb(0, 0, 0);
    parseColour(r.property(normal, u"textcolor").value_or(QString()), &text);
    parseColour(r.property(normal, u"textshadowcolor").value_or(QString()), &shadow);
    const QStringList off = r.property(normal, u"textshadowoffset").value_or(QStringLiteral("1, 1")).split(QLatin1Char(','));
    const QPoint shadowOffset(off.value(0).trimmed().toInt(), off.value(1, off.value(0)).trimmed().toInt());
    const QString shadowType = r.property(normal, u"textshadowtype").value_or(QString());

    // Measured (Luna, 800 x 600): the button is its content margins round
    // the flag and the label with a pixel either side of it, 2px taller than
    // the bar (the rest is off the screen's bottom); the flag at the
    // content's left, centred.
    const QMargins cm = r.contentMargins(normal);
    const int textWidth = gdiTextWidth(font, label) + 2;
    const QSize size(cm.left() + flag.width() + textWidth + cm.right(), kTaskbarHeight + 2);
    const QRect content = QRect(QPoint(), size).marginsRemoved(cm);

    StartButtonArt art;
    QImage *out[] = {&art.normal, &art.hover, &art.pressed};
    for (int state = 1; state <= 3; ++state) {
        QImage img = r.background({normal.cls, normal.part, state, normal.sub}, size);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.drawImage(content.left(), content.top() + (content.height() - flag.height()) / 2, flag);
            const QRect labelRect(content.left() + flag.width(), content.top(), textWidth, content.height());
            drawLabel(p, labelRect, label, font, QColor(text), shadowType, QColor(shadow), shadowOffset);
        }
        *out[state - 1] = img.copy(0, 0, size.width(), kTaskbarHeight);
    }
    return art;
}

// Windows Classic: a push button 2px inside the bar, the flag and the label
// in the caption font, bold.
std::optional<StartButtonArt> classicButton(const Look &look, const QImage &flag, const QString &label)
{
    const ClassicScheme &s = look.system;
    const QPalette pal = s.palette();
    const int barHeight = 28;
    const int h = barHeight - 6;
    QFont font(s.metrics.captionFont.face);
    font.setPixelSize(-s.metrics.captionFont.height);
    font.setBold(true);
    const QImage icon = flag.scaledToHeight(qMin(16, h - 6), Qt::SmoothTransformation);
    const int textWidth = QFontMetrics(font).horizontalAdvance(label);
    const int w = 6 + icon.width() + 4 + textWidth + 8;
    StartButtonArt art;
    QImage *out[] = {&art.normal, &art.hover, &art.pressed};
    for (int state = 0; state < 3; ++state) {
        QImage img(w, barHeight, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        const QRect button(0, 3, w, h);
        const bool down = state == 2;
        classic::pushEdge(&p, button, pal, down);
        const QPoint shift = down ? QPoint(1, 1) : QPoint();
        p.drawImage(button.topLeft() + QPoint(6, (h - icon.height()) / 2) + shift, icon);
        p.setFont(font);
        p.setPen(s.colour(u"ButtonText"));
        p.drawText(QRect(button.left() + 6 + icon.width() + 4, button.top(), textWidth, h).translated(shift),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, label);
        p.end();
        *out[state] = img;
    }
    return art;
}

} // namespace

std::optional<StartButtonArt> startButton(const Look &look, const QImage &flag, const QString &label, QString *error)
{
    if (flag.isNull()) {
        fail(error, QStringLiteral("no Start flag: XPLasma needs the Windows XP media for it"));
        return std::nullopt;
    }
    const QImage premultiplied = flag.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return look.classic() ? classicButton(look, premultiplied, label) : styleButton(look, premultiplied, label, error);
}

} // namespace xpl
