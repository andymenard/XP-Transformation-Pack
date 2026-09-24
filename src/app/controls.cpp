#include "app/controls.h"

#include "app/panel.h"
#include "app/paths.h"
#include "app/qttarget.h"
#include "core/schema.h"
#include "core/themefile.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSlider>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>

#include <tuple>

namespace xpl {

namespace {

using Elements = QList<std::pair<QString, QImage>>;

QImage canvas(QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

// A style option as a live control's: enabled, in an active window.
template<typename Option>
Option option(QStyle *style, QSize size, QStyle::State state = {})
{
    Option o;
    o.rect = QRect(QPoint(), size);
    o.palette = style->standardPalette();
    o.state = QStyle::State_Enabled | QStyle::State_Active | state;
    o.direction = Qt::LeftToRight;
    o.fontMetrics = QFontMetrics(QApplication::font());
    return o;
}

// `image` in a transparent ring `pad` wide: for Plasma's hover and focus
// frames, laid over a control grown by their margins, so what's inside
// the ring lands on the control.
QImage padded(const QImage &image, int pad)
{
    QImage out = canvas(image.size() + QSize(2 * pad, 2 * pad));
    QPainter p(&out);
    p.drawImage(pad, pad, image);
    return out;
}

QMargins grow(const QMargins &m, int by)
{
    return m + QMargins(by, by, by, by);
}

// What a mark adds to an empty box: the pixels that differ (a radio
// button's dot), everything else transparent.
QImage difference(const QImage &with, const QImage &without)
{
    QImage out = canvas(with.size());
    for (int y = 0; y < with.height(); ++y)
        for (int x = 0; x < with.width(); ++x)
            if (with.pixel(x, y) != without.pixel(x, y))
                out.setPixel(x, y, with.pixel(x, y));
    return out;
}

// The part of `image` anything was drawn in.
QImage trimmed(const QImage &image)
{
    QRect box;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)))
                box |= QRect(x, y, 1, 1);
    return box.isEmpty() ? image : image.copy(box);
}

QImage solid(QSize size, const QColor &colour)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(colour);
    return image;
}

// Margins no more than half of `size` (less one): a frame keeps a middle.
QMargins within(const QMargins &m, QSize size)
{
    const int w = qMax(0, size.width() / 2 - 1), h = qMax(0, size.height() / 2 - 1);
    return QMargins(qMin(m.left(), w), qMin(m.top(), h), qMin(m.right(), w), qMin(m.bottom(), h));
}

// widgets/button.svg: push buttons (normal, pressed; hot and default laid
// over them), and the toolbar buttons Plasma's flat buttons are.
Elements buttons(QStyle *style, const Look &look)
{
    Elements out;
    const QSize size(48, 23);
    auto bevel = [&](QStyle::State state, bool isDefault) {
        auto o = option<QStyleOptionButton>(style, size, state);
        if (isDefault)
            o.features |= QStyleOptionButton::DefaultButton;
        QImage image = canvas(size);
        QPainter p(&image);
        style->drawControl(QStyle::CE_PushButtonBevel, &o, &p, nullptr);
        return image;
    };
    QMargins m(3, 3, 3, 3);
    if (!look.classic()) {
        const PartRef ref{QStringLiteral("button"), schema::partId(u"button", u"pushbutton"), 1, {}};
        if (look.renderer->hasPart(ref))
            m = within(look.renderer->margins(ref, u"sizingmargins"), size);
    }
    const QMargins content(6, 3, 6, 3);
    appendFrame(out, QStringLiteral("normal-"), bevel(QStyle::State_Raised, false), m, 0, content);
    appendFrame(out, QStringLiteral("pressed-"), bevel(QStyle::State_Sunken, false), m, 0, content);
    if (!look.classic())   // (Classic's don't light up)
        appendFrame(out, QStringLiteral("hover-"), padded(bevel(QStyle::State_Raised | QStyle::State_MouseOver, false), 1), grow(m, 1), 0,
                    QMargins(1, 1, 1, 1));
    appendFrame(out, QStringLiteral("focus-"), padded(bevel(QStyle::State_Raised | QStyle::State_HasFocus, true), 1), grow(m, 1), 0,
                QMargins(1, 1, 1, 1));

    auto tool = [&](QStyle::State state) {
        auto o = option<QStyleOption>(style, QSize(24, 22), state);
        QImage image = canvas(o.rect.size());
        QPainter p(&image);
        style->drawPrimitive(QStyle::PE_PanelButtonTool, &o, &p, nullptr);
        return image;
    };
    const QMargins toolMargins(3, 3, 3, 3);
    appendFrame(out, QStringLiteral("toolbutton-hover-"),
                tool(QStyle::State_Raised | QStyle::State_AutoRaise | QStyle::State_MouseOver), toolMargins, 0, toolMargins);
    appendFrame(out, QStringLiteral("toolbutton-pressed-"), tool(QStyle::State_Sunken | QStyle::State_AutoRaise), toolMargins, 0,
                toolMargins);
    // (No focus ring on a flat button: XP's toolbars had none.)
    appendFrame(out, QStringLiteral("toolbutton-focus-"), canvas(QSize(4, 4)), QMargins(1, 1, 1, 1), 0, QMargins(1, 1, 1, 1));
    return out;
}

