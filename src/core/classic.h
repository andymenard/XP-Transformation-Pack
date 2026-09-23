#pragma once
// Windows Classic: XP with visual styles off. No bitmaps -- GDI draws bevels
// from 29 system colours, and the frame, caption, menu and scroll bar sizes
// and fonts come from NONCLIENTMETRICS. A colour scheme is exactly that
// data, and here it is in XP's own encodings:
//
//   * a .theme's [Control Panel\Colors] ("R G B" per colour) and [Metrics]
//     NonclientMetrics / IconMetrics (NONCLIENTMETRICSA / ICONMETRICSA as
//     decimal bytes);
//   * the SCHEMEDATA blobs XP ships its stock schemes as
//     (HKCU\Control Panel\Appearance\Schemes, in I386\HIVEDEF.INF).
#include <QByteArray>
#include <QColor>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace xpl {

class ThemeFile;

// A LOGFONT: what XP stores for every UI font. Height is in pixels at 96 dpi,
// negative for a character height (as Windows always writes it).
struct LogFont
{
    int height = -11;       // 8 pt
    int weight = 400;
    bool italic = false;
    bool underline = false;
    quint8 charset = 0;     // DEFAULT_CHARSET is 1; XP's own schemes say 0 (ANSI)
    QString face = QStringLiteral("Tahoma");

    int points() const;     // the size XP's Appearance dialog shows
    bool bold() const { return weight >= 600; }
    static LogFont fromPoints(const QString &face, int points, bool bold = false);

    static LogFont fromA(const char *data);        // LOGFONTA: 60 bytes
    void toA(char *data) const;
    static LogFont fromW(const char *data);        // LOGFONTW: 92 bytes

    bool operator==(const LogFont &o) const;
    bool operator!=(const LogFont &o) const { return !(*this == o); }
};

struct NonClientMetrics
{
    int borderWidth = 1;          // SM_CXSIZEFRAME is this + 3
    int scrollWidth = 16;
    int scrollHeight = 16;
    int captionWidth = 18;        // caption buttons are 2 px narrower
    int captionHeight = 18;
    LogFont captionFont = LogFont::fromPoints(QStringLiteral("Tahoma"), 8, true);
    int smCaptionWidth = 12;
    int smCaptionHeight = 15;
    LogFont smCaptionFont;
    int menuWidth = 18;
    int menuHeight = 18;
    LogFont menuFont;
    LogFont statusFont;
    LogFont messageFont;

    static constexpr int kSizeA = 340;
    static constexpr int kSizeW = 500;
    static std::optional<NonClientMetrics> fromA(const QByteArray &blob);
    QByteArray toA() const;
    static std::optional<NonClientMetrics> fromW(const QByteArray &blob);

    bool operator==(const NonClientMetrics &o) const;
};

struct IconMetrics
{
    int horzSpacing = 75;
    int vertSpacing = 75;
    bool titleWrap = true;
    LogFont font;

    static constexpr int kSizeA = 76;
    static std::optional<IconMetrics> fromA(const QByteArray &blob);
    QByteArray toA() const;

    bool operator==(const IconMetrics &o) const;
};

// A .theme's "12 34 56 ..." byte list, and back.
QByteArray bytesFromDecimal(const QString &text);
QString decimalFromBytes(const QByteArray &bytes);

class ClassicScheme
{
public:
    // COLOR_SCROLLBAR (0) .. COLOR_MENUBAR (30), by the names a .theme's
    // [Control Panel\Colors] uses ("Scrollbar", "Background", "ActiveTitle"...).
    static constexpr int kColourCount = 31;
    static const QStringList &colourNames();
    // The index of a colour by its .theme name or by the name a visual style's
    // [SysMetrics] uses ("btnface", "activecaption"...), case-insensitively;
    // -1 if neither.
    static int colourIndex(QStringView name);

    QString name = QStringLiteral("Windows Standard");
    std::array<QColor, kColourCount> colours;
    NonClientMetrics metrics;
    IconMetrics icons;

    ClassicScheme();   // Windows Standard, XP's default

    QColor colour(QStringView name) const;   // by either spelling; black if unknown
    void setColour(QStringView name, const QColor &colour);

    // From a .theme: its colours and metrics over Windows Standard's (a
    // .theme that leaves one out leaves XP's current value, and Windows
    // Standard is the value a fresh XP has).
    static ClassicScheme fromThemeFile(const ThemeFile &file);
    // Into a .theme: [Control Panel\Colors] and [Metrics], as XP writes them.
    void writeTo(ThemeFile &file) const;

    // A SCHEMEDATA (version 2) blob, as HIVEDEF.INF has XP's stock schemes.
    static std::optional<ClassicScheme> fromSchemeData(const QByteArray &blob);

    bool operator==(const ClassicScheme &o) const;
};

} // namespace xpl
