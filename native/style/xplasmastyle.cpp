#include "xplasmastyle.h"

#include "core/schema.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QToolButton>

using xpl::PartRef;

namespace {

// uxtheme's state numbers for the common four-state parts.
enum { Normal = 1, Hot = 2, Pressed = 3, Disabled = 4 };

bool enabled(const QStyleOption *o) { return o->state & QStyle::State_Enabled; }
bool hovered(const QStyleOption *o) { return o->state & QStyle::State_MouseOver; }
bool sunken(const QStyleOption *o) { return o->state & QStyle::State_Sunken; }

int fourState(const QStyleOption *o)
{
    if (!enabled(o))
        return Disabled;
    if (sunken(o))
        return Pressed;
    return hovered(o) ? Hot : Normal;
}

// Where XPLasma's configuration says the style to draw is. XPLASMA_THEME
// ("/path/Luna.msstyles" or "/path/Luna.msstyles:NORMALMETALLIC_INI")
// overrides it per process, for tests and previews.
std::pair<QString, QString> configuredTheme()
{
    const QString env = qEnvironmentVariable("XPLASMA_THEME");
    if (!env.isEmpty()) {
        const int colon = int(env.lastIndexOf(QLatin1Char(':')));
        if (colon > 0 && env.mid(colon + 1).endsWith(QLatin1String("_INI"), Qt::CaseInsensitive))
            return {env.left(colon), env.mid(colon + 1)};
        return {env, QString()};
    }
    const QString rc = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                       + QLatin1String("/xplasmarc");
    QSettings settings(rc, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Theme"));
    return {settings.value(QStringLiteral("Path")).toString(), settings.value(QStringLiteral("Variant")).toString()};
}

} // namespace

XPLasmaStyle::XPLasmaStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Windows")))
{
    setObjectName(QStringLiteral("XPLasma"));
    m_safe = qEnvironmentVariableIntValue("XPLASMA_SAFE") != 0;
    m_cache.setMaxCost(16 * 1024 * 1024);   // bytes of rendered parts
    if (!m_safe)
        loadTheme();
}

void XPLasmaStyle::loadTheme()
{
    const auto [path, variant] = configuredTheme();
    if (path.isEmpty())
        return;
    QString error;
    if (auto theme = xpl::Theme::load(path, variant, &error))
        m_renderer = std::make_unique<xpl::Renderer>(theme);
    else
        qWarning("XPLasma: can't load %s (%s); drawing classic", qPrintable(path), qPrintable(error));
}

// --- drawing parts -----------------------------------------------------------

bool XPLasmaStyle::hasPart(const PartRef &ref) const
{
    return m_renderer && m_renderer->hasPart(ref);
}

bool XPLasmaStyle::drawPart(QPainter *painter, const PartRef &ref, const QRect &rect) const
{
    if (!hasPart(ref) || rect.isEmpty())
        return false;
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QSize size = (QSizeF(rect.size()) * dpr).toSize();
    const int dpi = qRound(96 * dpr);
    const QString key = QStringLiteral("%1|%2|%3|%4|%5x%6@%7")
                            .arg(ref.sub, ref.cls).arg(ref.part).arg(ref.state)
                            .arg(size.width()).arg(size.height()).arg(dpi);
    QImage *image = m_cache.object(key);
    if (!image) {
        image = new QImage(m_renderer->background(ref, size, dpi));
        image->setDevicePixelRatio(dpr);
        m_cache.insert(key, image, int(qMin<qint64>(image->sizeInBytes(), INT_MAX)));
        image = m_cache.object(key);
        if (!image)
            return false;
    }
    painter->drawImage(rect.topLeft(), *image);
    return true;
}

QSize XPLasmaStyle::trueSize(const PartRef &ref) const
{
    return m_renderer ? m_renderer->partSize(ref, xpl::PartSize::True) : QSize();
}

QColor XPLasmaStyle::themeColour(const PartRef &ref, QStringView property, const QColor &fallback) const
{
    QRgb rgb;
    if (m_renderer && xpl::parseColour(m_renderer->property(ref, property).value_or(QString()), &rgb))
        return QColor(rgb);
    return fallback;
}

QColor XPLasmaStyle::sysColour(QStringView name, const QColor &fallback) const
{
    QRgb rgb;
    if (m_renderer && xpl::parseColour(m_renderer->theme().property(u"sysmetrics", u"", u"", name).value_or(QString()), &rgb))
        return QColor(rgb);
    return fallback;
}

int XPLasmaStyle::sysMetric(QStringView name, int fallback) const
{
    bool ok = false;
    const int v = m_renderer ? m_renderer->theme().property(u"sysmetrics", u"", u"", name).value_or(QString()).toInt(&ok) : 0;
    return ok && v > 0 ? v : fallback;
}

// --- palette and polish -----------------------------------------------------------

QPalette XPLasmaStyle::standardPalette() const
{
    if (!m_renderer)
        return QProxyStyle::standardPalette();
    // XP's system colours from the style. (In a Plasma session the colour
    // scheme XPLasma generates from the same SysMetrics is what apps use.)
    const QColor face = sysColour(u"btnface", QColor(236, 233, 216));
    const QColor window = sysColour(u"window", Qt::white);
    const QColor text = sysColour(u"windowtext", Qt::black);
    const QColor gray = sysColour(u"graytext", QColor(172, 168, 153));
    QPalette pal(sysColour(u"btntext", text), face, sysColour(u"btnhighlight", Qt::white),
                 sysColour(u"btnshadow", QColor(172, 168, 153)), face, text, text, window, face);
    pal.setColor(QPalette::Highlight, sysColour(u"highlight", QColor(49, 106, 197)));
    pal.setColor(QPalette::HighlightedText, sysColour(u"highlighttext", Qt::white));
    pal.setColor(QPalette::ToolTipBase, sysColour(u"infobk", QColor(255, 255, 225)));
    pal.setColor(QPalette::ToolTipText, sysColour(u"infotext", Qt::black));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, gray);
    return pal;
}