// widgets/checkmarks.svg and widgets/radiobutton.svg: XP's check box and
// radio button. (Plasma draws a check box as a button with the mark over it:
// the mark is XP's whole checked box, covering it.)
void indicators(QStyle *style, Elements &checkmarks, Elements &radio)
{
    const QSize size(style->pixelMetric(QStyle::PM_IndicatorWidth), style->pixelMetric(QStyle::PM_IndicatorHeight));
    const QSize radioSize(style->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth), style->pixelMetric(QStyle::PM_ExclusiveIndicatorHeight));
    auto draw = [&](QStyle::PrimitiveElement element, QSize s, QStyle::State state) {
        auto o = option<QStyleOptionButton>(style, s, state);
        QImage image = canvas(s);
        QPainter p(&image);
        style->drawPrimitive(element, &o, &p, nullptr);
        return image;
    };
    const QImage radioOff = draw(QStyle::PE_IndicatorRadioButton, radioSize, QStyle::State_Off);
    const QImage radioOn = draw(QStyle::PE_IndicatorRadioButton, radioSize, QStyle::State_On);
    checkmarks.append({QStringLiteral("checkbox"), draw(QStyle::PE_IndicatorCheckBox, size, QStyle::State_On)});
    checkmarks.append({QStringLiteral("radiobutton"), difference(radioOn, radioOff)});
    radio.append({QStringLiteral("normal"), radioOff});
    radio.append({QStringLiteral("hover"), draw(QStyle::PE_IndicatorRadioButton, radioSize, QStyle::State_Off | QStyle::State_MouseOver)});
    radio.append({QStringLiteral("symbol"), difference(radioOn, radioOff)});
    radio.append({QStringLiteral("hint-size"), canvas(radioSize)});
}

// widgets/switch.svg: XP had no switch; its check box stands in -- the
// bar the box, unchecked, and (on) checked over it, the box's own size; the
// handle nothing.
Elements toggleSwitch(QStyle *style)
{
    Elements out;
    const QSize size(style->pixelMetric(QStyle::PM_IndicatorWidth), style->pixelMetric(QStyle::PM_IndicatorHeight));
    auto draw = [&](QStyle::State state) {
        auto o = option<QStyleOptionButton>(style, size, state);
        QImage image = canvas(size);
        QPainter p(&image);
        style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &o, &p, nullptr);
        return image;
    };
    const QMargins m = within(QMargins(3, 3, 3, 3), size);
    appendFrame(out, QStringLiteral("inactive-"), draw(QStyle::State_Off), m);
    appendFrame(out, QStringLiteral("active-"), draw(QStyle::State_On), m);
    // (Plasma insets the bars a pixel each side: the size given, two over.)
    const QSize bar = size + QSize(2, 0);
    for (const char *handle : {"handle", "handle-hover", "handle-pressed"})
        out.append({QLatin1String(handle), canvas(bar)});
    out.append({QStringLiteral("hint-bar-size"), canvas(bar)});
    return out;
}

