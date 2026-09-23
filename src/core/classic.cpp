#include "classic.h"

#include "theme.h"
#include "themefile.h"

#include <limits>   // before QtEndian: Qt 5.15.2's qfloat16.h uses it unincluded (GCC 11+)

#include <QPainter>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace xpl {

namespace {

qint32 i32(const char *p)
{
    return qFromLittleEndian<qint32>(p);
}

void put32(char *p, qint32 v)
{
    qToLittleEndian<qint32>(v, p);
}

// (NONCLIENTMETRICS's integers, then its fonts, at their offsets in the A and
// W layouts: a LOGFONTA is 60 bytes, a LOGFONTW 92.)
struct Layout
{
    int font;       // sizeof(LOGFONT)
    int caption, smWidth, smFont, menuWidth, menuFont, statusFont, messageFont, size;
};
constexpr Layout kA{60, 24, 84, 92, 152, 160, 220, 280, 340};
constexpr Layout kW{92, 24, 116, 124, 216, 224, 316, 408, 500};

template<typename FontReader>
NonClientMetrics readMetrics(const char *p, const Layout &l, FontReader font)
{
    NonClientMetrics m;
    m.borderWidth = i32(p + 4);
    m.scrollWidth = i32(p + 8);
    m.scrollHeight = i32(p + 12);
    m.captionWidth = i32(p + 16);
    m.captionHeight = i32(p + 20);
    m.captionFont = font(p + l.caption);
    m.smCaptionWidth = i32(p + l.smWidth);
    m.smCaptionHeight = i32(p + l.smWidth + 4);
    m.smCaptionFont = font(p + l.smFont);
    m.menuWidth = i32(p + l.menuWidth);
    m.menuHeight = i32(p + l.menuWidth + 4);
    m.menuFont = font(p + l.menuFont);
    m.statusFont = font(p + l.statusFont);
    m.messageFont = font(p + l.messageFont);
    return m;
}

// Metrics beyond this are corrupt, not a style (and would ask Qt for
// absurd sizes).
bool plausible(const NonClientMetrics &m)
{
    for (int v : {m.borderWidth, m.scrollWidth, m.scrollHeight, m.captionWidth, m.captionHeight, m.smCaptionWidth,
                  m.smCaptionHeight, m.menuWidth, m.menuHeight})
        if (v < 0 || v > 512)
            return false;
    return true;
}

// The names [SysMetrics] uses where they differ from [Control Panel\Colors].
struct Alias
{
    const char *sysMetrics;
    int index;
};
constexpr Alias kAliases[] = {
    {"activecaption", 2}, {"inactivecaption", 3}, {"captiontext", 9}, {"highlight", 13}, {"highlighttext", 14},
    {"btnface", 15}, {"btnshadow", 16}, {"btntext", 18}, {"inactivecaptiontext", 19}, {"btnhighlight", 20},
    {"dkshadow3d", 21}, {"light3d", 22}, {"infobk", 24}, {"hottracking", 26}, {"gradientactivecaption", 27},
    {"gradientinactivecaption", 28}, {"menuhilight", 29}, {"menubar", 30},
};

} // namespace

// --- LOGFONT ----------------------------------------------------------------------

int LogFont::points() const
{
    return height < 0 ? int(std::lround(-height * 72.0 / 96.0)) : height;
}

LogFont LogFont::fromPoints(const QString &face, int points, bool bold)
{
    LogFont f;
    f.face = face;
    f.height = -int(std::lround(points * 96.0 / 72.0));
    f.weight = bold ? 700 : 400;
    return f;
}

LogFont LogFont::fromA(const char *p)
{
    LogFont f;
    f.height = i32(p);
    f.weight = i32(p + 16);
    f.italic = p[20] != 0;
    f.underline = p[21] != 0;
    f.charset = quint8(p[23]);
    f.face = QString::fromLatin1(p + 28, int(qstrnlen(p + 28, 32)));
    return f;
}

void LogFont::toA(char *p) const
{
    std::memset(p, 0, 60);
    put32(p, height);
    put32(p + 16, weight);
    p[20] = italic;
    p[21] = underline;
    p[23] = char(charset);
    const QByteArray name = face.toLatin1().left(31);
    std::memcpy(p + 28, name.constData(), size_t(name.size()));
}

