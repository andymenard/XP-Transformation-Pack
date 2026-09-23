#include "xplasmastyle.h"

#include "core/schema.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QRawFont>
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
        case PE_IndicatorBranch:
            if (hasPart({QStringLiteral("treeview"), 2, 1, {}})) {
                drawTreeBranch(option, painter, widget);
                return;
            }
            break;
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
        case PE_Frame:
            // Widget context: a frame is drawn by what the widget is, as XP
            // themes a control's client edge with its own class -- list
            // views, list boxes and tree views get a flat 1px border in the
            // class colour (Luna Silver 127,157,185), multi-line text the
            // Edit border. Anything else keeps the classic edge.
            if (const QString cls = frameClass(widget); !cls.isEmpty()) {
                if (drawPart(painter, {cls, cls == QLatin1String("edit") ? 1 : 0, enabled(option) ? Normal : Disabled, {}},
                             option->rect))
                    return;
            }
            break;
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
        case CE_CheckBoxLabel:
        case CE_RadioButtonLabel:
            if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option)) {
                // The part's own TextColor for its state (disabled included).
                const bool check = element == CE_CheckBoxLabel;
                const int base = (btn->state & State_On) ? 4 : (check && (btn->state & State_NoChange)) ? 8 : 0;
                const PartRef ref{QStringLiteral("button"), check ? 3 : 2, base + fourState(btn), {}};
                QStyleOptionButton copy(*btn);
                const QPalette::ColorGroup group = enabled(btn) ? QPalette::Active : QPalette::Disabled;
                copy.palette.setColor(group, QPalette::WindowText,
                                      themeColour(ref, u"textcolor", btn->palette.color(group, QPalette::WindowText)));
                QProxyStyle::drawControl(element, &copy, painter, widget);
                return;
            }
            break;
        case CE_PushButtonLabel:
            if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option)) {
                QStyleOptionButton copy(*btn);
                const int state = enabled(btn) ? Normal : Disabled;
                const QPalette::ColorGroup group = enabled(btn) ? QPalette::Active : QPalette::Disabled;
                copy.palette.setColor(group, QPalette::ButtonText,
                                      themeColour({QStringLiteral("button"), 1, state, {}}, u"textcolor",
                                                  btn->palette.color(group, QPalette::ButtonText)));
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
                    // XP's geometry (TCM_GETITEMRECT, Luna): tabs start 2px in
                    // and 2px down, 18px tall; the selected one is drawn 2px
                    // wider each side, 2px taller, and 1px into the pane.
                    QRect r = xpTabRect(tab);
                    if (selected)
                        r.adjust(-2, -2, 2, 1);
                    if (drawPart(painter, {QStringLiteral("tab"), part, state, {}}, r))
                        return;
                }
            }
            break;
        case CE_HeaderLabel:
            if (const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option)) {
                if (hasPart({QStringLiteral("header"), 1, Normal, {}})) {
                    // QCommonStyle insets the label by PM_HeaderMargin on every
                    // side through a non-proxied call, which clips a 20px
                    // header's text to 2px. XP's margin is horizontal only.
                    const int m = proxy()->pixelMetric(PM_HeaderMargin, option, widget);
                    QStyleOptionHeader copy(*header);
                    copy.rect = header->rect.adjusted(0, -m, 0, m);
                    QProxyStyle::drawControl(element, &copy, painter, widget);
                    return;
                }
            }
            break;
        case CE_ComboBoxLabel:
            if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
                if (!combo->editable && hasPart({QStringLiteral("combobox"), 1, 1, {}})) {
                    // QCommonStyle insets the label 1px into the field; XP
                    // draws it at the field's edge.
                    painter->save();
                    painter->translate(-1, 0);
                    QProxyStyle::drawControl(element, option, painter, widget);
                    painter->restore();
                    return;
                }
            }
            break;
        case CE_TabBarTabLabel:
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
                if (hasPart({QStringLiteral("tab"), 1, 1, {}})
                    && (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth)) {
                    QStyleOptionTab copy(*tab);
                    copy.rect = xpTabRect(tab);
                    // The selected tab's label rides up with it.
                    if (tab->state & State_Selected)
                        copy.rect.translate(0, -1);
                    QProxyStyle::drawControl(element, &copy, painter, widget);
                    return;
                }
            }
            break;
        case CE_ProgressBar:
            // Laid out here rather than by QCommonStyle, which computes the
            // contents rect with its own (non-proxied) classic inset.
            if (const auto *bar = qstyleoption_cast<const QStyleOptionProgressBar *>(option)) {
                if (hasPart({QStringLiteral("progress"), (bar->state & State_Horizontal) ? 1 : 2, 1, {}})) {
                    QStyleOptionProgressBar sub(*bar);
                    sub.rect = proxy()->subElementRect(SE_ProgressBarGroove, bar, widget);
                    proxy()->drawControl(CE_ProgressBarGroove, &sub, painter, widget);
                    proxy()->drawControl(CE_ProgressBarContents, &sub, painter, widget);
                    if (bar->textVisible) {
                        sub.rect = proxy()->subElementRect(SE_ProgressBarLabel, bar, widget);
                        proxy()->drawControl(CE_ProgressBarLabel, &sub, painter, widget);
                    }
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
    // comctl32's progress bar, measured against XP (Luna, 14 values x 3
    // widths): the filled length is value x the *whole* control's length;
    // chunks of ProgressChunkSize, ProgressSpaceSize apart, start at the
    // content rect and span its thickness; there are ceil(filled / step) of
    // them, the last clipped 1px short of the far edge (at 99% on a 150px
    // bar, chunk 19 shows a single pixel).
    const bool horizontal = bar->state & State_Horizontal;
    const PartRef groove{QStringLiteral("progress"), horizontal ? 1 : 2, 1, {}};
    const PartRef chunk{QStringLiteral("progress"), horizontal ? 3 : 4, 1, {}};
    const QRect content = m_renderer->contentRect(groove, bar->rect);
    const qint64 range = qint64(bar->maximum) - bar->minimum;
    if (range <= 0 || content.isEmpty())
        return;   // busy indicator: the classic base draws it for now
    const int size = qMax(1, m_renderer->theme().property(u"progress", u"", u"", u"progresschunksize")
                                 .value_or(QStringLiteral("8")).toInt());
    const int space = qMax(0, m_renderer->theme().property(u"progress", u"", u"", u"progressspacesize")
                                  .value_or(QStringLiteral("2")).toInt());
    const int length = horizontal ? bar->rect.width() : bar->rect.height();
    const qint64 value = qBound<qint64>(0, qint64(bar->progress) - bar->minimum, range);
    const int filled = int((value * length + range / 2) / range);   // MulDiv
    const int step = size + space;
    const int count = (filled + step - 1) / step;
    painter->save();
    painter->setClipRect(horizontal ? bar->rect.adjusted(0, 0, -1, 0) : bar->rect.adjusted(0, 1, 0, 0));
    for (int i = 0; i < count; ++i) {
        const QRect r = horizontal
            ? QRect(content.left() + i * step, content.top(), size, content.height())
            : QRect(content.left(), content.bottom() + 1 - i * step - size, content.width(), size);
        drawPart(painter, chunk, r);
    }
    painter->restore();
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
                    // The frame is the ComboBox class's own background (part 0:
                    // Luna Silver's border is 127,157,185, not Edit's
                    // 165,172,178), falling back to Edit's; then the button.
                    const int frameState = enabled(combo) ? Normal : Disabled;
                    if (!drawPart(painter, {QStringLiteral("combobox"), 0, frameState, {}}, combo->rect))
                        drawPart(painter, {QStringLiteral("edit"), 1, enabled(combo) ? Normal : Disabled, {}}, combo->rect);
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
                    const QRect channel = proxy()->subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
                    const QRect handle = proxy()->subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
                    drawPart(painter, {QStringLiteral("trackbar"), horizontal ? 1 : 2, 1, {}}, channel);
                    if ((slider->subControls & SC_SliderTickmarks) && slider->tickPosition != QSlider::NoTicks)
                        drawSliderTicks(slider, channel, handle, painter);
                    int state = Normal;
                    if (!enabled(slider))
                        state = 5;
                    else if ((slider->activeSubControls & SC_SliderHandle) && sunken(slider))
                        state = Pressed;
                    else if ((slider->activeSubControls & SC_SliderHandle) && hovered(slider))
                        state = Hot;
                    else if (slider->state & State_HasFocus)
                        state = 4;
                    drawPart(painter, {QStringLiteral("trackbar"), sliderThumbPart(slider), state, {}}, handle);
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
        const PartRef thumbPart{QStringLiteral("scrollbar"), horizontal ? 2 : 3, stateOf(SC_ScrollBarSlider), {}};
        const PartRef gripper{QStringLiteral("scrollbar"), horizontal ? 8 : 9, stateOf(SC_ScrollBarSlider), {}};
        drawPart(painter, thumbPart, thumb);
        // The gripper only when the thumb's content area is strictly longer
        // than it: Luna Silver (content margins 4+4, 8px gripper) leaves 11
        // and 16px thumbs plain and grips 17px and up (measured on XP).
        const QSize grip = trueSize(gripper);
        const QRect room = m_renderer->contentRect(thumbPart, thumb);
        if (grip.isValid() && (horizontal ? room.width() : room.height()) > (horizontal ? grip.width() : grip.height()))
            drawPart(painter, gripper, thumb);
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
        case PM_DefaultFrameWidth:
            // A themed client edge is the class's BorderSize wide (1px), not
            // the classic 2px sunken edge.
            if (const QString cls = frameClass(widget); !cls.isEmpty()) {
                bool ok = false;
                const int border = m_renderer->theme().property(cls, u"", u"", u"bordersize").value_or(QString()).toInt(&ok);
                return ok ? border : 1;
            }
            break;
        case PM_FocusFrameHMargin:
            // Item text insets (Qt's text margin is this + 1), from XP's
            // controls: 7px in a report view, 6px after a tree's branches.
            if (const int extra = itemRowExtra(widget); extra > 0 && hasPart({QStringLiteral("listview"), 0, Normal, {}}))
                return extra == 3 ? 5 : 6;
            break;
        case PM_HeaderMargin:
            // XP's header labels sit the item's content margin (Luna: 3) plus
            // comctl32's 6px in.
            if (hasPart({QStringLiteral("header"), 1, Normal, {}}))
                return m_renderer->contentMargins({QStringLiteral("header"), 1, Normal, {}}).left() + 6;
            break;
        case PM_TreeViewIndentation:
            if (hasPart({QStringLiteral("treeview"), 0, Normal, {}}))
                return 19;   // TVM_GETINDENT
            break;
        // XP puts both labels at x = 16 (a 13px box and 3px). QCommonStyle
        // places a check box's label at indicator.right() + spacing -- the
        // inclusive right edge, 12 -- but a radio button's at left + width +
        // spacing, 13; so the check box needs one more.
        case PM_CheckBoxLabelSpacing:
            return 4;
        case PM_RadioButtonLabelSpacing:
            return 3;
        case PM_ButtonShiftHorizontal:
        case PM_ButtonShiftVertical:
            return 0;   // XP's pressed buttons keep their label still
        case PM_TabBarTabShiftVertical:
        case PM_TabBarTabShiftHorizontal:
            return 0;
        case PM_TabBarBaseOverlap:
            return 0;
        case PM_TabBarTabHSpace:
            if (hasPart({QStringLiteral("tab"), 1, 1, {}}))
                return 10;   // 5px either side of the label
            break;
        case PM_TabBarTabVSpace:
            if (hasPart({QStringLiteral("tab"), 1, 1, {}}))
                return 8;    // font height 13 + 8 = 21: 2px headroom, 18px tab, 1 row over the pane
            break;
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
        case CT_ItemViewItem:
            if (const int extra = itemRowExtra(widget); extra >= 0 && hasPart({QStringLiteral("listview"), 0, Normal, {}})) {
                // XP's row heights follow the text height (13 for Tahoma 8):
                // list box 13, report list view 14, tree view 16
                // (LB_GETITEMHEIGHT, LVM_GETITEMRECT, TVM_GETITEMHEIGHT).
                QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
                const int row = xpTextHeight(widget ? widget->font() : QApplication::font()) + extra;
                if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option))
                    if (item->decorationSize.height() > row && (item->features & QStyleOptionViewItem::HasDecoration))
                        return s;   // a larger icon wins, as it does in XP
                s.setHeight(row);
                return s;
            }
            break;
        case CT_HeaderSection:
            if (hasPart({QStringLiteral("header"), 1, Normal, {}})) {
                QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
                s.setHeight(xpTextHeight(widget ? widget->font() : QApplication::font()) + 7);   // 20 for Tahoma 8
                return s;
            }
            break;
        case CT_TabBarTab:
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
                if (hasPart({QStringLiteral("tab"), 1, 1, {}})
                    && (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth)) {
                    // Label + 10 wide (TCM_GETITEMRECT: 49/70/60 for General/
                    // Appearance/Advanced in Tahoma 8), 18 tall plus the 2px
                    // headroom the selected tab rises into; the first tab 2px
                    // wider for XP's left inset.
                    QSize s = size;
                    if (tab->position == QStyleOptionTab::Beginning || tab->position == QStyleOptionTab::OnlyOneTab)
                        s.rwidth() += 2;
                    // 2px headroom, the 18px tab (text + 5), one row shared
                    // with the pane -- from XP's text height (tmHeight).
                    s.setHeight(xpTextHeight(widget ? widget->font() : QApplication::font()) + 8);
                    return s;
                }
            }
            break;
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
    if (m_renderer && hasPart({QStringLiteral("tab"), 9, 1, {}})) {
        if (const auto *tw = qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(option)) {
            if (tw->shape == QTabBar::RoundedNorth || tw->shape == QTabBar::TriangularNorth) {
                // XP: the pane starts where the tabs end (y = 20), and the
                // page sits 4px in at the sides and bottom, 2px from the top
                // (TCM_ADJUSTRECT: 4,22 - 296,116 in a 300 x 120 control).
                // The pane's top edge shares the bar's last row, under the tabs.
                const int top = qMax(0, tw->tabBarSize.height() - 1);
                const QRect pane(tw->rect.left(), tw->rect.top() + top, tw->rect.width(), tw->rect.height() - top);
                if (element == SE_TabWidgetTabBar)
                    return QRect(tw->rect.left(), tw->rect.top(), qMin(tw->tabBarSize.width(), tw->rect.width()),
                                 tw->tabBarSize.height());
                if (element == SE_TabWidgetTabPane)
                    return pane;
                if (element == SE_TabWidgetTabContents)
                    return pane.adjusted(4, 2, -4, -4);
            }
        }
    }
    if (m_renderer && element == SE_LineEditContents) {
        const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
        if (frame && frame->lineWidth > 0 && hasPart({QStringLiteral("edit"), 1, Normal, {}})) {
            // XP's single-line Edit (EM_GETRECT, Luna, Tahoma 8): text 4px in
            // from the 2px client edge and top-aligned 1px down -- not
            // centred. QLineEdit adds 2px of its own and centres within this
            // rect, so hand it a rect exactly one text line tall.
            const QRect r = frame->rect;
            const int line = xpTextHeight(widget ? widget->font() : QApplication::font());
            return QRect(r.left() + 4, r.top() + 3, qMax(0, r.width() - 8), qMin(line, qMax(0, r.height() - 3)));
        }
    }
    if (m_renderer && element == SE_HeaderLabel && hasPart({QStringLiteral("header"), 1, Normal, {}})) {
        // The header margin is horizontal only (QCommonStyle would take it
        // off the height too, clipping the label).
        const int m = proxy()->pixelMetric(PM_HeaderMargin, option, widget);
        return option->rect.adjusted(m, 0, -m, 0);
    }
    if (m_renderer && element == SE_ItemViewItemText && itemRowExtra(widget) == 0) {
        // XP's list box draws its labels 1px lower within their row.
        return QProxyStyle::subElementRect(element, option, widget).translated(0, 1);
    }
    if (m_renderer && (element == SE_ProgressBarContents || element == SE_ProgressBarGroove)
        && hasPart({QStringLiteral("progress"), 1, 1, {}})) {
        // The chunks are laid out over the groove's own content rect, so
        // Contents is the whole groove -- not the classic inset one. The
        // groove leaves room for a label beside it, as the classic base does.
        return QProxyStyle::subElementRect(SE_ProgressBarGroove, option, widget);
    }
    return QProxyStyle::subElementRect(element, option, widget);
}