// widgets/arrows.svg: XP's arrows (a combo box's, a menu button's).
Elements arrows(QStyle *style)
{
    Elements out;
    for (const auto &[name, element] : {std::pair("down-arrow", QStyle::PE_IndicatorArrowDown), std::pair("up-arrow", QStyle::PE_IndicatorArrowUp),
                                         std::pair("left-arrow", QStyle::PE_IndicatorArrowLeft), std::pair("right-arrow", QStyle::PE_IndicatorArrowRight)}) {
        // (XP's are small, 7 across: drawn in the middle 9x9 of the icon)
        auto o = option<QStyleOption>(style, QSize(9, 9));
        o.rect.moveTo(4, 4);
        QImage image = canvas(QSize(16, 16));
        QPainter p(&image);
        style->drawPrimitive(element, &o, &p, nullptr);
        p.end();
        out.append({QLatin1String(name), image});
    }
    return out;
}

// widgets/lineedit.svg: XP's text box, white inside its edge.
Elements textField(QStyle *style)
{
    Elements out;
    auto o = option<QStyleOptionFrame>(style, QSize(40, 21), QStyle::State_Sunken);
    o.lineWidth = style->pixelMetric(QStyle::PM_DefaultFrameWidth);
    o.midLineWidth = 0;
    QImage image = canvas(o.rect.size());
    QPainter p(&image);
    style->drawPrimitive(QStyle::PE_PanelLineEdit, &o, &p, nullptr);
    p.end();
    appendFrame(out, QStringLiteral("base-"), image, QMargins(2, 2, 2, 2), 0, QMargins(3, 3, 3, 3));
    return out;
}

// widgets/listitem.svg and widgets/viewitem.svg: XP's selection, the
// highlight colour; hovering (which XP didn't show) a tint of it.
void listHighlights(const Look &look, Elements &listItem, Elements &viewItem)
{
    const QColor hilight = look.system.colour(u"Hilight");
    QColor tint = hilight;
    tint.setAlpha(56);
    const QMargins m(1, 1, 1, 1), content(4, 2, 4, 2);
    appendFrame(listItem, QStringLiteral("pressed-"), solid(QSize(8, 8), hilight), m, 0, content);
    appendFrame(listItem, QStringLiteral("hover-"), solid(QSize(8, 8), tint), m, 0, content);
    appendFrame(viewItem, QStringLiteral("selected-"), solid(QSize(8, 8), hilight), m, 0, content);
    appendFrame(viewItem, QStringLiteral("selected+hover-"), solid(QSize(8, 8), hilight), m, 0, content);
    appendFrame(viewItem, QStringLiteral("hover-"), solid(QSize(8, 8), tint), m, 0, content);
}

// widgets/bar_meter_horizontal.svg: XP's progress bar -- its track, and
// its chunks (or Classic's blocks) as a strip Plasma tiles.
Elements progress(QStyle *style, const Look &look)
{
    Elements out;
    const int h = 15;
    auto o = option<QStyleOptionProgressBar>(style, QSize(120, h), QStyle::State_Horizontal);
    o.minimum = 0;
    o.maximum = 100;
    o.progress = 0;
    QImage track = canvas(o.rect.size());
    {
        QPainter p(&track);
        style->drawControl(QStyle::CE_ProgressBarGroove, &o, &p, nullptr);
    }
    appendFrame(out, QStringLiteral("bar-inactive-"), track, QMargins(3, 3, 3, 3));
    o.progress = 100;
    QImage full = canvas(o.rect.size());
    {
        QPainter p(&full);
        style->drawControl(QStyle::CE_ProgressBarContents, &o, &p, nullptr);
    }
    // One period of the chunks: a chunk and the space after it.
    int period = (h - 4) * 2 / 3 + 2;
    if (!look.classic()) {
        const PartRef base{QStringLiteral("progress"), 0, 0, {}};
        period = look.renderer->property(base, u"progresschunksize").value_or(QStringLiteral("8")).toInt()
                 + look.renderer->property(base, u"progressspacesize").value_or(QStringLiteral("2")).toInt();
    }
    const QRect contents = style->subElementRect(QStyle::SE_ProgressBarContents, &o, nullptr);
    const QImage strip = full.copy(contents.left(), 0, qMax(1, period), h);
    appendFrame(out, QStringLiteral("bar-active-"), strip, QMargins(0, contents.top(), 0, h - contents.bottom() - 1), period);
    out.append({QStringLiteral("hint-bar-size"), canvas(QSize(1, h))});
    out.append({QStringLiteral("hint-tile-center"), canvas(QSize(1, 1))});
    return out;
}