void XPLasmaStyle::polish(QPalette &palette)
{
    QProxyStyle::polish(palette);
}

void XPLasmaStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    // Hot tracking, as XP had: these controls redraw on hover.
    if (qobject_cast<QAbstractButton *>(widget) || qobject_cast<QComboBox *>(widget)
        || qobject_cast<QScrollBar *>(widget) || qobject_cast<QSlider *>(widget) || qobject_cast<QTabBar *>(widget)
        || widget->inherits("QAbstractSpinBox") || widget->inherits("QHeaderView"))
        widget->setAttribute(Qt::WA_Hover, true);
}

void XPLasmaStyle::unpolish(QWidget *widget)
{
    QProxyStyle::unpolish(widget);
}

// --- primitives -------------------------------------------------------------------

void XPLasmaStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                                 const QWidget *widget) const
{
    if (m_renderer) {
        switch (element) {
        case PE_IndicatorCheckBox: {
            // CBS_*: unchecked 1-4, checked 5-8, mixed 9-12.
            const int base = (option->state & State_NoChange) ? 8 : (option->state & State_On) ? 4 : 0;
            if (drawPart(painter, {QStringLiteral("button"), 3, base + fourState(option), {}}, option->rect))
                return;
            break;
        }
        case PE_IndicatorRadioButton: {
            const int base = (option->state & State_On) ? 4 : 0;
            if (drawPart(painter, {QStringLiteral("button"), 2, base + fourState(option), {}}, option->rect))
                return;
            break;
        }
        case PE_FrameGroupBox:
            if (drawPart(painter, {QStringLiteral("button"), 4, enabled(option) ? 1 : 2, {}}, option->rect))
                return;
            break;
        case PE_FrameTabWidget:
            if (drawPart(painter, {QStringLiteral("tab"), 9, 1, {}}, option->rect))
                return;
            break;
        case PE_PanelLineEdit:
        case PE_FrameLineEdit: {
            // ETS_*: normal 1, hot 2, selected 3, disabled 4, focused 5, readonly 6.
            int state = Normal;
            if (!enabled(option))
                state = Disabled;
            else if (option->state & State_ReadOnly)
                state = 6;
            else if (option->state & State_HasFocus)
                state = 5;
            else if (hovered(option))
                state = Hot;
            if (element == PE_PanelLineEdit) {
                const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
                if (frame && frame->lineWidth <= 0) {
                    painter->fillRect(option->rect, option->palette.brush(QPalette::Base));
                    return;
                }
            }
            if (drawPart(painter, {QStringLiteral("edit"), 1, state, {}}, option->rect)) {
                // The theme draws the border; the control fills its inside,
                // in the button face when disabled or read-only (as XP's
                // WM_CTLCOLORSTATIC does), in the window colour otherwise.
                if (element == PE_PanelLineEdit) {
                    const QRect inside = m_renderer->contentRect({QStringLiteral("edit"), 1, state, {}}, option->rect);
                    const bool face = state == Disabled || state == 6;
                    painter->fillRect(inside, option->palette.brush(face ? QPalette::Disabled : QPalette::Active,
                                                                     face ? QPalette::Window : QPalette::Base));
                }
                return;
            }
            break;
        }
        case PE_PanelButtonTool: {
            // TS_*: normal, hot, pressed, disabled, checked, hot-checked.
            int state = fourState(option);
            if (enabled(option) && (option->state & State_On))
                state = hovered(option) ? 6 : 5;
            if (!(option->state & State_AutoRaise) || state != Normal) {
                if (drawPart(painter, {QStringLiteral("toolbar"), 1, state, {}}, option->rect))
                    return;
            } else {
                return;   // a flat tool button at rest draws nothing, as in XP
            }
            break;
        }
        default:
            break;
        }
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

// --- controls ------------------------------------------------------------------------

void XPLasmaStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                               const QWidget *widget) const
{
    if (m_renderer) {
        switch (element) {
        case CE_PushButtonBevel:
            if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option)) {
                if (btn->features & QStyleOptionButton::Flat && !(btn->state & (State_Sunken | State_On | State_MouseOver)))
                    return;
                // PBS_DEFAULTED is a complete bitmap for the resting state only;
                // hot, pressed and disabled keep their own art.
                int state = fourState(btn);
                if (btn->state & State_On)
                    state = Pressed;
                if (state == Normal && (btn->features & QStyleOptionButton::DefaultButton))
                    state = 5;
                if (drawPart(painter, {QStringLiteral("button"), 1, state, {}}, btn->rect)) {
                    if (btn->features & QStyleOptionButton::HasMenu) {
                        QStyleOption arrow(*btn);
                        const int w = proxy()->pixelMetric(PM_MenuButtonIndicator, btn, widget);
                        arrow.rect = QRect(btn->rect.right() - w - 3, btn->rect.top(), w, btn->rect.height());
                        proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrow, painter, widget);
                    }
                    return;
                }
            }
            break;
        case CE_PushButtonLabel:
            if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option)) {
                QStyleOptionButton copy(*btn);
                const int state = enabled(btn) ? Normal : Disabled;
                copy.palette.setColor(QPalette::ButtonText,
                                      themeColour({QStringLiteral("button"), 1, state, {}}, u"textcolor",
                                                  btn->palette.color(QPalette::ButtonText)));
                // No sunken text shift: XP's pressed buttons keep their label still.
                copy.state &= ~(State_Sunken | State_On);
                QProxyStyle::drawControl(element, &copy, painter, widget);
                return;
            }
            break;
        case CE_TabBarTabShape:
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
                if (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth) {
                    const bool selected = tab->state & State_Selected;
                    // TABP_TABITEM / *LEFTEDGE / *RIGHTEDGE / *BOTHEDGE, and the
                    // TOPTABITEM set for the selected tab.
                    int part = selected ? 5 : 1;
                    if (tab->position == QStyleOptionTab::Beginning)
                        part += 1;
                    else if (tab->position == QStyleOptionTab::End)
                        part += 2;
                    else if (tab->position == QStyleOptionTab::OnlyOneTab)
                        part += 3;
                    int state = selected ? 3 : hovered(tab) ? Hot : Normal;
                    if (!enabled(tab))
                        state = Disabled;
                    QRect r = tab->rect;
                    if (!selected)
                        r.adjust(0, 2, 0, 0);   // unselected tabs sit 2px lower, as in XP
                    else
                        r.adjust(-2, 0, 2, 1);  // the selected tab widens over its neighbours and meets the pane
                    if (drawPart(painter, {QStringLiteral("tab"), part, state, {}}, r))
                        return;
                }
            }
            break;
        case CE_ProgressBarGroove:
            if (drawPart(painter, {QStringLiteral("progress"), (option->state & State_Horizontal) ? 1 : 2, 1, {}},
                         option->rect))
                return;
            break;
        case CE_ProgressBarContents:
            if (const auto *bar = qstyleoption_cast<const QStyleOptionProgressBar *>(option)) {
                if (hasPart({QStringLiteral("progress"), (bar->state & State_Horizontal) ? 3 : 4, 1, {}})) {
                    drawProgressChunks(bar, painter, widget);
                    return;
                }
            }
            break;
        case CE_HeaderSection:
            if (drawPart(painter, {QStringLiteral("header"), 1, fourState(option) == Disabled ? Normal : fourState(option), {}},
                         option->rect))
                return;
            break;
        default:
            break;
        }
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void XPLasmaStyle::drawProgressChunks(const QStyleOptionProgressBar *bar, QPainter *painter, const QWidget *) const
{
    const bool horizontal = bar->state & State_Horizontal;
    const PartRef groove{QStringLiteral("progress"), horizontal ? 1 : 2, 1, {}};
    const PartRef chunk{QStringLiteral("progress"), horizontal ? 3 : 4, 1, {}};
    const QRect content = m_renderer->contentRect(groove, bar->rect);
    const qint64 range = qint64(bar->maximum) - bar->minimum;
    if (range <= 0 || content.isEmpty()) {
        // Busy indicator: left to the classic base for now.
        return;
    }
    const int size = m_renderer->theme().property(u"progress", u"", u"", u"progresschunksize").value_or(QStringLiteral("8")).toInt();
    const int space = m_renderer->theme().property(u"progress", u"", u"", u"progressspacesize").value_or(QStringLiteral("2")).toInt();
    const int length = horizontal ? content.width() : content.height();
    const int filled = int(length * (qint64(bar->progress) - bar->minimum) / range);
    const int step = qMax(1, size + space);
    for (int at = 0; at < filled; at += step) {
        // XP draws whole chunks, clipped only by the groove's content area.
        const int w = qMin(size, length - at);
        const QRect r = horizontal ? QRect(content.left() + at, content.top(), w, content.height())
                                   : QRect(content.left(), content.bottom() - at - w + 1, content.width(), w);
        drawPart(painter, chunk, r);
    }
}