// How much taller than the text XP's rows are in this item view: a tree
// view (decorated root) +3, a report-style list (a QTreeView without root
// decoration, or a table) +1, a plain list box 0; -1 when unknown.
int XPLasmaStyle::itemRowExtra(const QWidget *widget) const
{
    for (const QWidget *w = widget; w; w = w->parentWidget()) {
        if (w->inherits("QTreeView"))
            return w->property("rootIsDecorated").toBool() ? 3 : 1;
        if (w->inherits("QTableView"))
            return 1;
        if (w->inherits("QListView"))
            return 0;
        if (w->inherits("QAbstractItemView"))
            return -1;
    }
    return -1;
}

// The theme class that frames `widget`'s client edge, or empty for the
// classic edge. (Phase 4 grows this into the full context resolver.)
QString XPLasmaStyle::frameClass(const QWidget *widget) const
{
    if (!widget || !m_renderer)
        return QString();
    // The same reading as itemRowExtra(): a tree with a decorated root is a
    // TreeView, an undecorated tree or a table is a report-style ListView,
    // a plain list a ListBox.
    QString cls;
    if (widget->inherits("QTreeView"))
        cls = widget->property("rootIsDecorated").toBool() ? QStringLiteral("treeview") : QStringLiteral("listview");
    else if (widget->inherits("QTableView") || widget->inherits("QColumnView"))
        cls = QStringLiteral("listview");
    else if (widget->inherits("QListView"))
        cls = QStringLiteral("listbox");
    else if (widget->inherits("QTextEdit") || widget->inherits("QPlainTextEdit"))
        cls = QStringLiteral("edit");
    if (cls.isEmpty() || !hasPart({cls, cls == QLatin1String("edit") ? 1 : 0, Normal, {}}))
        return QString();
    return cls;
}

