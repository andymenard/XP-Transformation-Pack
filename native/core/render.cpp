#include "render.h"

#include "schema.h"

#include <QRegularExpression>

namespace xpl {
namespace {

// Frames run vertically unless ImageLayout says otherwise -- or unless the
// count only divides the width: Royale's ResizeGrip2.bmp is 32x17 with
// ImageCount 2 and no ImageLayout, and XP reports its frames as 16x17.
bool horizontalFrames(const std::optional<QString> &layout, QSize image, int count)
{
    if (layout)
        return layout->compare(QLatin1String("horizontal"), Qt::CaseInsensitive) == 0;
    return count > 1 && image.height() % count != 0 && image.width() % count == 0;
}

QList<int> numbers(const QString &value)
{
    QList<int> out;
    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    for (const QString &part : value.split(separators, Qt::SkipEmptyParts)) {
        bool ok = false;
        const int n = part.toInt(&ok);
        if (!ok)
            return {};
        out.append(n);
    }
    return out;
}

// Premultiply one 8-bit channel. uxtheme premultiplies its 32bpp art once, at
// load, and truncates: floor(c*a/255) matched all 82 semi-transparent pixels
// of Royale's push button, where rounding matched 15 (oracle, 2026-09-23).
inline int premul(int c, int a)
{
    return c * a / 255;
}

// Nearest-neighbour source index for destination pixel `d` of `dstLen`,
// sampling at pixel centres: floor((d + 1/2) * srcLen / dstLen). Measured on
// a squeezed Royale push button: margins 9+9 into 11 rows sample rows
// 0,2,4,6,8 | 14,16,17,19,20,22 -- exactly this.
// Nearest-neighbour source index for destination pixel `d`, sampling at
// pixel centres: floor((d + 1/2) * srcLen / dstLen). Measured on a squeezed
// Royale push button: margins 9+9 into 11 rows sample rows 0,2,4,6,8 |
// 14,16,17,19,20,22 -- exactly this.
inline int sample(int d, int srcLen, int dstLen)
{
    return int((qint64(2 * d + 1) * srcLen) / (qint64(2) * dstLen));
}

// Vertically, XP samples the same way but counting from the *bottom*: DIBs
// are stored bottom-up. It only shows at exact ties (8 rows stretched to 35:
// row 17 samples at exactly 4.0 and XP takes the row below), and it lifted
// Royale from 743 to 825 of 1023 exact renders (oracle, 2026-09-23).
inline int sampleFromBottom(int d, int srcLen, int dstLen)
{
    return srcLen - 1 - sample(dstLen - 1 - d, srcLen, dstLen);
}

enum class Sizing { Stretch, Tile, TrueSize };

Sizing sizingOf(const std::optional<QString> &value)
{
    if (value) {
        if (value->compare(QLatin1String("tile"), Qt::CaseInsensitive) == 0)
            return Sizing::Tile;
        if (value->compare(QLatin1String("truesize"), Qt::CaseInsensitive) == 0)
            return Sizing::TrueSize;
    }
    return Sizing::Stretch;
}

// Copy `src` (a premultiplied frame) region `from` into `dst` region `to`,
// stretching with nearest-neighbour sampling or tiling, per axis. uxtheme
// never filters: every destination pixel is exactly one source pixel.
void blit(QImage &dst, const QRect &to, const QImage &src, const QRect &from, Sizing sizingX, Sizing sizingY)
{
    if (to.isEmpty() || from.isEmpty())
        return;
    const QRect clipped = to.intersected(dst.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        const int dy = y - to.top();
        const int sy = from.top() + (sizingY == Sizing::Tile ? dy % from.height()
                                                           : sampleFromBottom(dy, from.height(), to.height()));
        const auto *srow = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
        auto *drow = reinterpret_cast<QRgb *>(dst.scanLine(y));
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            const int dx = x - to.left();
            const int sx = from.left() + (sizingX == Sizing::Tile ? dx % from.width()
                                                               : sample(dx, from.width(), to.width()));
            const QRgb s = srow[sx];
            const int a = qAlpha(s);
            if (a == 255) {
                drow[x] = s;
            } else if (a != 0) {
                // Source-over in premultiplied space (AlphaBlend's formula).
                const QRgb d = drow[x];
                const int k = 255 - a;
                drow[x] = qRgba(qRed(s) + (qRed(d) * k + 127) / 255, qGreen(s) + (qGreen(d) * k + 127) / 255,
                                qBlue(s) + (qBlue(d) * k + 127) / 255, a + (qAlpha(d) * k + 127) / 255);
            }
        }
    }
}

// Split one axis into (near, middle, far) for a nine-grid. When the
// destination is smaller than the two fixed margins, they shrink in
// proportion and the middle vanishes.
struct Axis
{
    int near, far;
};
Axis split(int destination, int nearMargin, int farMargin)
{
    if (nearMargin + farMargin <= destination || nearMargin + farMargin == 0)
        return {nearMargin, farMargin};
    const int near = int(qint64(destination) * nearMargin / (nearMargin + farMargin));
    return {near, destination - near};
}

} // namespace

