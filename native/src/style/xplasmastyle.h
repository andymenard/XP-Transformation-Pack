// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/classic.h"
#include "core/render.h"

#include <QCache>
#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QProxyStyle>
#include <QStyleOption>

#include <memory>
#include <optional>

// XPlasma's Qt style.
//
// A QProxyStyle over Qt's built-in "Windows" style: this mirrors XP itself,
// where comctl32 asks uxtheme to draw a control and whatever the visual style
// doesn't cover is drawn classic. The base style's internal calls come back
// through proxy(), so sub-elements it draws on our behalf are ours too.
//
// Everything themed is drawn by xpl::Renderer straight from the .msstyles --
// the same renderer the oracle holds pixel-exact against real XP.
class XPlasmaStyle : public QProxyStyle
{
    Q_OBJECT

public:
    XPlasmaStyle();
    Q_INVOKABLE void setLiveColors(const QVariantMap &) {}

    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;
    void polish(QApplication *app) override;
    void unpolish(QApplication *app) override;
    void polish(QPalette &palette) override;
    QPalette standardPalette() const override;
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    // Windows Classic: no visual style, a colour scheme's colours.
    bool classic() const { return !m_renderer && m_scheme.has_value(); }

private Q_SLOTS:
    void reloadIfChanged();

private:
    void loadTheme(const std::pair<QString, QString> &theme);
    void reloadLiveColors(bool repaint = true);
    void forgetTheme();
    void tintScrollPart(QImage &image, const xpl::PartRef &ref) const;
    QJsonObject m_liveColors;
    bool m_liveEnabled = false;            // m_liveColors' "enabled" and "menu", read once
    QColor m_liveAccent;
    QString m_livePath;
    // Draw `ref` into `rect`; false (and nothing drawn) when the theme has
    // no art for it, so the caller can fall down the degradation ladder.
    bool drawPart(QPainter *painter, const xpl::PartRef &ref, const QRect &rect) const;
    bool hasPart(const xpl::PartRef &ref) const;
    const QImage *partImage(const xpl::PartRef &ref, QSize size, qreal dpr) const;
    QSize trueSize(const xpl::PartRef &ref) const;
    QColor themeColour(const xpl::PartRef &ref, QStringView property, const QColor &fallback) const;
    QColor sysColour(QStringView name, const QColor &fallback) const;
    int sysMetric(QStringView name, int fallback) const;
    QFont sysFont(QStringView name, const QFont &fallback) const;
    // A class-level property ([SysMetrics], [Progress]...), looked up once per theme.
    std::optional<QString> themeProperty(QStringView cls, QStringView property) const;

    QRect xpTitleBarRect(const QStyleOptionTitleBar *bar, SubControl sub, const QWidget *widget) const;
    void drawTitleBar(const QStyleOptionTitleBar *bar, QPainter *painter, const QWidget *widget) const;
    void drawMdiControls(const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const;
    void drawClassicTitleBar(const QStyleOptionTitleBar *bar, QPainter *painter, const QWidget *widget) const;
    void drawScrollBar(const QStyleOptionSlider *option, QPainter *painter, const QWidget *widget) const;
    void drawProgressChunks(const QStyleOptionProgressBar *option, QPainter *painter, const QWidget *widget) const;
    void drawSliderTicks(const QStyleOptionSlider *slider, const QRect &channel, const QRect &handle, QPainter *painter) const;
    int sliderThumbPart(const QStyleOptionSlider *slider) const;
    QRect xpTabRect(const QStyleOptionTab *tab) const;
    void drawXpHeader(const QStyleOptionHeader *header, QPainter *painter, const QWidget *widget) const;
    QSize xpToolButtonSize(const QStyleOptionToolButton *button, const QSize &size) const;
    bool xpDropDown(const QStyleOptionToolButton *button) const;
    void drawXpDropDownButton(const QStyleOptionToolButton *button, QPainter *painter, const QWidget *widget) const;
    bool drawXpToolButtonLabel(const QStyleOptionToolButton *button, QPainter *painter, const QWidget *widget) const;
    QSize xpTabSize(const QStyleOptionTab *tab, const QSize &size, const QWidget *widget) const;
    void drawXpTabLabel(const QStyleOptionTab *tab, QPainter *painter, const QWidget *widget) const;
    int xpTextHeight(const QFont &font) const;
    void unkern(QApplication *app);
    QString frameClass(const QWidget *widget) const;
    bool isTabBody(const QWidget *page) const;
    bool isPlacesBar(const QWidget *view) const;
    int itemRowExtra(const QWidget *widget) const;
    QSize xpItemSize(const QStyleOption *option, const QSize &size, const QWidget *widget) const;
    void drawTreeBranch(const QStyleOption *option, QPainter *painter, const QWidget *widget) const;
    bool drawClassicComplex(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget) const;
    bool drawClassicControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                            const QWidget *widget) const;
    std::pair<QColor, QColor> focusColours(const QWidget *widget) const;
    void drawFocusDots(QPainter *painter, const QRect &rect, const QColor &odd, const QColor &even = Qt::black) const;
    bool drawXpViewItem(const QStyleOptionViewItem *item, QPainter *painter, const QWidget *widget) const;
    QSize sliderThumbSize(const QStyleOptionSlider *slider) const;
    bool flatMenus() const;
    QSize xpMenuItemSize(const QStyleOptionMenuItem *item, const QSize &size, const QWidget *widget) const;
    void drawMenuItem(const QStyleOptionMenuItem *item, QPainter *painter, const QWidget *widget) const;
    void drawMenuBarItem(const QStyleOptionMenuItem *item, QPainter *painter, const QWidget *widget) const;