// XP's tree branches (TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT),
// measured against the real control (Luna Silver, 16px rows, indent 19):
// each level's branch centre is its column's left + 13, the row's middle
// + 1; lines are GrayText dots where x + y is odd; the row's line runs 8px
// right of the centre; the vertical runs down to a next sibling and up to a
// previous item -- never above the first root. The expand box is TVP_GLYPH,
// centred on the branch.
void XPLasmaStyle::drawTreeBranch(const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    const QRect r = option->rect;
    const int cx = r.width() >= 19 ? r.left() + 13 : r.center().x();
    const int cy = r.top() + r.height() / 2 + 1;
    const QColor colour = sysColour(u"graytext", option->palette.color(QPalette::Disabled, QPalette::Text));
    auto dotsH = [&](int x0, int x1, int y) {
        for (int x = x0; x <= x1; ++x)
            if ((x + y) & 1)
                painter->fillRect(x, y, 1, 1, colour);
    };
    auto dotsV = [&](int x, int y0, int y1) {
        for (int y = y0; y <= y1; ++y)
            if ((x + y) & 1)
                painter->fillRect(x, y, 1, 1, colour);
    };
    if (option->state & State_Item) {
        dotsH(cx, cx + 8, cy);
        // Up to the previous item: a sibling above, or the parent. The
        // option doesn't say, so ask the view about this row's index.
        bool above = true;
        if (const auto *view = qobject_cast<const QAbstractItemView *>(widget)) {
            const QModelIndex index = view->indexAt(QPoint(r.right() + 2, cy));
            above = !index.isValid() || index.row() > 0 || index.parent().isValid();
        }
        if (above)
            dotsV(cx, r.top(), cy);
        if (option->state & State_Sibling)
            dotsV(cx, cy, r.bottom());
    } else if (option->state & State_Sibling) {
        dotsV(cx, r.top(), r.bottom());   // an ancestor's line passing through
    }
    if (option->state & State_Children) {
        const PartRef glyph{QStringLiteral("treeview"), 2, (option->state & State_Open) ? 2 : 1, {}};
        const QSize size = trueSize(glyph).isValid() ? trueSize(glyph) : QSize(9, 9);
        drawPart(painter, glyph, QRect(cx - size.width() / 2, cy - size.height() / 2, size.width(), size.height()));
    }
}