// widgets/slider.svg: XP's trackbar -- its groove, its thumb (and lit).
Elements slider(QStyle *style)
{
    Elements out;
    for (const bool horizontal : {true, false}) {
        const QSize size = horizontal ? QSize(120, 26) : QSize(26, 120);
        auto o = option<QStyleOptionSlider>(style, size);
        o.orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
        if (horizontal)
            o.state |= QStyle::State_Horizontal;
        o.minimum = 0;
        o.maximum = 100;
        o.sliderPosition = o.sliderValue = 50;
        o.tickPosition = QSlider::NoTicks;
        auto draw = [&](QStyle::SubControls parts, QStyle::SubControls active, QStyle::State extra) {
            auto d = o;
            d.subControls = parts;
            d.activeSubControls = active;
            d.state |= extra;
            QImage image = canvas(size);
            QPainter p(&image);
            style->drawComplexControl(QStyle::CC_Slider, &d, &p, nullptr);
            return image;
        };
        const QRect handle = style->subControlRect(QStyle::CC_Slider, &o, QStyle::SC_SliderHandle, nullptr);
        const QString name = horizontal ? QStringLiteral("horizontal-slider-") : QStringLiteral("vertical-slider-");
        out.append({name + QLatin1String("handle"), draw(QStyle::SC_SliderHandle, {}, {}).copy(handle)});
        out.append({name + QLatin1String("hover"), draw(QStyle::SC_SliderHandle, QStyle::SC_SliderHandle, QStyle::State_MouseOver).copy(handle)});
        if (horizontal) {
            out.append({QStringLiteral("hint-handle-size"), canvas(handle.size())});
            // The groove, clear of the thumb (the style draws it however
            // asked): with the thumb at the start, the far half of the track
            // -- its end cap, and a column to stretch -- and the cap again,
            // mirrored, for the start.
            auto clear = o;
            clear.sliderPosition = clear.sliderValue = 0;
            clear.subControls = QStyle::SC_SliderGroove;
            QImage drawn = canvas(size);
            {
                QPainter p(&drawn);
                style->drawComplexControl(QStyle::CC_Slider, &clear, &p, nullptr);
            }
            const QRect groove = style->subControlRect(QStyle::CC_Slider, &clear, QStyle::SC_SliderGroove, nullptr);
            const QImage far = trimmed(drawn.copy(groove.center().x(), 0, groove.right() - groove.center().x() + 1, size.height()));
            const int cap = qMin(3, far.width() - 1);
            QImage track = canvas(QSize(2 * cap + 1, far.height()));
            {
                QPainter p(&track);
                p.drawImage(0, 0, far.copy(far.width() - cap, 0, cap, far.height()).mirrored(true, false));
                p.drawImage(cap, 0, far.copy(far.width() - cap - 1, 0, 1, far.height()));
                p.drawImage(cap + 1, 0, far.copy(far.width() - cap, 0, cap, far.height()));
            }
            appendFrame(out, QStringLiteral("groove-"), track, within(QMargins(cap, 2, cap, 2), track.size()));
        }
    }
    return out;
}