// --- complex controls -----------------------------------------------------------

void XPLasmaStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                                      const QWidget *widget) const
{
    if (m_renderer) {
        switch (control) {
        case CC_ScrollBar:
            if (const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
                if (hasPart({QStringLiteral("scrollbar"), 1, 1, {}})) {
                    drawScrollBar(bar, painter, widget);
                    return;
                }
            }
            break;
        case CC_ComboBox:
            if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
                const PartRef button{QStringLiteral("combobox"), 1, 0, {}};
                if (hasPart(button)) {
                    // The field: the Edit border, then XP's drop-down button.
                    int editState = enabled(combo) ? ((combo->state & State_HasFocus) ? 5 : Normal) : Disabled;
                    drawPart(painter, {QStringLiteral("edit"), 1, editState, {}}, combo->rect);
                    const QRect arrow = proxy()->subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
                    int state = Normal;
                    if (!enabled(combo))
                        state = Disabled;
                    else if ((combo->activeSubControls & SC_ComboBoxArrow) && sunken(combo))
                        state = Pressed;
                    else if (hovered(combo))
                        state = Hot;
                    drawPart(painter, {QStringLiteral("combobox"), 1, state, {}}, arrow);
                    if (combo->state & State_HasFocus && !combo->editable) {
                        // A focused drop-down list highlights its field, as XP does.
                        const QRect field = proxy()->subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget);
                        painter->fillRect(field, combo->palette.brush(QPalette::Highlight));
                    }
                    return;
                }
            }
            break;
        case CC_SpinBox:
            if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
                if (hasPart({QStringLiteral("spin"), 1, 1, {}})) {
                    if (spin->frame)
                        drawPart(painter, {QStringLiteral("edit"), 1, enabled(spin) ? Normal : Disabled, {}}, spin->rect);
                    for (const auto &[sub, part, flag] : {std::tuple{SC_SpinBoxUp, 1, QAbstractSpinBox::StepUpEnabled},
                                                          std::tuple{SC_SpinBoxDown, 2, QAbstractSpinBox::StepDownEnabled}}) {
                        int state = Normal;
                        if (!enabled(spin) || !(spin->stepEnabled & flag))
                            state = Disabled;
                        else if ((spin->activeSubControls & sub) && sunken(spin))
                            state = Pressed;
                        else if ((spin->activeSubControls & sub) && hovered(spin))
                            state = Hot;
                        drawPart(painter, {QStringLiteral("spin"), part, state, {}},
                                 proxy()->subControlRect(CC_SpinBox, spin, sub, widget));
                    }
                    return;
                }
            }
            break;
        case CC_Slider:
            if (const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
                const bool horizontal = slider->orientation == Qt::Horizontal;
                if (hasPart({QStringLiteral("trackbar"), horizontal ? 1 : 2, 1, {}})) {
                    const QRect groove = proxy()->subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
                    const QRect handle = proxy()->subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
                    // XP's track is a thin channel through the thumb's middle.
                    QRect track = horizontal ? QRect(groove.left(), handle.center().y() - 1, groove.width(), 4)
                                             : QRect(handle.center().x() - 1, groove.top(), 4, groove.height());
                    drawPart(painter, {QStringLiteral("trackbar"), horizontal ? 1 : 2, 1, {}}, track);
                    if (slider->subControls & SC_SliderTickmarks) {
                        QStyleOptionSlider ticks(*slider);
                        ticks.subControls = SC_SliderTickmarks;
                        QProxyStyle::drawComplexControl(control, &ticks, painter, widget);
                    }
                    // TKP_THUMB*: the pointed thumb faces its tick marks.
                    int part = horizontal ? 3 : 6;
                    if (horizontal && slider->tickPosition == QSlider::TicksBelow)
                        part = 4;
                    else if (horizontal && slider->tickPosition == QSlider::TicksAbove)
                        part = 5;
                    else if (!horizontal && slider->tickPosition == QSlider::TicksLeft)
                        part = 7;
                    else if (!horizontal && slider->tickPosition == QSlider::TicksRight)
                        part = 8;
                    int state = Normal;
                    if (!enabled(slider))
                        state = 5;
                    else if ((slider->activeSubControls & SC_SliderHandle) && sunken(slider))
                        state = Pressed;
                    else if ((slider->activeSubControls & SC_SliderHandle) && hovered(slider))
                        state = Hot;
                    else if (slider->state & State_HasFocus)
                        state = 4;
                    drawPart(painter, {QStringLiteral("trackbar"), part, state, {}}, handle);
                    return;
                }
            }
            break;
        case CC_GroupBox:
            if (const auto *box = qstyleoption_cast<const QStyleOptionGroupBox *>(option)) {
                QStyleOptionGroupBox copy(*box);
                copy.textColor = themeColour({QStringLiteral("button"), 4, 1, {}}, u"textcolor", box->textColor);
                QProxyStyle::drawComplexControl(control, &copy, painter, widget);
                return;
            }
            break;
        default:
            break;
        }
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