// GDI's tmHeight for a font, computed as GDI does: the OS/2 table's
// usWinAscent and usWinDescent scaled to the pixel size, each rounded
// (Tahoma 8 at 96 DPI: 11 + 2 = 13). Neither Qt's metrics will do: Qt 5
// rounds the descent up (14) even for an unhinted QRawFont, while Qt 6
// happens to agree (13).
int XPLasmaStyle::xpTextHeight(const QFont &font) const
{
    const QRawFont raw = QRawFont::fromFont(font);
    const QByteArray os2 = raw.isValid() ? raw.fontTable("OS/2") : QByteArray();
    if (os2.size() >= 78 && raw.unitsPerEm() > 0) {
        const auto *p = reinterpret_cast<const uchar *>(os2.constData());
        const int winAscent = p[74] << 8 | p[75];
        const int winDescent = p[76] << 8 | p[77];
        const qreal scale = raw.pixelSize() / raw.unitsPerEm();
        return qRound(winAscent * scale) + qRound(winDescent * scale);
    }
    const QFontMetrics fm(font);
    return fm.ascent() + fm.descent();
}

// A north tab's rect as XP lays it out, inside Qt's: 2px down (the tab bar
// is 20px tall), and for the first tab 2px in from the left (Qt's first tab
// is 2px wider to make room, so every later tab lands where XP puts it).
QRect XPLasmaStyle::xpTabRect(const QStyleOptionTab *tab) const
{
    // The bar is 21px: 2px headroom, the 18px tab, and one row shared with
    // the pane's top edge, which only the selected tab covers.
    QRect r(tab->rect.left(), tab->rect.top() + 2, tab->rect.width(), qMax(0, tab->rect.height() - 3));
    if (tab->position == QStyleOptionTab::Beginning || tab->position == QStyleOptionTab::OnlyOneTab)
        r.setLeft(r.left() + 2);
    return r;
}