// widgets/scrollbar.svg: XP's scroll bar -- its track and thumb (and lit),
// the thumb the style's own (its grip stretched with it), the width XP's.
Elements scrollBar(QStyle *style, const Look &look)
{
    Elements out;
    const int width = qMax(8, look.system.metrics.scrollWidth);
    out.append({QStringLiteral("hint-scrollbar-size"), canvas(QSize(width, width))});
    for (const bool horizontal : {false, true}) {
        const QSize size = horizontal ? QSize(160, width) : QSize(width, 160);
        auto o = option<QStyleOptionSlider>(style, size);
        o.orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
        if (horizontal)
            o.state |= QStyle::State_Horizontal;
        o.minimum = 0;
        o.maximum = 100;
        o.pageStep = 40;
        o.sliderPosition = o.sliderValue = 50;
        auto draw = [&](QStyle::SubControls parts, QStyle::SubControls active, QStyle::State extra) {
            auto d = o;
            d.subControls = parts;
            d.activeSubControls = active;
            d.state |= extra;
            QImage image = canvas(size);
            QPainter p(&image);
            style->drawComplexControl(QStyle::CC_ScrollBar, &d, &p, nullptr);
            return image;
        };
        const QString background = horizontal ? QStringLiteral("background-horizontal-") : QStringLiteral("background-vertical-");
        // A visual style's parts drawn alone: the style's whole scroll bar
        // would leave its track in the thumb's rounded corners, and its grip
        // in the thumb's middle, stretched along with it (Plasma's frames
        // can't centre anything) -- so Plasma's thumb goes without one.
        if (!look.classic()) {
            // SBP_LOWERTRACKHORZ 4 / SBP_LOWERTRACKVERT 6; the thumb,
            // SBP_THUMBBTNVERT 3 (Plasma has one thumb for both ways).
            const PartRef track{QStringLiteral("scrollbar"), horizontal ? 4 : 6, 1, {}};
            const PartRef thumb{QStringLiteral("scrollbar"), 3, 1, {}}, thumbHot{QStringLiteral("scrollbar"), 3, 2, {}};
            if (look.renderer->hasPart(track) && look.renderer->hasPart(thumb)) {
                appendFrame(out, background, look.renderer->background(track, size),
                            within(look.renderer->margins(track, u"sizingmargins"), size));
                if (!horizontal) {
                    const QSize thumbSize(width, 48);
                    const QMargins m = within(look.renderer->margins(thumb, u"sizingmargins"), thumbSize);
                    appendFrame(out, QStringLiteral("slider-"), look.renderer->background(thumb, thumbSize), m);
                    appendFrame(out, QStringLiteral("mouseover-slider-"), look.renderer->background(thumbHot, thumbSize), m);
                }
                continue;
            }
        }
        const QRect page = style->subControlRect(QStyle::CC_ScrollBar, &o, QStyle::SC_ScrollBarSubPage, nullptr);
        const QImage track = draw(QStyle::SC_ScrollBarSubPage, {}, {}).copy(page);
        appendFrame(out, background, track, within(QMargins(1, 1, 1, 1), track.size()));
        if (!horizontal) {
            const QRect thumb = style->subControlRect(QStyle::CC_ScrollBar, &o, QStyle::SC_ScrollBarSlider, nullptr);
            const QMargins m = within(QMargins(3, 3, 3, 3), thumb.size());
            appendFrame(out, QStringLiteral("slider-"), draw(QStyle::SC_ScrollBarSlider, {}, {}).copy(thumb), m);
            appendFrame(out, QStringLiteral("mouseover-slider-"),
                        draw(QStyle::SC_ScrollBarSlider, QStyle::SC_ScrollBarSlider, QStyle::State_MouseOver).copy(thumb), m);
        }
    }
    return out;
}

// widgets/tabbar.svg: XP's selected tab, under the current one of Plasma's
// tab bars (the rest are their labels, as Plasma draws them).
Elements tabs(QStyle *style)
{
    Elements out;
    for (const bool north : {true, false}) {
        // (in room to spare: XP draws a selected tab a little larger than
        // its box, over its neighbours and the pane's edge)
        auto o = option<QStyleOptionTab>(style, QSize(64, 22), QStyle::State_Selected);
        o.rect.translate(6, 6);
        o.shape = north ? QTabBar::RoundedNorth : QTabBar::RoundedSouth;
        o.position = QStyleOptionTab::Middle;
        QImage image = canvas(o.rect.size() + QSize(12, 12));
        {
            QPainter p(&image);
            style->drawControl(QStyle::CE_TabBarTabShape, &o, &p, nullptr);
        }
        const QImage tab = trimmed(image);
        appendFrame(out, north ? QStringLiteral("north-active-tab-") : QStringLiteral("south-active-tab-"), tab,
                    within(QMargins(3, 3, 3, 3), tab.size()), 0, QMargins(6, 3, 6, 3));
    }
    return out;
}

} // namespace