bool parseColour(const QString &value, QRgb *out)
{
    const QList<int> n = numbers(value);
    if (n.size() < 3)
        return false;
    *out = qRgb(qBound(0, n[0], 255), qBound(0, n[1], 255), qBound(0, n[2], 255));
    return true;
}

bool parseMargins(const QString &value, QMargins *out)
{
    const QList<int> n = numbers(value);
    if (n.size() < 4)
        return false;
    // MARGINS order: cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight.
    *out = QMargins(n[0], n[2], n[1], n[3]);
    return true;
}

bool parseSize(const QString &value, QSize *out)
{
    const QList<int> n = numbers(value);
    if (n.size() < 2)
        return false;
    *out = QSize(n[0], n[1]);
    return true;
}

bool parseBool(const QString &value)
{
    return value.trimmed().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || value.trimmed() == QLatin1String("1");
}

Renderer::Renderer(std::shared_ptr<const Theme> theme)
    : m_theme(std::move(theme))
{
}

std::optional<QString> Renderer::property(const PartRef &ref, QStringView name) const
{
    const QString part = schema::partName(ref.cls, ref.part);
    const QString state = schema::stateName(ref.cls, part, ref.state);
    return m_theme->property(ref.cls, part, state, name, ref.sub);
}

QString Renderer::chooseImage(const PartRef &ref, int dpi, QSize destination, bool glyph) const
{
    // A plain ImageFile (GlyphImageFile for a glyph) always wins. Only
    // without one do ImageFile1..5 come in -- which is how a scroll arrow's
    // ImageFile is the button while ImageFile1/2 are its arrow glyphs.
    if (auto file = property(ref, glyph ? u"glyphimagefile" : u"imagefile"))
        return *file;
    const QString select = property(ref, u"imageselecttype").value_or(QString()).toLower();
    if (select == QLatin1String("dpi")) {
        // The highest-numbered image whose MinDpi the screen meets.
        for (int i = 5; i >= 1; --i) {
            const int minimum = property(ref, QStringLiteral("mindpi%1").arg(i)).value_or(QStringLiteral("0")).toInt();
            if (minimum != 0 && dpi >= minimum)
                if (auto file = property(ref, QStringLiteral("imagefile%1").arg(i)))
                    return *file;
        }
    } else if (select == QLatin1String("size")) {
        // The highest-numbered image that fits: its MinSize, or failing that
        // its own frame size, within the destination.
        const int count = qMax(1, property(ref, u"imagecount").value_or(QStringLiteral("1")).toInt());
        for (int i = 5; i >= 1; --i) {
            const auto file = property(ref, QStringLiteral("imagefile%1").arg(i));
            if (!file)
                continue;
            QSize need;
            if (!parseSize(property(ref, QStringLiteral("minsize%1").arg(i)).value_or(QString()), &need)) {
                const QImage img = m_theme->bitmap(*file).image;
                const bool horizontal = horizontalFrames(property(ref, u"imagelayout"), img.size(), count);
                need = horizontal ? QSize(img.width() / count, img.height()) : QSize(img.width(), img.height() / count);
            }
            if (need.width() <= destination.width() && need.height() <= destination.height())
                return *file;
        }
    } else {
        return QString();
    }
    return property(ref, u"imagefile1").value_or(QString());
}

