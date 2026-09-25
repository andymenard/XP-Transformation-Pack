#include "app/gtk.h"

#include "app/controls.h"
#include "app/flatpak.h"
#include "app/paths.h"
#include "core/render.h"
#include "core/schema.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QSaveFile>
#include <QSlider>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>

namespace xpl::gtk {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QImage canvas(QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

// A style option as a live control's: enabled (unless `state` says it
// isn't, by leaving State_Enabled out when `disabled`), in an active window.
template<typename Option>
Option option(QStyle *style, QSize size, QStyle::State state = {}, bool disabled = false)
{
    Option o;
    o.rect = QRect(QPoint(), size);
    o.palette = style->standardPalette();
    o.state = QStyle::State_Active | state;
    if (!disabled)
        o.state |= QStyle::State_Enabled;
    o.direction = Qt::LeftToRight;
    o.fontMetrics = QFontMetrics(QApplication::font());
    return o;
}

QString hex(const QColor &c)
{
    return c.name(QColor::HexRgb);
}

// The images the CSS names, drawn for the look, into the theme's assets/.
class Assets
{
public:
    Assets(const QString &dir)
        : m_dir(dir)
    {
        QDir().mkpath(dir);
    }
    bool ok() const { return m_ok; }
    // Saves `image` as NAME.png; the CSS's url() for it (from gtk-N.0/).
    QString put(const QString &name, const QImage &image)
    {
        if (!image.isNull() && !image.convertToFormat(QImage::Format_ARGB32).save(m_dir + QLatin1Char('/') + name + QLatin1String(".png")))
            m_ok = false;
        return QStringLiteral("url(\"../assets/%1.png\")").arg(name);
    }

    // An image the CSS draws as a border-image cut at `slice`: it, and its
    // middle alone, NAME-fill.png -- GTK 3 draws a border-image's edges but
    // not its middle, so that goes in as the background, inside the border.
    // Both keys into `values`: KEY and KEY_fill.
    void frame(QHash<QString, QString> &values, const QString &key, const QString &name, const QImage &image, const QMargins &slice)
    {
        values.insert(key, put(name, image));
        const QRect middle = image.rect().marginsRemoved(slice);
        values.insert(key + QLatin1String("_fill"), put(name + QLatin1String("-fill"), middle.isValid() ? image.copy(middle) : image));
    }

private:
    QString m_dir;
    bool m_ok = true;
};

enum class Mode { Normal, Hot, Pressed, Disabled };
constexpr Mode kModes[] = {Mode::Normal, Mode::Hot, Mode::Pressed, Mode::Disabled};
const char *modeName(Mode m)
{
    switch (m) {
    case Mode::Normal:
        return "normal";
    case Mode::Hot:
        return "hot";
    case Mode::Pressed:
        return "pressed";
    case Mode::Disabled:
        return "disabled";
    }
    return "";
}
QStyle::State modeState(Mode m)
{
    switch (m) {
    case Mode::Hot:
        return QStyle::State_MouseOver;
    case Mode::Pressed:
        return QStyle::State_Sunken | QStyle::State_MouseOver;
    default:
        return {};
    }
}

// --- the controls ------------------------------------------------------------------

QImage pushButton(QStyle *style, QStyle::State state, bool disabled, bool isDefault)
{
    const QSize size(48, 23);
    auto o = option<QStyleOptionButton>(style, size, state, disabled);
    if (isDefault)
        o.features |= QStyleOptionButton::DefaultButton;
    QImage image = canvas(size);
    QPainter p(&image);
    style->drawControl(QStyle::CE_PushButtonBevel, &o, &p, nullptr);
    return image;
}

QImage toolButton(QStyle *style, QStyle::State state)
{
    auto o = option<QStyleOption>(style, QSize(24, 22), QStyle::State_AutoRaise | state);
    QImage image = canvas(o.rect.size());
    QPainter p(&image);
    style->drawPrimitive(QStyle::PE_PanelButtonTool, &o, &p, nullptr);
    return image;
}

QImage indicator(QStyle *style, QStyle::PrimitiveElement element, QStyle::State state, bool disabled)
{
    const bool radio = element == QStyle::PE_IndicatorRadioButton;
    const QSize size(style->pixelMetric(radio ? QStyle::PM_ExclusiveIndicatorWidth : QStyle::PM_IndicatorWidth),
                     style->pixelMetric(radio ? QStyle::PM_ExclusiveIndicatorHeight : QStyle::PM_IndicatorHeight));
    auto o = option<QStyleOptionButton>(style, size, state, disabled);
    QImage image = canvas(size);
    QPainter p(&image);
    style->drawPrimitive(element, &o, &p, nullptr);
    return image;
}

QImage lineEdit(QStyle *style, QStyle::State state, bool disabled)
{
    auto o = option<QStyleOptionFrame>(style, QSize(60, 21), QStyle::State_Sunken | state, disabled);
    o.lineWidth = style->pixelMetric(QStyle::PM_DefaultFrameWidth);
    o.midLineWidth = 0;
    QImage image = canvas(o.rect.size());
    QPainter p(&image);
    style->drawPrimitive(QStyle::PE_PanelLineEdit, &o, &p, nullptr);
    return image;
}

QImage comboBox(QStyle *style, QStyle::State state, bool disabled, bool arrowActive)
{
    auto o = option<QStyleOptionComboBox>(style, QSize(120, 21), state, disabled);
    o.editable = false;
    o.frame = true;
    o.subControls = QStyle::SC_All;
    if (arrowActive)
        o.activeSubControls = QStyle::SC_ComboBoxArrow;
    QImage image = canvas(o.rect.size());
    QPainter p(&image);
    style->drawComplexControl(QStyle::CC_ComboBox, &o, &p, nullptr);
    return image;
}

QImage tab(QStyle *style, QStyle::State state)
{
    // (in room to spare: XP draws a selected tab a little larger than its box)
    auto o = option<QStyleOptionTab>(style, QSize(64, 22), state);
    o.rect.translate(6, 6);
    o.shape = QTabBar::RoundedNorth;
    o.position = QStyleOptionTab::Middle;
    QImage image = canvas(o.rect.size() + QSize(12, 12));
    {
        QPainter p(&image);
        style->drawControl(QStyle::CE_TabBarTabShape, &o, &p, nullptr);
    }
    QRect box;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)))
                box |= QRect(x, y, 1, 1);
    return box.isEmpty() ? image : image.copy(box);
}

QImage tabPane(QStyle *style)
{
    auto o = option<QStyleOptionTabWidgetFrame>(style, QSize(100, 60));
    o.shape = QTabBar::RoundedNorth;
    o.lineWidth = style->pixelMetric(QStyle::PM_DefaultFrameWidth);
    QImage image = canvas(o.rect.size());
    QPainter p(&image);
    style->drawPrimitive(QStyle::PE_FrameTabWidget, &o, &p, nullptr);
    return image;
}

QImage header(QStyle *style, QStyle::State state)
{
    auto o = option<QStyleOptionHeader>(style, QSize(80, 20), QStyle::State_Raised | QStyle::State_Horizontal | state);
    o.orientation = Qt::Horizontal;
    o.position = QStyleOptionHeader::Middle;
    o.section = 1;
    QImage image = canvas(o.rect.size());
    QPainter p(&image);
    style->drawControl(QStyle::CE_HeaderSection, &o, &p, nullptr);
    return image;
}