// TKP_THUMB*: the pointed thumb faces its tick marks; TKP_THUMB(VERT) when
// there are ticks on both sides or none.
int XPLasmaStyle::sliderThumbPart(const QStyleOptionSlider *slider) const
{
    const bool horizontal = slider->orientation == Qt::Horizontal;
    if (horizontal)
        return slider->tickPosition == QSlider::TicksBelow ? 4 : slider->tickPosition == QSlider::TicksAbove ? 5 : 3;
    return slider->tickPosition == QSlider::TicksLeft ? 7 : slider->tickPosition == QSlider::TicksRight ? 8 : 6;
}

// comctl32 sizes the thumb from the system metrics, not the image: 4/3 of
// the scroll bar thickness across, half that along (Luna: 22 x 11 around an
// 11 x 21 image), shrinking to the client's size - 6 when that is smaller
// (a 21px-tall trackbar gets a 7 x 15 thumb). The image is drawn inside.
QSize XPLasmaStyle::sliderThumbSize(const QStyleOptionSlider *slider) const
{
    const bool horizontal = slider->orientation == Qt::Horizontal;
    int across = sysMetric(horizontal ? u"scrollbarheight" : u"scrollbarwidth", 17) * 4 / 3;
    const int room = horizontal ? slider->rect.height() : slider->rect.width();
    if (room - 2 < across)
        across = qMax(1, room - 6);
    return horizontal ? QSize(across / 2, across) : QSize(across, across / 2);
}