Renderer::Frame Renderer::frame(const PartRef &ref, const QString &imageProperty, int dpi, QSize destination) const
{
    Frame out;
    const bool glyph = imageProperty == QLatin1String("glyphimagefile");
    const QString file = chooseImage(ref, dpi, destination, glyph);
    if (file.isEmpty())
        return out;
    const Dib dib = m_theme->bitmap(file);
    if (dib.image.isNull())
        return out;
    const int count = qMax(1, property(ref, u"imagecount").value_or(QStringLiteral("1")).toInt());
    const bool horizontal = horizontalFrames(property(ref, u"imagelayout"), dib.image.size(), count);
    const int w = horizontal ? dib.image.width() / count : dib.image.width();
    const int h = horizontal ? dib.image.height() : dib.image.height() / count;
    if (w <= 0 || h <= 0)
        return out;
    const int index = (ref.state >= 1 && ref.state <= count) ? ref.state - 1 : 0;
    QImage crop = dib.image.copy(horizontal ? QRect(index * w, 0, w, h) : QRect(0, index * h, w, h));

    // Transparency: real per-pixel alpha wins; otherwise an optional colour key.
    const bool keyed = parseBool(property(ref, glyph ? u"glyphtransparent" : u"transparent").value_or(QString()));
    QRgb key = qRgb(255, 0, 255);
    parseColour(property(ref, glyph ? u"glyphtransparentcolor" : u"transparentcolor").value_or(QString()), &key);
    QImage frame(crop.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < crop.height(); ++y) {
        const auto *src = reinterpret_cast<const QRgb *>(crop.constScanLine(y));
        auto *dst = reinterpret_cast<QRgb *>(frame.scanLine(y));
        for (int x = 0; x < crop.width(); ++x) {
            const QRgb c = src[x];
            if (dib.hasAlpha) {
                const int a = qAlpha(c);
                dst[x] = qRgba(premul(qRed(c), a), premul(qGreen(c), a), premul(qBlue(c), a), a);
            } else if (keyed && (c & 0x00ffffffu) == (key & 0x00ffffffu)) {
                dst[x] = 0;
            } else {
                dst[x] = c | 0xff000000u;
            }
        }
    }
    out.image = frame;
    out.valid = true;
    return out;
}

void Renderer::drawImageBackground(QImage &canvas, const PartRef &ref, int dpi) const
{
    const Frame f = frame(ref, QStringLiteral("imagefile"), dpi, canvas.size());
    if (!f.valid)
        return;
    const QImage &img = f.image;
    const QRect dest = canvas.rect();
    const Sizing sizing = sizingOf(property(ref, u"sizingtype"));

    if (sizing == Sizing::TrueSize) {
        const QSize drawn = trueSizeDraw(ref, img.size(), dest.size());
        blit(canvas, QRect(aligned(ref, drawn, dest), drawn), img, img.rect(), Sizing::Stretch, Sizing::Stretch);
        return;
    }

    QMargins m;
    parseMargins(property(ref, u"sizingmargins").value_or(QString()), &m);
    // Margins can't exceed the source frame.
    m.setLeft(qBound(0, m.left(), img.width()));
    m.setRight(qBound(0, m.right(), img.width() - m.left()));
    m.setTop(qBound(0, m.top(), img.height()));
    m.setBottom(qBound(0, m.bottom(), img.height() - m.top()));
    const Axis hx = split(dest.width(), m.left(), m.right());
    const Axis vy = split(dest.height(), m.top(), m.bottom());
    const bool borderOnly = parseBool(property(ref, u"borderonly").value_or(QString()));

    const int sx[] = {0, m.left(), img.width() - m.right(), img.width()};
    const int sy[] = {0, m.top(), img.height() - m.bottom(), img.height()};
    const int dx[] = {0, hx.near, dest.width() - hx.far, dest.width()};
    const int dy[] = {0, vy.near, dest.height() - vy.far, dest.height()};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (borderOnly && row == 1 && col == 1)
                continue;
            const QRect from(sx[col], sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row]);
            const QRect to(dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row]);
            // Only the stretchable middle tiles. Corners, and each edge
            // across its fixed thickness, are always sampled -- the identity
            // at full size, and a proportional squeeze below it (a Tile
            // trackbar thumb squeezed to 5 wide resamples its margin columns).
            // Open: Royale's maximised caption (Tile) stretches its edges
            // along their length instead; see NOTES.md.
            blit(canvas, to, img, from, col == 1 ? sizing : Sizing::Stretch, row == 1 ? sizing : Sizing::Stretch);
        }
    }
}