LogFont LogFont::fromW(const char *p)
{
    LogFont f;
    f.height = i32(p);
    f.weight = i32(p + 16);
    f.italic = p[20] != 0;
    f.underline = p[21] != 0;
    f.charset = quint8(p[23]);
    f.face.clear();
    for (int i = 0; i < 32; ++i) {
        const auto c = qFromLittleEndian<quint16>(p + 28 + 2 * i);
        if (!c)
            break;
        f.face += QChar(c);
    }
    return f;
}

bool LogFont::operator==(const LogFont &o) const
{
    return height == o.height && weight == o.weight && italic == o.italic && underline == o.underline
           && charset == o.charset && face == o.face;
}

// --- NONCLIENTMETRICS / ICONMETRICS -------------------------------------------------

std::optional<NonClientMetrics> NonClientMetrics::fromA(const QByteArray &blob)
{
    if (blob.size() < kSizeA)
        return std::nullopt;
    const NonClientMetrics m = readMetrics(blob.constData(), kA, &LogFont::fromA);
    return plausible(m) ? std::optional(m) : std::nullopt;
}

// (The blobs are built in a local buffer: GCC 12 takes a write through a
// fresh QByteArray's data() for one into Qt 5's empty shared null.)
QByteArray NonClientMetrics::toA() const
{
    char p[kSizeA] = {};
    put32(p, kSizeA);
    put32(p + 4, borderWidth);
    put32(p + 8, scrollWidth);
    put32(p + 12, scrollHeight);
    put32(p + 16, captionWidth);
    put32(p + 20, captionHeight);
    captionFont.toA(p + kA.caption);
    put32(p + kA.smWidth, smCaptionWidth);
    put32(p + kA.smWidth + 4, smCaptionHeight);
    smCaptionFont.toA(p + kA.smFont);
    put32(p + kA.menuWidth, menuWidth);
    put32(p + kA.menuWidth + 4, menuHeight);
    menuFont.toA(p + kA.menuFont);
    statusFont.toA(p + kA.statusFont);
    messageFont.toA(p + kA.messageFont);
    return QByteArray(p, kSizeA);
}

std::optional<NonClientMetrics> NonClientMetrics::fromW(const QByteArray &blob)
{
    if (blob.size() < kSizeW)
        return std::nullopt;
    const NonClientMetrics m = readMetrics(blob.constData(), kW, &LogFont::fromW);
    return plausible(m) ? std::optional(m) : std::nullopt;
}

bool NonClientMetrics::operator==(const NonClientMetrics &o) const
{
    return borderWidth == o.borderWidth && scrollWidth == o.scrollWidth && scrollHeight == o.scrollHeight
           && captionWidth == o.captionWidth && captionHeight == o.captionHeight && captionFont == o.captionFont
           && smCaptionWidth == o.smCaptionWidth && smCaptionHeight == o.smCaptionHeight
           && smCaptionFont == o.smCaptionFont && menuWidth == o.menuWidth && menuHeight == o.menuHeight
           && menuFont == o.menuFont && statusFont == o.statusFont && messageFont == o.messageFont;
}

std::optional<IconMetrics> IconMetrics::fromA(const QByteArray &blob)
{
    if (blob.size() < kSizeA)
        return std::nullopt;
    const char *p = blob.constData();
    IconMetrics m;
    m.horzSpacing = i32(p + 4);
    m.vertSpacing = i32(p + 8);
    m.titleWrap = i32(p + 12) != 0;
    m.font = LogFont::fromA(p + 16);
    if (m.horzSpacing <= 0 || m.horzSpacing > 1024 || m.vertSpacing <= 0 || m.vertSpacing > 1024)
        return std::nullopt;
    return m;
}

QByteArray IconMetrics::toA() const
{
    char p[kSizeA] = {};
    put32(p, kSizeA);
    put32(p + 4, horzSpacing);
    put32(p + 8, vertSpacing);
    put32(p + 12, titleWrap ? 1 : 0);
    font.toA(p + 16);
    return QByteArray(p, kSizeA);
}