// The scroll bar's parts for `horizontal`: each drawn alone from the look's
// parts (a visual style: the thumb without its grip, which the CSS centres),
// or, Classic, cut out of the whole bar the style draws.
struct ScrollParts
{
    QImage track, thumb[3], grip[3], back[4], forward[4];   // thumb/grip: normal, hot, pressed; arrows: + disabled
};

ScrollParts scrollParts(QStyle *style, const Look &look, bool horizontal, int width)
{
    ScrollParts out;
    const QSize arrow(width, width);
    if (!look.classic()) {
        const Renderer &r = *look.renderer;
        auto part = [&](int partId, int state, QSize size) { return r.background({QStringLiteral("scrollbar"), partId, state, {}}, size); };
        // SBP_LOWERTRACKHORZ 4 / VERT 6; THUMBBTNHORZ 2 / VERT 3; GRIPPERHORZ 8 / VERT 9;
        // ARROWBTN 1, states up 1-4, down 5-8, left 9-12, right 13-16.
        out.track = part(horizontal ? 4 : 6, 1, horizontal ? QSize(48, width) : QSize(width, 48));
        const QSize thumb = horizontal ? QSize(48, width) : QSize(width, 48);
        for (int s = 0; s < 3; ++s) {
            out.thumb[s] = part(horizontal ? 2 : 3, s + 1, thumb);
            const PartRef grip{QStringLiteral("scrollbar"), horizontal ? 8 : 9, s + 1, {}};
            if (r.hasPart(grip))
                out.grip[s] = r.stateImage(grip);
        }
        const int backBase = horizontal ? 8 : 0, forwardBase = horizontal ? 12 : 4;
        for (int s = 0; s < 4; ++s) {
            out.back[s] = part(1, backBase + s + 1, arrow);
            out.forward[s] = part(1, forwardBase + s + 1, arrow);
        }
        return out;
    }
    const QSize size = horizontal ? QSize(120, width) : QSize(width, 120);
    auto o = option<QStyleOptionSlider>(style, size);
    o.orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
    if (horizontal)
        o.state |= QStyle::State_Horizontal;
    o.minimum = 0;
    o.maximum = 100;
    o.pageStep = 40;
    o.sliderPosition = o.sliderValue = 50;
    auto draw = [&](QStyle::SubControls active, QStyle::State extra, bool disabled) {
        auto d = o;
        d.subControls = QStyle::SC_All;
        d.activeSubControls = active;
        d.state |= extra;
        if (disabled)
            d.state &= ~QStyle::State_Enabled;
        QImage image = canvas(size);
        QPainter p(&image);
        style->drawComplexControl(QStyle::CC_ScrollBar, &d, &p, nullptr);
        return image;
    };
    auto rect = [&](QStyle::SubControl sc) { return style->subControlRect(QStyle::CC_ScrollBar, &o, sc, nullptr); };
    const QImage plain = draw({}, {}, false);
    out.track = plain.copy(rect(QStyle::SC_ScrollBarSubPage));
    out.thumb[0] = out.thumb[1] = plain.copy(rect(QStyle::SC_ScrollBarSlider));
    out.thumb[2] = out.thumb[0];
    const QImage pressedBack = draw(QStyle::SC_ScrollBarSubLine, QStyle::State_Sunken, false);
    const QImage pressedForward = draw(QStyle::SC_ScrollBarAddLine, QStyle::State_Sunken, false);
    const QImage disabledBar = draw({}, {}, true);
    const QRect back = rect(QStyle::SC_ScrollBarSubLine), forward = rect(QStyle::SC_ScrollBarAddLine);
    out.back[0] = out.back[1] = plain.copy(back);
    out.back[2] = pressedBack.copy(back);
    out.back[3] = disabledBar.copy(back);
    out.forward[0] = out.forward[1] = plain.copy(forward);
    out.forward[2] = pressedForward.copy(forward);
    out.forward[3] = disabledBar.copy(forward);
    return out;
}

// The progress bar: its groove, and one period of its chunks (a chunk and the
// space after it) to repeat.
std::pair<QImage, QImage> progressParts(QStyle *style, const Look &look)
{
    const int h = 15;
    auto o = option<QStyleOptionProgressBar>(style, QSize(120, h), QStyle::State_Horizontal);
    o.minimum = 0;
    o.maximum = 100;
    o.progress = 0;
    QImage groove = canvas(o.rect.size());
    {
        QPainter p(&groove);
        style->drawControl(QStyle::CE_ProgressBarGroove, &o, &p, nullptr);
    }
    o.progress = 100;
    QImage full = canvas(o.rect.size());
    {
        QPainter p(&full);
        style->drawControl(QStyle::CE_ProgressBarContents, &o, &p, nullptr);
    }
    int period = (h - 4) * 2 / 3 + 2;
    if (!look.classic()) {
        const PartRef base{QStringLiteral("progress"), 0, 0, {}};
        period = look.renderer->property(base, u"progresschunksize").value_or(QStringLiteral("8")).toInt()
                 + look.renderer->property(base, u"progressspacesize").value_or(QStringLiteral("2")).toInt();
    }
    const QRect contents = style->subElementRect(QStyle::SE_ProgressBarContents, &o, nullptr);
    return {groove, full.copy(contents.left(), contents.top(), qMax(1, period), contents.height())};
}

// The trackbar: its groove (clear of the thumb) and the thumb, plain and hot.
struct SliderParts
{
    QImage groove, thumb, thumbHot;
};

SliderParts sliderParts(QStyle *style)
{
    SliderParts out;
    const QSize size(120, 26);
    auto o = option<QStyleOptionSlider>(style, size, QStyle::State_Horizontal);
    o.orientation = Qt::Horizontal;
    o.minimum = 0;
    o.maximum = 100;
    o.sliderPosition = o.sliderValue = 50;
    o.tickPosition = QSlider::NoTicks;
    auto draw = [&](QStyle::SubControls parts, QStyle::SubControls active, QStyle::State extra, int position) {
        auto d = o;
        d.subControls = parts;
        d.activeSubControls = active;
        d.state |= extra;
        d.sliderPosition = d.sliderValue = position;
        QImage image = canvas(size);
        QPainter p(&image);
        style->drawComplexControl(QStyle::CC_Slider, &d, &p, nullptr);
        return image;
    };
    const QRect handle = style->subControlRect(QStyle::CC_Slider, &o, QStyle::SC_SliderHandle, nullptr);
    out.thumb = draw(QStyle::SC_SliderHandle, {}, {}, 50).copy(handle);
    out.thumbHot = draw(QStyle::SC_SliderHandle, QStyle::SC_SliderHandle, QStyle::State_MouseOver, 50).copy(handle);
    // The groove with the thumb out of the way at the start: its far half.
    const QRect groove = style->subControlRect(QStyle::CC_Slider, &o, QStyle::SC_SliderGroove, nullptr);
    const QImage clear = draw(QStyle::SC_SliderGroove, {}, {}, 100);
    QRect ink;
    for (int y = groove.top(); y <= groove.bottom(); ++y)
        for (int x = groove.left(); x <= groove.right(); ++x)
            if (qAlpha(clear.pixel(x, y)))
                ink |= QRect(x, y, 1, 1);
    out.groove = ink.isEmpty() ? clear.copy(groove) : clear.copy(ink);
    return out;
}

// XP's own sizes: the scroll bar's width, the menu's colours...
struct Metrics
{
    int scrollWidth = 17;
};

