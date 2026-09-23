#pragma once

#include "core/render.h"

#include <QCache>
#include <QProxyStyle>
#include <QStyleOption>

#include <memory>

// XPLasma's Qt style.
//
// A QProxyStyle over Qt's built-in "Windows" style: this mirrors XP itself,
// where comctl32 asks uxtheme to draw a control and whatever the visual style
// doesn't cover is drawn classic. The base style's internal calls come back
// through proxy(), so sub-elements it draws on our behalf are ours too.
//
// Everything themed is drawn by xpl::Renderer straight from the .msstyles --
// the same renderer the oracle holds pixel-exact against real XP.
class XPLasmaStyle : public QProxyStyle
{
    Q_OBJECT

public:
    XPLasmaStyle();

    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;
    void polish(QPalette &palette) override;
    QPalette standardPalette() const override;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget = nullptr) const override;

    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget = nullptr) const override;
    QRect subElementRect(SubElement element, const QStyleOption *option,
                         const QWidget *widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub,
                         const QWidget *widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;

    // The theme in use, or nullptr (then everything is classic).
    const xpl::Renderer *renderer() const { return m_renderer.get(); }

private:
    void loadTheme();
    // Draw `ref` into `rect`; false (and nothing drawn) when the theme has
    // no art for it, so the caller can fall down the degradation ladder.
    bool drawPart(QPainter *painter, const xpl::PartRef &ref, const QRect &rect) const;
    bool hasPart(const xpl::PartRef &ref) const;
    QSize trueSize(const xpl::PartRef &ref) const;
    QColor themeColour(const xpl::PartRef &ref, QStringView property, const QColor &fallback) const;
    QColor sysColour(QStringView name, const QColor &fallback) const;
    int sysMetric(QStringView name, int fallback) const;

    void drawScrollBar(const QStyleOptionSlider *option, QPainter *painter, const QWidget *widget) const;
    void drawProgressChunks(const QStyleOptionProgressBar *option, QPainter *painter, const QWidget *widget) const;
    void drawSliderTicks(const QStyleOptionSlider *slider, const QRect &channel, const QRect &handle, QPainter *painter) const;
    int sliderThumbPart(const QStyleOptionSlider *slider) const;
    QRect xpTabRect(const QStyleOptionTab *tab) const;
    QSize sliderThumbSize(const QStyleOptionSlider *slider) const;

    std::unique_ptr<xpl::Renderer> m_renderer;
    bool m_safe = false;                   // XPLASMA_SAFE=1: classic only, draw nothing of our own
    mutable QCache<QString, QImage> m_cache; // rendered parts, keyed by part + size + dpi
};
