// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// uxtheme's DrawThemeBackground and friends, on QImage.
//
// Everything here is measured against real XP through refharness/ (the
// oracle): a part rendered here, composited over black and over white, has to
// match what uxtheme drew over black and white pixel for pixel. Rendering is
// offscreen into premultiplied ARGB -- the representation uxtheme itself
// blends from -- so the comparison needs no lossy un-premultiplying.

#include "theme.h"

#include <QImage>
#include <QMargins>
#include <QRect>

#include <memory>

namespace xpl {

// One drawable thing: class, part, state (by schema number), and the
// SetWindowTheme-style subclass ("Media" in Media::Toolbar), if any.
struct PartRef
{
    QString cls;
    int part = 0;
    int state = 0;
    QString sub;
};

enum class PartSize { Min, True, Draw };

class Renderer
{
public:
    explicit Renderer(std::shared_ptr<const Theme> theme);
    ~Renderer();

    const Theme &theme() const { return *m_theme; }

    // The part's background at `size`, premultiplied ARGB, transparent where
    // uxtheme leaves the destination untouched. `dpi` picks between
    // ImageFile1..5 the way XP does at 96/120/144 DPI.
    QImage background(const PartRef &ref, QSize size, int dpi = 96) const;

    // The part's bitmap for its state as uxtheme picks and keys it --
    // ImageFile's (GlyphImageFile's with `glyph`) frame, premultiplied, at its
    // own size -- for consumers that stretch art themselves (a Plasma
    // FrameSvg, a QML BorderImage) by the part's SizingMargins. `destination`
    // chooses among ImageFile1..5 as background() would at that size. Null if
    // the part has no image.
    QImage stateImage(const PartRef &ref, bool glyph = false, QSize destination = QSize(), int dpi = 96) const;

    // GetThemePartSize / GetThemeBackgroundContentRect / GetThemeMargins.
    QSize partSize(const PartRef &ref, PartSize kind, QSize draw = QSize(), int dpi = 96) const;
    QRect contentRect(const PartRef &ref, const QRect &bounds) const;
    QMargins margins(const PartRef &ref, QStringView property) const;   // "sizingmargins", ...
    QMargins contentMargins(const PartRef &ref) const;   // ContentMargins, or what stands in for them

    // Is there anything themed to draw for this part at all? (L0/L1 of the
    // degradation ladder; false sends the caller down to L2..L4.)
    bool hasPart(const PartRef &ref) const;

    // A property as uxtheme resolves it for `ref` (state and part names come
    // from the schema), or nullopt.
    std::optional<QString> property(const PartRef &ref, QStringView name) const;

    // Composite a premultiplied render over an opaque colour, the way the
    // oracle's black and white captures are made.
    static QImage over(const QImage &premultiplied, QRgb background);

private:
    // One part's properties, looked up and parsed once (the INI cascade costs
    // a dozen hash lookups per property), and the frames cut from its art.
    struct Resolved;
    struct Cache;
    const Resolved &resolved(const PartRef &ref) const;
    QImage frame(const Resolved &p, bool glyph, int dpi, QSize destination) const;
    QString chooseImage(const Resolved &p, int dpi, QSize destination, bool glyph) const;
    QSize trueSizeDraw(const Resolved &p, QSize source, QSize destination) const;
    QPoint aligned(const Resolved &p, QSize size, const QRect &room) const;
    void drawImageBackground(QImage &canvas, const Resolved &p, int dpi) const;
    void drawBorderFill(QImage &canvas, const Resolved &p) const;
    void drawGlyph(QImage &canvas, const Resolved &p, int dpi) const;
    QMargins contentMargins(const Resolved &p) const;
    QRect contentRect(const Resolved &p, const QRect &bounds) const;

    std::shared_ptr<const Theme> m_theme;
    std::unique_ptr<Cache> m_cache;
};

// Property value parsing, shared with the style.
bool parseColour(const QString &value, QRgb *out);
bool parseMargins(const QString &value, QMargins *out);   // "l, r, t, b" (MARGINS order)
bool parseSize(const QString &value, QSize *out);
bool parseBool(const QString &value);

} // namespace xpl