bool IconMetrics::operator==(const IconMetrics &o) const
{
    return horzSpacing == o.horzSpacing && vertSpacing == o.vertSpacing && titleWrap == o.titleWrap && font == o.font;
}

QByteArray bytesFromDecimal(const QString &text)
{
    QByteArray bytes;
    for (const QString &b : text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = b.toInt(&ok);
        if (!ok || v < 0 || v > 255)
            return QByteArray();
        bytes.append(char(v));
    }
    return bytes;
}

QString decimalFromBytes(const QByteArray &bytes)
{
    QString text;
    for (const char c : bytes) {
        if (!text.isEmpty())
            text += QLatin1Char(' ');
        text += QString::number(quint8(c));
    }
    return text;
}

// --- the scheme ---------------------------------------------------------------------

const QStringList &ClassicScheme::colourNames()
{
    static const QStringList names = {
        QStringLiteral("Scrollbar"),         QStringLiteral("Background"),        QStringLiteral("ActiveTitle"),
        QStringLiteral("InactiveTitle"),     QStringLiteral("Menu"),              QStringLiteral("Window"),
        QStringLiteral("WindowFrame"),       QStringLiteral("MenuText"),          QStringLiteral("WindowText"),
        QStringLiteral("TitleText"),         QStringLiteral("ActiveBorder"),      QStringLiteral("InactiveBorder"),
        QStringLiteral("AppWorkspace"),      QStringLiteral("Hilight"),           QStringLiteral("HilightText"),
        QStringLiteral("ButtonFace"),        QStringLiteral("ButtonShadow"),      QStringLiteral("GrayText"),
        QStringLiteral("ButtonText"),        QStringLiteral("InactiveTitleText"), QStringLiteral("ButtonHilight"),
        QStringLiteral("ButtonDkShadow"),    QStringLiteral("ButtonLight"),       QStringLiteral("InfoText"),
        QStringLiteral("InfoWindow"),        QStringLiteral("ButtonAlternateFace"), QStringLiteral("HotTrackingColor"),
        QStringLiteral("GradientActiveTitle"), QStringLiteral("GradientInactiveTitle"), QStringLiteral("MenuHilight"),
        QStringLiteral("MenuBar"),
    };
    return names;
}

int ClassicScheme::colourIndex(QStringView name)
{
    const QStringList &names = colourNames();
    for (int i = 0; i < names.size(); ++i)
        if (names.at(i).compare(name, Qt::CaseInsensitive) == 0)
            return i;
    for (const Alias &a : kAliases)
        if (name.compare(QLatin1String(a.sysMetrics), Qt::CaseInsensitive) == 0)
            return a.index;
    return -1;
}

ClassicScheme::ClassicScheme()
{
    // HKCU\Control Panel\Colors as a fresh XP has it (HIVEDEF.INF), which is
    // the Windows Standard scheme; MenuHilight and MenuBar, which classic
    // menus don't draw, as XP derives them.
    static const QRgb standard[kColourCount] = {
        qRgb(212, 208, 200), qRgb(58, 110, 165),  qRgb(10, 36, 106),   qRgb(128, 128, 128), qRgb(212, 208, 200),
        qRgb(255, 255, 255), qRgb(0, 0, 0),       qRgb(0, 0, 0),       qRgb(0, 0, 0),       qRgb(255, 255, 255),
        qRgb(212, 208, 200), qRgb(212, 208, 200), qRgb(128, 128, 128), qRgb(10, 36, 106),   qRgb(255, 255, 255),
        qRgb(212, 208, 200), qRgb(128, 128, 128), qRgb(128, 128, 128), qRgb(0, 0, 0),       qRgb(212, 208, 200),
        qRgb(255, 255, 255), qRgb(64, 64, 64),    qRgb(212, 208, 200), qRgb(0, 0, 0),       qRgb(255, 255, 225),
        qRgb(181, 181, 181), qRgb(0, 0, 128),     qRgb(166, 202, 240), qRgb(192, 192, 192), qRgb(10, 36, 106),
        qRgb(212, 208, 200),
    };
    for (int i = 0; i < kColourCount; ++i)
        colours[size_t(i)] = QColor(standard[i]);
    metrics.captionFont = LogFont::fromPoints(QStringLiteral("Tahoma"), 8, true);
    metrics.smCaptionWidth = metrics.smCaptionHeight = 12;
    metrics.smCaptionFont = LogFont::fromPoints(QStringLiteral("Tahoma"), 8, true);
}