QString fill(QString css, const QHash<QString, QString> &values)
{
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        css.replace(QLatin1String("${") + it.key() + QLatin1Char('}'), it.value());
    return css;
}

// --- the stylesheets ---------------------------------------------------------------

// Shared by GTK 3 and GTK 4: the look's colours and the controls both have.
const char *const kCommonCss = R"css(
/* XPlasma: ${NAME}. Generated -- a new apply writes it again. */

@define-color theme_bg_color ${ButtonFace};
@define-color theme_fg_color ${ButtonText};
@define-color theme_base_color ${Window};
@define-color theme_text_color ${WindowText};
@define-color theme_selected_bg_color ${Hilight};
@define-color theme_selected_fg_color ${HilightText};
@define-color insensitive_fg_color ${GrayText};
@define-color theme_unfocused_bg_color ${ButtonFace};
@define-color theme_unfocused_fg_color ${ButtonText};
@define-color theme_unfocused_selected_bg_color ${Hilight};
@define-color theme_unfocused_selected_fg_color ${HilightText};
@define-color borders ${ButtonShadow};
@define-color unfocused_borders ${ButtonShadow};
/* libadwaita's names */
@define-color accent_color ${Hilight};
@define-color accent_bg_color ${Hilight};
@define-color accent_fg_color ${HilightText};
@define-color window_bg_color ${ButtonFace};
@define-color window_fg_color ${ButtonText};
@define-color view_bg_color ${Window};
@define-color view_fg_color ${WindowText};
@define-color headerbar_bg_color ${ButtonFace};
@define-color headerbar_fg_color ${ButtonText};
@define-color card_bg_color ${Window};
@define-color card_fg_color ${WindowText};
@define-color popover_bg_color ${Menu};
@define-color popover_fg_color ${MenuText};
@define-color dialog_bg_color ${ButtonFace};
@define-color dialog_fg_color ${ButtonText};
@define-color sidebar_bg_color ${ButtonFace};
@define-color sidebar_fg_color ${ButtonText};

window, .background, dialog { background-color: ${ButtonFace}; color: ${ButtonText}; }
.view, textview text, treeview.view, iconview, list, listview, columnview, gridview { background-color: ${Window}; color: ${WindowText}; }
.view:selected, .view:selected:focus, treeview.view:selected, row:selected, list row:selected, listview > row:selected,
textview text selection, entry selection, text selection, label selection, *:selected {
    background-color: ${Hilight}; color: ${HilightText};
}
*:disabled, label:disabled { color: ${GrayText}; }