QStyle *xpStyle(const Look &look)
{
    if (!qobject_cast<QApplication *>(QCoreApplication::instance()))
        return nullptr;
    static bool pathsAdded = false;
    if (!pathsAdded) {
        // The plugin this XPLasma ships, and wherever it's installed.
        const QString shipped = paths::shippedStylePlugin(6);
        if (!shipped.isEmpty())
            QCoreApplication::addLibraryPath(QFileInfo(shipped).dir().absolutePath() + QLatin1String("/.."));
        QCoreApplication::addLibraryPath(paths::stylePluginRoot(6));
        pathsAdded = true;
    }
    // A look is known by what it draws from: the style and its variant, or
    // (Classic) the scheme, as a .theme the plugin reads.
    QString spec;
    if (look.classic()) {
        ThemeFile file;
        look.system.writeTo(file);
        const QByteArray bytes = file.toBytes();
        const QString dir = paths::cache() + QLatin1String("/styles");
        QDir().mkpath(dir);
        spec = dir + QLatin1Char('/') + QString::number(qHash(bytes), 16) + QLatin1String(".theme");
        if (!QFileInfo::exists(spec)) {
            QFile f(spec);
            if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
                return nullptr;
        }
    } else {
        spec = look.stylePath + QLatin1Char(':') + look.variant;
    }
    static QHash<QString, QStyle *> styles;
    if (QStyle *s = styles.value(spec))
        return s;
    // (The plugin reads XPLASMA_THEME when it's made: one instance per look.)
    const QByteArray old = qgetenv("XPLASMA_THEME");
    qputenv("XPLASMA_THEME", QFile::encodeName(spec));
    QStyle *style = QStyleFactory::create(QLatin1String(qtstyle::kStyleKey));
    if (old.isNull())
        qunsetenv("XPLASMA_THEME");
    else
        qputenv("XPLASMA_THEME", old);
    if (style)
        styles.insert(spec, style);
    return style;
}

QHash<QString, QMap<int, QImage>> spinIcons(const Look &look)
{
    QHash<QString, QMap<int, QImage>> out;
    QStyle *style = xpStyle(look);
    for (const auto &[name, part, arrow] : {std::tuple{"value-decrease", 4, QStyle::PE_IndicatorArrowLeft},
                                           std::tuple{"value-increase", 3, QStyle::PE_IndicatorArrowRight}}) {
        // The arrow alone, unscaled in the middle: the icon sits on Plasma's
        // flat button, and XP's button behind it would be a button in one.
        // A visual style's is the up-down's glyph (SpinLeftGlyph.bmp and the
        // like); Classic's, the style's arrow.
        QImage glyph;
        if (!look.classic()) {
            const PartRef ref{QStringLiteral("spin"), part, 1, {}};
            if (look.renderer->hasPart(ref))
                glyph = trimmed(look.renderer->stateImage(ref, true));
        }
        // (Every size a label's height might ask for -- 13 under XP's 8pt
        // Tahoma -- so none is a scaled one.)
        for (int size = 12; size <= 32; size += size < 24 ? 1 : 8) {
            QImage image = canvas(QSize(size, size));
            if (!glyph.isNull()) {
                QPainter p(&image);
                p.drawImage((size - glyph.width()) / 2, (size - glyph.height()) / 2, glyph);
            } else if (style) {
                QPainter p(&image);
                auto o = option<QStyleOption>(style, image.size());
                const int inset = size / 4;
                o.rect = o.rect.adjusted(inset, inset, -inset, -inset);
                style->drawPrimitive(arrow, &o, &p, nullptr);
            } else {
                continue;
            }
            out[QLatin1String(name)].insert(size, image);
        }
    }
    return out;
}

QHash<QString, QList<std::pair<QString, QImage>>> plasmaControls(const Look &look)
{
    QHash<QString, Elements> out;
    QStyle *style = xpStyle(look);
    if (!style)
        return out;
    out.insert(QStringLiteral("button"), buttons(style, look));
    Elements checkmarks, radio;
    indicators(style, checkmarks, radio);
    out.insert(QStringLiteral("checkmarks"), checkmarks);
    out.insert(QStringLiteral("radiobutton"), radio);
    out.insert(QStringLiteral("lineedit"), textField(style));
    out.insert(QStringLiteral("switch"), toggleSwitch(style));
    out.insert(QStringLiteral("arrows"), arrows(style));
    Elements listItem, viewItem;
    listHighlights(look, listItem, viewItem);
    out.insert(QStringLiteral("listitem"), listItem);
    out.insert(QStringLiteral("viewitem"), viewItem);
    out.insert(QStringLiteral("bar_meter_horizontal"), progress(style, look));
    out.insert(QStringLiteral("slider"), slider(style));
    out.insert(QStringLiteral("scrollbar"), scrollBar(style, look));
    out.insert(QStringLiteral("tabbar"), tabs(style));
    return out;
}

} // namespace xpl