QColor ClassicScheme::colour(QStringView name) const
{
    const int i = colourIndex(name);
    return i >= 0 ? colours[size_t(i)] : QColor(Qt::black);
}

void ClassicScheme::setColour(QStringView name, const QColor &colour)
{
    if (const int i = colourIndex(name); i >= 0)
        colours[size_t(i)] = colour;
}

ClassicScheme ClassicScheme::fromThemeFile(const ThemeFile &file)
{
    ClassicScheme s;
    const QString colours = QStringLiteral("Control Panel\\Colors");
    for (const QString &key : file.keys(colours)) {
        const int i = colourIndex(key);
        const QStringList rgb = file.value(colours, key).simplified().split(QLatin1Char(' '));
        if (i < 0 || rgb.size() != 3)
            continue;
        bool ok[3];
        const int r = rgb[0].toInt(&ok[0]), g = rgb[1].toInt(&ok[1]), b = rgb[2].toInt(&ok[2]);
        if (ok[0] && ok[1] && ok[2])
            s.colours[size_t(i)] = QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
    }
    if (auto m = NonClientMetrics::fromA(bytesFromDecimal(file.value(u"Metrics", u"NonclientMetrics"))))
        s.metrics = *m;
    if (auto m = IconMetrics::fromA(bytesFromDecimal(file.value(u"Metrics", u"IconMetrics"))))
        s.icons = *m;
    return s;
}

void ClassicScheme::writeTo(ThemeFile &file) const
{
    const QString section = QStringLiteral("Control Panel\\Colors");
    const QStringList &names = colourNames();
    for (int i = 0; i < kColourCount; ++i) {
        const QColor &c = colours[size_t(i)];
        file.setValue(section, names.at(i), QStringLiteral("%1 %2 %3").arg(c.red()).arg(c.green()).arg(c.blue()));
    }
    file.setValue(u"Metrics", u"IconMetrics", decimalFromBytes(icons.toA()));
    file.setValue(u"Metrics", u"NonclientMetrics", decimalFromBytes(metrics.toA()));
}

std::optional<ClassicScheme> ClassicScheme::fromSchemeData(const QByteArray &blob)
{
    // SCHEMEDATA: SHORT version (2), WORD padding, NONCLIENTMETRICSW (its
    // cbSize not reliably set), LOGFONTW lfIconTitle, COLORREF rgb[29].
    constexpr int kIconFont = 4 + NonClientMetrics::kSizeW, kColours = kIconFont + 92;
    if (blob.size() < kColours + 29 * 4 || qFromLittleEndian<qint16>(blob.constData()) != 2)
        return std::nullopt;
    const auto metrics = NonClientMetrics::fromW(blob.mid(4));
    if (!metrics)
        return std::nullopt;
    ClassicScheme s;
    s.metrics = *metrics;
    s.icons.font = LogFont::fromW(blob.constData() + kIconFont);
    for (int i = 0; i < 29; ++i) {
        // A COLORREF: 0x00bbggrr, the top byte a palette flag.
        const quint32 ref = qFromLittleEndian<quint32>(blob.constData() + kColours + 4 * i);
        s.colours[size_t(i)] = QColor(int(ref & 0xff), int((ref >> 8) & 0xff), int((ref >> 16) & 0xff));
    }
    // MenuHilight and MenuBar aren't in a version 2 scheme; XP takes them
    // from Hilight and Menu.
    s.colours[29] = s.colours[13];
    s.colours[30] = s.colours[4];
    return s;
}