// comctl32's trackbar layout, measured on XP (Luna Silver, widths 60-200,
// heights 21-45, values 0/40/100): the thumb at its true size, 2px in from
// the edge its point faces away from; a 4px channel running from
// thumb/2 + 3 to the same distance from the far end, (thumb - 6)/2 into the
// thumb; the thumb placed at MulDiv(value, channel - thumb, range).
static QRect sliderLayout(const QStyleOptionSlider *slider, QSize thumb, QRect *channel)
{
    const bool horizontal = slider->orientation == Qt::Horizontal;
    const QRect r = slider->rect;
    const int along = horizontal ? thumb.width() : thumb.height();
    const int across = horizontal ? thumb.height() : thumb.width();
    const int inset = along / 2 + 3;
    const int length = (horizontal ? r.width() : r.height()) - 2 * inset;
    const bool pointsBack = horizontal ? slider->tickPosition == QSlider::TicksAbove
                                       : slider->tickPosition == QSlider::TicksLeft;
    const int crossStart = pointsBack ? (horizontal ? r.height() : r.width()) - 2 - across : 2;
    const int channelCross = crossStart + (across - 6) / 2;
    const qint64 range = qint64(slider->maximum) - slider->minimum;
    const qint64 value = slider->upsideDown ? qint64(slider->maximum) - slider->sliderPosition
                                            : qint64(slider->sliderPosition) - slider->minimum;
    const int offset = range > 0 ? int((value * (length - along) + range / 2) / range) : 0;
    if (horizontal) {
        *channel = QRect(r.left() + inset, r.top() + channelCross, length, 4);
        return QRect(r.left() + inset + offset, r.top() + crossStart, thumb.width(), thumb.height());
    }
    // Vertical sliders run bottom (minimum) to top, as XP's do by default.
    *channel = QRect(r.left() + channelCross, r.top() + inset, 4, length);
    return QRect(r.left() + crossStart, r.top() + inset + (length - along) - offset, thumb.width(), thumb.height());
}