QSize Renderer::trueSizeDraw(const PartRef &ref, QSize source, QSize destination) const
{
    if (source.isEmpty())
        return source;
    const bool uniform = parseBool(property(ref, u"uniformsizing").value_or(QString()));
    const QString scaling = property(ref, u"truesizescalingtype").value_or(QString()).toLower();
    auto fit = [&](QSize room) {
        if (!uniform)
            return room;
        if (qint64(room.width()) * source.height() < qint64(room.height()) * source.width())
            return QSize(room.width(), int(qint64(source.height()) * room.width() / source.width()));
        return QSize(int(qint64(source.width()) * room.height() / source.height()), room.height());
    };
    // Too small a room. A part that scales (UniformSizing, or a
    // TrueSizeScalingType) shrinks to fit: Royale's 13x13 check box fills a
    // 6x6 room. One that doesn't is drawn at true size and clipped: the More
    // Programs arrow at 8x12 reports TS_DRAW 16,24 and is cut off.
    if (destination.width() < source.width() || destination.height() < source.height()) {
        const bool scales = uniform || (!scaling.isEmpty() && scaling != QLatin1String("none"));
        return scales ? fit(destination.boundedTo(QSize(qMax(destination.width(), 1), qMax(destination.height(), 1)))) : source;
    }
    // TrueSizeScalingType=Size: once the room is StretchMark percent of the
    // image, it grows to fill it -- uniformly with UniformSizing. Royale's
    // 9x9 scroll-arrow glyph in a 71x39 content rect is drawn at 39x39.
    const int mark = property(ref, u"truesizestretchmark").value_or(QStringLiteral("0")).toInt();
    if (scaling != QLatin1String("size") || mark <= 0)
        return source;
    if (qint64(destination.width()) * 100 < qint64(source.width()) * mark
        || qint64(destination.height()) * 100 < qint64(source.height()) * mark)
        return source;
    return fit(destination);
}

// Where an image of `size` goes in `room` under the part's HAlign/VAlign
// (centred by default). Offsets never go negative: an image bigger than its
// room is pinned to the top-left and clipped (Royale's size grip, VAlign
// Bottom, shows only its empty top rows in a 16x4 room).
QPoint Renderer::aligned(const PartRef &ref, QSize size, const QRect &room) const
{
    const QString h = property(ref, u"halign").value_or(QString()).toLower();
    const QString v = property(ref, u"valign").value_or(QString()).toLower();
    const int spareX = room.width() - size.width(), spareY = room.height() - size.height();
    const int x = h == QLatin1String("left") ? 0 : h == QLatin1String("right") ? spareX : spareX / 2;
    const int y = v == QLatin1String("top") ? 0 : v == QLatin1String("bottom") ? spareY : spareY / 2;
    return room.topLeft() + QPoint(qMax(0, x), qMax(0, y));
}