ClassicScheme ClassicScheme::fromSysMetrics(const Theme &theme)
{
    ClassicScheme s;
    auto value = [&](const char *name) {
        return theme.property(u"sysmetrics", u"", u"", QString::fromLatin1(name)).value_or(QString()).trimmed();
    };
    for (int i = 0; i < kColourCount; ++i) {
        // [SysMetrics] spells most of them its own way ("BtnFace").
        QStringList spellings{colourNames().at(i)};
        for (const Alias &a : kAliases)
            if (a.index == i)
                spellings << QLatin1String(a.sysMetrics);
        for (const QString &name : std::as_const(spellings)) {
            const QStringList rgb = theme.property(u"sysmetrics", u"", u"", name).value_or(QString())
                                        .simplified().replace(QLatin1Char(','), QLatin1Char(' ')).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (rgb.size() == 3)
                s.colours[size_t(i)] = QColor(qBound(0, rgb[0].toInt(), 255), qBound(0, rgb[1].toInt(), 255), qBound(0, rgb[2].toInt(), 255));
        }
    }
    auto size = [&](const char *name, int *out) {
        bool ok = false;
        const int v = value(name).toInt(&ok);
        if (ok && v > 0 && v < 512)
            *out = v;
    };
    NonClientMetrics &m = s.metrics;
    size("SizingBorderWidth", &m.borderWidth);
    size("ScrollbarWidth", &m.scrollWidth);
    size("ScrollbarHeight", &m.scrollHeight);
    size("CaptionBarWidth", &m.captionWidth);
    size("CaptionBarHeight", &m.captionHeight);
    size("SmCaptionBarWidth", &m.smCaptionWidth);
    size("SmCaptionBarHeight", &m.smCaptionHeight);
    size("MenuBarWidth", &m.menuWidth);
    size("MenuBarHeight", &m.menuHeight);
    // "Trebuchet MS, 10, bold"
    auto font = [&](const char *name, LogFont *out) {
        const QStringList parts = value(name).split(QLatin1Char(','));
        if (parts.size() < 2 || parts.at(0).trimmed().isEmpty())
            return;
        bool ok = false;
        const int points = parts.at(1).trimmed().toInt(&ok);
        if (!ok || points <= 0 || points > 200)
            return;
        bool bold = false, italic = false, underline = false;
        for (int i = 2; i < parts.size(); ++i) {
            const QString flag = parts.at(i).trimmed().toLower();
            bold |= flag == QLatin1String("bold");
            italic |= flag == QLatin1String("italic");
            underline |= flag == QLatin1String("underline");
        }
        *out = LogFont::fromPoints(parts.at(0).trimmed(), points, bold);
        out->italic = italic;
        out->underline = underline;
    };
    font("CaptionFont", &m.captionFont);
    font("SmallCaptionFont", &m.smCaptionFont);
    font("MenuFont", &m.menuFont);
    font("StatusFont", &m.statusFont);
    font("MsgBoxFont", &m.messageFont);
    font("IconTitleFont", &s.icons.font);
    return s;
}

QPalette ClassicScheme::palette() const
{
    const QColor face = colour(u"ButtonFace"), text = colour(u"WindowText");
    QPalette pal(text, face, colour(u"ButtonHilight"), colour(u"ButtonShadow"), face, text, text, colour(u"Window"), face);
    pal.setColor(QPalette::ButtonText, colour(u"ButtonText"));
    pal.setColor(QPalette::Shadow, colour(u"ButtonDkShadow"));
    pal.setColor(QPalette::Midlight, colour(u"ButtonLight"));
    pal.setColor(QPalette::Highlight, colour(u"Hilight"));
    pal.setColor(QPalette::HighlightedText, colour(u"HilightText"));
    return pal;
}

bool ClassicScheme::operator==(const ClassicScheme &o) const
{
    return name == o.name && colours == o.colours && metrics == o.metrics && icons == o.icons;
}

// --- drawing --------------------------------------------------------------------------

namespace classic {

void frame(QPainter *p, const QRect &r, const QColor &topLeft, const QColor &bottomRight)
{
    p->fillRect(QRect(r.left(), r.top(), r.width() - 1, 1), topLeft);
    p->fillRect(QRect(r.left(), r.top(), 1, r.height() - 1), topLeft);
    p->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), bottomRight);
    p->fillRect(QRect(r.right(), r.top(), 1, r.height()), bottomRight);
}

void sunkenEdge(QPainter *p, const QRect &r, const QPalette &pal)
{
    frame(p, r, pal.color(QPalette::Dark), pal.color(QPalette::Light));
    frame(p, r.adjusted(1, 1, -1, -1), pal.color(QPalette::Shadow), pal.color(QPalette::Midlight));
}