void XPLasmaStyle::drawScrollBar(const QStyleOptionSlider *bar, QPainter *painter, const QWidget *widget) const
{
    const bool horizontal = bar->orientation == Qt::Horizontal;
    const bool on = enabled(bar) && bar->maximum > bar->minimum;
    auto stateOf = [&](SubControl sc) {
        if (!on)
            return Disabled;
        if ((bar->activeSubControls & sc) && sunken(bar))
            return Pressed;
        if ((bar->activeSubControls & sc) && hovered(bar))
            return Hot;
        return Normal;
    };
    // ABS_*: up 1-4, down 5-8, left 9-12, right 13-16.
    const int subBase = horizontal ? 8 : 0, addBase = horizontal ? 12 : 4;
    drawPart(painter, {QStringLiteral("scrollbar"), 1, subBase + stateOf(SC_ScrollBarSubLine), {}},
             proxy()->subControlRect(CC_ScrollBar, bar, SC_ScrollBarSubLine, widget));
    drawPart(painter, {QStringLiteral("scrollbar"), 1, addBase + stateOf(SC_ScrollBarAddLine), {}},
             proxy()->subControlRect(CC_ScrollBar, bar, SC_ScrollBarAddLine, widget));
    // SBP_LOWERTRACK* is the side before the thumb, UPPERTRACK* after it.
    drawPart(painter, {QStringLiteral("scrollbar"), horizontal ? 4 : 6, stateOf(SC_ScrollBarSubPage), {}},
             proxy()->subControlRect(CC_ScrollBar, bar, SC_ScrollBarSubPage, widget));
    drawPart(painter, {QStringLiteral("scrollbar"), horizontal ? 5 : 7, stateOf(SC_ScrollBarAddPage), {}},
             proxy()->subControlRect(CC_ScrollBar, bar, SC_ScrollBarAddPage, widget));
    if (on) {
        const QRect thumb = proxy()->subControlRect(CC_ScrollBar, bar, SC_ScrollBarSlider, widget);
        drawPart(painter, {QStringLiteral("scrollbar"), horizontal ? 2 : 3, stateOf(SC_ScrollBarSlider), {}}, thumb);
        drawPart(painter, {QStringLiteral("scrollbar"), horizontal ? 8 : 9, stateOf(SC_ScrollBarSlider), {}}, thumb);
    }
}