void Renderer::drawBorderFill(QImage &canvas, const PartRef &ref) const
{
    QRgb border = qRgb(0, 0, 0), fill = qRgb(255, 255, 255);
    parseColour(property(ref, u"bordercolor").value_or(QString()), &border);
    parseColour(property(ref, u"fillcolor").value_or(QString()), &fill);
    const int size = qMax(0, property(ref, u"bordersize").value_or(QStringLiteral("1")).toInt());
    const bool borderOnly = parseBool(property(ref, u"borderonly").value_or(QString()));
    // No room inside the border, no border: a 1x1 Edit (BorderSize 1) is
    // drawn in its fill colour.
    const bool framed = canvas.width() > 2 * size && canvas.height() > 2 * size;
    for (int y = 0; y < canvas.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(canvas.scanLine(y));
        for (int x = 0; x < canvas.width(); ++x) {
            const bool edge = framed && (x < size || y < size || x >= canvas.width() - size || y >= canvas.height() - size);
            if (edge)
                row[x] = border | 0xff000000u;
            else if (!borderOnly)
                row[x] = fill | 0xff000000u;
        }
    }
}

void Renderer::drawGlyph(QImage &canvas, const PartRef &ref, int dpi) const
{
    if (property(ref, u"glyphtype").value_or(QString()).compare(QLatin1String("imageglyph"), Qt::CaseInsensitive) != 0)
        return;
    // Chosen by the content rect's size, not the whole part's: a squeezed
    // scroll arrow falls back to its small glyph.
    const QRect content = contentRect(ref, canvas.rect());
    const Frame g = frame(ref, QStringLiteral("glyphimagefile"), dpi, content.size());
    if (!g.valid)
        return;
    const QSize drawn = trueSizeDraw(ref, g.image.size(), content.size());
    blit(canvas, QRect(aligned(ref, drawn, content), drawn), g.image, g.image.rect(), Sizing::Stretch, Sizing::Stretch);
}

QImage Renderer::background(const PartRef &ref, QSize size, int dpi) const
{
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(0);
    if (size.isEmpty())
        return canvas;
    QSize minimum;
    if (parseSize(property(ref, u"minsize").value_or(QString()), &minimum)
        && (size.width() < minimum.width() || size.height() < minimum.height()))
        return canvas;
    const QString bg = property(ref, u"bgtype").value_or(QStringLiteral("imagefile")).toLower();
    if (bg == QLatin1String("borderfill"))
        drawBorderFill(canvas, ref);
    else if (bg != QLatin1String("none"))
        drawImageBackground(canvas, ref, dpi);
    drawGlyph(canvas, ref, dpi);
    return canvas;
}

QSize Renderer::partSize(const PartRef &ref, PartSize kind, QSize draw, int dpi) const
{
    const bool borderFill = property(ref, u"bgtype").value_or(QString()).compare(
                                QLatin1String("borderfill"), Qt::CaseInsensitive) == 0;
    if (kind == PartSize::Min) {
        // The content margins (or what stands in for them) on both sides,
        // never below 1: push button 3+3 -> 6,6; group box (SizingMargins
        // 4,4,4,4) -> 8,8; check box (all 0) -> 1,1; Edit (border 1) -> 2,2.
        const QMargins m = contentMargins(ref);
        return QSize(qMax(1, m.left() + m.right()), qMax(1, m.top() + m.bottom()));
    }
    if (borderFill) {
        // Edit (BorderSize 1): true 3,3 -- the border on both sides plus a
        // pixel of fill -- and no draw size at all.
        const int border = qMax(0, property(ref, u"bordersize").value_or(QStringLiteral("1")).toInt());
        return kind == PartSize::Draw ? QSize(0, 0) : QSize(2 * border + 1, 2 * border + 1);
    }
    const QSize dest = draw.isValid() ? draw : QSize(1 << 16, 1 << 16);
    const Frame f = frame(ref, QStringLiteral("imagefile"), dpi, dest);
    if (!f.valid)
        return QSize();
    if (kind == PartSize::Draw) {
        if (sizingOf(property(ref, u"sizingtype")) != Sizing::TrueSize)
            return draw;
        return trueSizeDraw(ref, f.image.size(), dest);
    }
    return f.image.size();
}