void raisedEdge(QPainter *p, const QRect &r, const QPalette &pal)
{
    frame(p, r, pal.color(QPalette::Midlight), pal.color(QPalette::Shadow));
    frame(p, r.adjusted(1, 1, -1, -1), pal.color(QPalette::Light), pal.color(QPalette::Dark));
}

// (The highlight and dark shadow outside, Light3D and the shadow inside;
// pushed, the dark shadow and highlight outside, the shadow and Light3D
// inside.)
void pushEdge(QPainter *p, const QRect &r, const QPalette &pal, bool pressed)
{
    p->fillRect(r.adjusted(2, 2, -2, -2), pal.button());
    if (pressed) {
        frame(p, r, pal.color(QPalette::Shadow), pal.color(QPalette::Light));
        frame(p, r.adjusted(1, 1, -1, -1), pal.color(QPalette::Dark), pal.color(QPalette::Midlight));
    } else {
        frame(p, r, pal.color(QPalette::Light), pal.color(QPalette::Shadow));
        frame(p, r.adjusted(1, 1, -1, -1), pal.color(QPalette::Midlight), pal.color(QPalette::Dark));
    }
}

void gradientFill(QPainter *p, const QRect &r, const QColor &from, const QColor &to)
{
    // step = delta * 65536 / width, column i the start + step * (i + 1) >> 16,
    // over a rect 1px wider than `r`.
    const int span = qMax(1, r.width() + 1);
    for (int i = 0; i < r.width(); ++i) {
        auto mix = [&](int a, int b) {
            const qint64 step = qint64(b - a) * 65536 / span;
            return a + int(std::floor(double(step * (i + 1)) / 65536.0));
        };
        p->fillRect(QRect(r.left() + i, r.top(), 1, r.height()),
                    QColor(mix(from.red(), to.red()), mix(from.green(), to.green()), mix(from.blue(), to.blue())));
    }
}

void captionGlyph(QPainter *p, QPoint at, CaptionGlyph glyph, const QColor &colour)
{
    // Rows of '#' at the standard 16x14 button, from its corner.
    static const char *const kMin[] = {"...........", ".........", "...", "...", "...", "...", "...", "...", "...",
                                       "....######", "....######"};
    static const char *const kMax[] = {"", "", "...#########", "...#########", "...#.......#", "...#.......#",
                                       "...#.......#", "...#.......#", "...#.......#", "...#.......#", "...#########"};
    static const char *const kRestore[] = {"", "", ".....######", ".....######", ".....#....#", "...######.#",
                                           "...######.#", "...#....###", "...#....#", "...#....#", "...######"};
    static const char *const kClose[] = {"", "", "", "....##....##", ".....##..##", "......####", ".......##",
                                         "......####", ".....##..##", "....##....##", ""};
    const char *const *rows = glyph == CaptionGlyph::Close      ? kClose
                              : glyph == CaptionGlyph::Minimize ? kMin
                              : glyph == CaptionGlyph::Maximize ? kMax
                                                                : kRestore;
    for (int y = 0; y < 11; ++y)
        for (int x = 0; rows[y][x]; ++x)
            if (rows[y][x] == '#')
                p->fillRect(at.x() + x, at.y() + y, 1, 1, colour);
}

void captionButton(QPainter *p, const QRect &r, CaptionGlyph glyph, const QPalette &pal, bool pressed, bool disabled)
{
    pushEdge(p, r, pal, pressed);
    // (Centred on the standard 16x14 button's placement at other sizes.)
    const QPoint at = r.topLeft() + QPoint((r.width() - 16) / 2, (r.height() - 14) / 2) + (pressed ? QPoint(1, 1) : QPoint());
    if (disabled) {
        captionGlyph(p, at + QPoint(1, 1), glyph, pal.color(QPalette::Light));
        captionGlyph(p, at, glyph, pal.color(QPalette::Dark));
    } else {
        captionGlyph(p, at, glyph, pal.color(QPalette::ButtonText));
    }
}

} // namespace classic

} // namespace xpl