// --- metrics ---------------------------------------------------------------------------

int XPLasmaStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    if (m_renderer) {
        switch (metric) {
        case PM_IndicatorWidth:
        case PM_IndicatorHeight: {
            const QSize s = trueSize({QStringLiteral("button"), 3, 1, {}});
            if (s.isValid())
                return metric == PM_IndicatorWidth ? s.width() : s.height();
            break;
        }
        case PM_ExclusiveIndicatorWidth:
        case PM_ExclusiveIndicatorHeight: {
            const QSize s = trueSize({QStringLiteral("button"), 2, 1, {}});
            if (s.isValid())
                return metric == PM_ExclusiveIndicatorWidth ? s.width() : s.height();
            break;
        }
        case PM_ScrollBarExtent:
            return sysMetric(u"scrollbarwidth", 17);
        case PM_SliderThickness:
        case PM_SliderLength: {
            const QSize s = trueSize({QStringLiteral("trackbar"), 4, 1, {}});
            if (s.isValid())
                return metric == PM_SliderLength ? s.width() : s.height() + 4;
            break;
        }
        case PM_CheckBoxLabelSpacing:
        case PM_RadioButtonLabelSpacing:
            return 3;   // XP: a 13px box, then the label at x = 16
        case PM_ButtonShiftHorizontal:
        case PM_ButtonShiftVertical:
            return 0;   // XP's pressed buttons keep their label still
        case PM_TabBarTabShiftVertical:
        case PM_TabBarTabShiftHorizontal:
            return 0;
        case PM_TabBarBaseOverlap:
            return 1;
        default:
            break;
        }
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

QSize XPLasmaStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                                     const QWidget *widget) const
{
    if (m_renderer) {
        switch (type) {
        case CT_PushButton: {
            // Label plus the part's ContentMargins -- not its SizingMargins,
            // which are only how the bitmap is sliced -- and room for the
            // focus rectangle, never below MinSize.
            const PartRef ref{QStringLiteral("button"), 1, 1, {}};
            if (hasPart(ref)) {
                const QMargins m = m_renderer->contentMargins(ref);
                QSize s = size + QSize(m.left() + m.right() + 12, m.top() + m.bottom() + 4);
                return s.expandedTo(m_renderer->partSize(ref, xpl::PartSize::Min)).expandedTo(QSize(75, 23));
            }
            break;
        }
        default:
            break;
        }
    }
    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

QRect XPLasmaStyle::subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const
{
    return QProxyStyle::subElementRect(element, option, widget);
}

QRect XPLasmaStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub,
                                   const QWidget *widget) const
{
    if (m_renderer && control == CC_ComboBox && hasPart({QStringLiteral("combobox"), 1, 1, {}})) {
        if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
            // XP's drop-down button: a scroll bar's width, inside the 1px border.
            const int w = sysMetric(u"scrollbarwidth", 17);
            const QRect inner = combo->rect.adjusted(1, 1, -1, -1);
            const QRect arrow(inner.right() - w + 1, inner.top(), w, inner.height());
            if (sub == SC_ComboBoxArrow)
                return visualRect(combo->direction, combo->rect, arrow);
            if (sub == SC_ComboBoxEditField)
                return visualRect(combo->direction, combo->rect,
                                  QRect(inner.left() + 1, inner.top() + 1, inner.width() - w - 2, inner.height() - 2));
        }
    }
    return QProxyStyle::subControlRect(control, option, sub, widget);
}

int XPLasmaStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                            QStyleHintReturn *returnData) const
{
    switch (hint) {
    case SH_ComboBox_Popup:
        return 0;
    default:
        break;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}