void XPLasmaStyle::drawSliderTicks(const QStyleOptionSlider *slider, const QRect &channel, const QRect &handle,
                                   QPainter *painter) const
{
    // In the Tics part's colour; the two end ticks 4px long, the rest 3,
    // starting 1px past the thumb, one per tickInterval at the thumb's
    // centre for that value.
    const bool horizontal = slider->orientation == Qt::Horizontal;
    const QColor colour = themeColour({QStringLiteral("trackbar"), horizontal ? 9 : 10, 1, {}}, u"color",
                                      slider->palette.color(QPalette::WindowText));
    const qint64 range = qint64(slider->maximum) - slider->minimum;
    if (range <= 0)
        return;
    const int interval = slider->tickInterval > 0 ? slider->tickInterval : qMax(1, slider->pageStep);
    const int along = horizontal ? handle.width() : handle.height();
    const int span = (horizontal ? channel.width() : channel.height()) - along;
    const int start = (horizontal ? channel.left() : channel.top()) + along / 2;
    // One pixel clear of the thumb rect's (exclusive) far edge.
    const int from = horizontal ? handle.bottom() + 2 : handle.right() + 2;
    for (qint64 v = 0; v <= range; v += interval) {
        const bool end = v == 0 || v + interval > range;
        const int pos = start + int((v * span + range / 2) / range);
        const int at = horizontal ? pos : start + span - (pos - start);
        const int len = end ? 4 : 3;
        painter->fillRect(horizontal ? QRect(at, from, 1, len) : QRect(from, at, len, 1), colour);
        if (v + interval > range && v != range) {
            // The last tick sits at the maximum even off the interval.
            const int endAt = horizontal ? start + span : start;
            painter->fillRect(horizontal ? QRect(endAt, from, 1, 4) : QRect(from, endAt, 4, 1), colour);
        }
    }
}

QRect XPLasmaStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub,
                                   const QWidget *widget) const
{
    if (m_renderer && control == CC_Slider) {
        if (const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            const QSize thumb = sliderThumbSize(slider);
            if (thumb.isValid() && hasPart({QStringLiteral("trackbar"), slider->orientation == Qt::Horizontal ? 1 : 2, 1, {}})) {
                QRect channel;
                const QRect handle = sliderLayout(slider, thumb, &channel);
                if (sub == SC_SliderHandle)
                    return handle;
                if (sub == SC_SliderGroove)
                    return channel;
                if (sub == SC_SliderTickmarks)
                    return slider->rect;
            }
        }
    }
    if (m_renderer && control == CC_ComboBox && hasPart({QStringLiteral("combobox"), 1, 1, {}})) {
        if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
            // XP's own geometry (GetComboBoxInfo, 150 x 21, Luna): the button
            // is a scroll bar's width inside the 1px border (132,1 - 149,20),
            // the field 3px in from the edges and 2px short of the button
            // (3,3 - 130,18).
            const int w = sysMetric(u"scrollbarwidth", 17);
            const QRect r = combo->rect;
            const QRect arrow(r.right() - w, r.top() + 1, w, r.height() - 2);
            if (sub == SC_ComboBoxArrow)
                return visualRect(combo->direction, r, arrow);
            if (sub == SC_ComboBoxEditField) {
                // An editable combo's line edit sits 1px further in and up,
                // so its text lands where XP's does.
                const int dx = combo->editable ? 4 : 3, dy = combo->editable ? 2 : 3;
                return visualRect(combo->direction, r,
                                  QRect(r.left() + dx, r.top() + dy, arrow.left() - 2 - (r.left() + dx), r.height() - 6));
            }
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
    case SH_EtchDisabledText:
        // Etched (white-shadowed) disabled text is classic Windows; XP's
        // themed controls draw it flat in the part's disabled TextColor.
        if (m_renderer)
            return 0;
        break;
    default:
        break;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}