/* Push buttons: XP's, 3px of their edge kept, the middle stretched. */
button {
    border-image: ${button_normal} 3 3 3 3 stretch;
    background-image: ${button_normal_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box;
    border-width: 3px; border-style: solid; border-radius: 0;
    background-color: transparent; box-shadow: none; text-shadow: none; -gtk-icon-shadow: none; outline: none;
    color: ${ButtonText}; padding: 1px 6px; min-height: 17px;
}
button:hover { border-image-source: ${button_hot}; background-image: ${button_hot_fill}; }
button:active, button:checked { border-image-source: ${button_pressed}; background-image: ${button_pressed_fill}; }
button:disabled { border-image-source: ${button_disabled}; background-image: ${button_disabled_fill}; color: ${GrayText}; }
button:focus, button.default, button.suggested-action { border-image-source: ${button_default}; background-image: ${button_default_fill}; }
button:focus:hover, button.default:hover, button.suggested-action:hover { border-image-source: ${button_hot}; background-image: ${button_hot_fill}; }
button:focus:active, button.default:active, button.suggested-action:active { border-image-source: ${button_pressed}; background-image: ${button_pressed_fill}; }
button.destructive-action { color: ${ButtonText}; }

/* Flat buttons (toolbars, headers): nothing until the pointer's over them. */
button.flat, toolbar button, .toolbar button, headerbar button, .titlebar button, button.image-button.flat {
    border-image: none; border-color: transparent; background: none;
}
button.flat:hover, toolbar button:hover, .toolbar button:hover, headerbar button:hover {
    border-image: ${tool_hot} 3 3 3 3 stretch;
    background-image: ${tool_hot_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box; border-color: transparent;
}
button.flat:active, button.flat:checked, toolbar button:active, toolbar button:checked, .toolbar button:active, .toolbar button:checked,
headerbar button:active, headerbar button:checked {
    border-image: ${tool_pressed} 3 3 3 3 stretch;
    background-image: ${tool_pressed_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box; border-color: transparent;
}
/* (The window's own buttons are the window decoration's.) */
headerbar button.titlebutton, .titlebar button.titlebutton, windowcontrols button { border-image: none; }

/* Check boxes and radio buttons: XP's, at their size. */
check, radio {
    min-width: ${check_size}px; min-height: ${check_size}px;
    border: none; border-image: none; background: none; box-shadow: none; -gtk-icon-shadow: none; outline: none;
    margin: 0 4px 0 0; padding: 0;
    -gtk-icon-source: ${check_off_normal};
}
check:hover { -gtk-icon-source: ${check_off_hot}; }
check:active { -gtk-icon-source: ${check_off_pressed}; }
check:disabled { -gtk-icon-source: ${check_off_disabled}; }
check:checked { -gtk-icon-source: ${check_on_normal}; }
check:checked:hover { -gtk-icon-source: ${check_on_hot}; }
check:checked:active { -gtk-icon-source: ${check_on_pressed}; }
check:checked:disabled { -gtk-icon-source: ${check_on_disabled}; }
check:indeterminate { -gtk-icon-source: ${check_mixed_normal}; }
check:indeterminate:hover { -gtk-icon-source: ${check_mixed_hot}; }
check:indeterminate:active { -gtk-icon-source: ${check_mixed_pressed}; }
check:indeterminate:disabled { -gtk-icon-source: ${check_mixed_disabled}; }
radio { min-width: ${radio_size}px; min-height: ${radio_size}px; -gtk-icon-source: ${radio_off_normal}; }
radio:hover { -gtk-icon-source: ${radio_off_hot}; }
radio:active { -gtk-icon-source: ${radio_off_pressed}; }
radio:disabled { -gtk-icon-source: ${radio_off_disabled}; }
radio:checked { -gtk-icon-source: ${radio_on_normal}; }
radio:checked:hover { -gtk-icon-source: ${radio_on_hot}; }
radio:checked:active { -gtk-icon-source: ${radio_on_pressed}; }
radio:checked:disabled { -gtk-icon-source: ${radio_on_disabled}; }

/* Switches: XP had none; its check box stands in, as in Plasma's popups --
   the switch the box, empty or ticked, its slider nothing (GTK sizes a
   switch from twice its slider: half the box each). */
switch {
    min-width: ${check_size}px; min-height: ${check_size}px; padding: 0; margin: 0;
    border: none; border-image: none; border-radius: 0; box-shadow: none; outline: none;
    color: transparent;
    background-image: ${check_off_normal}; background-size: ${check_size}px ${check_size}px;
    background-position: center; background-repeat: no-repeat; background-color: transparent;
}
switch:hover { background-image: ${check_off_hot}; }
switch:active { background-image: ${check_off_pressed}; }
switch:disabled { background-image: ${check_off_disabled}; color: transparent; }
switch:checked { background-image: ${check_on_normal}; }
switch:checked:hover { background-image: ${check_on_hot}; }
switch:checked:active { background-image: ${check_on_pressed}; }
switch:checked:disabled { background-image: ${check_on_disabled}; color: transparent; }
switch slider, switch image, switch slider:disabled, switch image:disabled {
    min-width: ${switch_half}px; min-height: ${check_size}px; margin: 0; padding: 0;
    border: none; border-image: none; border-radius: 0; background: none; box-shadow: none; outline: none;
    opacity: 0; -gtk-icon-source: none; color: transparent;
}

/* Text fields: XP's edit border round the window colour. */
entry, spinbutton:not(.vertical) {
    border-image: ${entry_normal} ${entry_edge} stretch;
    background-image: ${entry_normal_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box;
    border-width: ${entry_edge}px; border-style: solid; border-radius: 0;
    background-color: transparent; box-shadow: none; outline: none;
    color: ${WindowText}; caret-color: ${WindowText};
}
entry { padding: 1px 2px; min-height: 17px; }
entry:disabled { border-image-source: ${entry_disabled}; background-image: ${entry_disabled_fill}; color: ${GrayText}; }
spinbutton entry, spinbutton text, dropdown entry { border-image: none; background-image: none; border-width: 0; }
/* A combo box with a text field: the field, then its drop-down button. */
combobox entry.combo {
    border-image: ${entry_normal} ${entry_edge} 0 ${entry_edge} ${entry_edge} stretch;
    border-width: ${entry_edge}px 0 ${entry_edge}px ${entry_edge}px;
}
combobox entry.combo + button.combo {
    border-image: ${combo_button_normal} 3 3 3 0 stretch; background-image: ${combo_button_normal_fill};
    border-width: 3px 3px 3px 0; padding: 0; min-width: ${combo_button_w}px;
}
combobox entry.combo + button.combo:hover { border-image-source: ${combo_button_hot}; background-image: ${combo_button_hot_fill}; }
combobox entry.combo + button.combo:disabled { border-image-source: ${combo_button_disabled}; background-image: ${combo_button_disabled_fill}; }

/* Progress bars: XP's groove, and its chunks repeated. */
progressbar trough {
    border-image: ${progress_groove} 3 3 3 3 stretch;
    background-image: ${progress_groove_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box; border-width: 3px; border-style: solid; border-radius: 0;
    background-color: transparent; box-shadow: none; min-height: ${progress_inner}px; padding: 0;
}
progressbar progress {
    background-image: ${progress_chunk}; background-repeat: repeat-x; background-position: left center;
    background-size: auto 100%; background-color: transparent;
    border: none; border-radius: 0; box-shadow: none; min-height: ${progress_inner}px;
}
progressbar.vertical trough { border-image-source: ${progress_groove_v}; background-image: ${progress_groove_v_fill}; min-width: ${progress_inner}px; min-height: 0; }
progressbar.vertical progress {
    background-image: ${progress_chunk_v}; background-repeat: repeat-y; background-position: center bottom; background-size: 100% auto;
    min-width: ${progress_inner}px; min-height: 0;
}

/* Tabs: XP's, the current one raised over the page. */
notebook > header { background: none; border: none; box-shadow: none; padding: 0; }
notebook > header tab {
    border-image: ${tab_normal} 3 3 0 3 stretch;
    background-image: ${tab_normal_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box; border-width: 3px 3px 0 3px; border-style: solid; border-radius: 0;
    background-color: transparent; box-shadow: none; outline: none; padding: 1px 6px; min-height: 16px; color: ${ButtonText};
}
notebook > header tab:hover { border-image-source: ${tab_hot}; background-image: ${tab_hot_fill}; }
notebook > header tab:checked { border-image-source: ${tab_selected}; background-image: ${tab_selected_fill}; }
notebook > stack:not(:only-child) {
    border-image: ${tab_pane} 2 2 2 2 stretch;
    background-image: ${tab_pane_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box; border-width: 2px; border-style: solid; background-color: transparent;
}

/* Trackbars: XP's groove and thumb. */
scale trough {
    border-image: ${slider_groove} 0 ${slider_cap} 0 ${slider_cap} stretch;
    background-image: ${slider_groove_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box; border-width: 0 ${slider_cap}px;
    border-style: solid; border-radius: 0; background-color: transparent; box-shadow: none; outline: none;
    min-height: ${slider_groove_h}px;
}
scale slider {
    background-image: ${slider_thumb}; background-size: 100% 100%; background-color: transparent;
    border: none; border-radius: 0; box-shadow: none; outline: none;
    min-width: ${slider_thumb_w}px; min-height: ${slider_thumb_h}px; margin: -${slider_margin}px 0;
}
scale slider:hover { background-image: ${slider_thumb_hot}; }
scale.vertical trough {
    border-image: ${slider_groove_v} ${slider_cap} 0 ${slider_cap} 0 stretch; background-image: ${slider_groove_v_fill};
    border-width: ${slider_cap}px 0; min-width: ${slider_groove_h}px; min-height: 0;
}
scale.vertical slider { background-image: ${slider_thumb_v}; min-width: ${slider_thumb_h}px; min-height: ${slider_thumb_w}px; margin: 0 -${slider_margin}px; }
scale.vertical slider:hover { background-image: ${slider_thumb_v_hot}; }
scale highlight, scale fill { background: none; border: none; box-shadow: none; }

/* Combo boxes: XP's field with its drop-down button. */
combobox button.combo, dropdown > button {
    border-image: ${combo_normal} 3 ${combo_arrow} 3 3 stretch;
    background-image: ${combo_normal_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box;
    border-width: 3px ${combo_arrow}px 3px 3px; border-style: solid; border-radius: 0;
    background-color: transparent; box-shadow: none; outline: none; padding: 0 2px; color: ${WindowText};
}
combobox button.combo:hover, dropdown > button:hover { border-image-source: ${combo_hot}; background-image: ${combo_hot_fill}; }
combobox button.combo:disabled, dropdown > button:disabled { border-image-source: ${combo_disabled}; background-image: ${combo_disabled_fill}; color: ${GrayText}; }
combobox button.combo arrow, dropdown > button arrow { -gtk-icon-source: none; min-width: 0; min-height: 0; }

/* Column headers. */
treeview.view header button, columnview > header > columntitle, columnview listview header {
    border-image: ${header_normal} 2 2 2 2 stretch;
    background-image: ${header_normal_fill}; background-size: 100% 100%; background-repeat: no-repeat; background-origin: padding-box; background-clip: padding-box; border-width: 2px; border-style: solid; border-radius: 0;
    background-color: transparent; box-shadow: none; color: ${ButtonText}; padding: 1px 4px;
}
treeview.view header button:hover { border-image-source: ${header_hot}; background-image: ${header_hot_fill}; }
treeview.view header button:active { border-image-source: ${header_pressed}; background-image: ${header_pressed_fill}; }

/* Menus: XP's flat menus. */
menubar { background-color: ${MenuBar}; color: ${MenuText}; box-shadow: none; padding: 0; }
menubar > menuitem { padding: 2px 6px; }
menubar > menuitem:hover { background-color: ${MenuHilight}; color: ${HilightText}; box-shadow: none; }
menu, .menu, .context-menu, popover.menu > contents, popover.menu {
    background-color: ${Menu}; color: ${MenuText}; border: 1px solid ${ButtonShadow}; border-radius: 0; box-shadow: none; padding: 2px;
}
menuitem, popover.menu modelbutton { color: ${MenuText}; padding: 3px 17px; border-radius: 0; min-height: 13px; }
menuitem:hover, popover.menu modelbutton:hover, popover.menu modelbutton:selected {
    background-color: ${MenuHilight}; color: ${HilightText};
}
menu separator, popover.menu separator { background-color: ${ButtonShadow}; min-height: 1px; margin: 3px 1px; }

/* Tooltips: XP's yellow InfoWindow in a black line. */
tooltip, tooltip.background {
    background-color: ${InfoWindow}; color: ${InfoText}; border: 1px solid #000000; border-radius: 0; box-shadow: none; padding: 1px 3px;
}
tooltip * { color: ${InfoText}; background-color: transparent; }
)css";

// GTK 3 only: its scroll bars have XP's arrow buttons (steppers).
const char *const kGtk3Css = R"css(
scrollbar {
    -GtkScrollbar-has-backward-stepper: true; -GtkScrollbar-has-forward-stepper: true;
    -GtkRange-slider-width: ${scroll_width};
    background: none; border: none; box-shadow: none; padding: 0;
}
scrollbar.vertical trough { background-image: ${scroll_track_v}; background-size: 100% 100%; border: none; min-width: ${scroll_width}px; }
scrollbar.horizontal trough { background-image: ${scroll_track_h}; background-size: 100% 100%; border: none; min-height: ${scroll_width}px; }
scrollbar slider {
    border-image: ${scroll_thumb_v_normal} 3 3 3 3 stretch; border-width: 3px; border-style: solid; border-radius: 0;
    background-image: ${scroll_grip_v_normal}, ${scroll_thumb_v_normal_fill}; background-position: ${grip_across}px center, center; background-repeat: no-repeat, no-repeat;
    background-size: auto, 100% 100%; background-origin: padding-box, padding-box; background-clip: padding-box, padding-box; background-color: transparent;
    box-shadow: none; margin: 0; min-width: ${scroll_inner}px; min-height: 8px;
}
scrollbar slider:hover { border-image-source: ${scroll_thumb_v_hot}; background-image: ${scroll_grip_v_hot}, ${scroll_thumb_v_hot_fill}; }
scrollbar slider:active { border-image-source: ${scroll_thumb_v_pressed}; background-image: ${scroll_grip_v_pressed}, ${scroll_thumb_v_pressed_fill}; }
scrollbar.horizontal slider {
    background-position: center ${grip_across}px, center;
    border-image-source: ${scroll_thumb_h_normal}; background-image: ${scroll_grip_h_normal}, ${scroll_thumb_h_normal_fill}; min-width: 8px; min-height: ${scroll_inner}px;
}
scrollbar.horizontal slider:hover { border-image-source: ${scroll_thumb_h_hot}; background-image: ${scroll_grip_h_hot}, ${scroll_thumb_h_hot_fill}; }
scrollbar.horizontal slider:active { border-image-source: ${scroll_thumb_h_pressed}; background-image: ${scroll_grip_h_pressed}, ${scroll_thumb_h_pressed_fill}; }
scrollbar button {
    border: none; border-image: none; box-shadow: none; padding: 0; margin: 0; border-radius: 0;
    min-width: ${scroll_width}px; min-height: ${scroll_width}px; -gtk-icon-source: none;
    background-size: 100% 100%; background-color: transparent;
}
scrollbar.vertical button.up { background-image: ${scroll_up_normal}; }
scrollbar.vertical button.up:hover { background-image: ${scroll_up_hot}; }
scrollbar.vertical button.up:active { background-image: ${scroll_up_pressed}; }
scrollbar.vertical button.up:disabled { background-image: ${scroll_up_disabled}; }
scrollbar.vertical button.down { background-image: ${scroll_down_normal}; }
scrollbar.vertical button.down:hover { background-image: ${scroll_down_hot}; }
scrollbar.vertical button.down:active { background-image: ${scroll_down_pressed}; }
scrollbar.vertical button.down:disabled { background-image: ${scroll_down_disabled}; }
scrollbar.horizontal button.up { background-image: ${scroll_left_normal}; }
scrollbar.horizontal button.up:hover { background-image: ${scroll_left_hot}; }
scrollbar.horizontal button.up:active { background-image: ${scroll_left_pressed}; }
scrollbar.horizontal button.up:disabled { background-image: ${scroll_left_disabled}; }
scrollbar.horizontal button.down { background-image: ${scroll_right_normal}; }
scrollbar.horizontal button.down:hover { background-image: ${scroll_right_hot}; }
scrollbar.horizontal button.down:active { background-image: ${scroll_right_pressed}; }
scrollbar.horizontal button.down:disabled { background-image: ${scroll_right_disabled}; }
)css";

// GTK 4: no steppers; the thumb and track only.
const char *const kGtk4Css = R"css(
scrollbar { background: none; border: none; box-shadow: none; padding: 0; }
scrollbar.vertical > range > trough { background-image: ${scroll_track_v}; background-size: 100% 100%; border: none; min-width: ${scroll_width}px; }
scrollbar.horizontal > range > trough { background-image: ${scroll_track_h}; background-size: 100% 100%; border: none; min-height: ${scroll_width}px; }
scrollbar slider {
    border-image: ${scroll_thumb_v_normal} 3 3 3 3 stretch; border-width: 3px; border-style: solid; border-radius: 0;
    background-image: ${scroll_grip_v_normal}, ${scroll_thumb_v_normal_fill}; background-position: ${grip_across}px center, center; background-repeat: no-repeat, no-repeat;
    background-size: auto, 100% 100%; background-origin: padding-box, padding-box; background-clip: padding-box, padding-box; background-color: transparent;
    box-shadow: none; margin: 0; min-width: ${scroll_inner}px; min-height: 8px;
}
scrollbar slider:hover { border-image-source: ${scroll_thumb_v_hot}; background-image: ${scroll_grip_v_hot}, ${scroll_thumb_v_hot_fill}; }
scrollbar slider:active { border-image-source: ${scroll_thumb_v_pressed}; background-image: ${scroll_grip_v_pressed}, ${scroll_thumb_v_pressed_fill}; }
scrollbar.horizontal slider { background-position: center ${grip_across}px, center; border-image-source: ${scroll_thumb_h_normal}; background-image: ${scroll_grip_h_normal}, ${scroll_thumb_h_normal_fill}; min-width: 8px; min-height: ${scroll_inner}px; }
scrollbar.horizontal slider:hover { border-image-source: ${scroll_thumb_h_hot}; background-image: ${scroll_grip_h_hot}, ${scroll_thumb_h_hot_fill}; }
scrollbar.horizontal slider:active { border-image-source: ${scroll_thumb_h_pressed}; background-image: ${scroll_grip_h_pressed}, ${scroll_thumb_h_pressed_fill}; }
)css";

bool writeText(const QString &path, const QString &text, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit())
        return fail(error, QStringLiteral("%1: %2").arg(path, f.errorString()));
    return true;
}

} // namespace

// Every rule's selectors made to outrank Adwaita's, which the theme starts
// from and whose selectors are often longer (notebook > header.top > tabs >
// tab:checked:backdrop): each selector's subject gets four classes it can't
// have. GTK's CSS has no !important; the rules keep their order among
// themselves, as each gains the same.
QString boosted(const QString &css)
{
    static const QString kBoost = QStringLiteral(":not(.xp-1):not(.xp-2):not(.xp-3):not(.xp-4)");
    QString out;
    int i = 0;
    const int n = int(css.size());
    while (i < n) {
        if (css.at(i).isSpace()) {
            out += css.at(i++);
            continue;
        }
        if (css.mid(i, 2) == QLatin1String("/*")) {
            const int end = int(css.indexOf(QLatin1String("*/"), i + 2));
            const int stop = end < 0 ? n : end + 2;
            out += css.mid(i, stop - i);
            i = stop;
            continue;
        }
        if (css.at(i) == QLatin1Char('@')) {   // @define-color, @import: as they are
            const int end = int(css.indexOf(QLatin1Char(';'), i));
            const int stop = end < 0 ? n : end + 1;
            out += css.mid(i, stop - i);
            i = stop;
            continue;
        }
        const int open = int(css.indexOf(QLatin1Char('{'), i));
        const int close = open < 0 ? -1 : int(css.indexOf(QLatin1Char('}'), open));
        if (open < 0 || close < 0) {
            out += css.mid(i);
            break;
        }
        QStringList selectors;
        for (QString sel : css.mid(i, open - i).split(QLatin1Char(','))) {
            sel = sel.trimmed();
            if (!sel.isEmpty())
                selectors << sel + kBoost;
        }
        out += selectors.join(QStringLiteral(",\n")) + QLatin1Char(' ') + css.mid(open, close + 1 - open);
        i = close + 1;
    }
    return out;
}

QString themeName(const Look &look)
{
    return QStringLiteral("XPlasma-") + look.id;
}

QString writeTheme(const Look &look, const QString &directory, QString *error)
{
    QStyle *style = xpStyle(look);
    if (!style) {
        fail(error, QStringLiteral("XPlasma's style plugin isn't installed: the GTK theme's controls can't be drawn"));
        return QString();
    }
    const QString root = QDir(directory).filePath(themeName(look));
    QDir(root).removeRecursively();
    Assets a(root + QLatin1String("/assets"));
    QHash<QString, QString> v;
    v.insert(QStringLiteral("NAME"), look.name);
    const ClassicScheme &s = look.system;
    for (const char *name : {"ButtonFace", "ButtonText", "Window", "WindowText", "Hilight", "HilightText", "GrayText", "ButtonShadow",
                             "Menu", "MenuText", "MenuBar", "MenuHilight", "InfoWindow", "InfoText"})
        v.insert(QLatin1String(name), hex(s.colour(QString::fromLatin1(name))));
    // (Classic schemes without the flat-menu colours: XP's fallbacks.)
    if (!s.colour(u"MenuHilight").isValid() || s.colour(u"MenuHilight") == Qt::black)
        v.insert(QStringLiteral("MenuHilight"), hex(s.colour(u"Hilight")));
    if (!s.colour(u"MenuBar").isValid() || s.colour(u"MenuBar") == Qt::black)
        v.insert(QStringLiteral("MenuBar"), hex(s.colour(u"Menu")));

    // Buttons.
    a.frame(v, QStringLiteral("button_normal"), QStringLiteral("button-normal"), pushButton(style, QStyle::State_Raised, false, false), QMargins(3, 3, 3, 3));
    a.frame(v, QStringLiteral("button_hot"), QStringLiteral("button-hot"), pushButton(style, QStyle::State_Raised | QStyle::State_MouseOver, false, false), QMargins(3, 3, 3, 3));
    a.frame(v, QStringLiteral("button_pressed"), QStringLiteral("button-pressed"), pushButton(style, QStyle::State_Sunken, false, false), QMargins(3, 3, 3, 3));
    a.frame(v, QStringLiteral("button_disabled"), QStringLiteral("button-disabled"), pushButton(style, QStyle::State_Raised, true, false), QMargins(3, 3, 3, 3));
    a.frame(v, QStringLiteral("button_default"), QStringLiteral("button-default"), pushButton(style, QStyle::State_Raised | QStyle::State_HasFocus, false, true), QMargins(3, 3, 3, 3));
    a.frame(v, QStringLiteral("tool_hot"), QStringLiteral("tool-hot"), toolButton(style, QStyle::State_Raised | QStyle::State_MouseOver), QMargins(3, 3, 3, 3));
    a.frame(v, QStringLiteral("tool_pressed"), QStringLiteral("tool-pressed"), toolButton(style, QStyle::State_Sunken), QMargins(3, 3, 3, 3));

    // Check boxes and radio buttons, every state.
    for (const auto &[prefix, element] : {std::pair("check", QStyle::PE_IndicatorCheckBox), std::pair("radio", QStyle::PE_IndicatorRadioButton)}) {
        const bool radio = element == QStyle::PE_IndicatorRadioButton;
        for (const auto &[value, valueState] : {std::pair("off", QStyle::State_Off), std::pair("on", QStyle::State_On),
                                                std::pair("mixed", QStyle::State_NoChange)}) {
            if (radio && valueState == QStyle::State_NoChange)
                continue;
            for (const Mode m : kModes) {
                const QString key = QStringLiteral("%1_%2_%3").arg(QLatin1String(prefix), QLatin1String(value), QLatin1String(modeName(m)));
                QString file = key;
                file.replace(QLatin1Char('_'), QLatin1Char('-'));
                v.insert(key, a.put(file, indicator(style, element, valueState | modeState(m), m == Mode::Disabled)));
            }
        }
    }
    v.insert(QStringLiteral("check_size"), QString::number(style->pixelMetric(QStyle::PM_IndicatorWidth)));
    v.insert(QStringLiteral("switch_half"), QString::number((style->pixelMetric(QStyle::PM_IndicatorWidth) + 1) / 2));
    v.insert(QStringLiteral("radio_size"), QString::number(style->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth)));

    // Text fields.
    const int edge = qMax(1, style->pixelMetric(QStyle::PM_DefaultFrameWidth));
    a.frame(v, QStringLiteral("entry_normal"), QStringLiteral("entry-normal"), lineEdit(style, {}, false), QMargins(edge, edge, edge, edge));
    a.frame(v, QStringLiteral("entry_disabled"), QStringLiteral("entry-disabled"), lineEdit(style, {}, true), QMargins(edge, edge, edge, edge));
    v.insert(QStringLiteral("entry_edge"), QString::number(edge));

    // Combo boxes: the drop-down button's width is the right slice.
    {
        auto o = option<QStyleOptionComboBox>(style, QSize(120, 21));
        o.editable = false;
        o.frame = true;
        const QRect arrow = style->subControlRect(QStyle::CC_ComboBox, &o, QStyle::SC_ComboBoxArrow, nullptr);
        const int w = qMax(3, 120 - arrow.left());
        v.insert(QStringLiteral("combo_arrow"), QString::number(w));
        a.frame(v, QStringLiteral("combo_normal"), QStringLiteral("combo-normal"), comboBox(style, {}, false, false), QMargins(3, 3, w, 3));
        a.frame(v, QStringLiteral("combo_hot"), QStringLiteral("combo-hot"), comboBox(style, QStyle::State_MouseOver, false, true), QMargins(3, 3, w, 3));
        a.frame(v, QStringLiteral("combo_disabled"), QStringLiteral("combo-disabled"), comboBox(style, {}, true, false), QMargins(3, 3, w, 3));
        // (Its drop-down button alone, beside a text field.)
        v.insert(QStringLiteral("combo_button_w"), QString::number(qMax(1, w - 3)));
        a.frame(v, QStringLiteral("combo_button_normal"), QStringLiteral("combo-button-normal"), comboBox(style, {}, false, false).copy(120 - w, 0, w, 21), QMargins(0, 3, 3, 3));
        a.frame(v, QStringLiteral("combo_button_hot"), QStringLiteral("combo-button-hot"), comboBox(style, QStyle::State_MouseOver, false, true).copy(120 - w, 0, w, 21), QMargins(0, 3, 3, 3));
        a.frame(v, QStringLiteral("combo_button_disabled"), QStringLiteral("combo-button-disabled"), comboBox(style, {}, true, false).copy(120 - w, 0, w, 21), QMargins(0, 3, 3, 3));
    }

    // Tabs and their page.
    a.frame(v, QStringLiteral("tab_normal"), QStringLiteral("tab-normal"), tab(style, {}), QMargins(3, 3, 3, 0));
    a.frame(v, QStringLiteral("tab_hot"), QStringLiteral("tab-hot"), tab(style, QStyle::State_MouseOver), QMargins(3, 3, 3, 0));
    a.frame(v, QStringLiteral("tab_selected"), QStringLiteral("tab-selected"), tab(style, QStyle::State_Selected), QMargins(3, 3, 3, 0));
    a.frame(v, QStringLiteral("tab_pane"), QStringLiteral("tab-pane"), tabPane(style), QMargins(2, 2, 2, 2));

    // Column headers.
    a.frame(v, QStringLiteral("header_normal"), QStringLiteral("header-normal"), header(style, {}), QMargins(2, 2, 2, 2));
    a.frame(v, QStringLiteral("header_hot"), QStringLiteral("header-hot"), header(style, QStyle::State_MouseOver), QMargins(2, 2, 2, 2));
    a.frame(v, QStringLiteral("header_pressed"), QStringLiteral("header-pressed"), header(style, QStyle::State_Sunken), QMargins(2, 2, 2, 2));

    // Progress bars.
    {
        const auto [groove, chunk] = progressParts(style, look);
        a.frame(v, QStringLiteral("progress_groove"), QStringLiteral("progress-groove"), groove, QMargins(3, 3, 3, 3));
        v.insert(QStringLiteral("progress_chunk"), a.put(QStringLiteral("progress-chunk"), chunk));
        v.insert(QStringLiteral("progress_inner"), QString::number(qMax(1, groove.height() - 6)));
        // (Vertical: the same, turned, the chunks growing upwards.)
        a.frame(v, QStringLiteral("progress_groove_v"), QStringLiteral("progress-groove-v"), groove.transformed(QTransform().rotate(-90)), QMargins(3, 3, 3, 3));
        v.insert(QStringLiteral("progress_chunk_v"), a.put(QStringLiteral("progress-chunk-v"), chunk.transformed(QTransform().rotate(-90))));
    }

    // Trackbars.
    {
        const SliderParts sp = sliderParts(style);
        const int cap = qMin(3, sp.groove.width() / 3);
        a.frame(v, QStringLiteral("slider_groove"), QStringLiteral("slider-groove"), sp.groove, QMargins(cap, 0, cap, 0));
        v.insert(QStringLiteral("slider_thumb"), a.put(QStringLiteral("slider-thumb"), sp.thumb));
        v.insert(QStringLiteral("slider_thumb_hot"), a.put(QStringLiteral("slider-thumb-hot"), sp.thumbHot));
        v.insert(QStringLiteral("slider_cap"), QString::number(cap));
        v.insert(QStringLiteral("slider_groove_h"), QString::number(sp.groove.height()));
        v.insert(QStringLiteral("slider_thumb_w"), QString::number(sp.thumb.width()));
        v.insert(QStringLiteral("slider_thumb_h"), QString::number(sp.thumb.height()));
        v.insert(QStringLiteral("slider_margin"), QString::number(qMax(0, (sp.thumb.height() - sp.groove.height()) / 2)));
        // (Vertical: turned, the thumb pointing right, as XP's does.)
        const QTransform turn = QTransform().rotate(-90);
        a.frame(v, QStringLiteral("slider_groove_v"), QStringLiteral("slider-groove-v"), sp.groove.transformed(turn), QMargins(0, cap, 0, cap));
        v.insert(QStringLiteral("slider_thumb_v"), a.put(QStringLiteral("slider-thumb-v"), sp.thumb.transformed(turn)));
        v.insert(QStringLiteral("slider_thumb_v_hot"), a.put(QStringLiteral("slider-thumb-v-hot"), sp.thumbHot.transformed(turn)));
    }

    // Scroll bars.
    const int width = qMax(8, look.system.metrics.scrollWidth);
    v.insert(QStringLiteral("scroll_width"), QString::number(width));
    v.insert(QStringLiteral("scroll_inner"), QString::number(qMax(1, width - 6)));
    // (The grip across the thumb at a whole pixel: centred on a half one, GTK
    // blends its ribs into a flat square.)
    {
        int grip = 8;
        if (!look.classic())
            if (const QImage g = look.renderer->stateImage({QStringLiteral("scrollbar"), 9, 1, {}}); !g.isNull())
                grip = g.width();
        v.insert(QStringLiteral("grip_across"), QString::number(qMax(0, (width - 6 - grip) / 2)));
    }
    for (const bool horizontal : {false, true}) {
        const ScrollParts sp = scrollParts(style, look, horizontal, width);
        const QString o = horizontal ? QStringLiteral("h") : QStringLiteral("v");
        v.insert(QStringLiteral("scroll_track_") + o, a.put(QStringLiteral("scroll-track-") + o, sp.track));
        const char *states[] = {"normal", "hot", "pressed", "disabled"};
        for (int st = 0; st < 3; ++st) {
            const QString suffix = o + QLatin1Char('_') + QLatin1String(states[st]);
            a.frame(v, QStringLiteral("scroll_thumb_") + suffix, QStringLiteral("scroll-thumb-%1-%2").arg(o, QLatin1String(states[st])), sp.thumb[st],
                    QMargins(3, 3, 3, 3));
            v.insert(QStringLiteral("scroll_grip_") + suffix,
                     sp.grip[st].isNull() ? QStringLiteral("none")
                                          : a.put(QStringLiteral("scroll-grip-%1-%2").arg(o, QLatin1String(states[st])), sp.grip[st]));
        }
        const QString back = horizontal ? QStringLiteral("left") : QStringLiteral("up");
        const QString forward = horizontal ? QStringLiteral("right") : QStringLiteral("down");
        for (int st = 0; st < 4; ++st) {
            v.insert(QStringLiteral("scroll_%1_%2").arg(back, QLatin1String(states[st])),
                     a.put(QStringLiteral("scroll-%1-%2").arg(back, QLatin1String(states[st])), sp.back[st]));
            v.insert(QStringLiteral("scroll_%1_%2").arg(forward, QLatin1String(states[st])),
                     a.put(QStringLiteral("scroll-%1-%2").arg(forward, QLatin1String(states[st])), sp.forward[st]));
        }
    }
    if (!a.ok()) {
        fail(error, QStringLiteral("can't write the GTK theme's images under %1").arg(root));
        return QString();
    }

    const QString common = boosted(fill(QString::fromUtf8(kCommonCss), v));
    const QString gtk3 = QStringLiteral("@import url(\"resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css\");\n") + common
                         + boosted(fill(QString::fromUtf8(kGtk3Css), v));
    // GTK 4: the XP layer on its own too, for libadwaita apps (which ignore
    // the theme but take the user's ~/.config/gtk-4.0/gtk.css).
    const QString layer4 = common + boosted(fill(QString::fromUtf8(kGtk4Css), v));
    const QString gtk4 = QStringLiteral("@import url(\"resource:///org/gtk/libgtk/theme/Default/Default-light.css\");\n@import url(\"xplasma.css\");\n");
    const QString index = QStringLiteral("[Desktop Entry]\nType=X-GNOME-Metatheme\nName=%1\nComment=The Windows XP look \"%2\", made by XPlasma\n"
                                         "Encoding=UTF-8\n\n[X-GNOME-Metatheme]\nGtkTheme=%1\n")
                              .arg(themeName(look), look.name);
    if (!writeText(root + QLatin1String("/gtk-3.0/gtk.css"), gtk3, error) || !writeText(root + QLatin1String("/gtk-4.0/xplasma.css"), layer4, error)
        || !writeText(root + QLatin1String("/gtk-4.0/gtk.css"), gtk4, error) || !writeText(root + QLatin1String("/index.theme"), index, error))
        return QString();
    return root;
}

namespace {

const QString kImportMarker = QStringLiteral("/* XPlasma */");

QString userCss()
{
    return paths::config() + QLatin1String("/gtk-4.0/gtk.css");
}

// Libadwaita apps ignore the GTK theme but load ~/.config/gtk-4.0/gtk.css,
// which KDE's GTK settings also write (their colours' import): XPlasma's
// line added to it, or taken out, and the rest left as it is.
void setUserImport(const QString &layer)
{
    QString text;
    if (QFile f(userCss()); f.open(QIODevice::ReadOnly))
        text = QString::fromUtf8(f.readAll());
    QStringList lines = text.split(QLatin1Char('\n'));
    lines.removeIf([](const QString &line) { return line.contains(kImportMarker); });
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    if (!layer.isEmpty())
        lines << QStringLiteral("@import url(\"file://%1\"); %2").arg(layer, kImportMarker);
    const QString out = lines.join(QLatin1Char('\n'));
    if (out.trimmed().isEmpty()) {
        // (Nothing left but what XPlasma added: no file, as before.)
        if (!text.isEmpty() && text.contains(kImportMarker))
            QFile::remove(userCss());
        return;
    }
    writeText(userCss(), out + QLatin1Char('\n'), nullptr);
}

void setGtkTheme(Session &session, const QString &name)
{
    // Plasma's GTK settings (kded's gtkconfig) write it everywhere GTK looks
    // -- settings.ini for 3 and 4, GSettings, xsettingsd -- and tell running
    // apps; the files directly as well, for a session without it.
    session.call(QStringLiteral("org.kde.kded6"), QStringLiteral("/modules/gtkconfig"), QStringLiteral("org.kde.GtkConfig"),
                 QStringLiteral("setGtkTheme"), {name});
    for (const char *file : {"gtk-3.0/settings.ini", "gtk-4.0/settings.ini"})
        session.writeConfig(QLatin1String(file), {QStringLiteral("Settings")}, QStringLiteral("gtk-theme-name"), name, false);
}

void removeThemes(const QString &keep)
{
    const QDir themes(paths::localShare() + QLatin1String("/themes"));
    for (const QString &name : themes.entryList({QStringLiteral("XPlasma-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        if (name != keep)
            QDir(themes.filePath(name)).removeRecursively();
}

} // namespace

bool apply(Session &session, const Look &look, State &state, QStringList *log, QString *error)
{
    const QString root = writeTheme(look, paths::localShare() + QLatin1String("/themes"), error);
    if (root.isEmpty())
        return false;
    removeThemes(themeName(look));
    const QString current = session.readConfig(QStringLiteral("gtk-3.0/settings.ini"), {QStringLiteral("Settings")}, QStringLiteral("gtk-theme-name"));
    if (!state.settings.contains(QStringLiteral("previousGtkTheme")) && !current.startsWith(QLatin1String("XPlasma-")))
        state.settings.insert(QStringLiteral("previousGtkTheme"), current.isEmpty() ? QStringLiteral("Breeze") : current);
    setGtkTheme(session, themeName(look));
    // XP's scroll bars always there, with their arrows, not GTK 3's overlaid
    // ones that show on hover.
    // (GTK 3 on Wayland takes it from GSettings; settings.ini for the rest.)
    session.writeConfig(QStringLiteral("gtk-3.0/settings.ini"), {QStringLiteral("Settings")}, QStringLiteral("gtk-overlay-scrolling"),
                        QStringLiteral("false"), false);
    const QStringList overlayKey{QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("overlay-scrolling")};
    if (const host::Result was = session.run(QStringList{QStringLiteral("gsettings"), QStringLiteral("get")} + overlayKey); was.ok()) {
        if (!state.settings.contains(QStringLiteral("previousGtkOverlayScrolling")))
            state.settings.insert(QStringLiteral("previousGtkOverlayScrolling"), QString::fromUtf8(was.out).trimmed());
        session.run(QStringList{QStringLiteral("gsettings"), QStringLiteral("set")} + overlayKey + QStringList{QStringLiteral("false")});
    }
    setUserImport(root + QLatin1String("/gtk-4.0/xplasma.css"));
    if (log)
        *log << QStringLiteral("GTK apps: %1 (GTK 3 and 4; libadwaita apps' controls too)").arg(themeName(look));
    // Flatpak's GTK apps too (a failure there isn't the look's).
    QString why;
    if (!flatpak::installGtk(session, root, themeName(look), log, &why) && log)
        *log << QStringLiteral("Flatpak apps: not the GTK theme (%1)").arg(why);
    return true;
}

void revert(Session &session, State &state, QStringList *log)
{
    const QString previous = state.settings.value(QStringLiteral("previousGtkTheme")).toString();
    const QString current = session.readConfig(QStringLiteral("gtk-3.0/settings.ini"), {QStringLiteral("Settings")}, QStringLiteral("gtk-theme-name"));
    if (current.startsWith(QLatin1String("XPlasma-")) || !previous.isEmpty())
        setGtkTheme(session, previous.isEmpty() ? QStringLiteral("Breeze") : previous);
    session.deleteConfig(QStringLiteral("gtk-3.0/settings.ini"), {QStringLiteral("Settings")}, QStringLiteral("gtk-overlay-scrolling"), false);
    if (const QString overlay = state.settings.value(QStringLiteral("previousGtkOverlayScrolling")).toString(); !overlay.isEmpty())
        session.run({QStringLiteral("gsettings"), QStringLiteral("set"), QStringLiteral("org.gnome.desktop.interface"),
                     QStringLiteral("overlay-scrolling"), overlay});
    state.settings.remove(QStringLiteral("previousGtkOverlayScrolling"));
    state.settings.remove(QStringLiteral("previousGtkTheme"));
    setUserImport(QString());
    flatpak::removeGtk(log);
    const bool had = QDir(paths::localShare() + QLatin1String("/themes")).entryList({QStringLiteral("XPlasma-*")}, QDir::Dirs).size() > 0;
    removeThemes(QString());
    if (had && log)
        *log << QStringLiteral("GTK apps: back to %1").arg(previous.isEmpty() ? QStringLiteral("Breeze") : previous);
}

} // namespace xpl::gtk
