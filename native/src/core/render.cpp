// SPDX-License-Identifier: GPL-3.0-or-later
#include "render.h"

#include "schema.h"

#include <QHash>
#include <QMutex>
#include <QVarLengthArray>

#include <algorithm>
#include <cstring>

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

inline bool isSeparator(QChar c)
{
    // [,\s] as QRegularExpression reads it (no UCP): ',' and ASCII whitespace.
    const ushort u = c.unicode();
    return u == ',' || u == ' ' || (u >= '\t' && u <= '\r');
}

// The integers in a "1, 2, 3" property value; empty if any of them isn't one.
// (Split by hand: a regular expression split cost more than the lookup that
// found the value.)
QList<int> numbers(const QString &value)
{
    QList<int> out;
    const QChar *p = value.constData(), *end = p + value.size();
    while (p < end) {
        while (p < end && isSeparator(*p))
            ++p;
        if (p == end)
            break;
        const QChar *start = p;
        while (p < end && !isSeparator(*p))
            ++p;
        // The common case, a short decimal, directly; anything else as
        // QString::toInt reads it, so nothing parses differently.
        const QChar *d = start;
        const bool negative = d->unicode() == '-';
        if (negative || d->unicode() == '+')
            ++d;
        if (d < p && p - d <= 9 && std::all_of(d, p, [](QChar c) { return c.unicode() >= '0' && c.unicode() <= '9'; })) {
            int n = 0;
            for (; d < p; ++d)
                n = n * 10 + (d->unicode() - '0');
            out.append(negative ? -n : n);
            continue;
        }
        bool ok = false;
        const int n = QString(start, int(p - start)).toInt(&ok);
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
    if (clipped.isEmpty())
        return;
    // Each destination column's source column, worked out once per blit
    // rather than (with a 64-bit division) for every pixel.
    const int n = clipped.width();
    QVarLengthArray<int, 512> columns(n);
    for (int i = 0; i < n; ++i) {
        const int dx = clipped.left() + i - to.left();
        columns[i] = from.left() + (sizingX == Sizing::Tile ? dx % from.width() : sample(dx, from.width(), to.width()));
    }
    // A source row whose sampled pixels are all opaque is copied as it is,
    // whatever lies beneath: in one fill when it is one colour (a frame's
    // edge or middle), and the row below that samples it again copied from
    // the row just made -- which is most of a large stretched frame.
    int previousSy = -1;
    bool previousOpaque = false;
    const QRgb *previousRow = nullptr;
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        const int dy = y - to.top();
        const int sy = from.top() + (sizingY == Sizing::Tile ? dy % from.height()
                                                           : sampleFromBottom(dy, from.height(), to.height()));
        auto *drow = reinterpret_cast<QRgb *>(dst.scanLine(y)) + clipped.left();
        if (sy == previousSy && previousOpaque) {
            std::memcpy(drow, previousRow, size_t(n) * sizeof(QRgb));
            previousRow = drow;
            continue;
        }
        const auto *srow = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
        const QRgb first = srow[columns[0]];
        bool opaque = true, uniform = true;
        for (int i = 0; i < n && opaque; ++i) {
            const QRgb s = srow[columns[i]];
            opaque = qAlpha(s) == 255;
            uniform = uniform && s == first;
        }
        previousSy = sy;
        previousOpaque = opaque;
        previousRow = drow;
        if (opaque) {
            if (uniform)
                std::fill_n(drow, n, first);
            else
                for (int i = 0; i < n; ++i)
                    drow[i] = srow[columns[i]];
            continue;
        }
        for (int i = 0; i < n; ++i) {
            const QRgb s = srow[columns[i]];
            const int a = qAlpha(s);
            if (a == 255) {
                drow[i] = s;
            } else if (a != 0) {
                // Source-over in premultiplied space (AlphaBlend's formula).
                const QRgb d = drow[i];
                const int k = 255 - a;
                drow[i] = qRgba(qRed(s) + (qRed(d) * k + 127) / 255, qGreen(s) + (qGreen(d) * k + 127) / 255,
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

// Everything the renderer reads about one (sub, class, part, state), as the
// expressions it used to evaluate at every call evaluated them.
struct Renderer::Resolved
{
    QString cls, sub, partName, stateName;
    int state = 0;

    enum Bg { Image, BorderFill, None } bg = Image;
    bool hasMinSize = false;
    QSize minSize;

    std::optional<QString> image[2];         // ImageFile, GlyphImageFile
    QString select;                          // ImageSelectType, lower-case
    std::optional<QString> imageN[6];        // ImageFile1..5
    int minDpi[6] = {};
    bool hasMinSizeN[6] = {};
    QSize minSizeN[6];
    int count = 1;
    std::optional<QString> layout;
    bool transparent[2] = {};                // Transparent, GlyphTransparent
    QRgb transparentColour[2] = {qRgb(255, 0, 255), qRgb(255, 0, 255)};

    Sizing sizing = Sizing::Stretch;
    QMargins sizingMargins;
    bool borderOnly = false;
    bool uniform = false;
    QString scaling;                         // TrueSizeScalingType, lower-case
    int stretchMark = 0;
    QString halign, valign;                  // lower-case

    QRgb borderColour = qRgb(0, 0, 0), fillColour = qRgb(255, 255, 255);
    int borderSize = 1;
    bool imageGlyph = false;
    bool hasContentMargins = false;
    QMargins contentMarginsValue;
    bool sourceShrink = false;

    // Filled in on demand, under Cache::lock.
    int hasPart = -1;
    QHash<QString, QImage> frames[2];        // by image file
    QHash<QString, std::optional<QString>> properties;
};

namespace {
struct ResolvedKey
{
    QString cls, sub;
    int part, state;
    bool operator==(const ResolvedKey &o) const
    {
        return part == o.part && state == o.state && cls == o.cls && sub == o.sub;
    }
};
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
size_t qHash(const ResolvedKey &k, size_t seed = 0)
#else
uint qHash(const ResolvedKey &k, uint seed = 0)
#endif
{
    return ::qHash(k.cls, seed) ^ (::qHash(k.sub, seed) * 31) ^ ::qHash(k.part * 64 + k.state, seed);
}
} // namespace

struct Renderer::Cache
{
    QMutex lock;
    QHash<ResolvedKey, Resolved *> parts;   // owned; never removed while the renderer lives
    ~Cache() { qDeleteAll(parts); }
};

Renderer::Renderer(std::shared_ptr<const Theme> theme)
    : m_theme(std::move(theme)), m_cache(std::make_unique<Cache>())
{
}

Renderer::~Renderer() = default;

const Renderer::Resolved &Renderer::resolved(const PartRef &ref) const
{
    const ResolvedKey key{ref.cls, ref.sub, ref.part, ref.state};
    QMutexLocker lock(&m_cache->lock);
    if (auto it = m_cache->parts.constFind(key); it != m_cache->parts.cend())
        return *it.value();

    auto *p = new Resolved;
    p->cls = ref.cls;
    p->sub = ref.sub;
    p->state = ref.state;
    p->partName = schema::partName(ref.cls, ref.part);
    p->stateName = schema::stateName(ref.cls, p->partName, ref.state);
    const auto get = [&](QStringView name) {
        return m_theme->property(p->cls, p->partName, p->stateName, name, p->sub);
    };
    const auto text = [&](QStringView name) { return get(name).value_or(QString()); };
    const auto lower = [&](QStringView name) { return text(name).toLower(); };

    const QString bg = get(u"bgtype").value_or(QStringLiteral("imagefile")).toLower();
    p->bg = bg == QLatin1String("borderfill") ? Resolved::BorderFill
          : bg == QLatin1String("none")       ? Resolved::None
                                              : Resolved::Image;
    p->hasMinSize = parseSize(text(u"minsize"), &p->minSize);

    p->image[0] = get(u"imagefile");
    p->image[1] = get(u"glyphimagefile");
    p->select = lower(u"imageselecttype");
    for (int i = 1; i <= 5; ++i) {
        p->imageN[i] = get(QStringLiteral("imagefile%1").arg(i));
        p->minDpi[i] = get(QStringLiteral("mindpi%1").arg(i)).value_or(QStringLiteral("0")).toInt();
        p->hasMinSizeN[i] = parseSize(text(QStringLiteral("minsize%1").arg(i)), &p->minSizeN[i]);
    }
    p->count = qMax(1, get(u"imagecount").value_or(QStringLiteral("1")).toInt());
    p->layout = get(u"imagelayout");
    p->transparent[0] = parseBool(text(u"transparent"));
    p->transparent[1] = parseBool(text(u"glyphtransparent"));
    parseColour(text(u"transparentcolor"), &p->transparentColour[0]);
    parseColour(text(u"glyphtransparentcolor"), &p->transparentColour[1]);

    p->sizing = sizingOf(get(u"sizingtype"));
    parseMargins(text(u"sizingmargins"), &p->sizingMargins);
    p->borderOnly = parseBool(text(u"borderonly"));
    p->uniform = parseBool(text(u"uniformsizing"));
    p->scaling = lower(u"truesizescalingtype");
    p->stretchMark = get(u"truesizestretchmark").value_or(QStringLiteral("0")).toInt();
    p->halign = lower(u"halign");
    p->valign = lower(u"valign");

    parseColour(text(u"bordercolor"), &p->borderColour);
    parseColour(text(u"fillcolor"), &p->fillColour);
    p->borderSize = qMax(0, get(u"bordersize").value_or(QStringLiteral("1")).toInt());
    p->imageGlyph = text(u"glyphtype").compare(QLatin1String("imageglyph"), Qt::CaseInsensitive) == 0;
    p->hasContentMargins = parseMargins(text(u"contentmargins"), &p->contentMarginsValue);
    p->sourceShrink = parseBool(text(u"sourceshrink"));

    m_cache->parts.insert(key, p);
    return *p;   // (stable: the cache holds pointers)
}

std::optional<QString> Renderer::property(const PartRef &ref, QStringView name) const
{
    Resolved &p = const_cast<Resolved &>(resolved(ref));
    const QString key = name.toString();
    QMutexLocker lock(&m_cache->lock);
    if (auto it = p.properties.constFind(key); it != p.properties.cend())
        return it.value();
    const auto value = m_theme->property(p.cls, p.partName, p.stateName, name, p.sub);
    p.properties.insert(key, value);
    return value;
}

QString Renderer::chooseImage(const Resolved &p, int dpi, QSize destination, bool glyph) const
{
    // A plain ImageFile (GlyphImageFile for a glyph) always wins. Only
    // without one do ImageFile1..5 come in -- which is how a scroll arrow's
    // ImageFile is the button while ImageFile1/2 are its arrow glyphs.
    if (p.image[glyph])
        return *p.image[glyph];
    if (p.select == QLatin1String("dpi")) {
        // The highest-numbered image whose MinDpi the screen meets.
        for (int i = 5; i >= 1; --i)
            if (p.minDpi[i] != 0 && dpi >= p.minDpi[i] && p.imageN[i])
                return *p.imageN[i];
    } else if (p.select == QLatin1String("size")) {
        // The highest-numbered image that fits: its MinSize, or failing that
        // its own frame size, within the destination.
        for (int i = 5; i >= 1; --i) {
            if (!p.imageN[i])
                continue;
            QSize need = p.minSizeN[i];
            if (!p.hasMinSizeN[i]) {
                const QImage img = m_theme->bitmap(*p.imageN[i]).image;
                const bool horizontal = horizontalFrames(p.layout, img.size(), p.count);
                need = horizontal ? QSize(img.width() / p.count, img.height()) : QSize(img.width(), img.height() / p.count);
            }
            if (need.width() <= destination.width() && need.height() <= destination.height())
                return *p.imageN[i];
        }
    } else {
        return QString();
    }
    return p.imageN[1].value_or(QString());
}

QImage Renderer::stateImage(const PartRef &ref, bool glyph, QSize destination, int dpi) const
{
    return frame(resolved(ref), glyph, dpi, destination);
}

// The state's frame of the chosen image, keyed and premultiplied; null if
// there is none. Cut once per image file and kept.
QImage Renderer::frame(const Resolved &p, bool glyph, int dpi, QSize destination) const
{
    const QString file = chooseImage(p, dpi, destination, glyph);
    if (file.isEmpty())
        return QImage();
    {
        QMutexLocker lock(&m_cache->lock);
        if (auto it = p.frames[glyph].constFind(file); it != p.frames[glyph].cend())
            return it.value();
    }
    QImage out;
    const Dib dib = m_theme->bitmap(file);
    const int count = p.count;
    const bool horizontal = horizontalFrames(p.layout, dib.image.size(), count);
    const int w = horizontal ? dib.image.width() / count : dib.image.width();
    const int h = horizontal ? dib.image.height() : dib.image.height() / count;
    if (!dib.image.isNull() && w > 0 && h > 0) {
        const int index = (p.state >= 1 && p.state <= count) ? p.state - 1 : 0;
        const int left = horizontal ? index * w : 0, top = horizontal ? 0 : index * h;

        // Transparency: real per-pixel alpha wins -- only when the bitmap has
        // partial alpha, as uxtheme decides; a 0/255-only alpha channel is
        // ignored -- otherwise an optional colour key.
        const bool alpha = dib.hasAlpha && dib.partialAlpha;
        const bool keyed = p.transparent[glyph];
        const QRgb key = p.transparentColour[glyph] & 0x00ffffffu;
        out = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < h; ++y) {
            const auto *src = reinterpret_cast<const QRgb *>(dib.image.constScanLine(top + y)) + left;
            auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
            if (alpha) {
                for (int x = 0; x < w; ++x) {
                    const QRgb c = src[x];
                    const int a = qAlpha(c);
                    dst[x] = qRgba(premul(qRed(c), a), premul(qGreen(c), a), premul(qBlue(c), a), a);
                }
            } else if (keyed) {
                for (int x = 0; x < w; ++x)
                    dst[x] = (src[x] & 0x00ffffffu) == key ? 0 : src[x] | 0xff000000u;
            } else {
                for (int x = 0; x < w; ++x)
                    dst[x] = src[x] | 0xff000000u;
            }
        }
    }
    QMutexLocker lock(&m_cache->lock);
    const_cast<Resolved &>(p).frames[glyph].insert(file, out);
    return out;
}

void Renderer::drawImageBackground(QImage &canvas, const Resolved &p, int dpi) const
{
    const QImage img = frame(p, false, dpi, canvas.size());
    if (img.isNull())
        return;
    const QRect dest = canvas.rect();
    const Sizing sizing = p.sizing;

    if (sizing == Sizing::TrueSize) {
        const QSize drawn = trueSizeDraw(p, img.size(), dest.size());
        blit(canvas, QRect(aligned(p, drawn, dest), drawn), img, img.rect(), Sizing::Stretch, Sizing::Stretch);
        return;
    }

    QMargins m = p.sizingMargins;
    // Margins can't exceed the source frame.
    m.setLeft(qBound(0, m.left(), img.width()));
    m.setRight(qBound(0, m.right(), img.width() - m.left()));
    m.setTop(qBound(0, m.top(), img.height()));
    m.setBottom(qBound(0, m.bottom(), img.height() - m.top()));
    const Axis hx = split(dest.width(), m.left(), m.right());
    const Axis vy = split(dest.height(), m.top(), m.bottom());

    const int sx[] = {0, m.left(), img.width() - m.right(), img.width()};
    const int sy[] = {0, m.top(), img.height() - m.bottom(), img.height()};
    const int dx[] = {0, hx.near, dest.width() - hx.far, dest.width()};
    const int dy[] = {0, vy.near, dest.height() - vy.far, dest.height()};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (p.borderOnly && row == 1 && col == 1)
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

QSize Renderer::trueSizeDraw(const Resolved &p, QSize source, QSize destination) const
{
    if (source.isEmpty())
        return source;
    const bool uniform = p.uniform;
    const QString &scaling = p.scaling;
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
    const int mark = p.stretchMark;
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
QPoint Renderer::aligned(const Resolved &p, QSize size, const QRect &room) const
{
    const QString &h = p.halign;
    const QString &v = p.valign;
    const int spareX = room.width() - size.width(), spareY = room.height() - size.height();
    const int x = h == QLatin1String("left") ? 0 : h == QLatin1String("right") ? spareX : spareX / 2;
    const int y = v == QLatin1String("top") ? 0 : v == QLatin1String("bottom") ? spareY : spareY / 2;
    return room.topLeft() + QPoint(qMax(0, x), qMax(0, y));
}

void Renderer::drawBorderFill(QImage &canvas, const Resolved &p) const
{
    const QRgb border = p.borderColour | 0xff000000u, fill = p.fillColour | 0xff000000u;
    const int size = p.borderSize;
    const int w = canvas.width(), h = canvas.height();
    // No room inside the border, no border: a 1x1 Edit (BorderSize 1) is
    // drawn in its fill colour.
    const bool framed = w > 2 * size && h > 2 * size;
    for (int y = 0; y < h; ++y) {
        auto *row = reinterpret_cast<QRgb *>(canvas.scanLine(y));
        if (!framed) {
            if (!p.borderOnly)
                std::fill_n(row, w, fill);
        } else if (y < size || y >= h - size) {
            std::fill_n(row, w, border);
        } else {
            std::fill_n(row, size, border);
            if (!p.borderOnly)
                std::fill_n(row + size, w - 2 * size, fill);
            std::fill_n(row + w - size, size, border);
        }
    }
}

void Renderer::drawGlyph(QImage &canvas, const Resolved &p, int dpi) const
{
    if (!p.imageGlyph)
        return;
    // Chosen by the content rect's size, not the whole part's: a squeezed
    // scroll arrow falls back to its small glyph.
    const QRect content = contentRect(p, canvas.rect());
    const QImage g = frame(p, true, dpi, content.size());
    if (g.isNull())
        return;
    const QSize drawn = trueSizeDraw(p, g.size(), content.size());
    blit(canvas, QRect(aligned(p, drawn, content), drawn), g, g.rect(), Sizing::Stretch, Sizing::Stretch);
}

QImage Renderer::background(const PartRef &ref, QSize size, int dpi) const
{
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(0);
    if (size.isEmpty())
        return canvas;
    const Resolved &p = resolved(ref);
    if (p.hasMinSize && (size.width() < p.minSize.width() || size.height() < p.minSize.height()))
        return canvas;
    if (p.bg == Resolved::BorderFill)
        drawBorderFill(canvas, p);
    else if (p.bg != Resolved::None)
        drawImageBackground(canvas, p, dpi);
    drawGlyph(canvas, p, dpi);
    return canvas;
}

QSize Renderer::partSize(const PartRef &ref, PartSize kind, QSize draw, int dpi) const
{
    const Resolved &p = resolved(ref);
    if (kind == PartSize::Min) {
        // The content margins (or what stands in for them) on both sides,
        // never below 1: push button 3+3 -> 6,6; group box (SizingMargins
        // 4,4,4,4) -> 8,8; check box (all 0) -> 1,1; Edit (border 1) -> 2,2.
        const QMargins m = contentMargins(p);
        return QSize(qMax(1, m.left() + m.right()), qMax(1, m.top() + m.bottom()));
    }
    if (p.bg == Resolved::BorderFill) {
        // Edit (BorderSize 1): true 3,3 -- the border on both sides plus a
        // pixel of fill -- and no draw size at all.
        const int border = p.borderSize;
        return kind == PartSize::Draw ? QSize(0, 0) : QSize(2 * border + 1, 2 * border + 1);
    }
    const QSize dest = draw.isValid() ? draw : QSize(1 << 16, 1 << 16);
    const QImage f = frame(p, false, dpi, dest);
    if (f.isNull())
        return QSize();
    if (kind == PartSize::Draw) {
        if (p.sizing != Sizing::TrueSize)
            return draw;
        return trueSizeDraw(p, f.size(), dest);
    }
    return f.size();
}

QMargins Renderer::contentMargins(const PartRef &ref) const
{
    return contentMargins(resolved(ref));
}

QMargins Renderer::contentMargins(const Resolved &p) const
{
    // ContentMargins; failing those, SizingMargins for an image background or
    // BorderSize for a BorderFill one (Royale's GroupBox, which has only
    // SizingMargins 4,4,4,4, reports a content rect inset by 4).
    if (p.hasContentMargins)
        return p.contentMarginsValue;
    if (p.bg == Resolved::BorderFill)
        return QMargins(p.borderSize, p.borderSize, p.borderSize, p.borderSize);
    return p.sizingMargins;
}

QRect Renderer::contentRect(const PartRef &ref, const QRect &bounds) const
{
    return contentRect(resolved(ref), bounds);
}

QRect Renderer::contentRect(const Resolved &p, const QRect &bounds) const
{
    // Below MinSize nothing is drawn, and the content rect is the whole rect
    // (Royale's progress bar at 9 wide, MinSize 10).
    if (p.hasMinSize && (bounds.width() < p.minSize.width() || bounds.height() < p.minSize.height()))
        return bounds;
    QMargins m = contentMargins(p);
    // Margins are subtracted as they are -- an over-large margin gives an
    // inverted rect, which XP reports as such (a 4x2 toolbar separator:
    // left 5, right 2). Only a SourceShrink part scales them down with its
    // sizing margins, rounding as MulDiv does: a scroll arrow (SizingMargins
    // 5+5, ContentMargins 3) reports 3 at 9 high, 2 at 8 and 1 at 4.
    const auto mulDiv = [](int a, int b, int c) { return int((qint64(a) * b + c / 2) / c); };
    if (p.sourceShrink) {
        const QMargins s = p.sizingMargins;
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
    const Resolved &p = resolved(ref);
    {
        QMutexLocker lock(&m_cache->lock);
        if (p.hasPart >= 0)
            return p.hasPart;
    }
    const bool has = p.bg == Resolved::BorderFill
                     || (p.bg != Resolved::None && !chooseImage(p, 96, QSize(1 << 16, 1 << 16), false).isEmpty());
    QMutexLocker lock(&m_cache->lock);
    const_cast<Resolved &>(p).hasPart = has;
    return has;
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