    // Phase 4 context: the subclass in force for the current call (set by a
    // ContextScope at each entry point), applied to every part looked up.
    QString contextClass(const QWidget *widget, const QStyleOption *option) const;
    xpl::PartRef inContext(const xpl::PartRef &ref) const;
    struct ContextScope
    {
        ContextScope(const XPlasmaStyle *style, const QWidget *widget, const QStyleOption *option);
        ~ContextScope();
        const XPlasmaStyle *m_style;
        QString m_saved;
    };
    mutable QString m_context;
    bool m_anyContext = false;   // some widget has set xplasma-class

    std::unique_ptr<xpl::Renderer> m_renderer;
    bool m_safe = false;                   // XPLASMA_SAFE=1: classic only, draw nothing of our own
    std::optional<xpl::ClassicScheme> m_scheme;   // Classic: the colour scheme (a .theme's colours and metrics)
    // Rendered parts, keyed by part + size + dpi (+ the live accent for a
    // tinted scroll bar).
    struct PartKey
    {
        QString sub, cls;
        int part, state, width, height, dpi;
        QRgb live;
        bool operator==(const PartKey &o) const
        {
            return part == o.part && state == o.state && width == o.width && height == o.height && dpi == o.dpi
                   && live == o.live && cls == o.cls && sub == o.sub;
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        friend size_t qHash(const PartKey &k, size_t seed = 0)
#else
        friend uint qHash(const PartKey &k, uint seed = 0)
#endif
        {
            return ::qHash(k.cls, seed) ^ (::qHash(k.sub, seed) * 31)
                   ^ ::qHash((quint64(k.part * 64 + k.state) << 40) ^ (quint64(k.width) << 24) ^ (quint64(k.height) << 8)
                                 ^ quint64(k.dpi) ^ (quint64(k.live) << 13), seed);
        }
    };
    mutable QCache<PartKey, QImage> m_cache;
    mutable QHash<QString, std::optional<QString>> m_themeProperties;   // themeProperty(), per theme
    mutable QHash<QFont, int> m_textHeights;     // xpTextHeight(), by font
    mutable std::optional<qreal> m_scrollTone[2];   // tintScrollPart()'s reference tone: arrows/thumb, track
    // The last part too big for m_cache (a window-sized frame): kept apart so
    // that resizing one doesn't evict every small part, re-rendered per size.
    mutable PartKey m_largeKey{};
    mutable QImage m_large;
    bool m_cues = false;                   // Alt held: show mnemonic underlines (XP's keyboard cues)
    QString m_configPath;                  // ~/.config/xplasmarc, when live
    bool m_crispEnabled = false;
    bool m_kerning = true;                 // xplasmarc [Text] Kerning
    std::pair<QString, QString> m_loaded;  // (Path, Variant) in use
    class QFileSystemWatcher *m_watcher = nullptr;
    class QTimer *m_reloadTimer = nullptr;
};
