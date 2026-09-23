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

    const Theme &theme() const { return *m_theme; }

    // The part's background at `size`, premultiplied ARGB, transparent where
    // uxtheme leaves the destination untouched. `dpi` picks between
    // ImageFile1..5 the way XP does at 96/120/144 DPI.
    QImage background(const PartRef &ref, QSize size, int dpi = 96) const;

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
    struct Frame
    {
        QImage image;       // premultiplied ARGB, one state's frame
        bool valid = false;
    };
    Frame frame(const PartRef &ref, const QString &imageProperty, int dpi, QSize destination) const;
    QString chooseImage(const PartRef &ref, int dpi, QSize destination, bool glyph) const;
    QSize trueSizeDraw(const PartRef &ref, QSize source, QSize destination) const;
    QPoint aligned(const PartRef &ref, QSize size, const QRect &room) const;
    void drawImageBackground(QImage &canvas, const PartRef &ref, int dpi) const;
    void drawBorderFill(QImage &canvas, const PartRef &ref) const;
    void drawGlyph(QImage &canvas, const PartRef &ref, int dpi) const;

    std::shared_ptr<const Theme> m_theme;
};

// Property value parsing, shared with the style.
bool parseColour(const QString &value, QRgb *out);
bool parseMargins(const QString &value, QMargins *out);   // "l, r, t, b" (MARGINS order)
bool parseSize(const QString &value, QSize *out);
bool parseBool(const QString &value);

} // namespace xpl