QMargins Renderer::contentMargins(const PartRef &ref) const
{
    // ContentMargins; failing those, SizingMargins for an image background or
    // BorderSize for a BorderFill one (Royale's GroupBox, which has only
    // SizingMargins 4,4,4,4, reports a content rect inset by 4).
    QMargins m;
    if (parseMargins(property(ref, u"contentmargins").value_or(QString()), &m))
        return m;
    if (property(ref, u"bgtype").value_or(QString()).compare(QLatin1String("borderfill"), Qt::CaseInsensitive) == 0) {
        const int b = qMax(0, property(ref, u"bordersize").value_or(QStringLiteral("1")).toInt());
        return QMargins(b, b, b, b);
    }
    parseMargins(property(ref, u"sizingmargins").value_or(QString()), &m);
    return m;
}

QRect Renderer::contentRect(const PartRef &ref, const QRect &bounds) const
{
    // Below MinSize nothing is drawn, and the content rect is the whole rect
    // (Royale's progress bar at 9 wide, MinSize 10).
    QSize minimum;
    if (parseSize(property(ref, u"minsize").value_or(QString()), &minimum)
        && (bounds.width() < minimum.width() || bounds.height() < minimum.height()))
        return bounds;
    QMargins m = contentMargins(ref);
    // Margins are subtracted as they are -- an over-large margin gives an
    // inverted rect, which XP reports as such (a 4x2 toolbar separator:
    // left 5, right 2). Only a SourceShrink part scales them down with its
    // sizing margins, rounding as MulDiv does: a scroll arrow (SizingMargins
    // 5+5, ContentMargins 3) reports 3 at 9 high, 2 at 8 and 1 at 4.
    const auto mulDiv = [](int a, int b, int c) { return int((qint64(a) * b + c / 2) / c); };
    if (parseBool(property(ref, u"sourceshrink").value_or(QString()))) {
        const QMargins s = margins(ref, u"sizingmargins");
        if (s.left() + s.right() > bounds.width()) {
            m.setLeft(mulDiv(m.left(), bounds.width(), s.left() + s.right()));
            m.setRight(mulDiv(m.right(), bounds.width(), s.left() + s.right()));
        }
        if (s.top() + s.bottom() > bounds.height()) {
            m.setTop(mulDiv(m.top(), bounds.height(), s.top() + s.bottom()));
            m.setBottom(mulDiv(m.bottom(), bounds.height(), s.top() + s.bottom()));
        }
    }
    return bounds.marginsRemoved(m);
}

QMargins Renderer::margins(const PartRef &ref, QStringView name) const
{
    QMargins m;
    parseMargins(property(ref, name).value_or(QString()), &m);
    return m;
}

bool Renderer::hasPart(const PartRef &ref) const
{
    const QString bg = property(ref, u"bgtype").value_or(QStringLiteral("imagefile")).toLower();
    if (bg == QLatin1String("borderfill"))
        return true;
    return bg != QLatin1String("none") && !chooseImage(ref, 96, QSize(1 << 16, 1 << 16), false).isEmpty();
}

QImage Renderer::over(const QImage &premultiplied, QRgb background)
{
    QImage out(premultiplied.size(), QImage::Format_RGB32);
    const int br = qRed(background), bgc = qGreen(background), bb = qBlue(background);
    for (int y = 0; y < premultiplied.height(); ++y) {
        const auto *src = reinterpret_cast<const QRgb *>(premultiplied.constScanLine(y));
        auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < premultiplied.width(); ++x) {
            const QRgb s = src[x];
            const int k = 255 - qAlpha(s);
            dst[x] = qRgb(qRed(s) + (br * k + 127) / 255, qGreen(s) + (bgc * k + 127) / 255,
                          qBlue(s) + (bb * k + 127) / 255);
        }
    }
    return out;
}

} // namespace xpl
