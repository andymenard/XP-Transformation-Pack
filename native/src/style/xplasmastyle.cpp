// SPDX-License-Identifier: GPL-3.0-or-later
#include "xplasmastyle.h"

#include "core/schema.h"
#include "core/themefile.h"

#include <QAbstractButton>
#include <cstring>
#include <cmath>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include <QTimer>
#include <QtEndian>
#include <QScreen>
#include <QWindow>
#include <QFileSystemWatcher>
#include <QMenu>
#include <QPainter>
#include <qdrawutil.h>
#include <QRawFont>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QScreen>
#include "core/crispfont.h"
#include <QSlider>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QStackedWidget>
#include <QDialog>
#include <QToolBar>
#include <QMdiArea>
#include <QMdiSubWindow>
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

// xplasmarc: in the config folder -- or, in a Flatpak sandbox, whose config
// folder is the app's own, the copy XPlasma keeps beside the frozen look
// (HOST_XDG_DATA_HOME, else ~/.local/share, /xplasma/active), the folder its
// Flatpak permission shows every app, live. That one first there: an
// xplasmarc in the app's own folder is at most a stand-in Flatpak left.
QString rcPath()
{
    const QString own = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/xplasmarc");
    if (!QFileInfo::exists(QStringLiteral("/.flatpak-info")))
        return own;
    const QString shared = qEnvironmentVariable("HOST_XDG_DATA_HOME", QDir::homePath() + QLatin1String("/.local/share"))
                           + QLatin1String("/xplasma/active/xplasmarc");
    return QFileInfo::exists(shared) ? shared : own;
}

// Where XPlasma's configuration says the style to draw is. XPLASMA_THEME
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
    QSettings settings(rcPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Theme"));
    return {settings.value(QStringLiteral("Path")).toString(), settings.value(QStringLiteral("Variant")).toString()};
}

// Windows Classic: the colour scheme a .theme holds. [XPlasma] ScrollWidth
// overrides NonclientMetrics' scroll bar size (what the Python app writes).
std::optional<xpl::ClassicScheme> loadScheme(const QString &path)
{
    const auto file = xpl::ThemeFile::load(path);
    if (!file || !file->hasSection(u"Control Panel\\Colors"))
        return std::nullopt;
    xpl::ClassicScheme scheme = xpl::ClassicScheme::fromThemeFile(*file);
    bool ok = false;
    if (const int v = file->value(u"XPlasma", u"ScrollWidth").toInt(&ok); ok && v > 0 && v < 512)
        scheme.metrics.scrollWidth = scheme.metrics.scrollHeight = v;
    return scheme;
}

} // namespace

XPlasmaStyle::XPlasmaStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Windows")))
{
    setObjectName(QStringLiteral("XPlasma"));
    m_safe = qEnvironmentVariableIntValue("XPLASMA_SAFE") != 0;
    m_cache.setMaxCost(16 * 1024 * 1024);   // bytes of rendered parts
    if (m_safe)
        return;
    loadTheme();
    // XPLASMA_DUMP=DIR: a testing aid -- after XPLASMA_DUMP_DELAY ms (3000),
    // save every visible top-level widget to DIR, so real apps can be
    // inspected offscreen.
    if (const QString dump = qEnvironmentVariable("XPLASMA_DUMP"); !dump.isEmpty()) {
        const int delay = qEnvironmentVariableIsSet("XPLASMA_DUMP_DELAY") ? qEnvironmentVariableIntValue("XPLASMA_DUMP_DELAY") : 3000;
        QTimer::singleShot(delay, this, [dump] {
            QDir().mkpath(dump);
            int n = 0;
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->isVisible() && w->width() > 1 && w->height() > 1)
                    w->grab().save(QStringLiteral("%1/%2-%3.png").arg(dump, QCoreApplication::applicationName()).arg(n++));
            // QtQuick windows (qqc2-desktop-style draws through us with no
            // widget): from the screen, e.g. offscreen with QT_QUICK_BACKEND=software.
            for (QWindow *w : QGuiApplication::topLevelWindows())
                if (w->isVisible() && !w->inherits("QWidgetWindow") && w->screen())
                    w->screen()->grabWindow(w->winId()).save(
                        QStringLiteral("%1/%2-%3.png").arg(dump, QCoreApplication::applicationName()).arg(n++));
        });
    }
    // Live: follow xplasmarc (unless the environment pins a theme). The app
    // replaces the file atomically, so the watch is on the file and re-armed
    // after each change; a short delay folds bursts together.
    if (qEnvironmentVariableIsEmpty("XPLASMA_THEME")) {
        m_configPath = rcPath();
        m_loaded = configuredTheme();
    m_crispEnabled = QSettings(rcPath(), QSettings::IniFormat).value(QStringLiteral("Text/Crisp"),false).toBool();
        m_watcher = new QFileSystemWatcher(this);
        m_watcher->addPath(QFileInfo(m_configPath).absolutePath());
        if (QFileInfo::exists(m_configPath))
            m_watcher->addPath(m_configPath);
        m_reloadTimer = new QTimer(this);
        m_reloadTimer->setSingleShot(true);
        m_reloadTimer->setInterval(150);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, m_reloadTimer, qOverload<>(&QTimer::start));
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
            if (QFileInfo::exists(m_configPath) && !m_watcher->files().contains(m_configPath))
                m_watcher->addPath(m_configPath);
            m_reloadTimer->start();
        });
        connect(m_reloadTimer, &QTimer::timeout, this, &XPlasmaStyle::reloadIfChanged);
    }
}

// xplasmarc changed: when it names another theme, load it, drop what was
// rendered from the old one, and repolish every widget so metrics, palettes
// and pixels all follow -- no app restart.
void XPlasmaStyle::reloadIfChanged()
{
    if (QFileInfo::exists(m_configPath) && !m_watcher->files().contains(m_configPath))
        m_watcher->addPath(m_configPath);
    const auto now = configuredTheme();
    const bool crisp=QSettings(rcPath(), QSettings::IniFormat).value(QStringLiteral("Text/Crisp"),false).toBool();
    if (now == m_loaded && crisp == m_crispEnabled)
        return;
    m_crispEnabled=crisp;
    if(qApp)unkern(qApp);
    m_loaded = now;
    m_renderer.reset();
    m_scheme.reset();
    loadTheme();
    m_cache.clear();
    if (!qApp)
        return;
    const QList<QWidget *> widgets = QApplication::allWidgets();
    for (QWidget *w : widgets)
        if (w->style() == this || w->style() == qApp->style()) {
            unpolish(w);
            polish(w);
        }
    for (QWidget *w : widgets) {
        QEvent change(QEvent::StyleChange);
        QCoreApplication::sendEvent(w, &change);
        w->updateGeometry();
        w->update();
    }
}

void XPlasmaStyle::loadTheme()
{
    const auto [path, variant] = configuredTheme();
    if (path.isEmpty())
        return;
    if (path.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive)) {
        // Windows Classic: no visual style, a colour scheme.
        m_scheme = loadScheme(path);
        if (!m_scheme)
            qWarning("XPlasma: no [Control Panel\\Colors] in %s", qPrintable(path));
        return;
    }
    QString error;
    if (auto theme = xpl::Theme::load(path, variant, &error))
        m_renderer = std::make_unique<xpl::Renderer>(theme);
    else
        qWarning("XPlasma: can't load %s (%s); drawing classic", qPrintable(path), qPrintable(error));
}

// --- drawing parts -----------------------------------------------------------

// --- context -------------------------------------------------------------------

// The subclass (uxtheme's SetWindowTheme app name, the `Sub` of
// [Sub::Class]) the thing being drawn asks for: an `xplasma-class` property
// on the widget or an ancestor -- or, for QtQuick controls (no widget), on
// the option's style object or its parents (where QML can declare it as
// `xplasmaClass`). Parts the theme has no [Sub::Class] section for fall
// back to the plain class, as in uxtheme.
QString XPlasmaStyle::contextClass(const QWidget *widget, const QStyleOption *option) const
{
    // (Widgets only once some widget has set the property: the walk costs
    // a meta-object lookup per ancestor on every call.)
    if (m_anyContext)
        for (const QWidget *w = widget; w; w = w->parentWidget())
            if (const QVariant v = w->property("xplasma-class"); v.isValid())
                return v.toString().toLower();
    if (!widget && option)
        for (const QObject *o = option->styleObject; o; o = o->parent())
            for (const char *name : {"xplasma-class", "xplasmaClass"})
                if (const QVariant v = o->property(name); v.isValid() && !v.toString().isEmpty())
                    return v.toString().toLower();
    return QString();
}

PartRef XPlasmaStyle::inContext(const PartRef &ref) const
{
    if (!ref.sub.isEmpty() || m_context.isEmpty())
        return ref;
    PartRef r = ref;
    r.sub = m_context;
    return r;
}

XPlasmaStyle::ContextScope::ContextScope(const XPlasmaStyle *style, const QWidget *widget, const QStyleOption *option)
    : m_style(style), m_saved(style->m_context)
{
    style->m_context = style->m_renderer ? style->contextClass(widget, option) : QString();
}

XPlasmaStyle::ContextScope::~ContextScope()
{
    m_style->m_context = m_saved;
}

bool XPlasmaStyle::hasPart(const PartRef &ref) const
{
    return m_renderer && m_renderer->hasPart(inContext(ref));
}

bool XPlasmaStyle::drawPart(QPainter *painter, const PartRef &ref, const QRect &rect) const
{
    if (!hasPart(ref) || rect.isEmpty())
        return false;
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QImage *image = partImage(ref, rect.size(), dpr);
    if (!image)
        return false;
    painter->drawImage(rect.topLeft(), *image);
    return true;
}

// `ref` rendered at `size` (device-independent), cached.
const QImage *XPlasmaStyle::partImage(const PartRef &ref, QSize logical, qreal dpr) const
{
    const QSize size = (QSizeF(logical) * dpr).toSize();
    const int dpi = qRound(96 * dpr);
    const QString key = QStringLiteral("%1|%2|%3|%4|%5x%6@%7")
                            .arg(inContext(ref).sub, ref.cls).arg(ref.part).arg(ref.state)
                            .arg(size.width()).arg(size.height()).arg(dpi);
    QImage *image = m_cache.object(key);
    if (!image) {
        // XP scrollbar geometry is expressed in logical 96-DPI pixels.
        // Feeding a physical HiDPI rectangle to the theme triggers its
        // TrueSizeScalingType=Size arrow enlargement while leaving the grip
        // and nine-slice borders at their original physical pixel sizes.
        // Render the complete part at its logical size, then scale all its
        // details together, preserving the original bitmap pixel edges.
        if (ref.cls == QLatin1String("scrollbar")) {
            const QImage logicalImage = m_renderer->background(inContext(ref), logical, 96);
            image = new QImage(logicalImage.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation));
        } else {
            image = new QImage(m_renderer->background(inContext(ref), size, dpi));
        }
        image->setDevicePixelRatio(dpr);
        m_cache.insert(key, image, int(qMin<qint64>(image->sizeInBytes(), INT_MAX)));
        image = m_cache.object(key);
    }
    return image;
}

QSize XPlasmaStyle::trueSize(const PartRef &ref) const
{
    return m_renderer ? m_renderer->partSize(inContext(ref), xpl::PartSize::True) : QSize();
}

QColor XPlasmaStyle::themeColour(const PartRef &ref, QStringView property, const QColor &fallback) const
{
    QRgb rgb;
    if (m_renderer && xpl::parseColour(m_renderer->property(inContext(ref), property).value_or(QString()), &rgb))
        return QColor(rgb);
    return fallback;
}

QColor XPlasmaStyle::sysColour(QStringView name, const QColor &fallback) const
{
    if (classic())
        return xpl::ClassicScheme::colourIndex(name) >= 0 ? m_scheme->colour(name) : fallback;
    QRgb rgb;
    if (m_renderer && xpl::parseColour(m_renderer->theme().property(u"sysmetrics", u"", u"", name).value_or(QString()), &rgb))
        return QColor(rgb);
    return fallback;
}

// A SysMetrics font spec ("Trebuchet MS, 10, bold").
static QFont fontFromMetric(const QString &spec, const QFont &fallback)
{
    const QStringList parts = spec.split(QLatin1Char(','));
    if (parts.size() < 2)
        return fallback;
    QFont font(parts.at(0).trimmed());
    font.setPointSize(parts.at(1).trimmed().toInt());
    for (int i = 2; i < parts.size(); ++i) {
        const QString flag = parts.at(i).trimmed().toLower();
        if (flag == QLatin1String("bold"))
            font.setBold(true);
        else if (flag == QLatin1String("italic"))
            font.setItalic(true);
    }
    return font;
}

// A SysMetrics font ("captionfont"...): the style's, or the scheme's.
QFont XPlasmaStyle::sysFont(QStringView name, const QFont &fallback) const
{
    if (classic()) {
        const xpl::NonClientMetrics &m = m_scheme->metrics;
        const xpl::LogFont *lf = name == u"captionfont"        ? &m.captionFont
                                 : name == u"smallcaptionfont" ? &m.smCaptionFont
                                 : name == u"menufont"         ? &m.menuFont
                                 : name == u"statusfont"       ? &m.statusFont
                                 : name == u"msgboxfont"       ? &m.messageFont
                                 : name == u"iconfont"         ? &m_scheme->icons.font
                                                               : nullptr;
        if (!lf || lf->face.isEmpty())
            return fallback;
        QFont font(lf->face);
        font.setPointSize(lf->points());
        font.setBold(lf->bold());
        return font;
    }
    QString spec;
    if (m_renderer)
        spec = m_renderer->theme().property(u"sysmetrics", u"", u"", name).value_or(QString());
    return fontFromMetric(spec, fallback);
}

int XPlasmaStyle::sysMetric(QStringView name, int fallback) const
{
    if (classic()) {
        const xpl::NonClientMetrics &m = m_scheme->metrics;
        const int v = name == u"sizingborderwidth"    ? m.borderWidth
                      : name == u"scrollbarwidth"     ? m.scrollWidth
                      : name == u"scrollbarheight"    ? m.scrollHeight
                      : name == u"captionbarwidth"    ? m.captionWidth
                      : name == u"captionbarheight"   ? m.captionHeight
                      : name == u"smcaptionbarwidth"  ? m.smCaptionWidth
                      : name == u"smcaptionbarheight" ? m.smCaptionHeight
                      : name == u"menubarwidth"       ? m.menuWidth
                      : name == u"menubarheight"      ? m.menuHeight
                                                      : 0;
        return v > 0 ? v : fallback;
    }
    bool ok = false;
    const int v = m_renderer ? m_renderer->theme().property(u"sysmetrics", u"", u"", name).value_or(QString()).toInt(&ok) : 0;
    return ok && v > 0 ? v : fallback;
}

// --- palette and polish -----------------------------------------------------------

QPalette XPlasmaStyle::standardPalette() const
{
    if (!m_renderer && !m_scheme)
        return QProxyStyle::standardPalette();
    // XP's system colours from the style. (In a Plasma session the colour
    // scheme XPlasma generates from the same SysMetrics is what apps use.)
    const QColor face = sysColour(u"btnface", QColor(236, 233, 216));
    const QColor window = sysColour(u"window", Qt::white);
    const QColor text = sysColour(u"windowtext", Qt::black);
    const QColor gray = sysColour(u"graytext", QColor(172, 168, 153));
    QPalette pal(text, face, sysColour(u"btnhighlight", Qt::white),
                 sysColour(u"btnshadow", QColor(172, 168, 153)), face, text, text, window, face);
    // Window text for check box and radio labels, button text for buttons,
    // headers, tabs and arrows -- as XP's DCs have them.
    pal.setColor(QPalette::ButtonText, sysColour(u"btntext", text));
    // The 3D colours: the classic bevels' outer shadow and inner light.
    pal.setColor(QPalette::Shadow, sysColour(u"dkshadow3d", QColor(113, 111, 100)));
    pal.setColor(QPalette::Midlight, sysColour(u"light3d", face));
    pal.setColor(QPalette::Highlight, sysColour(u"highlight", QColor(49, 106, 197)));
    pal.setColor(QPalette::HighlightedText, sysColour(u"highlighttext", Qt::white));
    // Links as SysLink draws them: the hot-tracking colour (COLOR_HOTLIGHT).
    pal.setColor(QPalette::Link, sysColour(u"hottracking", QColor(0, 0, 128)));
    pal.setColor(QPalette::ToolTipBase, sysColour(u"infobk", QColor(255, 255, 225)));
    pal.setColor(QPalette::ToolTipText, sysColour(u"infotext", Qt::black));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, gray);
    return pal;
}

void XPlasmaStyle::polish(QPalette &palette)
{
    QProxyStyle::polish(palette);
}

void XPlasmaStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    // Hot tracking, as XP had: these controls redraw on hover.
    if (qobject_cast<QAbstractButton *>(widget) || qobject_cast<QComboBox *>(widget)
        || qobject_cast<QScrollBar *>(widget) || qobject_cast<QSlider *>(widget) || qobject_cast<QTabBar *>(widget)
        || widget->inherits("QAbstractSpinBox") || widget->inherits("QHeaderView"))
        widget->setAttribute(Qt::WA_Hover, true);
    if (!m_renderer && !classic())
        return;
    // Context menus use XP's menu font even when an application has its own
    // larger document/UI font. Keep the setting local to the popup.
    if (qobject_cast<QMenu *>(widget)) {
        if (!widget->property("_xplasmaMenuFont").isValid())
            widget->setProperty("_xplasmaMenuFont", widget->font());
        QFont font = sysFont(u"menufont", QFont(QStringLiteral("Tahoma"), 8));
        font.setKerning(false);
        if(m_crispEnabled)font=xpl::crispUiFont(font,widget->devicePixelRatioF(),widget->logicalDpiY());
        widget->setFont(font);
    }
    // The MDI workspace: COLOR_APPWORKSPACE.
    if (auto *area = qobject_cast<QMdiArea *>(widget); area && (classic() || hasPart({QStringLiteral("window"), 1, 1, {}})))
        area->setBackground(sysColour(u"appworkspace", QColor(128, 128, 128)));
    // The Places bar's checked button keeps the text colour.
    if (auto *view = qobject_cast<QAbstractItemView *>(widget); view && isPlacesBar(view)) {
        QPalette pal = view->palette();
        for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive})
            pal.setColor(g, QPalette::HighlightedText, pal.color(g, QPalette::Text));
        view->setPalette(pal);
        view->setAttribute(Qt::WA_Hover, true);
        view->viewport()->setAttribute(Qt::WA_Hover, true);
    }
    // XP's status bar is the text height + 10 (23 for Tahoma 8) whether or
    // not it has a size grip; QStatusBar's own layout would give 18.
    // QStatusBar rebuilds its layout (and with it its minimum size) on its
    // own schedule, so this is re-applied whenever it lays itself out.
    if (qobject_cast<QStatusBar *>(widget) && hasPart({QStringLiteral("status"), 0, 0, {}})) {
        widget->setMinimumHeight(xpTextHeight(widget->font()) + 10);
        widget->installEventFilter(this);
    }
    // XP's tooltip is the text + 4 tall; QTipLabel adds a row of its own
    // for fonts like Tahoma 8 (descent 2, ascent 11), which this takes back.
    if (widget->inherits("QTipLabel")) {
        const QFontMetrics fm(widget->font());
        widget->setContentsMargins(0, 0, 0, (fm.descent() == 2 && fm.ascent() >= 11) ? -1 : 0);
    }
}

// XP's keyboard cues: mnemonic underlines stay hidden until Alt is pressed
// (SPI_GETKEYBOARDCUES off, XP's default); focus rectangles until focus moves
// by keyboard (Qt's State_KeyboardFocusChange).
void XPlasmaStyle::polish(QApplication *app)
{
    QProxyStyle::polish(app);
    app->installEventFilter(this);
    unkern(app);
}

// XP's text (xplasmarc [Text] Kerning=false, which XPlasma's apply writes):
// GDI never kerned, and fontconfig, which sets the rest of XP's text up,
// can't tell Qt not to. The application font goes unkerned -- again after
// the platform theme hands out new fonts.
void XPlasmaStyle::unkern(QApplication *app)
{
    QFont font=app->font();
    const bool pixel=font.family()==QStringLiteral("MS Sans Serif") || font.family()==QStringLiteral("MS Sans Serif Bold");
    if(m_crispEnabled) {
        if(!pixel)app->setProperty("_xplasmaSmoothUiFont",font);
        const QFont original=app->property("_xplasmaSmoothUiFont").value<QFont>();
        QScreen *screen=app->primaryScreen();
        font=xpl::crispUiFont(original,screen?screen->devicePixelRatio():1.0,screen?screen->logicalDotsPerInchY():96.0);
    } else if(pixel && app->property("_xplasmaSmoothUiFont").isValid()) {
        font=app->property("_xplasmaSmoothUiFont").value<QFont>();
        app->setProperty("_xplasmaSmoothUiFont",QVariant());
    }
    if(!QSettings(rcPath(),QSettings::IniFormat).value(QStringLiteral("Text/Kerning"),true).toBool())font.setKerning(false);
    if(font!=app->font())app->setFont(font);
}

void XPlasmaStyle::unpolish(QApplication *app)
{
    app->removeEventFilter(this);
    if(app->property("_xplasmaSmoothUiFont").isValid()) {
        app->setFont(app->property("_xplasmaSmoothUiFont").value<QFont>());
        app->setProperty("_xplasmaSmoothUiFont",QVariant());
    }
    QProxyStyle::unpolish(app);
}

bool XPlasmaStyle::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ApplicationFontChange && watched == qApp)
        QTimer::singleShot(0, this, [this] { unkern(qApp); });
    if (event->type() == QEvent::DynamicPropertyChange && !m_anyContext
        && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "xplasma-class") {
        m_anyContext = true;
        if (auto *w = qobject_cast<QWidget *>(watched))
            w->update();
    }
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
        || event->type() == QEvent::ApplicationStateChange || event->type() == QEvent::WindowDeactivate) {
        bool cues = m_cues;
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Alt && !key->isAutoRepeat())
                cues = event->type() == QEvent::KeyPress;
        } else {
            cues = false;
        }
        if (cues != m_cues) {
            m_cues = cues;
            if (QWidget *window = QApplication::activeWindow()) {
                window->update();
                for (QWidget *w : window->findChildren<QWidget *>())
                    w->update();
            }
        }
    }
    // A disabled list view or tree view: the button face behind its items,
    // as comctl32 fills them (a list box keeps the window colour).
    if (event->type() == QEvent::Paint && (m_renderer || classic()) && watched->isWidgetType()) {
        auto *viewport = static_cast<QWidget *>(watched);
        if (auto *view = qobject_cast<QAbstractItemView *>(viewport->parentWidget());
            view && view->viewport() == viewport && !view->isEnabled()) {
            if (const int extra = itemRowExtra(view); extra == 1 || extra == 3) {
                QPainter p(viewport);
                p.fillRect(static_cast<QPaintEvent *>(event)->rect(), sysColour(u"btnface", view->palette().color(QPalette::Button)));
            }
        }
    }
    // The Places bar: its background across the whole panel (the viewport
    // shows its share of it).
    if (event->type() == QEvent::Paint && m_renderer && watched->isWidgetType()) {
        auto *viewport = static_cast<QWidget *>(watched);
        if (auto *view = qobject_cast<QAbstractItemView *>(viewport->parentWidget());
            view && view->viewport() == viewport && isPlacesBar(view)) {
            QPainter p(viewport);
            p.setClipRegion(static_cast<QPaintEvent *>(event)->region());
            p.translate(-viewport->mapTo(view, QPoint(0, 0)));
            drawPart(&p, {QStringLiteral("toolbar"), 0, 0, QStringLiteral("placesbar")}, view->rect());
        }
    }
    // A property sheet's page: a tab widget's page in a dialog is painted
    // with the tab body (TABP_BODY), as EnableThemeDialogTexture has it.
    if (event->type() == QEvent::Paint && m_renderer && watched->isWidgetType()) {
        auto *page = static_cast<QWidget *>(watched);
        // (A pattern brush of the body at its true size, from the page's
        // corner: Luna's 600px gradient shows only its top on most pages.)
        const PartRef body{QStringLiteral("tab"), 10, 0, {}};
        if (const QSize tile = trueSize(body); tile.isValid() && !tile.isEmpty() && isTabBody(page)) {
            if (const QImage *image = partImage(body, tile, page->devicePixelRatioF())) {
                QPainter p(page);
                p.setClipRegion(static_cast<QPaintEvent *>(event)->region());
                p.fillRect(page->rect(), QBrush(*image));
            }
        }
    }
    if (auto *bar = qobject_cast<QStatusBar *>(watched);
        bar && m_renderer && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show)) {
        const int want = xpTextHeight(bar->font()) + 10;
        if (bar->minimumHeight() != want)
            bar->setMinimumHeight(want);
    }
    return QProxyStyle::eventFilter(watched, event);
}

// A page of a tab widget inside a dialog (and not one that fills its own
// background): what XP's property sheets texture with the tab body.
bool XPlasmaStyle::isTabBody(const QWidget *page) const
{
    const QWidget *stack = page->parentWidget();
    if (!stack || page->autoFillBackground() || !qobject_cast<const QStackedWidget *>(stack))
        return false;
    const auto *tabs = qobject_cast<const QTabWidget *>(stack->parentWidget());
    if (!tabs || tabs->documentMode() || !hasPart({QStringLiteral("tab"), 10, 0, {}}))
        return false;
    for (const QWidget *w = tabs; w; w = w->parentWidget())
        if (qobject_cast<const QDialog *>(w))
            return true;
    return false;
}

// A file dialog's places panel (KDE's KFilePlacesView, Qt's own QSidebar):
// what XP's Open and Save dialogs show as the Places bar, a toolbar themed
// PlacesBar::Toolbar.
bool XPlasmaStyle::isPlacesBar(const QWidget *view) const
{
    if (!view || !m_renderer || !(view->inherits("KFilePlacesView") || view->inherits("QSidebar")))
        return false;
    for (const QWidget *w = view->parentWidget(); w; w = w->parentWidget())
        if (w->inherits("KFileWidget") || w->inherits("QFileDialog"))
            return hasPart({QStringLiteral("toolbar"), 0, 0, QStringLiteral("placesbar")});
    return false;
}

void XPlasmaStyle::unpolish(QWidget *widget)
{
    if (qobject_cast<QMenu *>(widget) && widget->property("_xplasmaMenuFont").isValid()) {
        widget->setFont(widget->property("_xplasmaMenuFont").value<QFont>());
        widget->setProperty("_xplasmaMenuFont", QVariant());
    }
    if (qobject_cast<QStatusBar *>(widget)) {
        widget->removeEventFilter(this);
        widget->setMinimumHeight(0);
    }
    if (widget->inherits("QTipLabel"))
        widget->setContentsMargins(0, 0, 0, 0);
    QProxyStyle::unpolish(widget);
}

namespace {
// Marlett's check and submenu arrow as XP's menus draw them at 8pt.
const char *const kMenuCheck[] = {"......#", ".....##", "#...###", "##.###.", "#####..", ".###...", "..#...."};
const char *const kMenuBullet[] = {".###.", "#####", "#####", "#####", ".###."};
const char *const kMenuArrow[] = {"#...", "##..", "###.", "####", "###.", "##..", "#..."};

template<int N>
void drawGlyph(QPainter *painter, const char *const (&rows)[N], QPoint at, const QColor &colour)
{
    for (int y = 0; y < N; ++y)
        for (int x = 0; rows[y][x]; ++x)
            if (rows[y][x] == '#')
                painter->fillRect(at.x() + x, at.y() + y, 1, 1, colour);
}
} // namespace

static void classicSunken(QPainter *p, const QRect &r, const QPalette &pal);
static void classicPushEdge(QPainter *p, const QRect &r, const QPalette &pal, bool pressed);

// --- primitives -------------------------------------------------------------------

// Inside a combo box's drop-down list (XP's ComboLBox)?
static bool inComboPopup(const QWidget *widget)
{
    for (const QWidget *w = widget; w; w = w->parentWidget())
        if (w->inherits("QComboBoxPrivateContainer"))
            return true;
    return false;
}

void XPlasmaStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                                 const QWidget *widget) const
{
    const ContextScope context(this, widget, option);
    if ((m_renderer || classic()) && widget) {
        // A drop-down list: WS_BORDER, 1px of the window frame colour, and
        // no focus rectangle on its current item.
        if (element == PE_Frame && widget->inherits("QComboBoxPrivateContainer")) {
            painter->save();
            painter->setPen(sysColour(u"windowframe", Qt::black));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
            painter->restore();
            return;
        }
        if (element == PE_FrameFocusRect && inComboPopup(widget))
            return;
    }
    if (classic()) {
        switch (element) {
        case PE_FrameFocusRect:
            if (option->rect.isValid() && qobject_cast<const QScrollBar *>(widget)
                && (option->state & State_KeyboardFocusChange)) {
                // A focused scroll bar's thumb: its whole inside inverted in
                // a checkerboard (1px wider and taller than Qt's focus rect).
                const QRect r = option->rect.adjusted(0, 0, 1, 1);
                const QPoint origin = painter->deviceTransform().map(QPoint(0, 0));
                painter->save();
                painter->setCompositionMode(QPainter::RasterOp_SourceXorDestination);
                for (int y = r.top(); y <= r.bottom(); ++y)
                    for (int x = r.left(); x <= r.right(); ++x)
                        if ((x + y + origin.x() + origin.y()) & 1)
                            painter->fillRect(x, y, 1, 1, Qt::white);
                painter->restore();
                return;
            }
            if (!(option->state & State_KeyboardFocusChange))
                return;   // XP shows focus only once the keyboard's been used
            if (option->rect.isValid()) {
                const auto [odd, even] = focusColours(widget);
                drawFocusDots(painter, option->rect, odd, even);
                return;
            }
            break;
        case PE_FrameDefaultButton:
            return;   // drawn with the bevel (CE_PushButtonBevel)
        case PE_IndicatorBranch:
            drawTreeBranch(option, painter, widget);
            return;
        case PE_IndicatorTabTearLeft:
        case PE_IndicatorTabTearRight:
            return;
        case PE_PanelStatusBar: {
            // A simple status bar's one pane: a sunken 1px edge round it all.
            const QRect r = option->rect;
            painter->fillRect(r, option->palette.button());
            painter->fillRect(QRect(r.left(), r.top(), r.width() - 1, 1), option->palette.color(QPalette::Dark));
            painter->fillRect(QRect(r.left(), r.top(), 1, r.height() - 1), option->palette.color(QPalette::Dark));
            painter->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), option->palette.color(QPalette::Light));
            painter->fillRect(QRect(r.right(), r.top(), 1, r.height()), option->palette.color(QPalette::Light));
            return;
        }
        case PE_IndicatorToolBarSeparator: {
            // Etched: the shadow then the highlight, 2px in from the ends.
            const QRect r = option->rect;
            if (option->state & State_Horizontal) {
                painter->fillRect(QRect(r.left() + 2, r.top() + 2, 1, r.height() - 4), option->palette.color(QPalette::Dark));
                painter->fillRect(QRect(r.left() + 3, r.top() + 2, 1, r.height() - 4), option->palette.color(QPalette::Light));
            } else {
                painter->fillRect(QRect(r.left() + 2, r.top() + 2, r.width() - 4, 1), option->palette.color(QPalette::Dark));
                painter->fillRect(QRect(r.left() + 2, r.top() + 3, r.width() - 4, 1), option->palette.color(QPalette::Light));
            }
            return;
        }
        case PE_PanelButtonTool:
            if (option->state & State_AutoRaise) {
                // A flat toolbar button: nothing at rest, a 1px raised edge
                // when hot, sunken when pressed -- and checked, sunken over
                // a dither, 2px in.
                const QRect r = option->rect;
                const bool down = option->state & (State_Sunken | State_On);
                if (!enabled(option) || (!down && !hovered(option)))
                    return;
                const QColor tl = option->palette.color(down ? QPalette::Dark : QPalette::Light);
                const QColor br = option->palette.color(down ? QPalette::Light : QPalette::Dark);
                painter->fillRect(QRect(r.left(), r.top(), r.width() - 1, 1), tl);
                painter->fillRect(QRect(r.left(), r.top(), 1, r.height() - 1), tl);
                painter->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), br);
                painter->fillRect(QRect(r.right(), r.top(), 1, r.height()), br);
                if ((option->state & State_On) && !(option->state & State_Sunken)) {
                    const QRect inner = r.adjusted(2, 2, -2, -2);
                    const QPoint origin = painter->deviceTransform().map(QPoint(0, 0));
                    const QColor light = option->palette.color(QPalette::Light);
                    for (int y = inner.top(); y <= inner.bottom(); ++y)
                        for (int x = inner.left(); x <= inner.right(); ++x)
                            if ((x + y + origin.x() + origin.y()) & 1)
                                painter->fillRect(x, y, 1, 1, light);
                }
                return;
            }
            break;
        case PE_FrameTabWidget: {
            // The pane: a raised edge (the highlight top and left, the shadow
            // and dark shadow bottom and right).
            // (The push button's edge: the highlight and Light3D inside it at
            // the top left, the shadow and dark shadow at the bottom right.)
            const QRect r = option->rect;
            const QColor light = option->palette.color(QPalette::Light), dark = option->palette.color(QPalette::Dark),
                         shadow = option->palette.color(QPalette::Shadow), light3d = option->palette.color(QPalette::Midlight);
            painter->fillRect(QRect(r.left(), r.top(), r.width() - 1, 1), light);
            painter->fillRect(QRect(r.left(), r.top(), 1, r.height() - 1), light);
            painter->fillRect(QRect(r.left() + 1, r.top() + 1, r.width() - 3, 1), light3d);
            painter->fillRect(QRect(r.left() + 1, r.top() + 1, 1, r.height() - 3), light3d);
            painter->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), shadow);
            painter->fillRect(QRect(r.right(), r.top(), 1, r.height()), shadow);
            painter->fillRect(QRect(r.left() + 1, r.bottom() - 1, r.width() - 2, 1), dark);
            painter->fillRect(QRect(r.right() - 1, r.top() + 1, 1, r.height() - 2), dark);
            return;
        }
        case PE_IndicatorItemViewItemCheck: {
            // An item view's check box: DrawFrameControl's flat one -- a 2px
            // frame line round the window colour, Marlett's tick.
            const QRect r = option->rect;
            painter->fillRect(r, sysColour(u"windowframe", Qt::black));
            painter->fillRect(r.adjusted(2, 2, -2, -2), enabled(option) ? sysColour(u"window", Qt::white)
                                                                         : sysColour(u"btnface", Qt::lightGray));
            if (option->state & (State_On | State_NoChange)) {
                const bool grey = (option->state & State_NoChange) || !enabled(option);
                drawGlyph(painter, kMenuCheck, r.topLeft() + QPoint(3, 3),
                          grey ? sysColour(u"btnshadow", Qt::gray) : sysColour(u"windowtext", Qt::black));
            }
            return;
        }
        case PE_PanelMenu:
        case PE_FrameMenu: {
            // A Classic menu: the menu colour in a raised edge (Light3D and
            // DkShadow outside, Highlight and Shadow inside).
            const QRect r = option->rect;
            if (element == PE_PanelMenu)   // (the padding ring is the face; the items are menu-coloured)
                painter->fillRect(r, option->palette.button());
            auto frame = [&](const QRect &q, const QColor &tl, const QColor &br) {
                painter->fillRect(QRect(q.left(), q.top(), q.width() - 1, 1), tl);
                painter->fillRect(QRect(q.left(), q.top(), 1, q.height() - 1), tl);
                painter->fillRect(QRect(q.left(), q.bottom(), q.width(), 1), br);
                painter->fillRect(QRect(q.right(), q.top(), 1, q.height()), br);
            };
            frame(r, option->palette.color(QPalette::Midlight), option->palette.color(QPalette::Shadow));
            frame(r.adjusted(1, 1, -1, -1), option->palette.color(QPalette::Light), option->palette.color(QPalette::Dark));
            return;
        }
        case PE_PanelLineEdit:
            // The window colour inside the client edge (EDGE_SUNKEN) -- the
            // button face when disabled or read-only (WM_CTLCOLORSTATIC).
            if (const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option)) {
                const bool face = !enabled(option) || (option->state & State_ReadOnly);
                const QRect r = option->rect;
                const int w = frame->lineWidth > 0 ? 2 : 0;
                painter->fillRect(r.adjusted(w, w, -w, -w), face ? sysColour(u"btnface", option->palette.color(QPalette::Button))
                                                                 : sysColour(u"window", Qt::white));
                if (frame->lineWidth > 0)
                    classicSunken(painter, r, option->palette);
                return;
            }
            break;
        case PE_FrameLineEdit:
            classicSunken(painter, option->rect, option->palette);
            return;
        case PE_Frame:
            // A scroll area's client edge (EDGE_SUNKEN).
            if (option->state & State_Sunken) {
                classicSunken(painter, option->rect, option->palette);
                return;
            }
            break;
        case PE_PanelTipLabel:
            painter->fillRect(option->rect, sysColour(u"infobk", QColor(255, 255, 225)));
            painter->save();
            painter->setPen(sysColour(u"windowframe", Qt::black));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
            painter->restore();
            return;
        case PE_PanelMenuBar:
            // The bar, and under it a row of the face (SM_CYMENU is the menu
            // height + 1; measured in an overlapped window).
            painter->fillRect(option->rect, sysColour(u"menu", option->palette.color(QPalette::Button)));
            painter->fillRect(QRect(option->rect.left(), option->rect.bottom(), option->rect.width(), 1),
                              sysColour(u"btnface", Qt::lightGray));
            return;
        case PE_FrameWindow:
            // A sizing border: EDGE_RAISED, BorderWidth rings of the
            // active (inactive) border colour, then one of the face.
            if (widget && widget->inherits("QMdiSubWindow")) {
                auto ring = [&](const QRect &q, const QColor &tl, const QColor &br) {
                    painter->fillRect(QRect(q.left(), q.top(), q.width() - 1, 1), tl);
                    painter->fillRect(QRect(q.left(), q.top(), 1, q.height() - 1), tl);
                    painter->fillRect(QRect(q.left(), q.bottom(), q.width(), 1), br);
                    painter->fillRect(QRect(q.right(), q.top(), 1, q.height()), br);
                };
                QRect r = option->rect;
                ring(r, sysColour(u"light3d", Qt::lightGray), sysColour(u"dkshadow3d", Qt::black));
                r.adjust(1, 1, -1, -1);
                ring(r, sysColour(u"btnhighlight", Qt::white), sysColour(u"btnshadow", Qt::gray));
                const QColor border = (option->state & State_Active) ? sysColour(u"activeborder", Qt::lightGray)
                                                                     : sysColour(u"inactiveborder", Qt::lightGray);
                for (int i = 0; i < sysMetric(u"sizingborderwidth", 1); ++i) {
                    r.adjust(1, 1, -1, -1);
                    ring(r, border, border);
                }
                r.adjust(1, 1, -1, -1);
                ring(r, sysColour(u"btnface", Qt::lightGray), sysColour(u"btnface", Qt::lightGray));
                return;
            }
            break;
        case PE_IndicatorRadioButton: {
            // DFCS_BUTTONRADIO: Marlett's 12px circle in the four 3D colours,
            // the window colour inside (the face when disabled), the dot in
            // the text colour (the shadow when disabled).
            static const char *const kRadio[] = {
                "....SSSS....", "..SSDDDDSS..", ".SDDWWWWDDH.", ".SDWWWWWWLH.", "SDWWWKKWWWLH", "SDWWKKKKWWLH",
                "SDWWKKKKWWLH", "SDWWWKKWWWLH", ".SDWWWWWWLH.", ".SLLWWWWLLH.", "..HHLLLLHH..", "....HHHH....",
            };
            const bool on = option->state & State_On;
            const QColor colours[] = {sysColour(u"btnshadow", Qt::gray), sysColour(u"dkshadow3d", Qt::black),
                                      sysColour(u"btnhighlight", Qt::white), sysColour(u"light3d", Qt::lightGray),
                                      enabled(option) && !(option->state & State_Sunken) ? sysColour(u"window", Qt::white)
                                                                                          : sysColour(u"btnface", Qt::lightGray),
                                      enabled(option) ? sysColour(u"windowtext", Qt::black) : sysColour(u"btnshadow", Qt::gray)};
            // (Held down, the well is the face, as a check box's is.)
            const QPoint at = option->rect.topLeft() + QPoint(1, 0);
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < 12; ++x) {
                    const char *key = std::strchr("SDHLWK", kRadio[y][x]);
                    if (!key || kRadio[y][x] == '.')
                        continue;
                    int i = int(key - "SDHLWK");
                    if (i == 5 && !on)
                        i = 4;
                    painter->fillRect(at.x() + x, at.y() + y, 1, 1, colours[i]);
                }
            return;
        }
        case PE_IndicatorCheckBox: {
            // Qt's box is XP's; the tick is Marlett's (as in the menus), in
            // the button shadow when mixed or disabled, over the mixed
            // state's dither.
            QStyleOption box(*option);
            box.state &= ~(State_On | State_NoChange);
            box.state |= State_Off;
            QProxyStyle::drawPrimitive(element, &box, painter, widget);
            const QRect inner = option->rect.adjusted(2, 2, -2, -2);
            if (option->state & State_NoChange) {
                // The halftone of the face and the highlight -- only while the
                // highlight is white; otherwise Windows fills it solid.
                const QPoint origin = painter->deviceTransform().map(QPoint(0, 0));
                const QColor face = sysColour(u"btnface", option->palette.color(QPalette::Button));
                const QColor light = sysColour(u"btnhighlight", Qt::white);
                const bool dither = light == QColor(Qt::white);
                for (int y = inner.top(); y <= inner.bottom(); ++y)
                    for (int x = inner.left(); x <= inner.right(); ++x)
                        painter->fillRect(x, y, 1, 1, (!dither || ((x + y + origin.x() + origin.y()) & 1)) ? light : face);
            }
            if (option->state & (State_On | State_NoChange)) {
                const bool grey = (option->state & State_NoChange) || !enabled(option);
                drawGlyph(painter, kMenuCheck, option->rect.topLeft() + QPoint(3, 3),
                          grey ? sysColour(u"btnshadow", Qt::gray) : sysColour(u"windowtext", Qt::black));
            }
            return;
        }
        default:
            break;
        }
    }
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
        case PE_PanelMenu:
        case PE_FrameMenu:
            // XP's flat menus: the menu colour inside a 1px button-shadow line.
            if (flatMenus()) {
                if (element == PE_PanelMenu)
                    painter->fillRect(option->rect, sysColour(u"menu", Qt::white));
                painter->save();
                painter->setPen(sysColour(u"btnshadow", QColor(172, 168, 153)));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
                painter->restore();
                return;
            }
            break;
        case PE_PanelMenuBar:
            if (flatMenus()) {
                // The bar, and under it a row of the menu colour (SM_CYMENU
                // is the menu height + 1; measured in an overlapped window).
                painter->fillRect(option->rect, sysColour(u"menubar", option->palette.window().color()));
                painter->fillRect(QRect(option->rect.left(), option->rect.bottom(), option->rect.width(), 1),
                                  sysColour(u"menu", Qt::white));
                return;
            }
            break;
        case PE_FrameTabWidget:
            if (drawPart(painter, {QStringLiteral("tab"), 9, 1, {}}, option->rect))
                return;
            break;
        case PE_FrameWindow:
            // An MDI child's sides and bottom (WP_FRAMELEFT/RIGHT/BOTTOM,
            // FS_ACTIVE 1 / FS_INACTIVE 2), below its caption.
            if (hasPart({QStringLiteral("window"), 7, 1, {}}) && widget && widget->inherits("QMdiSubWindow")) {
                const int state = (option->state & State_Active) ? 1 : 2;
                const int fw = proxy()->pixelMetric(PM_MdiSubWindowFrameWidth, option, widget);
                QStyleOptionTitleBar probe;
                probe.QStyleOption::operator=(*option);
                const int top = proxy()->pixelMetric(PM_TitleBarHeight, &probe, widget) + fw;
                const QRect r = option->rect;
                drawPart(painter, {QStringLiteral("window"), 7, state, {}}, QRect(r.left(), top, fw, r.height() - top - fw));
                drawPart(painter, {QStringLiteral("window"), 8, state, {}}, QRect(r.right() - fw + 1, top, fw, r.height() - top - fw));
                drawPart(painter, {QStringLiteral("window"), 9, state, {}}, QRect(r.left(), r.bottom() - fw + 1, r.width(), fw));
                return;
            }
            break;
        case PE_PanelItemViewItem:
            // The Places bar's items are its buttons: hot under the pointer,
            // checked for the current place (TS_*: hot 2, pressed 3, checked
            // 5, hot checked 6).
            if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
                item && isPlacesBar(widget ? widget : item->widget)) {
                const bool selected = item->state & State_Selected;
                const bool hot = item->state & State_MouseOver;
                if (selected || hot)
                    drawPart(painter, {QStringLiteral("toolbar"), 1, selected ? (hot ? 6 : 5) : Hot, QStringLiteral("placesbar")},
                             item->rect);
                return;
            }
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
                    // A disabled combo box paints its own field (see
                    // CC_ComboBox), where XP's edit child sits.
                    if (!enabled(option) && widget && qobject_cast<const QComboBox *>(widget->parentWidget()))
                        return;
                    // A frameless edit (a spin box's, a combo box's): the
                    // window colour, or the face when disabled.
                    painter->fillRect(option->rect, enabled(option) ? option->palette.brush(QPalette::Base)
                                                                    : option->palette.brush(QPalette::Disabled, QPalette::Window));
                    return;
                }
            }
            if (drawPart(painter, {QStringLiteral("edit"), 1, state, {}}, option->rect)) {
                // The theme draws the border; the control fills its inside,
                // in the button face when disabled or read-only (as XP's
                // WM_CTLCOLORSTATIC does), in the window colour otherwise.
                if (element == PE_PanelLineEdit) {
                    const QRect inside = m_renderer->contentRect(inContext({QStringLiteral("edit"), 1, state, {}}), option->rect);
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
                             option->rect)) {
                    // A disabled list view's ring inside the border is the
                    // face its items sit on.
                    if (cls == QLatin1String("listview") && !enabled(option)) {
                        painter->save();
                        painter->setPen(sysColour(u"btnface", option->palette.color(QPalette::Button)));
                        painter->drawRect(option->rect.adjusted(1, 1, -2, -2));
                        painter->restore();
                    }
                    return;
                }
            }
            // Anything else sunken 2px: WS_EX_CLIENTEDGE's EDGE_SUNKEN, with
            // XP's 3DLIGHT inner highlight (Qt's panel uses the face there).
            if (const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
                frame && frame->lineWidth == 2 && (option->state & State_Sunken)) {
                classicSunken(painter, option->rect, option->palette);
                return;
            }
            break;
        case PE_IndicatorTabTearLeft:
        case PE_IndicatorTabTearRight:
            if (hasPart({QStringLiteral("tab"), 1, 1, {}}))
                return;   // XP marks no torn tabs
            break;
        case PE_PanelStatusBar:
            if (drawPart(painter, {QStringLiteral("status"), 0, 0, {}}, option->rect))
                return;
            break;
        case PE_FrameStatusBarItem:
            // SP_PANE: the pane's right-hand divider.
            if (drawPart(painter, {QStringLiteral("status"), 1, 0, {}}, option->rect))
                return;
            break;
        case PE_FrameFocusRect:
            // DrawFocusRect XORs with the control's DC colours (focusColours).
            if (!(option->state & State_KeyboardFocusChange))
                return;   // XP shows focus only once the keyboard's been used
            if (option->rect.isValid()) {
                const auto [odd, even] = focusColours(widget);
                drawFocusDots(painter, option->rect, odd, even);
                return;
            }
            break;
        case PE_IndicatorItemViewItemCheck: {
            // A list or tree view's check box: the button's CBS_* art, never
            // hot (XP draws these from a state image list).
            const int base = (option->state & State_NoChange) ? 8 : (option->state & State_On) ? 4 : 0;
            if (drawPart(painter, {QStringLiteral("button"), 3, base + (enabled(option) ? Normal : Disabled), {}}, option->rect))
                return;
            break;
        }
        case PE_IndicatorToolBarSeparator:
            // TP_SEPARATOR (a vertical line, for a horizontal toolbar) or
            // TP_SEPARATORVERT.
            if (drawPart(painter, {QStringLiteral("toolbar"), (option->state & State_Horizontal) ? 5 : 6, Normal, {}},
                         option->rect))
                return;
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

// A tab bar, and a header in an item view: XP draws them enabled even when
// their control is disabled.
static bool drawnEnabled(const QWidget *widget)
{
    if (qobject_cast<const QTabBar *>(widget))
        return true;
    return widget->inherits("QHeaderView") && widget->parentWidget() && widget->parentWidget()->inherits("QAbstractItemView");
}

void XPlasmaStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                               const QWidget *widget) const
{
    const ContextScope context(this, widget, option);
    // comctl32's tab control paints its tabs the same enabled or not, and a
    // list view's header is a child window of its own that stays enabled:
    // XP draws both as enabled whatever the control's state.
    if ((m_renderer || classic()) && !enabled(option) && widget && drawnEnabled(widget)) {
        auto enable = [](QStyleOption &copy) {
            copy.state |= State_Enabled;
            copy.palette.setCurrentColorGroup(QPalette::Active);
        };
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
            QStyleOptionTab copy(*tab);
            enable(copy);
            return drawControl(element, &copy, painter, widget);
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        if (const auto *header = qstyleoption_cast<const QStyleOptionHeaderV2 *>(option)) {
            QStyleOptionHeaderV2 copy(*header);
            enable(copy);
            return drawControl(element, &copy, painter, widget);
        }
#endif
        if (const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
            header && header->version == QStyleOptionHeader::Version) {
            QStyleOptionHeader copy(*header);
            enable(copy);
            return drawControl(element, &copy, painter, widget);
        }
        if (option->type == QStyleOption::SO_Default) {
            QStyleOption copy(*option);
            enable(copy);
            return drawControl(element, &copy, painter, widget);
        }
    }
    if (classic() && drawClassicControl(element, option, painter, widget))
        return;
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
                    int state = selected ? 3 : hovered(tab) ? Hot : Normal;
                    if (!enabled(tab))
                        state = Disabled;
                    // XP's geometry (TCM_GETITEMRECT, Luna): tabs start 2px in
                    // and 2px down, 18px tall; the selected one is drawn 2px
                    // wider each side and 2px taller at both ends, covering
                    // the pane's top edge and the white row under it.
                    QRect r = xpTabRect(tab);
                    if (selected)
                        r.adjust(-2, -2, 2, 2);
                    // TABP_TABITEM / *LEFTEDGE / *RIGHTEDGE / *BOTHEDGE, and the
                    // TOPTABITEM set for the selected tab. Measured on XP: an
                    // unselected tab takes its edge variant from its place in
                    // the row (the last tab is RIGHTEDGE even well short of
                    // the control's edge); the selected one only when its
                    // inflated rect actually meets the control's edge.
                    int part = selected ? 5 : 1;
                    if (selected) {
                        const QWidget *control = widget && qobject_cast<const QTabWidget *>(widget->parentWidget())
                                                     ? widget->parentWidget() : widget;
                        const QPoint origin = control && control != widget ? widget->mapTo(control, QPoint(0, 0)) : QPoint();
                        if (r.left() + origin.x() <= 0)
                            part += 1;
                        if (control && r.right() + origin.x() >= control->width() - 1)
                            part += 2;
                    } else if (tab->position == QStyleOptionTab::Beginning) {
                        part += 1;
                    } else if (tab->position == QStyleOptionTab::End) {
                        part += 2;
                    } else if (tab->position == QStyleOptionTab::OnlyOneTab) {
                        part += 3;
                    }
                    // Tabs stop where the scroll up-down starts, top to bottom
                    // (QTabBar only keeps the labels out from under it).
                    painter->save();
                    if (const auto *bar = qobject_cast<const QTabBar *>(widget))
                        for (const QToolButton *b : bar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
                            if (b->isVisible() && b->arrowType() == Qt::LeftArrow)
                                painter->setClipRect(QRect(0, 0, b->x(), bar->height()), Qt::IntersectClip);
                    const bool drawn = drawPart(painter, {QStringLiteral("tab"), part, state, {}}, r);
                    // XP then repaints an unselected edge tab's label area
                    // with plain TABP_TABITEM (visible only where the two
                    // stretch differently: Luna's text baseline row).
                    if (drawn && !selected && part != 1) {
                        const QMargins m = m_renderer->margins(inContext({QStringLiteral("tab"), 1, state, {}}), u"sizingmargins");
                        painter->setClipRect(r.adjusted(m.left(), 0, -m.right(), 0), Qt::IntersectClip);
                        drawPart(painter, {QStringLiteral("tab"), 1, state, {}}, r);
                    }
                    painter->restore();
                    if (drawn)
                        return;
                }
            }
            break;
        case CE_CheckBox:
        case CE_RadioButton:
            // QCommonStyle places the focus rect with a non-proxied call;
            // draw it at our SE_*FocusRect instead.
            if (const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
                button && (button->state & State_HasFocus)) {
                QStyleOptionButton copy(*button);
                copy.state &= ~State_HasFocus;
                QProxyStyle::drawControl(element, &copy, painter, widget);
                QStyleOptionFocusRect focus;
                focus.QStyleOption::operator=(*button);
                focus.rect = proxy()->subElementRect(element == CE_CheckBox ? SE_CheckBoxFocusRect : SE_RadioButtonFocusRect,
                                                     button, widget);
                proxy()->drawPrimitive(PE_FrameFocusRect, &focus, painter, widget);
                return;
            }
            break;
        case CE_SizeGrip:
            // SP_GRIPPER, at its true size in the bottom-right corner, 1px
            // in from the right as comctl32 places it.
            if (const QSize grip = trueSize({QStringLiteral("status"), 3, 0, {}}); grip.isValid() && !grip.isEmpty()) {
                const QRect r(option->rect.right() - grip.width(), option->rect.bottom() - grip.height() + 1,
                              grip.width(), grip.height());
                if (drawPart(painter, {QStringLiteral("status"), 3, 0, {}}, r))
                    return;
            }
            break;
        case CE_ItemViewItem:
            if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
                item && drawXpViewItem(item, painter, widget))
                return;
            break;
        case CE_HeaderEmptyArea:
            // Past the last column: the header class's own background.
            if (drawPart(painter, {QStringLiteral("header"), 0, 0, {}}, option->rect))
                return;
            break;
        case CE_MenuItem:
            if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option); item && flatMenus()) {
                drawMenuItem(item, painter, widget);
                return;
            }
            break;
        case CE_MenuEmptyArea:
            if (flatMenus()) {
                painter->fillRect(option->rect, sysColour(u"menu", Qt::white));
                return;
            }
            break;
        case CE_MenuBarItem:
            if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option); item && flatMenus()) {
                drawMenuBarItem(item, painter, widget);
                return;
            }
            break;
        case CE_MenuBarEmptyArea:
            if (flatMenus()) {
                painter->fillRect(option->rect, sysColour(u"menubar", option->palette.window().color()));
                painter->fillRect(QRect(option->rect.left(), option->rect.bottom(), option->rect.width(), 1),
                                  sysColour(u"menu", Qt::white));
                return;
            }
            break;
        case CE_ToolButtonLabel:
            if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
                button && hasPart({QStringLiteral("toolbar"), 1, Normal, {}}) && drawXpToolButtonLabel(button, painter, widget))
                return;
            break;
        case CE_Header:
            if (const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
                header && hasPart({QStringLiteral("header"), 1, Normal, {}})) {
                drawXpHeader(header, painter, widget);
                return;
            }
            break;
        case CE_HeaderLabel:
            if (const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option)) {
                if (hasPart({QStringLiteral("header"), 1, Normal, {}})) {
                    // QCommonStyle insets the label by PM_HeaderMargin on every
                    // side through a non-proxied call, which clips a 20px
                    // header's text to 2px. XP's margin is horizontal only.
                    // Vertically XP centres the text in the item's content
                    // rect and then 1px up (measured: Luna 2px down in its 20,
                    // Zune's 2px bottom margin 1px); hand Qt a rect exactly
                    // one line tall inside the margin it takes off.
                    const int m = proxy()->pixelMetric(PM_HeaderMargin, option, widget);
                    const QMargins cm = m_renderer->contentMargins(inContext({QStringLiteral("header"), 1, Normal, {}}));
                    // (GDI's line height where it agrees with Qt's, as for
                    // Tahoma 8; for other session fonts -- Noto's is far
                    // taller in GDI's terms than the line Qt draws -- Qt's.)
                    int th = xpTextHeight(widget ? widget->font() : QApplication::font());
                    if (const int qt = header->fontMetrics.height(); qAbs(qt - th) > 1)
                        th = qt;
                    const int avail = header->rect.height() - cm.top() - cm.bottom();
                    const int top = header->rect.top() + cm.top() + (avail - th) / 2 - 1;
                    QStyleOptionHeader copy(*header);
                    copy.rect = QRect(header->rect.left(), top - m, header->rect.width(), th + 2 * m);
                    // (Centred in that: QHeaderView asks for AlignVCenter,
                    // Dolphin's header for AlignLeft alone.)
                    if (!(copy.textAlignment & Qt::AlignVertical_Mask))
                        copy.textAlignment |= Qt::AlignVCenter;
                    if (header->state & State_Sunken)   // pressed: 1px down and right
                        copy.rect.translate(1, 1);
                    QProxyStyle::drawControl(element, &copy, painter, widget);
                    return;
                }
            }
            break;
        case CE_ComboBoxLabel:
            if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
                if (!combo->editable && hasPart({QStringLiteral("combobox"), 1, 1, {}})) {
                    // QCommonStyle insets the label 1px into the field; XP
                    // draws it at the field's edge, and clips it at the
                    // field's other edge -- 2px further than QCommonStyle's
                    // clip once shifted (the 'r' of "September" in Date and
                    // Time Properties' month box): the label is laid out
                    // 2px wider (only the label is drawn here).
                    QStyleOptionComboBox copy(*combo);
                    copy.rect.adjust(0, 0, 2, 0);
                    painter->save();
                    painter->translate(-1, 0);
                    QProxyStyle::drawControl(element, &copy, painter, widget);
                    painter->restore();
                    return;
                }
            }
            break;
        case CE_TabBarTabLabel:
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
                if (hasPart({QStringLiteral("tab"), 1, 1, {}})
                    && (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth)) {
                    drawXpTabLabel(tab, painter, widget);
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

void XPlasmaStyle::drawProgressChunks(const QStyleOptionProgressBar *bar, QPainter *painter, const QWidget *) const
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
    const QRect content = m_renderer->contentRect(inContext(groove), bar->rect);
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

void XPlasmaStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                                      const QWidget *widget) const
{
    const ContextScope context(this, widget, option);
    if (classic() && drawClassicComplex(control, option, painter, widget))
        return;
    if (control == CC_MdiControls && (classic() || m_renderer)) {
        drawMdiControls(option, painter, widget);
        return;
    }
    if (m_renderer) {
        switch (control) {
        case CC_ToolButton:
            if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option); button && xpDropDown(button)) {
                drawXpDropDownButton(button, painter, widget);
                return;
            }
            // A tab row's scroll buttons: the two halves of XP's up-down
            // (SPNP_DOWNHORZ pointing left, SPNP_UPHORZ right).
            if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
                button && widget && qobject_cast<const QTabBar *>(widget->parentWidget())
                && (button->arrowType == Qt::LeftArrow || button->arrowType == Qt::RightArrow)) {
                // QTabBar disables the button at the end of the range; XP's
                // up-down never greys an arrow while the tabs are enabled.
                QStyleOptionToolButton copy(*button);
                if (widget->parentWidget()->isEnabled())
                    copy.state |= State_Enabled;
                const int part = button->arrowType == Qt::LeftArrow ? 4 : 3;
                if (drawPart(painter, {QStringLiteral("spin"), part, fourState(&copy), {}}, button->rect))
                    return;
            }
            break;
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
                    // (Royale and Embedded also give the frame a hot colour.)
                    const int frameState = !enabled(combo) ? Disabled : hovered(combo) ? Hot : Normal;
                    if (!drawPart(painter, {QStringLiteral("combobox"), 0, frameState, {}}, combo->rect))
                        drawPart(painter, {QStringLiteral("edit"), 1, enabled(combo) ? Normal : Disabled, {}}, combo->rect);
                    const QRect arrow = proxy()->subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
                    if (!enabled(combo)) {
                        // Disabled, the field is the button face (XP's
                        // WM_CTLCOLORSTATIC): inside the ring for a drop-down
                        // list, the edit child's client for an editable one.
                        const QRect r = combo->rect;
                        const int in = combo->editable ? 3 : 2;
                        painter->fillRect(QRect(r.left() + in, r.top() + in, arrow.left() - r.left() - (combo->editable ? 2 : 1) - in, r.height() - 2 * in),
                                          combo->palette.brush(QPalette::Disabled, QPalette::Window));
                    }
                    int state = Normal;
                    if (!enabled(combo))
                        state = Disabled;
                    else if ((combo->activeSubControls & SC_ComboBoxArrow) && sunken(combo))
                        state = Pressed;   // (not while the list is merely down: XP keeps the normal art)
                    else if (hovered(combo))
                        state = Hot;
                    drawPart(painter, {QStringLiteral("combobox"), 1, state, {}}, arrow);
                    if (combo->state & State_HasFocus && !combo->editable) {
                        // A focused drop-down list highlights its field, as XP does.
                        const QRect field = proxy()->subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget);
                        painter->fillRect(field, combo->palette.brush(QPalette::Highlight));
                        // ...ringed by a focus rect XORed with the highlight
                        // colours the field is drawn in.
                        if (combo->state & State_KeyboardFocusChange)
                        drawFocusDots(painter, field, combo->palette.color(QPalette::Active, QPalette::Highlight),
                                      combo->palette.color(QPalette::Active, QPalette::HighlightedText));
                    }
                    return;
                }
            }
            break;
        case CC_SpinBox:
            if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
                if (hasPart({QStringLiteral("spin"), 1, 1, {}})) {
                    if (spin->frame) {
                        // The buddy edit: its border, its inside (the face
                        // when disabled), and the up-down's own white ground.
                        const PartRef edit{QStringLiteral("edit"), 1, enabled(spin) ? Normal : Disabled, {}};
                        drawPart(painter, edit, spin->rect);
                        const QRect inside = m_renderer->contentRect(inContext(edit), spin->rect);
                        painter->fillRect(inside, spin->palette.brush(enabled(spin) ? QPalette::Active : QPalette::Disabled,
                                                                      enabled(spin) ? QPalette::Base : QPalette::Window));
                        const QRect up = proxy()->subControlRect(CC_SpinBox, spin, SC_SpinBoxUp, widget);
                        const QRect down = proxy()->subControlRect(CC_SpinBox, spin, SC_SpinBoxDown, widget);
                        if (up.isValid()) {
                            painter->fillRect(QRect(QPoint(up.left(), inside.top()), inside.bottomRight()),
                                              spin->palette.brush(QPalette::Active, QPalette::Base));
                            painter->fillRect(QRect(up.left(), up.bottom() + 1, up.width(), down.top() - up.bottom() - 1),
                                              sysColour(u"btnface", spin->palette.window().color()));
                        }
                    }
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
                    // A focused trackbar's focus rect rings its whole client.
                    if ((slider->state & State_HasFocus) && (slider->state & State_KeyboardFocusChange))
                        drawFocusDots(painter, slider->rect, focusColours(widget).first, focusColours(widget).second);
                    return;
                }
            }
            break;
        case CC_TitleBar:
            if (const auto *bar = qstyleoption_cast<const QStyleOptionTitleBar *>(option);
                bar && hasPart({QStringLiteral("window"), 1, 1, {}})) {
                drawTitleBar(bar, painter, widget);
                return;
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

// --- title bars (MDI children) ---------------------------------------------------

// The whole caption as XP draws it: QMdiSubWindow hands the title bar in
// by its border; XP's caption art covers the top border too.
static QRect titleBarWhole(const QStyleOptionTitleBar *bar, const QWidget *widget, int border)
{
    if (widget && widget->inherits("QMdiSubWindow") && !(bar->titleBarState & Qt::WindowMaximized)
        && bar->rect.topLeft() == QPoint(border, border))
        return bar->rect.adjusted(-border, -border, border, 0);
    return bar->rect;
}

// XP's caption layout (measured, Luna Silver MDI child): buttons
// SM_CXSIZE - 4 square, 2px below the top frame and 2px apart, the close
// button 2px in from the right frame; the icon 16px, 2px in from the left
// frame, centred in the caption.
QRect XPlasmaStyle::xpTitleBarRect(const QStyleOptionTitleBar *bar, SubControl sub, const QWidget *widget) const
{
    const int fw = widget && widget->inherits("QMdiSubWindow") ? proxy()->pixelMetric(PM_MdiSubWindowFrameWidth, bar, widget) : 0;
    const QRect whole = titleBarWhole(bar, widget, fw);
    const int top = whole.top() + (whole == bar->rect ? 0 : fw);
    const int caption = whole.bottom() + 1 - top;
    // (Classic: SM_CXSIZE - 2 wide, SM_CYSIZE - 4 tall; min and max
    // touching, the close button 2px apart.)
    const QSize size = classic() ? QSize(sysMetric(u"captionbarwidth", 18) - 2, sysMetric(u"captionbarheight", 18) - 4)
                                 : QSize(sysMetric(u"captionbarheight", 25) - 4, sysMetric(u"captionbarheight", 25) - 4);
    const Qt::WindowFlags flags = bar->titleBarFlags;
    const bool minimized = bar->titleBarState & Qt::WindowMinimized, maximized = bar->titleBarState & Qt::WindowMaximized;
    auto slot = [&](int n) {
        const int gaps = classic() ? (n > 0 ? 2 : 0) : n * 2;
        return QRect(whole.right() - fw - 2 - size.width() + 1 - n * size.width() - gaps, top + 2, size.width(), size.height());
    };
    const bool hasClose = flags & Qt::WindowSystemMenuHint;
    const bool hasMax = (flags & Qt::WindowMaximizeButtonHint) && !maximized;
    const bool hasNormal = ((flags & Qt::WindowMinimizeButtonHint) && minimized) || ((flags & Qt::WindowMaximizeButtonHint) && maximized);
    const bool hasMin = (flags & Qt::WindowMinimizeButtonHint) && !minimized;
    int n = hasClose ? 1 : 0;
    const int maxSlot = (hasMax || (hasNormal && maximized)) ? n++ : -1;
    const int minSlot = (hasMin || (hasNormal && minimized)) ? n++ : -1;
    const int helpSlot = (flags & Qt::WindowContextHelpButtonHint) ? n++ : -1;
    switch (sub) {
    case SC_TitleBarCloseButton:
        return hasClose ? slot(0) : QRect();
    case SC_TitleBarMaxButton:
        return hasMax ? slot(maxSlot) : QRect();
    case SC_TitleBarNormalButton:
        return hasNormal ? slot(maximized ? maxSlot : minSlot) : QRect();
    case SC_TitleBarMinButton:
        return hasMin ? slot(minSlot) : QRect();
    case SC_TitleBarContextHelpButton:
        return helpSlot >= 0 ? slot(helpSlot) : QRect();
    case SC_TitleBarSysMenu:
        return (flags & Qt::WindowSystemMenuHint) ? QRect(whole.left() + fw + 2, top + (caption - 16) / 2, 16, 16) : QRect();
    case SC_TitleBarLabel: {
        const int gap = classic() ? 2 : 4;
        const int left = whole.left() + fw + ((flags & Qt::WindowSystemMenuHint) ? 2 + 16 + gap : gap);
        const int right = n ? slot(n - 1).left() - 2 : whole.right() - fw;
        return QRect(left, top, qMax(0, right - left), caption);
    }
    default:
        return QRect();
    }
}


void XPlasmaStyle::drawTitleBar(const QStyleOptionTitleBar *bar, QPainter *painter, const QWidget *widget) const
{
    const bool active = bar->state & State_Active;
    const int fw = widget && widget->inherits("QMdiSubWindow") ? proxy()->pixelMetric(PM_MdiSubWindowFrameWidth, bar, widget) : 0;
    const QRect whole = titleBarWhole(bar, widget, fw);
    // WP_CAPTION: CS_ACTIVE 1, CS_INACTIVE 2.
    drawPart(painter, {QStringLiteral("window"), 1, active ? 1 : 2, {}}, whole);

    // The buttons (WP_*BUTTON), normal/hot/pushed/disabled, an inactive
    // window's four frames after them.
    const struct { SubControl sub; int part; } buttons[] = {
        {SC_TitleBarCloseButton, 18}, {SC_TitleBarMaxButton, 17}, {SC_TitleBarNormalButton, 21},
        {SC_TitleBarMinButton, 15}, {SC_TitleBarContextHelpButton, 23},
    };
    for (const auto &b : buttons) {
        const QRect r = xpTitleBarRect(bar, b.sub, widget);
        if (!r.isValid() || !(bar->subControls & b.sub))
            continue;
        int state = Normal;
        if (!enabled(bar))
            state = Disabled;
        else if ((bar->activeSubControls & b.sub) && (bar->state & State_Sunken))
            state = Pressed;
        else if ((bar->activeSubControls & b.sub) && (bar->state & State_MouseOver))
            state = Hot;
        if (!active)
            state += 4;
        drawPart(painter, {QStringLiteral("window"), b.part, state, {}}, r);
    }

    if (const QRect icon = xpTitleBarRect(bar, SC_TitleBarSysMenu, widget); icon.isValid() && !bar->icon.isNull())
        bar->icon.paint(painter, icon);

    // The caption text: CaptionText / InactiveCaptionText in the theme's
    // CaptionFont, over its shadow when the caption part asks for one.
    const QRect label = xpTitleBarRect(bar, SC_TitleBarLabel, widget);
    if (label.isValid() && !bar->text.isEmpty()) {
        painter->save();
        const QFont font = sysFont(u"captionfont", painter->font());
        painter->setFont(font);
        const QString text = QFontMetrics(font).elidedText(bar->text, Qt::ElideRight, label.width());
        const PartRef caption{QStringLiteral("window"), 1, active ? 1 : 2, {}};
        const QString shadow = m_renderer->property(inContext(caption), u"textshadowtype").value_or(QString());
        if (shadow.compare(QLatin1String("single"), Qt::CaseInsensitive) == 0) {
            QPoint offset(1, 1);
            const QStringList xy = m_renderer->property(inContext(caption), u"textshadowoffset").value_or(QString()).split(QLatin1Char(','));
            if (xy.size() == 2)
                offset = QPoint(xy[0].trimmed().toInt(), xy[1].trimmed().toInt());
            painter->setPen(themeColour(caption, u"textshadowcolor", Qt::gray));
            painter->drawText(label.translated(offset), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
        }
        painter->setPen(active ? sysColour(u"captiontext", Qt::black) : sysColour(u"inactivecaptiontext", Qt::gray));
        painter->drawText(label, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
        painter->restore();
    }
}

// A maximized MDI child's buttons in the menu bar: the style's WP_MDIMIN /
// MDIRESTORE / MDICLOSEBUTTON; in Classic, DrawFrameControl's raised
// edges and Marlett's glyphs at the 16 x 15 button (measured).
void XPlasmaStyle::drawMdiControls(const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const
{
    if (m_renderer && hasPart({QStringLiteral("window"), 20, 1, {}})) {
        const struct { SubControl sub; int part; } parts[] = {
            {SC_MdiMinButton, 16}, {SC_MdiNormalButton, 22}, {SC_MdiCloseButton, 20},
        };
        for (const auto &b : parts) {
            if (!(option->subControls & b.sub))
                continue;
            int state = Normal;
            if (!enabled(option))
                state = Disabled;
            else if ((option->activeSubControls & b.sub) && (option->state & State_Sunken))
                state = Pressed;
            else if ((option->activeSubControls & b.sub) && (option->state & State_MouseOver))
                state = Hot;
            drawPart(painter, {QStringLiteral("window"), b.part, state, {}},
                     proxy()->subControlRect(CC_MdiControls, option, b.sub, widget));
        }
        return;
    }
    static const char *const kMin[] = {"", "", "", "", "", "", "", "", "", "", "...#######", "...#######", "", "", ""};
    static const char *const kRestore[] = {"", "", "", "....#######", "....#######", "....#.....#", "..#######.#",
                                           "..#######.#", "..#.....###", "..#.....#", "..#.....#", "..#######", "", "", ""};
    static const char *const kClose[] = {"", "", "", "...##....##", "...###..###", "....######", ".....####",
                                         ".....####", "....######", "...###..###", "...##....##", "", "", "", ""};
    const struct { SubControl sub; const char *const *glyph; } buttons[] = {
        {SC_MdiMinButton, kMin}, {SC_MdiNormalButton, kRestore}, {SC_MdiCloseButton, kClose},
    };
    QPalette pal = option->palette;
    pal.setColor(QPalette::Button, sysColour(u"btnface", pal.color(QPalette::Button)));
    pal.setColor(QPalette::Light, sysColour(u"btnhighlight", Qt::white));
    pal.setColor(QPalette::Midlight, sysColour(u"light3d", pal.color(QPalette::Midlight)));
    pal.setColor(QPalette::Dark, sysColour(u"btnshadow", Qt::gray));
    pal.setColor(QPalette::Shadow, sysColour(u"dkshadow3d", Qt::black));
    for (const auto &b : buttons) {
        if (!(option->subControls & b.sub))
            continue;
        const QRect r = proxy()->subControlRect(CC_MdiControls, option, b.sub, widget);
        const bool down = (option->activeSubControls & b.sub) && (option->state & State_Sunken);
        classicPushEdge(painter, r, pal, down);
        const QPoint at = r.topLeft() + (down ? QPoint(1, 1) : QPoint());
        const QColor ink = sysColour(u"btntext", Qt::black);
        for (int y = 0; y < 15 && y < r.height(); ++y)
            for (int x = 0; b.glyph[y][x]; ++x)
                if (b.glyph[y][x] == '#')
                    painter->fillRect(at.x() + x, at.y() + y, 1, 1, ink);
    }
}

// A classic caption (DrawCaption, measured on XP with Windows Classic's own
// metrics): the start colour behind the icon, GradientFill from 2px past
// the icon to 2px short of the first button, the end colour after; a face
// row under it; DFC_CAPTION buttons with Marlett's glyphs.
void XPlasmaStyle::drawClassicTitleBar(const QStyleOptionTitleBar *bar, QPainter *painter, const QWidget *widget) const
{
    const bool active = bar->state & State_Active;
    const int fw = widget && widget->inherits("QMdiSubWindow") ? proxy()->pixelMetric(PM_MdiSubWindowFrameWidth, bar, widget) : 0;
    const QRect whole = titleBarWhole(bar, widget, fw);
    const QRect caption = whole.adjusted(fw, whole == bar->rect ? 0 : fw, -fw, -1);
    const QColor from = sysColour(active ? u"activecaption" : u"inactivecaption", active ? QColor(10, 36, 106) : Qt::gray);
    const QColor to = sysColour(active ? u"gradientactivecaption" : u"gradientinactivecaption", from);
    painter->fillRect(QRect(caption.left(), caption.bottom() + 1, caption.width(), 1), sysColour(u"btnface", Qt::lightGray));

    const QRect icon = xpTitleBarRect(bar, SC_TitleBarSysMenu, widget);
    int right = caption.right() + 1;
    for (SubControl sc : {SC_TitleBarMinButton, SC_TitleBarNormalButton, SC_TitleBarMaxButton, SC_TitleBarContextHelpButton,
                          SC_TitleBarCloseButton})
        if (const QRect r = xpTitleBarRect(bar, sc, widget); r.isValid())
            right = qMin(right, r.left() - 2);
    const int left = icon.isValid() ? icon.right() + 2 : caption.left();
    painter->fillRect(QRect(caption.left(), caption.top(), left - caption.left(), caption.height()), from);
    painter->fillRect(QRect(right, caption.top(), caption.right() + 1 - right, caption.height()), to);
    xpl::classic::gradientFill(painter, QRect(left, caption.top(), qMax(0, right - left), caption.height()), from, to);
    if (icon.isValid() && !bar->icon.isNull())
        bar->icon.paint(painter, icon);

    // DFC_CAPTION buttons: Marlett's glyphs in the push bevel.
    using xpl::classic::CaptionGlyph;
    const struct { SubControl sub; CaptionGlyph glyph; } buttons[] = {
        {SC_TitleBarCloseButton, CaptionGlyph::Close}, {SC_TitleBarMaxButton, CaptionGlyph::Maximize},
        {SC_TitleBarNormalButton, CaptionGlyph::Restore}, {SC_TitleBarMinButton, CaptionGlyph::Minimize},
    };
    for (const auto &b : buttons) {
        const QRect r = xpTitleBarRect(bar, b.sub, widget);
        if (!r.isValid() || !(bar->subControls & b.sub))
            continue;
        const bool down = (bar->activeSubControls & b.sub) && (bar->state & State_Sunken);
        classicPushEdge(painter, r, bar->palette, down);
        const QPoint at = r.topLeft() + (down ? QPoint(1, 1) : QPoint());
        if (enabled(bar)) {
            xpl::classic::captionGlyph(painter, at, b.glyph, sysColour(u"btntext", Qt::black));
        } else {
            xpl::classic::captionGlyph(painter, at + QPoint(1, 1), b.glyph, sysColour(u"btnhighlight", Qt::white));
            xpl::classic::captionGlyph(painter, at, b.glyph, sysColour(u"btnshadow", Qt::gray));
        }
    }

    const QRect label = xpTitleBarRect(bar, SC_TitleBarLabel, widget);
    if (label.isValid() && !bar->text.isEmpty()) {
        painter->save();
        const QFont font = sysFont(u"captionfont", painter->font());
        painter->setFont(font);
        painter->setPen(sysColour(active ? u"captiontext" : u"inactivecaptiontext", active ? Qt::white : Qt::lightGray));
        // (Centred by GDI's text height in the rows above the face row.)
        const int th = xpTextHeight(font);
        const QRect text(label.left(), label.top() + (label.height() - 1 - th) / 2, label.width(), th);
        painter->drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine,
                          QFontMetrics(font).elidedText(bar->text, Qt::ElideRight, text.width()));
        painter->restore();
    }
}

void XPlasmaStyle::drawScrollBar(const QStyleOptionSlider *bar, QPainter *painter, const QWidget *widget) const
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
    // SBP_LOWERTRACK* is the side before the thumb, UPPERTRACK* after it;
    // with no thumb, one track runs the whole length.
    if (!on) {
        drawPart(painter, {QStringLiteral("scrollbar"), horizontal ? 4 : 6, Disabled, {}},
                 proxy()->subControlRect(CC_ScrollBar, bar, SC_ScrollBarGroove, widget));
        return;
    }
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
        const QRect room = m_renderer->contentRect(inContext(thumbPart), thumb);
        if (grip.isValid() && (horizontal ? room.width() : room.height()) > (horizontal ? grip.width() : grip.height()))
            drawPart(painter, gripper, thumb);
    }
}

// --- metrics ---------------------------------------------------------------------------

int XPlasmaStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    const ContextScope context(this, widget, option);
    if (metric == PM_DefaultFrameWidth && (m_renderer || classic()) && widget && widget->inherits("QComboBoxPrivateContainer"))
        return 1;   // a drop-down list's WS_BORDER
    if (classic()) {
        switch (metric) {
        case PM_ExclusiveIndicatorWidth:
        case PM_ExclusiveIndicatorHeight:
            return 13;   // DFCS_BUTTONRADIO's cell: the 12px glyph, 1px in
        case PM_RadioButtonLabelSpacing:
            return QProxyStyle::pixelMetric(metric, option, widget) - 1;   // the label stays where XP has it
        case PM_ScrollBarExtent:
            return sysMetric(u"scrollbarwidth", 17);   // SM_CXVSCROLL / SM_CYHSCROLL
        case PM_TreeViewIndentation:
            return 19;   // TVM_GETINDENT
        case PM_HeaderMargin:
            return 6;    // the header label's inset
        case PM_TabBarTabShiftVertical:
        case PM_TabBarTabShiftHorizontal:
        case PM_TabBarBaseOverlap:
            return 0;
        case PM_TabBarTabHSpace:
            return 12;   // as themed: TCM_SETPADDING's 6px either side
        case PM_TabBarScrollButtonWidth:
            // Square to the tab row less 1 (17 for Tahoma 8), whatever the
            // scroll bar width (measured at 16 and 17).
            return xpTextHeight(widget ? widget->font() : QApplication::font()) + 4;
        case PM_ToolBarFrameWidth:
        case PM_ToolBarItemMargin:
        case PM_ToolBarItemSpacing:
            return 0;    // buttons edge to edge from 0,0, as themed
        case PM_ToolBarSeparatorExtent:
            return 6;
        case PM_TabBarTabVSpace:
            return 9;
        case PM_MenuPanelWidth:
            return 2;   // the raised edge
        case PM_FocusFrameHMargin:
            // comctl32's item insets, as themed (see there).
            if (const int extra = itemRowExtra(widget); extra > 0)
                return extra == 3 ? 4 : 5;
            if (itemRowExtra(widget) == 0)
                return qMax(0, QProxyStyle::pixelMetric(metric, option, widget) - 1);
            break;
        case PM_MenuHMargin:
        case PM_MenuVMargin:
            return 1;
        case PM_TitleBarHeight:
            return sysMetric(u"captionbarheight", 18) + 1;   // SM_CYCAPTION
        case PM_MenuButtonIndicator:
            if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option); button && xpDropDown(button))
                return 13;   // a split button's arrow half (measured)
            break;
        case PM_MdiSubWindowFrameWidth:
            return sysMetric(u"sizingborderwidth", 1) + 3;   // SM_CXSIZEFRAME
        default:
            break;
        }
    }
    if (m_renderer) {
        switch (metric) {
        case PM_TitleBarHeight:
            // SM_CYCAPTION: the theme's CaptionBarHeight + 1 (QMdiSubWindow
            // adds its border on top, as XP's caption art covers it).
            if (hasPart({QStringLiteral("window"), 1, 1, {}}))
                return sysMetric(u"captionbarheight", 25) + 1;
            break;
        case PM_MdiSubWindowFrameWidth:
            if (hasPart({QStringLiteral("window"), 7, 1, {}}))
                return 4;   // SM_CXSIZEFRAME
            break;
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
                // A list view's, tree view's or list box's client edge keeps
                // a 1px ring of its fill inside the border: the client starts
                // at 2,2 (invisible until a selection shows where rows start).
                return (ok ? border : 1) + (cls == QLatin1String("edit") ? 0 : 1);
            }
            break;
        case PM_FocusFrameHMargin:
            // Item text insets (Qt's text margin is this + 1), from XP's
            // controls: 6px from a report view's client edge, 5px after a
            // tree's branches.
            if (const int extra = itemRowExtra(widget); extra > 0 && hasPart({QStringLiteral("listview"), 0, Normal, {}}))
                return extra == 3 ? 4 : 5;
            // A list box's labels sit 2px into the client.
            if (itemRowExtra(widget) == 0 && hasPart({QStringLiteral("listbox"), 0, Normal, {}}))
                return qMax(0, QProxyStyle::pixelMetric(metric, option, widget) - 1);
            break;
        case PM_HeaderMargin:
            // XP's header labels sit the item's content margin (Luna: 3) plus
            // comctl32's 6px in.
            if (hasPart({QStringLiteral("header"), 1, Normal, {}}))
                return m_renderer->contentMargins(inContext({QStringLiteral("header"), 1, Normal, {}})).left() + 6;
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
        case PM_MenuButtonIndicator:
            // A split button's arrow half: TP_SPLITBUTTONDROPDOWN's true
            // width + 2 (13 in Luna), for a toolbar button.
            if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
                button && xpDropDown(button))
                return trueSize({QStringLiteral("toolbar"), 4, Normal, {}}).width() + 2;
            break;
        case PM_ToolBarFrameWidth:
        case PM_ToolBarItemMargin:
        case PM_ToolBarItemSpacing:
            if (hasPart({QStringLiteral("toolbar"), 1, Normal, {}}))
                return 0;   // XP's toolbar buttons sit edge to edge from 0,0
            break;
        case PM_ToolBarSeparatorExtent:
            if (hasPart({QStringLiteral("toolbar"), 5, Normal, {}}))
                return 6;
            break;
        case PM_MenuPanelWidth:
            if (flatMenus())
                return 1;
            break;
        case PM_MenuHMargin:
        case PM_MenuVMargin:
            if (flatMenus())
                return 2;
            break;
        case PM_MenuBarPanelWidth:
        case PM_MenuBarHMargin:
        case PM_MenuBarVMargin:
        case PM_MenuBarItemSpacing:
            if (flatMenus())
                return 0;
            break;
        case PM_TabBarScrollButtonWidth:
            if (hasPart({QStringLiteral("spin"), 3, 1, {}}))
                return sysMetric(u"scrollbarwidth", 17);   // each half of XP's up-down
            break;
        case PM_TabBarTabShiftVertical:
        case PM_TabBarTabShiftHorizontal:
            return 0;
        case PM_TabBarBaseOverlap:
            return 0;
        case PM_TabBarTabHSpace:
            if (hasPart({QStringLiteral("tab"), 1, 1, {}}))
                return 12;   // TCM_SETPADDING's default: 6px either side of the label
            break;
        case PM_TabBarTabVSpace:
            if (hasPart({QStringLiteral("tab"), 1, 1, {}}))
                return 9;    // font height 13 + 9 = 22: 2px headroom, 18px tab, 2 rows over the pane
            break;
        default:
            break;
        }
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

// XP's menu items (Luna and Classic alike, Tahoma 8): text + 4 tall (17),
// separators 9; the label 17px in, 19px of room on the right (the submenu
// arrow's), and 8px before the accelerator column when the menu has one
// (QMenu adds the column itself).
QSize XPlasmaStyle::xpMenuItemSize(const QStyleOptionMenuItem *item, const QSize &size, const QWidget *widget) const
{
    if (item->menuItemType == QStyleOptionMenuItem::Separator)
        return QSize(1, 9);
    bool accelerators = false;
    if (const auto *menu = qobject_cast<const QMenu *>(widget))
        for (const QAction *a : menu->actions())
            accelerators = accelerators || a->text().contains(QLatin1Char('\t')) || !a->shortcut().isEmpty();
    const int h = qMax(xpTextHeight(item->font) + 4, qMin(item->maxIconWidth, 16) + 1);
    return QSize(17 + size.width() + 19 + (accelerators ? 8 : 0), h);
}

// XP's row heights follow the text height (13 for Tahoma 8): list box 13,
// report list view 14, tree view 16 (LB_GETITEMHEIGHT, LVM_GETITEMRECT,
// TVM_GETITEMHEIGHT) -- comctl32's, themed or not.
QSize XPlasmaStyle::xpItemSize(const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    const int extra = itemRowExtra(widget);
    QSize s = QProxyStyle::sizeFromContents(CT_ItemViewItem, option, size, widget);
    int row = xpTextHeight(widget ? widget->font() : QApplication::font()) + extra;
    // Check boxes are 16px state images: a report row fits one plus its
    // 1px, a tree row just the image.
    if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        item && (item->features & QStyleOptionViewItem::HasCheckIndicator) && (extra == 1 || extra == 3))
        row = qMax(row, extra == 1 ? 17 : 16);
    if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option))
        if (item->decorationSize.height() > row && (item->features & QStyleOptionViewItem::HasDecoration))
            return s;   // a larger icon wins, as it does in XP
    s.setHeight(row);
    return s;
}

QSize XPlasmaStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                                     const QWidget *widget) const
{
    const ContextScope context(this, widget, option);
    if (type == CT_LineEdit && (m_renderer || classic()) && !widget && option->styleObject
        && option->styleObject->property("elementType").toString() == QLatin1String("spinbox")) {
        // qqc2-desktop-style measures its SpinBox through CT_LineEdit. Its
        // template reserves 6px above and below the content item; QWidget's
        // smaller edit margins would leave only half a line of visible text.
        QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
        s.setHeight(qMax(s.height(), size.height() + 12));
        return s;
    }
    if (type == CT_ComboBox && (m_renderer || classic()) && widget) {
        // XP's combo box is its font's height + 8 tall: 21 at Tahoma 8.
        QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
        s.setHeight(qMax(s.height(), xpTextHeight(widget->font()) + 8));
        return s;
    }
    if (type == CT_MdiControls && m_renderer && hasPart({QStringLiteral("window"), 20, 1, {}}))
        return QSize(3 * sysMetric(u"menubarwidth", 18), sysMetric(u"menubarheight", 19));
    if (type == CT_MdiControls && (classic() || m_renderer))
        return QSize(3 * (sysMetric(u"menubarwidth", 18) - 2) + 2 + 2, sysMetric(u"menubarheight", 19));
    if (classic()) {
        switch (type) {
        case CT_ItemViewItem:
            if (itemRowExtra(widget) >= 0)
                return xpItemSize(option, size, widget);
            break;
        case CT_HeaderSection: {
            QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
            s.setHeight(xpTextHeight(widget ? widget->font() : QApplication::font()) + 4);   // 17 for Tahoma 8
            return s;
        }
        case CT_TabBarTab:
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
                tab && (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth))
                return xpTabSize(tab, size, widget);
            break;
        case CT_ToolButton:
            if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
                button && widget && qobject_cast<const QToolBar *>(widget->parentWidget()))
                return xpToolButtonSize(button, size);
            break;
        case CT_MenuItem:
            if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option))
                return xpMenuItemSize(item, size, widget);
            break;
        case CT_MenuBarItem:
            if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option))
                // SM_CYMENU: the scheme's menu height (18 for Tahoma 8) and
                // the face-coloured row under it.
                return QSize(size.width() + 12, sysMetric(u"menubarheight", xpTextHeight(item->font) + 5) + 1);
            break;
        default:
            break;
        }
    }
    if (m_renderer) {
        switch (type) {
        case CT_ItemViewItem:
            if (itemRowExtra(widget) >= 0 && hasPart({QStringLiteral("listview"), 0, Normal, {}}))
                return xpItemSize(option, size, widget);
            break;
        case CT_SizeGrip:
            if (const QSize grip = trueSize({QStringLiteral("status"), 3, 0, {}}); grip.isValid() && !grip.isEmpty())
                return grip;
            break;
        case CT_ToolButton:
            if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
                button && hasPart({QStringLiteral("toolbar"), 1, Normal, {}}))
                return xpToolButtonSize(button, size);
            break;
        case CT_MenuItem:
            if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option); item && flatMenus())
                return xpMenuItemSize(item, size, widget);
            break;
        case CT_MenuBarItem:
            if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option); item && flatMenus())
                return QSize(size.width() + 12, xpTextHeight(item->font) + 7);   // 6px either side; 19 tall + the line
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
                    && (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth))
                    return xpTabSize(tab, size, widget);
            }
            break;
        case CT_PushButton: {
            // Label plus the part's ContentMargins -- not its SizingMargins,
            // which are only how the bitmap is sliced -- and room for the
            // focus rectangle, never below MinSize.
            const PartRef ref{QStringLiteral("button"), 1, 1, {}};
            if (hasPart(ref)) {
                const QMargins m = m_renderer->contentMargins(inContext(ref));
                QSize s = size + QSize(m.left() + m.right() + 12, m.top() + m.bottom() + 4);
                return s.expandedTo(m_renderer->partSize(inContext(ref), xpl::PartSize::Min)).expandedTo(QSize(75, 23));
            }
            break;
        }
        default:
            break;
        }
    }
    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

QRect XPlasmaStyle::subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const
{
    const ContextScope context(this, widget, option);
    if (classic() && (element == SE_CheckBoxFocusRect || element == SE_RadioButtonFocusRect))
        return QProxyStyle::subElementRect(element, option, widget).adjusted(2, 0, -2, -1);
    if (m_renderer && (element == SE_CheckBoxFocusRect || element == SE_RadioButtonFocusRect)
        && hasPart({QStringLiteral("button"), element == SE_CheckBoxFocusRect ? 3 : 2, 1, {}})) {
        // XP rings the label 1px tighter than Qt (2px in, 1px down).
        return QProxyStyle::subElementRect(element, option, widget).adjusted(2, 1, -2, -1);
    }
    if (m_renderer && (element == SE_CheckBoxClickRect || element == SE_RadioButtonClickRect)
        && hasPart({QStringLiteral("button"), element == SE_CheckBoxClickRect ? 3 : 2, 1, {}})) {
        // An XP check box or radio button is hot, and takes clicks,
        // anywhere in its window -- not just over its box and label.
        return option->rect;
    }
    if ((classic() || (m_renderer && hasPart({QStringLiteral("spin"), 3, 1, {}}))) && option->rect.width() >= option->rect.height()) {
        // XP scrolls a tab row that doesn't fit with an up-down in its
        // top-right corner: two square halves (PM_TabBarScrollButtonWidth),
        // 1px down, 17px tall for Tahoma 8 (the bar less 5), with no tear
        // marks.
        const int w = proxy()->pixelMetric(PM_TabBarScrollButtonWidth, option, widget);
        const QRect r = option->rect;
        switch (element) {
        case SE_TabBarScrollLeftButton:
            return QRect(r.right() + 1 - 2 * w, r.top() + 1, w, qMax(1, r.height() - 5));
        case SE_TabBarScrollRightButton:
            return QRect(r.right() + 1 - w, r.top() + 1, w, qMax(1, r.height() - 5));
        case SE_TabBarTearIndicatorLeft:
        case SE_TabBarTearIndicatorRight:
            return QRect();
        default:
            break;
        }
    }
    if (classic() || (m_renderer && hasPart({QStringLiteral("tab"), 9, 1, {}}))) {
        if (const auto *tw = qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(option)) {
            // (A document-mode tab widget -- Konsole's, an editor's -- has no
            // pane: lineWidth 0. Its pages fill it, as Qt lays them out.)
            if ((tw->shape == QTabBar::RoundedNorth || tw->shape == QTabBar::TriangularNorth) && tw->lineWidth > 0) {
                // XP: the pane starts where the tabs end (y = 20), and the
                // page sits 4px in at the sides and bottom, 2px from the top
                // (TCM_ADJUSTRECT: 4,22 - 296,116 in a 300 x 120 control).
                // The bar's last two rows overlap the pane's top, so the
                // selected tab can cover it.
                const int top = qMax(0, tw->tabBarSize.height() - 2);
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
    if ((m_renderer || classic()) && element == SE_LineEditContents) {
        const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
        if (frame && frame->lineWidth > 0 && (classic() || hasPart({QStringLiteral("edit"), 1, Normal, {}}))) {
            // A content rectangle describes usable space, not a rendered
            // text line. Keep XP's insets constant as the control grows so
            // native-control bridges can measure borders without a resize
            // feedback loop. Qt positions the text inside this rectangle.
            const QRect r = frame->rect;
            return QRect(r.left() + qMin(4, qMax(0, r.width())),
                         r.top() + qMin(3, qMax(0, r.height())),
                         qMax(0, r.width() - 8), qMax(0, r.height() - 6));
        }
    }
    if (m_renderer && element == SE_HeaderLabel && hasPart({QStringLiteral("header"), 1, Normal, {}})) {
        // The header margin is horizontal only (QCommonStyle would take it
        // off the height too, clipping the label).
        const int m = proxy()->pixelMetric(PM_HeaderMargin, option, widget);
        return option->rect.adjusted(m, 0, -m, 0);
    }
    if ((m_renderer || classic()) && (element == SE_ItemViewItemText || element == SE_ItemViewItemCheckIndicator)
        && (itemRowExtra(widget) == 1 || itemRowExtra(widget) == 3) && (classic() || hasPart({QStringLiteral("button"), 3, 1, {}}))) {
        const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        const bool report = itemRowExtra(widget) == 1;
        if (item && (item->features & QStyleOptionViewItem::HasCheckIndicator)) {
            // XP's check boxes are state images: the 13px box takes the
            // label's place (a report view's 2px down in its 17px row, a
            // tree's 2px down), and the label moves over a 16px cell --
            // 3px more in a report view.
            QStyleOptionViewItem plain(*item);
            plain.features &= ~QStyleOptionViewItem::HasCheckIndicator;
            const QRect text = subElementRect(SE_ItemViewItemText, &plain, widget);
            if (element == SE_ItemViewItemText)
                return text.adjusted(report ? 19 : 16, 0, 0, 0);
            const int margin = proxy()->pixelMetric(PM_FocusFrameHMargin, option, widget) + 1;
            const int top = report ? item->rect.top() + (item->rect.height() - 13) / 2 : item->rect.top() + 2;
            return QRect(text.left() + margin, top, 13, 13);
        }
        if (element == SE_ItemViewItemText) {
            // Labels are centred in their row rounding up, where Qt rounds
            // down: hand Qt a rect exactly one line tall.
            const QRect r = QProxyStyle::subElementRect(element, option, widget);
            const int th = xpTextHeight(widget ? widget->font() : QApplication::font());
            return QRect(r.left(), item ? item->rect.top() + (item->rect.height() - th) / 2 : r.top(), r.width(), th);
        }
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
int XPlasmaStyle::itemRowExtra(const QWidget *widget) const
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
QString XPlasmaStyle::frameClass(const QWidget *widget) const
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
// each level's branch centre is its column's left + 12, the row's middle;
// lines are GrayText dots, each line starting on one; the row's line runs 8px
// right of the centre; the vertical runs down to a next sibling and up to a
// previous item -- never above the first root. The expand box is TVP_GLYPH,
// centred on the branch.
void XPlasmaStyle::drawTreeBranch(const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    const QRect r = option->rect;
    const int cx = r.width() >= 19 ? r.left() + 12 : r.center().x();
    const int cy = r.top() + r.height() / 2;
    const QColor colour = sysColour(u"graytext", option->palette.color(QPalette::Disabled, QPalette::Text));
    // XP's dots aren't a checkerboard: each line starts on a dot -- the
    // horizontal one at the column's centre (so with the odd 19px indent
    // the phase alternates by level), the vertical ones at the row's
    // centre line.
    auto dotsH = [&](int x0, int x1, int y) {
        for (int x = x0; x <= x1; ++x)
            if (!((x - cx) & 1))
                painter->fillRect(x, y, 1, 1, colour);
    };
    auto dotsV = [&](int x, int y0, int y1) {
        for (int y = y0; y <= y1; ++y)
            if (!((y - cy) & 1))
                painter->fillRect(x, y, 1, 1, colour);
    };
    if (option->state & State_Item) {
        // Up to the previous item: a sibling above, or the parent. The
        // option doesn't say, so ask the view about this row's index.
        bool above = true;
        if (const auto *view = qobject_cast<const QAbstractItemView *>(widget)) {
            const QModelIndex index = view->indexAt(QPoint(r.right() + 2, cy));
            above = !index.isValid() || index.row() > 0 || index.parent().isValid();
        }
        if (!above && !(option->state & State_Sibling) && (option->state & State_Children)) {
            // A lone root with a button: XP runs its line from just left of
            // the button, under it, and not on to the label.
            for (int x = cx - 5; x <= cx + 4; x += 2)
                painter->fillRect(x, cy, 1, 1, colour);
        } else {
            dotsH(cx, cx + 8, cy);
        }
        if (above)
            dotsV(cx, r.top(), cy);
        if (option->state & State_Sibling)
            dotsV(cx, cy, r.bottom());
    } else if (option->state & State_Sibling) {
        dotsV(cx, r.top(), r.bottom());   // an ancestor's line passing through
    }
    if ((option->state & State_Children) && classic()) {
        // Classic: a 9px square, the button shadow round the window colour,
        // a 5px minus (or plus) in the text colour.
        const QRect box(cx - 4, cy - 4, 9, 9);
        painter->fillRect(box, enabled(option) ? sysColour(u"window", Qt::white) : sysColour(u"btnface", Qt::lightGray));
        painter->save();
        painter->setPen(sysColour(u"graytext", Qt::gray));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(box.adjusted(0, 0, -1, -1));
        painter->restore();
        const QColor ink = sysColour(u"windowtext", Qt::black);
        painter->fillRect(cx - 2, cy, 5, 1, ink);
        if (!(option->state & State_Open))
            painter->fillRect(cx, cy - 2, 1, 5, ink);
        return;
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
int XPlasmaStyle::xpTextHeight(const QFont &font) const
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

// Label + 12 wide (TCM_GETITEMRECT: 49/70/60 for General/Appearance/
// Advanced in Tahoma 8), 18 tall plus the 2px headroom the selected tab
// rises into; the first tab 2px wider for XP's left inset, the last 2px
// wider on the right where the selected tab's inflation lands. comctl32's,
// themed or Classic.
QSize XPlasmaStyle::xpTabSize(const QStyleOptionTab *tab, const QSize &size, const QWidget *widget) const
{
    QSize s = size;
    if (tab->position == QStyleOptionTab::Beginning || tab->position == QStyleOptionTab::OnlyOneTab)
        s.rwidth() += 2;
    if (tab->position == QStyleOptionTab::End || tab->position == QStyleOptionTab::OnlyOneTab)
        s.rwidth() += 2;
    // 2px headroom, the 18px tab (text + 5), two rows over the pane -- from
    // XP's text height (tmHeight).
    s.setHeight(xpTextHeight(widget ? widget->font() : QApplication::font()) + 9);
    return s;
}

// A north tab's label where XP puts it: centred in the (unselected) tab
// rect, riding up 2px with the selected tab; the focus rect rings that rect
// 1px in, XORed with the button face.
void XPlasmaStyle::drawXpTabLabel(const QStyleOptionTab *tab, QPainter *painter, const QWidget *widget) const
{
    QStyleOptionTab copy(*tab);
    copy.rect = xpTabRect(tab);
    copy.state &= ~State_HasFocus;
    // Tab labels are button text (QCommonStyle draws them as window text).
    for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
        copy.palette.setColor(g, QPalette::WindowText, tab->palette.color(g, QPalette::ButtonText));
    if (tab->state & State_Selected)
        copy.rect.translate(0, -2);
    if (tab->icon.isNull()) {
        // xpTabRect already removes the pane overlap and headroom. The base
        // style removes PM_TabBarTabVSpace once more, clipping hinted glyphs.
        QRect textRect = QProxyStyle::subElementRect(SE_TabBarTabText, &copy, widget);
        textRect.setTop(copy.rect.top());
        textRect.setBottom(copy.rect.bottom());
        const int flags = Qt::AlignCenter | Qt::TextSingleLine
            | (proxy()->styleHint(SH_UnderlineShortcut, tab, widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic);
        proxy()->drawItemText(painter, textRect, flags, copy.palette, enabled(tab), tab->text, QPalette::ButtonText);
    } else {
        QProxyStyle::drawControl(CE_TabBarTabLabel, &copy, painter, widget);
    }
    if ((tab->state & State_HasFocus) && (tab->state & State_KeyboardFocusChange))
        drawFocusDots(painter, xpTabRect(tab).adjusted(1, 1, -1, -1), focusColours(widget).first, focusColours(widget).second);
}

// XP's toolbar buttons (TB_GETITEMRECT, Luna and Classic, Tahoma 8): a
// text-only button is the text + 11 by text + 6 (Back: 33 x 19); an icon
// button is the icon + 7 by + 6 (23 x 22). QToolButton hands in the text
// with a space either side.
QSize XPlasmaStyle::xpToolButtonSize(const QStyleOptionToolButton *button, const QSize &size) const
{
    QSize s;
    if (button->toolButtonStyle == Qt::ToolButtonTextOnly) {
        const int space = QFontMetrics(button->font).horizontalAdvance(QLatin1Char(' '));
        s = QSize(size.width() - 2 * space + 11, xpTextHeight(button->font) + 6);
    } else {
        s = size + QSize(7, 6);
    }
    // Drop-down buttons (measured, Luna Silver, TBSTYLE_EX_DRAWDDARROWS): a
    // split button's main half 6px wider than a plain button (Qt has
    // already added the arrow half, PM_MenuButtonIndicator); a whole
    // drop-down 9px wider for its arrow; both as tall as the arrow part
    // plus 2 each side (27).
    // (Classic: the main half as wide as a plain button, the height kept.)
    if (xpDropDown(button)) {
        const bool split = button->features & QStyleOptionToolButton::MenuButtonPopup;
        s.rwidth() += split ? (classic() ? 0 : 6) : 9;
        if (!classic())
            s.setHeight(qMax(s.height(), trueSize({QStringLiteral("toolbar"), 4, Normal, {}}).height() + 4));
    }
    return s;
}

// A toolbar button with a menu, drawn as XP's drop-down buttons: a split
// button (MenuButtonPopup: TP_SPLITBUTTON and TP_SPLITBUTTONDROPDOWN) or a
// whole drop-down (TP_DROPDOWNBUTTON; Qt's instant and delayed popups).
bool XPlasmaStyle::xpDropDown(const QStyleOptionToolButton *button) const
{
    return (button->features & (QStyleOptionToolButton::HasMenu | QStyleOptionToolButton::MenuButtonPopup))
           && (button->state & State_AutoRaise) && button->arrowType == Qt::NoArrow
           && (classic() || (m_renderer && hasPart({QStringLiteral("toolbar"), 3, Normal, {}})));
}

// comctl32's drop-down arrow: 5/3/1 rows, in the text colour.
static void drawDropArrow(QPainter *painter, QPoint at, const QColor &colour)
{
    for (int i = 0; i < 3; ++i)
        painter->fillRect(at.x() + i, at.y() + i, 5 - 2 * i, 1, colour);
}

void XPlasmaStyle::drawXpDropDownButton(const QStyleOptionToolButton *button, QPainter *painter, const QWidget *widget) const
{
    const bool split = button->features & QStyleOptionToolButton::MenuButtonPopup;
    const bool on = enabled(button);
    const bool sunkenMain = on && (button->state & State_Sunken) && (!split || (button->activeSubControls & SC_ToolButton));
    const bool sunkenArrow = on && split && (button->state & State_Sunken) && (button->activeSubControls & SC_ToolButtonMenu);
    const bool hot = on && ((button->state & State_MouseOver) || sunkenMain || sunkenArrow);
    const bool checked = on && (button->state & State_On);
    // TS_*: normal 1, hot 2, pressed 3, disabled 4, checked 5, hot checked 6.
    int mainState = !on ? Disabled : sunkenMain ? Pressed : checked ? (hot ? 6 : 5) : hot ? Hot : Normal;
    const QRect main = split ? proxy()->subControlRect(CC_ToolButton, button, SC_ToolButton, widget) : button->rect;
    const QRect arrow = split ? proxy()->subControlRect(CC_ToolButton, button, SC_ToolButtonMenu, widget) : QRect();
    const QColor ink = !on ? sysColour(u"graytext", Qt::gray)
                       : classic() ? sysColour(u"btntext", Qt::black)
                                   : themeColour({QStringLiteral("toolbar"), 0, Normal, {}}, u"textcolor",
                                                 button->palette.color(QPalette::ButtonText));
    if (classic()) {
        // Classic (measured): each half a 1px raised edge when hot, both
        // sunken when held (the glyph moving with them); the glyph 4px into
        // the arrow half, or 4px from a whole drop-down's edge, a row below
        // the middle.
        const bool held = sunkenMain || sunkenArrow;
        auto edge = [&](const QRect &q) {
            if (!hot && !held && !checked)
                return;
            const QColor light = sysColour(u"btnhighlight", Qt::white), dark = sysColour(u"btnshadow", Qt::gray);
            const QColor tl = (held || checked) ? dark : light, br = (held || checked) ? light : dark;
            painter->fillRect(QRect(q.left(), q.top(), q.width() - 1, 1), tl);
            painter->fillRect(QRect(q.left(), q.top(), 1, q.height() - 1), tl);
            painter->fillRect(QRect(q.left(), q.bottom(), q.width(), 1), br);
            painter->fillRect(QRect(q.right(), q.top(), 1, q.height()), br);
        };
        edge(main);
        const QPoint shift = held ? QPoint(1, 1) : QPoint();
        if (split) {
            edge(arrow);
            drawDropArrow(painter, QPoint(arrow.left() + 4, arrow.top() + (arrow.height() - 3) / 2 + 1) + shift, ink);
        } else {
            drawDropArrow(painter, QPoint(main.right() - 4 - 4, main.top() + (main.height() - 3) / 2 + 1) + shift, ink);
        }
        const int th = xpTextHeight(button->font);
        QRect text(main.left() + 2, main.top() + (main.height() - th) / 2, main.width() - 2, th);
        text.translate(shift);
        QPalette pal = button->palette;
        pal.setColor(QPalette::ButtonText, ink);
        painter->save();
        painter->setFont(button->font);
        if (button->toolButtonStyle == Qt::ToolButtonTextOnly) {
            proxy()->drawItemText(painter, text, Qt::AlignLeft | Qt::AlignTop | Qt::TextShowMnemonic, pal, on,
                                  button->text, QPalette::ButtonText);
        } else {
            QStyleOptionToolButton label(*button);
            label.rect = (split ? main : main.adjusted(0, 0, -9, 0)).translated(shift);
            label.features &= ~(QStyleOptionToolButton::HasMenu | QStyleOptionToolButton::MenuButtonPopup);
            label.state &= ~(State_Sunken | State_On);
            QProxyStyle::drawControl(CE_ToolButtonLabel, &label, painter, widget);
        }
        painter->restore();
        return;
    }
    if (mainState != Normal && mainState != Disabled)
        drawPart(painter, {QStringLiteral("toolbar"), split ? 3 : 2, mainState, {}}, main);
    if (split) {
        // The arrow half lights with the button; at rest only its glyph
        // shows, 3px in and centred (the part's glyph when lit). A whole
        // drop-down's arrow sits 4px from its right edge, a row lower.
        // (Held, the whole split button is pressed art, both halves.)
        const int arrowState = !on ? Disabled : (sunkenArrow || sunkenMain) ? Pressed : hot ? Hot : Normal;
        if (arrowState == Hot || arrowState == Pressed)
            drawPart(painter, {QStringLiteral("toolbar"), 4, arrowState, {}}, arrow);
        else
            drawDropArrow(painter, QPoint(arrow.left() + 3, arrow.top() + (arrow.height() - 3) / 2), ink);
    } else {
        drawDropArrow(painter, QPoint(main.right() - 4 - 4, main.top() + (main.height() - 3) / 2 + 1 + (sunkenMain ? 1 : 0)), ink);
    }
    // The label: TBSTYLE_LIST text 6px into a split button's main half, 2px
    // into a whole drop-down, centred vertically; 1px down and right held.
    QStyleOptionToolButton label(*button);
    label.rect = split ? main : main.adjusted(0, 0, -9, 0);
    label.features &= ~(QStyleOptionToolButton::HasMenu | QStyleOptionToolButton::MenuButtonPopup);
    label.state &= ~(State_Sunken | State_On);
    if (sunkenMain)
        label.state |= State_Sunken;
    if (button->toolButtonStyle == Qt::ToolButtonTextOnly) {
        if (sunkenMain && m_renderer)
            label.palette.setColor(QPalette::ButtonText, themeColour({QStringLiteral("toolbar"), 0, Pressed, {}}, u"textcolor", ink));
        else
            label.palette.setColor(QPalette::ButtonText, ink);
        const int th = xpTextHeight(button->font);
        QRect text(label.rect.left() + (split ? 6 : 2), label.rect.top() + (label.rect.height() - th) / 2,
                   label.rect.width() - (split ? 6 : 2), th);
        if (sunkenMain)
            text.translate(1, 1);
        painter->save();
        painter->setFont(button->font);
        proxy()->drawItemText(painter, text, Qt::AlignLeft | Qt::AlignTop | Qt::TextShowMnemonic, label.palette, on,
                              button->text, QPalette::ButtonText);
        painter->restore();
    } else {
        if (sunkenMain)
            label.rect.translate(1, 1);
        QProxyStyle::drawControl(CE_ToolButtonLabel, &label, painter, widget);
    }
    if ((button->state & State_HasFocus) && (button->state & State_KeyboardFocusChange)) {
        QStyleOptionFocusRect focus;
        focus.QStyleOption::operator=(*button);
        focus.rect = button->rect.adjusted(3, 3, -3, -3);
        proxy()->drawPrimitive(PE_FrameFocusRect, &focus, painter, widget);
    }
}

// XP presses a toolbar button's contents 1px down and right; TBSTYLE_LIST
// text sits 2px into the button, centred vertically. True when drawn.
bool XPlasmaStyle::drawXpToolButtonLabel(const QStyleOptionToolButton *button, QPainter *painter, const QWidget *widget) const
{
    const bool down = (button->state & (State_Sunken | State_On)) && enabled(button);
    // A button held down takes the class-level [Toolbar(Pressed)] TextColor
    // (comctl32 asks for part 0): Luna and Zune white, Royale and Embedded
    // black; Classic keeps the button text.
    QStyleOptionToolButton held(*button);
    if ((button->state & State_Sunken) && enabled(button) && m_renderer && widget
        && qobject_cast<const QToolBar *>(widget->parentWidget()))
        held.palette.setColor(QPalette::ButtonText, themeColour({QStringLiteral("toolbar"), 0, Pressed, {}}, u"textcolor",
                                                                button->palette.color(QPalette::ButtonText)));
    button = &held;
    if (button->toolButtonStyle == Qt::ToolButtonTextOnly && button->arrowType == Qt::NoArrow) {
        // (CC_ToolButton hands us the button inset by the frame width.)
        const int fw = proxy()->pixelMetric(PM_DefaultFrameWidth, button, widget);
        const QRect whole = button->rect.adjusted(-fw, -fw, fw, fw);
        const int th = xpTextHeight(button->font);
        QRect text(whole.left() + 2, whole.top() + (whole.height() - th) / 2, whole.width() - 2, th);
        if (down)
            text.translate(1, 1);
        painter->save();
        painter->setFont(button->font);
        proxy()->drawItemText(painter, text, Qt::AlignLeft | Qt::AlignTop | Qt::TextShowMnemonic, button->palette,
                              enabled(button), button->text, QPalette::ButtonText);
        painter->restore();
        return true;
    }
    if (down) {
        painter->save();
        painter->translate(1, 1);
        QProxyStyle::drawControl(CE_ToolButtonLabel, button, painter, widget);
        painter->restore();
        return true;
    }
    return false;
}

// A header section as QCommonStyle composes it (section, label, arrow), but
// with XP's sort arrow: a GrayText triangle (9 x 5 themed, 7 x 4 Classic)
// 9px past the label, centred in the item's content rect, pointing up for an
// ascending sort (HDF_SORTUP; Qt calls ascending SortDown). A sorted label
// moves right by the item's left content margin (Luna 3, Royale 0).
void XPlasmaStyle::drawXpHeader(const QStyleOptionHeader *header, QPainter *painter, const QWidget *widget) const
{
    painter->save();
    painter->setClipRect(header->rect, Qt::IntersectClip);
    proxy()->drawControl(CE_HeaderSection, header, painter, widget);
    const int m = proxy()->pixelMetric(PM_HeaderMargin, header, widget);
    const bool sorted = header->sortIndicator != QStyleOptionHeader::None && header->orientation == Qt::Horizontal
                        && (header->textAlignment & Qt::AlignLeft);
    const QMargins cm = m_renderer ? m_renderer->contentMargins(inContext({QStringLiteral("header"), 1, Normal, {}})) : QMargins();
    // (QCommonStyle's SE_HeaderLabel, which its CE_Header uses unproxied.)
    QStyleOptionHeader label(*header);
    label.rect = header->rect.adjusted(m, m, -m, -m);
    if (sorted)
        label.rect.translate(cm.left(), 0);
    if (label.rect.isValid())
        proxy()->drawControl(CE_HeaderLabel, &label, painter, widget);
    if (sorted) {
        const QSize size = classic() ? QSize(7, 4) : QSize(9, 5);
        const int origin = header->rect.left() + m + cm.left();
        const int avail = header->rect.height() - cm.top() - cm.bottom();
        const QRect arrow(QPoint(origin + header->fontMetrics.horizontalAdvance(header->text) + 9,
                                 header->rect.top() + cm.top() + (avail - size.height()) / 2), size);
        const bool up = header->sortIndicator == QStyleOptionHeader::SortDown;
        const QColor colour = sysColour(u"graytext", header->palette.color(QPalette::Disabled, QPalette::Text));
        for (int i = 0; i < size.height(); ++i) {
            const int w = up ? 1 + 2 * i : size.width() - 2 * i;
            painter->fillRect(arrow.left() + (size.width() - w) / 2, arrow.top() + i, w, 1, colour);
        }
    }
    painter->restore();
}

// A north tab's rect as XP lays it out, inside Qt's: 2px down (the tab bar
// is 20px tall), and for the first tab 2px in from the left (Qt's first tab
// is 2px wider to make room, so every later tab lands where XP puts it).
QRect XPlasmaStyle::xpTabRect(const QStyleOptionTab *tab) const
{
    // The bar is 22px: 2px headroom, the 18px tab, and two rows over the
    // pane's top, which only the selected tab covers.
    QRect r(tab->rect.left(), tab->rect.top() + 2, tab->rect.width(), qMax(0, tab->rect.height() - 4));
    if (tab->position == QStyleOptionTab::Beginning || tab->position == QStyleOptionTab::OnlyOneTab)
        r.setLeft(r.left() + 2);
    if (tab->position == QStyleOptionTab::End || tab->position == QStyleOptionTab::OnlyOneTab)
        r.setRight(r.right() - 2);
    return r;
}

bool XPlasmaStyle::flatMenus() const
{
    return m_renderer && xpl::parseBool(m_renderer->theme().property(u"sysmetrics", u"", u"", u"flatmenus").value_or(QString()));
}


// A flat menu item, as XP draws them with SPI_GETFLATMENU on (every XP
// visual style sets FlatMenus): measured against Luna Silver.
void XPlasmaStyle::drawMenuItem(const QStyleOptionMenuItem *item, QPainter *painter, const QWidget *widget) const
{
    const QRect r = item->rect;
    const bool flat = !classic();
    painter->fillRect(r, sysColour(u"menu", Qt::white));
    if (item->menuItemType == QStyleOptionMenuItem::Separator) {
        // Flat: one shadow line; Classic: etched (the highlight below it).
        painter->fillRect(QRect(r.left() + 1, r.top() + 3, r.width() - 2, 1), sysColour(u"btnshadow", QColor(172, 168, 153)));
        if (!flat)
            painter->fillRect(QRect(r.left() + 1, r.top() + 4, r.width() - 2, 1), sysColour(u"btnhighlight", Qt::white));
        return;
    }
    const bool selected = item->state & State_Selected;
    if (selected && flat) {
        painter->fillRect(r, sysColour(u"menuhilight", QColor(49, 106, 197)));
        painter->save();
        painter->setPen(sysColour(u"highlight", QColor(49, 106, 197)));
        painter->drawRect(r.adjusted(0, 0, -1, -1));
        painter->restore();
    } else if (selected) {
        painter->fillRect(r, sysColour(u"highlight", QColor(10, 36, 106)));   // Classic: a plain highlight
    }
    // Classic's disabled text is etched (the highlight 1px down-right) --
    // unless selected, and only while the menu is the button face's colour.
    const bool etched = !flat && !enabled(item) && !selected
                        && sysColour(u"menu", Qt::white) == sysColour(u"btnface", Qt::lightGray);
    const QColor fg = !enabled(item) ? sysColour(u"graytext", QColor(172, 168, 153))
                      : selected     ? sysColour(u"highlighttext", Qt::white)
                                     : sysColour(u"menutext", Qt::black);

    // XP shell context menus are text-only. Retain the check/bullet gutter
    // and submenu arrows, rather than painting application-supplied icons.
    if (item->checked && item->checkType == QStyleOptionMenuItem::Exclusive) {
        drawGlyph(painter, kMenuBullet, QPoint(r.left() + 5, r.top() + (r.height() - 5) / 2), fg);
    } else if (item->checked && item->checkType != QStyleOptionMenuItem::NotCheckable) {
        drawGlyph(painter, kMenuCheck, QPoint(r.left() + 4, r.top() + (r.height() - 7 + 1) / 2 + 1), fg);
    }

    QString text = item->text, accel;
    if (const int tab = text.indexOf(QLatin1Char('\t')); tab >= 0) {
        accel = text.mid(tab + 1);
        text.truncate(tab);
    }
    const int th = xpTextHeight(item->font);
    const int flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine | Qt::TextDontClip
                      | (proxy()->styleHint(SH_UnderlineShortcut, item, widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic);
    painter->save();
    painter->setFont(item->font);
    // GDI's centring puts the odd pixel below: 17px items put the text 1 down.
    const int top = r.top() + (r.height() - th - 1) / 2;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const int column = item->reservedShortcutWidth;
#else
    const int column = item->tabWidth;
#endif
    auto texts = [&](QPoint o) {
        painter->drawText(QRect(r.left() + 17, top, r.width() - 17, th).translated(o), flags, text);
        // The accelerator left-aligned in a column that ends 19px from the right.
        if (!accel.isEmpty())
            painter->drawText(QRect(r.right() + 1 - 19 - column, top, column + 19, th).translated(o), flags, accel);
    };
    if (etched) {
        painter->setPen(sysColour(u"btnhighlight", Qt::white));
        texts(QPoint(1, 1));
    }
    painter->setPen(fg);
    texts(QPoint());
    painter->restore();
    if (item->menuItemType == QStyleOptionMenuItem::SubMenu)
        drawGlyph(painter, kMenuArrow, QPoint(r.right() - 9, r.top() + (r.height() - 8) / 2), fg);
}

// A menu bar title: flat, and when hot or open, the menu highlight inside
// a 1px highlight frame.
void XPlasmaStyle::drawMenuBarItem(const QStyleOptionMenuItem *item, QPainter *painter, const QWidget *widget) const
{
    // (The item's last row is the menu-coloured line under the bar.)
    const QRect r = item->rect.adjusted(0, 0, 0, -1);
    painter->fillRect(r, sysColour(u"menubar", item->palette.window().color()));
    painter->fillRect(QRect(r.left(), r.bottom() + 1, r.width(), 1), sysColour(u"menu", Qt::white));
    const bool active = enabled(item) && (item->state & (State_Selected | State_Sunken));
    if (active) {
        painter->fillRect(r, sysColour(u"menuhilight", QColor(49, 106, 197)));
        painter->save();
        painter->setPen(sysColour(u"highlight", QColor(49, 106, 197)));
        painter->drawRect(r.adjusted(0, 0, -1, -1));
        painter->restore();
    }
    const QColor fg = !enabled(item) ? sysColour(u"graytext", QColor(172, 168, 153))
                      : active       ? sysColour(u"highlighttext", Qt::white)
                                     : sysColour(u"menutext", Qt::black);
    const int th = xpTextHeight(item->font);
    painter->save();
    painter->setFont(item->font);
    painter->setPen(fg);
    painter->drawText(QRect(r.left() + 6, r.top() + (r.height() - th - 1) / 2, r.width() - 6, th),
                      Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine | Qt::TextDontClip
                          | (proxy()->styleHint(SH_UnderlineShortcut, item, widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic),
                      item->text);
    painter->restore();
}

static void classicScrollButton(QPainter *p, const QRect &r, const QPalette &pal, bool pressed);
static void classicPushEdge(QPainter *p, const QRect &r, const QPalette &pal, bool pressed);

// EDGE_SUNKEN: the shadow and highlight outside, the dark shadow and Light3D
// inside.
static void classicSunken(QPainter *p, const QRect &r, const QPalette &pal)
{
    xpl::classic::sunkenEdge(p, r, pal);
}

// DrawFrameControl's push button (see core/classic).
static void classicPushEdge(QPainter *p, const QRect &r, const QPalette &pal, bool pressed)
{
    xpl::classic::pushEdge(p, r, pal, pressed);
}
static void classicArrow(QPainter *p, const QRect &r, Qt::ArrowType dir, const QPalette &pal, bool enabled, bool pressed);

// Windows Classic (visual styles off), control by control against XP's own
// classic controls in the scheme's colours. True when drawn.
bool XPlasmaStyle::drawClassicControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                                      const QWidget *widget) const
{
    switch (element) {
    case CE_PushButtonBevel:
        if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option)) {
            // DrawFrameControl(DFCS_BUTTONPUSH); the default button inside a
            // WindowFrame line, pressed as a single shadow line within it.
            QRect r = btn->rect;
            const bool pressed = btn->state & (State_Sunken | State_On);
            if (btn->features & QStyleOptionButton::DefaultButton) {
                painter->save();
                painter->setPen(sysColour(u"windowframe", Qt::black));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(r.adjusted(0, 0, -1, -1));
                r.adjust(1, 1, -1, -1);
                if (pressed) {
                    painter->setPen(btn->palette.color(QPalette::Dark));
                    painter->drawRect(r.adjusted(0, 0, -1, -1));
                    painter->fillRect(r.adjusted(1, 1, -1, -1), btn->palette.button());
                    painter->restore();
                    return true;
                }
                painter->restore();
            }
            classicPushEdge(painter, r, btn->palette, pressed);
            return true;
        }
        return false;
    case CE_PushButton:
        if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option); btn && (btn->state & State_HasFocus)) {
            // The focus rect 1px further in on a default button (its bevel
            // sits inside the frame line); QCommonStyle places it itself.
            QStyleOptionButton copy(*btn);
            copy.state &= ~State_HasFocus;
            QProxyStyle::drawControl(element, &copy, painter, widget);
            QStyleOptionFocusRect focus;
            focus.QStyleOption::operator=(*btn);
            focus.rect = QProxyStyle::subElementRect(SE_PushButtonFocusRect, btn, widget);
            if (btn->features & QStyleOptionButton::DefaultButton)
                focus.rect.adjust(1, 1, -1, -1);
            proxy()->drawPrimitive(PE_FrameFocusRect, &focus, painter, widget);
            return true;
        }
        return false;
    case CE_ScrollBarAddPage:
    case CE_ScrollBarSubPage:
        // Held down, the track's halftone is inverted.
        if (option->state & State_Sunken) {
            if (const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
                auto inverse = [](const QColor &c) { return QColor(255 - c.red(), 255 - c.green(), 255 - c.blue()); };
                QStyleOptionSlider copy(*bar);
                // (QWindowsStyle: sunken, the shadow's halftone over the dark.)
                copy.palette.setColor(QPalette::Shadow, inverse(bar->palette.color(QPalette::Light)));
                copy.palette.setColor(QPalette::Dark, inverse(bar->palette.color(QPalette::Window)));
                QProxyStyle::drawControl(element, &copy, painter, widget);
                return true;
            }
        }
        return false;
    case CE_ScrollBarSlider:
        // The thumb: the raised scroll edge (it never looks pressed). A
        // disabled scroll bar has none, only the track.
        if (!enabled(option)) {
            if (const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
                QStyleOptionSlider copy(*bar);
                copy.state &= ~State_Sunken;
                copy.activeSubControls = SC_None;
                proxy()->drawControl(CE_ScrollBarAddPage, &copy, painter, widget);
            }
            return true;
        }
        classicScrollButton(painter, option->rect, option->palette, false);
        return true;
    case CE_ScrollBarAddLine:
    case CE_ScrollBarSubLine:
        if (const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            // DFCS_SCROLL*: the raised scroll edge and Marlett's arrow.
            const bool horizontal = bar->orientation == Qt::Horizontal;
            const bool add = element == CE_ScrollBarAddLine;
            const Qt::ArrowType dir = horizontal ? (add ? Qt::RightArrow : Qt::LeftArrow) : (add ? Qt::DownArrow : Qt::UpArrow);
            const bool pressed = sunken(bar) && (bar->activeSubControls & (add ? SC_ScrollBarAddLine : SC_ScrollBarSubLine));
            classicScrollButton(painter, bar->rect, bar->palette, pressed);
            classicArrow(painter, bar->rect, dir, bar->palette, enabled(bar), pressed);
            return true;
        }
        return false;
    case CE_MenuItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            drawMenuItem(item, painter, widget);
            return true;
        }
        return false;
    case CE_ProgressBar:
        if (const auto *bar = qstyleoption_cast<const QStyleOptionProgressBar *>(option)) {
            // comctl32's classic progress: a 1px sunken edge, a face-coloured
            // ring, highlight blocks two-thirds the inside's height wide with
            // a 2px gap, as many as the value reaches into.
            const QRect r = bar->rect;
            painter->fillRect(r, bar->palette.button());
            painter->fillRect(QRect(r.left(), r.top(), r.width() - 1, 1), bar->palette.color(QPalette::Dark));
            painter->fillRect(QRect(r.left(), r.top(), 1, r.height() - 1), bar->palette.color(QPalette::Dark));
            painter->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), bar->palette.color(QPalette::Light));
            painter->fillRect(QRect(r.right(), r.top(), 1, r.height()), bar->palette.color(QPalette::Light));
            const QRect inner = r.adjusted(2, 2, -2, -2);
            const bool horizontal = bar->state & State_Horizontal;
            // The filled length is the value's share of the whole client
            // (inside the 1px edge), rounded; then every block it starts.
            const int length = horizontal ? r.width() - 2 : r.height() - 2;
            const int thickness = horizontal ? inner.height() : inner.width();
            const int block = qMax(1, thickness * 2 / 3), pitch = block + 2;
            const qint64 range = qint64(bar->maximum) - bar->minimum;
            const int filled = range > 0 ? int(((qint64(bar->progress) - bar->minimum) * length * 2 + range) / (2 * range)) : 0;
            const int blocks = (filled + pitch - 1) / pitch;
            painter->save();
            painter->setClipRect(inner, Qt::IntersectClip);
            for (int i = 0, at = 0; i < blocks; ++i, at += pitch) {
                const QRect chunk = horizontal ? QRect(inner.left() + at, inner.top(), block, inner.height())
                                               : QRect(inner.left(), inner.bottom() + 1 - at - block, inner.width(), block);
                painter->fillRect(chunk, bar->palette.brush(QPalette::Highlight));
            }
            painter->restore();
            return true;
        }
        return false;
    case CE_ItemViewItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option))
            return drawXpViewItem(item, painter, widget);
        return false;
    case CE_MenuEmptyArea:
        painter->fillRect(option->rect, option->palette.button());   // Classic's padding: the face
        return true;
    case CE_SizeGrip: {
        // Over a face-filled square (it cuts the pane's edge), the lines
        // 1px in from Qt's corner.
        painter->fillRect(option->rect, option->palette.button());
        painter->save();
        painter->setClipRect(option->rect.adjusted(0, 0, -1, -1), Qt::IntersectClip);   // 1px short of the edges
        painter->translate(-1, 0);
        QProxyStyle::drawControl(element, option, painter, widget);
        painter->restore();
        return true;
    }
    case CE_TabBarTabShape:
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
            tab && (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth)) {
            // comctl32's classic tab: the highlight up the left and along the
            // top with 1px diagonal corners, the shadow and dark shadow down
            // the right; the selected one inflated over the pane's top edge.
            QRect r = xpTabRect(tab);
            if (tab->state & State_Selected)
                r.adjust(-2, -2, 2, 2);
            const QColor light = tab->palette.color(QPalette::Light), dark = tab->palette.color(QPalette::Dark),
                         shadow = tab->palette.color(QPalette::Shadow);
            painter->save();
            if (const auto *bar = qobject_cast<const QTabBar *>(widget))
                for (const QToolButton *b : bar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
                    if (b->isVisible() && b->arrowType() == Qt::LeftArrow)
                        painter->setClipRect(QRect(0, 0, b->x(), bar->height()), Qt::IntersectClip);
            // (The selected tab's edges stop a row short of its foot, where its
            // face and inner light run on over the pane's edge.)
            const bool selected = tab->state & State_Selected;
            const int edge = r.height() - 2 - (selected ? 1 : 0);
            painter->fillRect(QRect(r.left() + 1, r.top() + 1, r.width() - (selected ? 1 : 2), r.height() - (selected ? 2 : 1)),
                              tab->palette.button());
            painter->fillRect(QRect(r.left(), r.top() + 2, 1, edge), light);
            painter->fillRect(r.left() + 1, r.top() + 1, 1, 1, light);
            painter->fillRect(QRect(r.left() + 2, r.top(), r.width() - 4, 1), light);
            // ...with Light3D just inside the highlight.
            const QColor light3d = tab->palette.color(QPalette::Midlight);
            painter->fillRect(QRect(r.left() + 2, r.top() + 1, r.width() - 4, 1), light3d);
            painter->fillRect(QRect(r.left() + 1, r.top() + 2, 1, edge), light3d);
            // The selected tab's foot covers the pane's top edge, all but
            // the pane's own two left-hand columns.
            if (selected) {
                const int left = qMax(r.left(), 2);
                painter->fillRect(QRect(left, r.bottom(), r.right() - left + 1, 1), tab->palette.button());
            }
            painter->fillRect(r.right() - 1, r.top() + 1, 1, 1, shadow);
            painter->fillRect(QRect(r.right() - 1, r.top() + 2, 1, edge), dark);
            painter->fillRect(QRect(r.right(), r.top() + 2, 1, edge), shadow);
            painter->restore();
            return true;
        }
        return false;
    case CE_TabBarTabLabel:
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
            tab && (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth)) {
            drawXpTabLabel(tab, painter, widget);
            return true;
        }
        return false;
    case CE_ToolButtonLabel:
        if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
            button && widget && qobject_cast<const QToolBar *>(widget->parentWidget()))
            return drawXpToolButtonLabel(button, painter, widget);
        return false;
    case CE_Header:
        if (const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option)) {
            drawXpHeader(header, painter, widget);
            return true;
        }
        return false;
    case CE_HeaderSection:
        // DFCS_BUTTONPUSH's edge; pressed, a flat shadow line.
        if (option->state & State_Sunken) {
            painter->fillRect(option->rect, option->palette.button());
            painter->save();
            painter->setPen(option->palette.color(QPalette::Dark));
            painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
            painter->restore();
        } else {
            classicPushEdge(painter, option->rect, option->palette, false);
        }
        return true;
    case CE_MenuBarEmptyArea:
        // (The bar's last row: the face-coloured line, under the items too.)
        painter->fillRect(option->rect, sysColour(u"menu", option->palette.color(QPalette::Button)));
        painter->fillRect(QRect(option->rect.left(), option->rect.bottom(), option->rect.width(), 1),
                          sysColour(u"btnface", Qt::lightGray));
        return true;
    case CE_HeaderEmptyArea: {
        // Past the last column: another raised section, running off the
        // right edge.
        // (QHeaderView hands over its whole viewport and paints the
        // sections over it: start where the columns end.)
        QRect area = option->rect;
        if (const auto *header = qobject_cast<const QHeaderView *>(widget); header && header->orientation() == Qt::Horizontal)
            area.setLeft(qMax(area.left(), header->length() - header->offset()));
        QStyleOptionHeader section;
        section.QStyleOption::operator=(*option);
        section.rect = area.adjusted(0, 0, 2, 0);
        section.state &= ~(State_Sunken | State_On | State_MouseOver);
        section.state |= State_Raised;
        painter->save();
        painter->setClipRect(area, Qt::IntersectClip);
        proxy()->drawControl(CE_HeaderSection, &section, painter, widget);
        painter->restore();
        return true;
    }
    case CE_HeaderLabel:
        if (const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option)) {
            // Centred (rounding up); QCommonStyle takes PM_HeaderMargin off
            // every side, so hand it a rect one line tall plus that margin.
            const int m = proxy()->pixelMetric(PM_HeaderMargin, option, widget);
            const int th = xpTextHeight(widget ? widget->font() : QApplication::font());
            const int top = header->rect.top() + (header->rect.height() - th) / 2;
            QStyleOptionHeader copy(*header);
            copy.rect = QRect(header->rect.left(), top - m, header->rect.width(), th + 2 * m);
            if (!(copy.textAlignment & Qt::AlignVertical_Mask))   // (see the themed CE_HeaderLabel)
                copy.textAlignment |= Qt::AlignVCenter;
            if (header->state & State_Sunken)
                copy.rect.translate(1, 1);
            QProxyStyle::drawControl(element, &copy, painter, widget);
            return true;
        }
        return false;
    case CE_MenuBarItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            // Hot: a raised 1px edge; open: sunken, the title pushed 1px.
            // (The item's last row is the face-coloured line under the bar.)
            const QRect r = item->rect.adjusted(0, 0, 0, -1);
            painter->fillRect(r, sysColour(u"menu", item->palette.color(QPalette::Button)));
            painter->fillRect(QRect(r.left(), r.bottom() + 1, r.width(), 1), sysColour(u"btnface", Qt::lightGray));
            const bool open = item->state & State_Sunken;
            const bool hot = enabled(item) && (item->state & (State_Selected | State_Sunken));
            if (hot) {
                const QColor tl = open ? item->palette.color(QPalette::Dark) : item->palette.color(QPalette::Light);
                const QColor br = open ? item->palette.color(QPalette::Light) : item->palette.color(QPalette::Dark);
                painter->fillRect(QRect(r.left(), r.top(), r.width() - 1, 1), tl);
                painter->fillRect(QRect(r.left(), r.top(), 1, r.height() - 1), tl);
                painter->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), br);
                painter->fillRect(QRect(r.right(), r.top(), 1, r.height()), br);
            }
            const int th = xpTextHeight(item->font);
            QRect text(r.left() + 6, r.top() + (r.height() - th) / 2, r.width() - 6, th);
            if (open)
                text.translate(1, 1);
            painter->save();
            painter->setFont(item->font);
            QPalette menuPal = item->palette;
            menuPal.setColor(QPalette::Active, QPalette::ButtonText, sysColour(u"menutext", Qt::black));
            menuPal.setColor(QPalette::Inactive, QPalette::ButtonText, sysColour(u"menutext", Qt::black));
            proxy()->drawItemText(painter, text,
                                  Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine
                                      | (proxy()->styleHint(SH_UnderlineShortcut, item, widget) ? Qt::TextShowMnemonic
                                                                                               : Qt::TextHideMnemonic),
                                  menuPal, enabled(item), item->text, QPalette::ButtonText);
            painter->restore();
            return true;
        }
        return false;
    case CE_ComboBoxLabel:
        if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option); combo && !combo->editable) {
            // QCommonStyle insets the label 1px into the field; XP draws it
            // at the field's edge -- and disabled, plain GrayText, unetched.
            // (And clips it at the field's edge: see the visual style's.)
            QStyleOptionComboBox copy(*combo);
            copy.palette.setColor(QPalette::Disabled, QPalette::Light, copy.palette.color(QPalette::Disabled, QPalette::Button));
            copy.rect.adjust(0, 0, 2, 0);
            painter->save();
            painter->translate(-1, 0);
            QProxyStyle::drawControl(element, &copy, painter, widget);
            painter->restore();
            return true;
        }
        return false;
    case CE_CheckBoxLabel:
    case CE_RadioButtonLabel:
        if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option)) {
            // GDI puts the label a line higher than Qt; disabled, it's
            // etched in the shadow over the highlight (DSS_DISABLED).
            QStyleOptionButton copy(*btn);
            copy.rect.translate(0, -1);
            copy.palette.setColor(QPalette::Disabled, QPalette::WindowText, copy.palette.color(QPalette::Dark));
            QProxyStyle::drawControl(element, &copy, painter, widget);
            return true;
        }
        return false;
    case CE_CheckBox:
    case CE_RadioButton:
        if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option); btn && (btn->state & State_HasFocus)) {
            QStyleOptionButton copy(*btn);
            copy.state &= ~State_HasFocus;
            QProxyStyle::drawControl(element, &copy, painter, widget);
            QStyleOptionFocusRect focus;
            focus.QStyleOption::operator=(*btn);
            focus.rect = proxy()->subElementRect(element == CE_CheckBox ? SE_CheckBoxFocusRect : SE_RadioButtonFocusRect,
                                                 btn, widget);
            proxy()->drawPrimitive(PE_FrameFocusRect, &focus, painter, widget);
            return true;
        }
        return false;
    case CE_PushButtonLabel:
        if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option)) {
            // The label centred as GDI centres it (a line above where Qt
            // would), pressed 1px down and right.
            QStyleOptionButton copy(*btn);
            const int th = xpTextHeight(widget ? widget->font() : QApplication::font());
            copy.rect = QRect(btn->rect.left(), btn->rect.top() + (btn->rect.height() - th) / 2 - 1, btn->rect.width(), th);
            copy.palette.setColor(QPalette::Disabled, QPalette::ButtonText, copy.palette.color(QPalette::Dark));   // etched
            if (btn->state & (State_Sunken | State_On))
                copy.rect.translate(1, 1);
            copy.state &= ~(State_Sunken | State_On);
            QProxyStyle::drawControl(element, &copy, painter, widget);
            return true;
        }
        return false;
    default:
        return false;
    }
}

// The raised edge DrawFrameControl gives scroll-type buttons (EDGE_RAISED:
// Light3D and DkShadow outside, Highlight and Shadow inside); pressed, a
// flat shadow line.
static void classicScrollButton(QPainter *p, const QRect &r, const QPalette &pal, bool pressed)
{
    p->fillRect(r, pal.button());
    auto frame = [&](const QRect &q, const QColor &tl, const QColor &br) {
        p->fillRect(QRect(q.left(), q.top(), q.width() - 1, 1), tl);
        p->fillRect(QRect(q.left(), q.top(), 1, q.height() - 1), tl);
        p->fillRect(QRect(q.left(), q.bottom(), q.width(), 1), br);
        p->fillRect(QRect(q.right(), q.top(), 1, q.height()), br);
    };
    if (pressed) {
        frame(r, pal.color(QPalette::Dark), pal.color(QPalette::Dark));
        return;
    }
    frame(r, pal.color(QPalette::Midlight), pal.color(QPalette::Shadow));
    frame(r.adjusted(1, 1, -1, -1), pal.color(QPalette::Light), pal.color(QPalette::Dark));
}

// Marlett's scroll arrows: 7px triangles, 4 deep, placed about the
// button's centre c as XP does (measured in a 17px button: left c-3..c,
// right c-1..c+2, up c-2..c+1, down c-1..c+2). Pressed, 1px down-right;
// disabled, etched -- the highlight 1px down-right under the shadow.
static void classicArrow(QPainter *p, const QRect &r, Qt::ArrowType dir, const QPalette &pal, bool enabled, bool pressed)
{
    // Marlett's arrows sit in the button's s x s square (s the smaller
    // side): across it at (s - 1) / 2, along it at s / 2 pointing left or
    // up and (s - 1) / 2 pointing right or down (measured at 16 and 17).
    const int side = qMin(r.width(), r.height());
    const QPoint square = r.topLeft() + QPoint((r.width() - side) / 2, (r.height() - side) / 2);
    const int lo = (side - 1) / 2, hi = side / 2;
    const bool back = dir == Qt::LeftArrow || dir == Qt::UpArrow;
    const bool horizontal = dir == Qt::LeftArrow || dir == Qt::RightArrow;
    const int along = back ? hi : lo;
    const QPoint c = square + (horizontal ? QPoint(along, lo) : QPoint(lo, along)) + (pressed ? QPoint(1, 1) : QPoint());
    auto draw = [&](QPoint o, const QColor &col) {
        for (int i = 0; i < 4; ++i) {
            switch (dir) {
            case Qt::LeftArrow:   // tip at c-3, widening to 7 at c
                p->fillRect(c.x() - 3 + i + o.x(), c.y() - i + o.y(), 1, 1 + 2 * i, col);
                break;
            case Qt::RightArrow:  // base at c-1, tip at c+2
                p->fillRect(c.x() - 1 + i + o.x(), c.y() - 3 + i + o.y(), 1, 7 - 2 * i, col);
                break;
            case Qt::UpArrow:     // tip at c-2, base at c+1
                p->fillRect(c.x() - i + o.x(), c.y() - 2 + i + o.y(), 1 + 2 * i, 1, col);
                break;
            default:              // down: base at c-1, tip at c+2
                p->fillRect(c.x() - 3 + i + o.x(), c.y() - 1 + i + o.y(), 7 - 2 * i, 1, col);
                break;
            }
        }
    };
    if (enabled) {
        draw(QPoint(), pal.color(QPalette::Active, QPalette::ButtonText));
    } else {
        draw(QPoint(1, 1), pal.color(QPalette::Light));
        draw(QPoint(), pal.color(QPalette::Dark));
    }
}

bool XPlasmaStyle::drawClassicComplex(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                                      const QWidget *widget) const
{
    switch (control) {
    case CC_ToolButton:
        if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option); button && xpDropDown(button)) {
            drawXpDropDownButton(button, painter, widget);
            return true;
        }
        // A tab row's scroll buttons: the halves of a classic up-down.
        if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
            button && widget && qobject_cast<const QTabBar *>(widget->parentWidget())
            && (button->arrowType == Qt::LeftArrow || button->arrowType == Qt::RightArrow)) {
            const bool pressed = sunken(button);
            classicScrollButton(painter, button->rect, button->palette, pressed);
            classicArrow(painter, button->rect, button->arrowType, button->palette, widget->parentWidget()->isEnabled(), pressed);
            return true;
        }
        return false;
    case CC_Slider:
        if (const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            // comctl32's classic trackbar, laid out as the themed one: a
            // sunken 2px channel, the ticks in the text colour, and the thumb
            // -- pointed towards its ticks -- in the four 3D colours.
            const QRect channel = proxy()->subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
            const QRect handle = proxy()->subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
            classicSunken(painter, channel, slider->palette);
            if ((slider->subControls & SC_SliderTickmarks) && slider->tickPosition != QSlider::NoTicks)
                drawSliderTicks(slider, channel, handle, painter);
            // The pointed thumb as XP draws it, 11 x 22, point down (W the
            // highlight, F the face, S the shadow, D the dark shadow).
            // (L the Light3D just inside the highlight.)
            static const char *const kPointed[] = {
                "WWWWWWWWWWD", "WLLLLLLLLSD", "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD",
                "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD",
                "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD", "WLFFFFFFFSD", ".WLFFFFFSD.",
                "..WLFFFSD..", "...WLFSD...", "....WSD....", ".....D.....",
            };
            const bool horizontal = slider->orientation == Qt::Horizontal;
            const QSlider::TickPosition ticks = slider->tickPosition;
            const bool pointed = ticks == QSlider::TicksBelow || ticks == QSlider::TicksAbove;
            const bool flip = ticks == QSlider::TicksAbove;   // (TicksLeft is TicksAbove)
            auto colourOf = [&](char c) {
                switch (c) {
                case 'W': return slider->palette.color(QPalette::Light);
                case 'L': return slider->palette.color(QPalette::Midlight);
                case 'S': return slider->palette.color(QPalette::Dark);
                case 'D': return slider->palette.color(QPalette::Shadow);
                default: return slider->palette.color(QPalette::Button);
                }
            };
            const int along = horizontal ? handle.width() : handle.height();
            const int across = horizontal ? handle.height() : handle.width();
            const bool dithered = !enabled(slider) || ((slider->activeSubControls & SC_SliderHandle) && sunken(slider));
            // The table's row for the thumb's row j: its body stretches or
            // shrinks, the 5-row point stays at the far end.
            auto tableRow = [&](int j) {
                const int r = flip ? across - 1 - j : j;
                return r >= across - 5 ? qMin(21, 17 + r - (across - 5)) : qMin(r, 16);
            };
            for (int j = 0; j < across; ++j) {
                for (int i = 0; i < along; ++i) {
                    // Row j of the down-pointing thumb: the point's rows only
                    // when pointed; a plain raised box otherwise.
                    char c;
                    if (pointed) {
                        const int row = tableRow(j);
                        const int col = qBound(0, i, 10);
                        c = kPointed[row][col];
                        if (flip && (c == 'W' || c == 'L' || c == 'S' || c == 'D') && row > 0 && row < 17 && col != 0 && col < 9)
                            c = 'F';
                        if (flip && row == 0)
                            c = col == 10 ? 'W' : 'W';
                    } else {
                        c = (j == 0 || i == 0) ? 'W' : (j == across - 1 || i == along - 1) ? 'D'
                            : (j == across - 2 || i == along - 2) ? 'S' : 'F';
                    }
                    if (c == '.')
                        continue;
                    const QPoint at = horizontal ? QPoint(handle.left() + i, handle.top() + j)
                                                 : QPoint(handle.left() + j, handle.top() + i);
                    // Disabled or held down, the face is dithered with the
                    // highlight -- all but the point's first row (so XP has it).
                    const bool plainRow = pointed && tableRow(j) == 17;
                    if (c == 'F' && dithered && !plainRow && ((at.x() + at.y()) & 1))
                        c = 'W';
                    painter->fillRect(at.x(), at.y(), 1, 1, colourOf(c));
                }
            }
            if ((slider->state & State_HasFocus) && (slider->state & State_KeyboardFocusChange))
                drawFocusDots(painter, slider->rect, focusColours(widget).first, focusColours(widget).second);
            return true;
        }
        return false;
    case CC_TitleBar:
        if (const auto *bar = qstyleoption_cast<const QStyleOptionTitleBar *>(option)) {
            drawClassicTitleBar(bar, painter, widget);
            return true;
        }
        return false;
    case CC_GroupBox:
        // Disabled, the caption is etched in the shadow (DSS_DISABLED).
        if (const auto *box = qstyleoption_cast<const QStyleOptionGroupBox *>(option); box && !enabled(box)) {
            QStyleOptionGroupBox copy(*box);
            copy.textColor = box->palette.color(QPalette::Dark);
            QProxyStyle::drawComplexControl(control, &copy, painter, widget);
            return true;
        }
        return false;
    case CC_SpinBox:
        if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
            spin && spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
            // The buddy edit's sunken edge, its inside, and the up-down: two
            // raised scroll-edge buttons with a face row between, Marlett's
            // arrows shrunk to 3x2.
            const QRect r = spin->rect;
            if (spin->frame) {
                qDrawWinPanel(painter, r, spin->palette, true, nullptr);
                painter->fillRect(r.adjusted(2, 2, -2, -2),
                                  enabled(spin) ? QBrush(sysColour(u"window", Qt::white))
                                                : QBrush(sysColour(u"btnface", spin->palette.color(QPalette::Button))));
            }
            const QRect up = proxy()->subControlRect(CC_SpinBox, spin, SC_SpinBoxUp, widget);
            const QRect down = proxy()->subControlRect(CC_SpinBox, spin, SC_SpinBoxDown, widget);
            painter->fillRect(QRect(up.left(), up.bottom() + 1, up.width(), down.top() - up.bottom() - 1), spin->palette.button());
            for (const auto &[rect, sub, flag, isUp] : {std::tuple{up, SC_SpinBoxUp, QAbstractSpinBox::StepUpEnabled, true},
                                                         std::tuple{down, SC_SpinBoxDown, QAbstractSpinBox::StepDownEnabled, false}}) {
                const bool pressed = (spin->activeSubControls & sub) && sunken(spin);
                const bool live = enabled(spin) && (spin->stepEnabled & flag);
                // (Held down, an up-down button is sunken -- EDGE_SUNKEN, not
                // the scroll bar's flat pressed arrow.)
                if (pressed) {
                    painter->fillRect(rect, spin->palette.button());
                    classicSunken(painter, rect, spin->palette);
                } else {
                    classicScrollButton(painter, rect, spin->palette, false);
                }
                const int cx = rect.left() + rect.width() / 2 + (pressed ? 1 : 0);
                const int cy = rect.top() + (rect.height() - 2) / 2 + (pressed ? 1 : 0);
                auto arrow = [&](QPoint o, const QColor &c) {
                    for (int i = 0; i < 2; ++i) {   // i = 0 the 1px tip, 1 the 3px base
                        const int row = isUp ? i : 1 - i;
                        painter->fillRect(cx - i + o.x(), cy + row + o.y(), 1 + 2 * i, 1, c);
                    }
                };
                if (live) {
                    arrow(QPoint(), spin->palette.color(QPalette::Active, QPalette::ButtonText));
                } else {
                    arrow(QPoint(1, 1), spin->palette.color(QPalette::Light));
                    arrow(QPoint(), spin->palette.color(QPalette::Dark));
                }
            }
            return true;
        }
        return false;
    case CC_ComboBox:
        if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
            // The edit's sunken client edge, the window colour inside (the
            // face when disabled), the drop-down button, its arrow.
            const QRect r = combo->rect;
            if (combo->frame) {
                qDrawWinPanel(painter, r, combo->palette, true, nullptr);
                painter->fillRect(r.adjusted(2, 2, -2, -2),
                                  enabled(combo) ? QBrush(sysColour(u"window", Qt::white))
                                                 : QBrush(sysColour(u"btnface", combo->palette.color(QPalette::Button))));
            }
            const QRect arrow = proxy()->subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
            const bool pressed = (combo->activeSubControls & SC_ComboBoxArrow) && sunken(combo);
            classicScrollButton(painter, arrow, combo->palette, pressed);
            classicArrow(painter, arrow, Qt::DownArrow, combo->palette, enabled(combo), pressed);
            if ((combo->state & State_HasFocus) && !combo->editable) {
                const QRect field = proxy()->subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget);
                painter->fillRect(field, combo->palette.brush(QPalette::Highlight));
                if (combo->state & State_KeyboardFocusChange)
                        drawFocusDots(painter, field, combo->palette.color(QPalette::Active, QPalette::Highlight),
                              combo->palette.color(QPalette::Active, QPalette::HighlightedText));
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

// DrawFocusRect: the outline XORed with a checkerboard brush, whose two
// phases take the DC's background and text colours -- white and black by
// default (every other pixel inverted), the highlight and highlight text
// where a tree view draws its selection. The pattern is anchored to the
// control's client origin (the painter's device here): `odd` applies where
// x + y is odd.
// The DC colours a control's DrawFocusRect XORs with (background for the
// odd phase, text for the even), as XP's controls set them -- measured with
// a scheme giving every colour its own value.
std::pair<QColor, QColor> XPlasmaStyle::focusColours(const QWidget *widget) const
{
    const QColor face = sysColour(u"btnface", QColor(236, 233, 216));
    if (qobject_cast<const QCheckBox *>(widget) || qobject_cast<const QRadioButton *>(widget))
        return {face, sysColour(u"windowtext", Qt::black)};
    if (qobject_cast<const QAbstractButton *>(widget) || qobject_cast<const QTabBar *>(widget))
        return {face, sysColour(u"btntext", Qt::black)};
    if (qobject_cast<const QSlider *>(widget))
        return {sysColour(u"btnhighlight", Qt::white), sysColour(u"windowtext", Qt::black)};
    if (widget && (widget->inherits("QAbstractItemView")
                   || (widget->parentWidget() && widget->parentWidget()->inherits("QAbstractItemView"))))
        return {sysColour(u"window", Qt::white), sysColour(u"windowtext", Qt::black)};
    return {Qt::white, Qt::black};
}

void XPlasmaStyle::drawFocusDots(QPainter *painter, const QRect &r, const QColor &odd, const QColor &even) const
{
    if (r.width() < 1 || r.height() < 1)
        return;
    const QPoint origin = painter->deviceTransform().map(QPoint(0, 0));
    painter->save();
    painter->setCompositionMode(QPainter::RasterOp_SourceXorDestination);
    const bool evenToo = even.rgb() != QColor(Qt::black).rgb();
    auto dot = [&](int x, int y) {
        if ((x + y + origin.x() + origin.y()) & 1)
            painter->fillRect(x, y, 1, 1, odd);
        else if (evenToo)
            painter->fillRect(x, y, 1, 1, even);
    };
    for (int x = r.left(); x <= r.right(); ++x) {
        dot(x, r.top());
        if (r.bottom() != r.top())
            dot(x, r.bottom());
    }
    for (int y = r.top() + 1; y < r.bottom(); ++y) {
        dot(r.left(), y);
        if (r.right() != r.left())
            dot(r.right(), y);
    }
    painter->restore();
}

// A tree view's or report list view's item, selected the XP way: only the
// label is highlighted (column 0 alone in a report view, as without
// LVS_EX_FULLROWSELECT), from 2px before the text to 1px (tree) or 5px
// (list view) past it, the full row tall; in the button face while the
// view hasn't focus (as TVS_/LVS_SHOWSELALWAYS apps -- Explorer, regedit
// -- show it). The focus rect rings the highlight. Measured on XP.
bool XPlasmaStyle::drawXpViewItem(const QStyleOptionViewItem *item, QPainter *painter, const QWidget *widget) const
{
    const int extra = itemRowExtra(widget);
    if ((extra != 1 && extra != 3) || !(classic() || hasPart({QStringLiteral("listview"), 0, Normal, {}})))
        return false;
    const bool tree = extra == 3;
    const bool focused = widget && widget->hasFocus();
    const bool labelCell = tree || item->index.column() == 0;
    const bool selected = labelCell && (item->state & State_Selected);
    const bool focusRect = labelCell && focused && (item->state & State_HasFocus) && (item->state & State_KeyboardFocusChange);

    QStyleOptionViewItem plain(*item);
    plain.state &= ~(State_Selected | State_HasFocus);
    // A disabled list view keeps its items' text colour (a tree greys them).
    if (!tree && !enabled(item))
        plain.palette.setBrush(QPalette::Disabled, QPalette::Text, item->palette.brush(QPalette::Active, QPalette::Text));
    if (selected && focused)
        plain.palette.setBrush(QPalette::Text, item->palette.brush(QPalette::Active, QPalette::HighlightedText));
    else if (selected)   // the unfocused selection: button text on the button face
        plain.palette.setColor(QPalette::Text, sysColour(u"btntext", item->palette.color(QPalette::ButtonText)));

    QRect label;
    if (selected || focusRect) {
        const QRect text = proxy()->subElementRect(SE_ItemViewItemText, &plain, widget);
        const int origin = text.left() + proxy()->pixelMetric(PM_FocusFrameHMargin, &plain, widget) + 1;
        const int advance = QFontMetrics(item->font).horizontalAdvance(item->text);
        label = QRect(QPoint(origin - 2, item->rect.top()), QPoint(origin + advance + (tree ? 1 : 5), item->rect.bottom()))
                    .intersected(item->rect);
    }
    if (selected)
        painter->fillRect(label, focused ? item->palette.brush(QPalette::Active, QPalette::Highlight)
                                         : QBrush(sysColour(u"btnface", item->palette.color(QPalette::Button))));
    QProxyStyle::drawControl(CE_ItemViewItem, &plain, painter, widget);
    if (focusRect)
        // (A tree XORs with its selection colours, a list view with the
        // DC's defaults.)
        drawFocusDots(painter, label, tree ? item->palette.color(QPalette::Active, QPalette::Highlight) : QColor(Qt::white),
                      tree ? item->palette.color(QPalette::Active, QPalette::HighlightedText) : QColor(Qt::black));
    return true;
}

// TKP_THUMB*: the pointed thumb faces its tick marks; TKP_THUMB(VERT) when
// there are ticks on both sides or none.
int XPlasmaStyle::sliderThumbPart(const QStyleOptionSlider *slider) const
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
QSize XPlasmaStyle::sliderThumbSize(const QStyleOptionSlider *slider) const
{
    const bool horizontal = slider->orientation == Qt::Horizontal;
    int across = sysMetric(horizontal ? u"scrollbarheight" : u"scrollbarwidth", 17) * 4 / 3;
    const int room = horizontal ? slider->rect.height() : slider->rect.width();
    if (room - 2 < across)
        across = qMax(1, room - 6);
    // (Along, half of across rounded up: Luna's 22 -> 11, Classic's 21 -> 11.)
    return horizontal ? QSize((across + 1) / 2, across) : QSize(across, (across + 1) / 2);
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

void XPlasmaStyle::drawSliderTicks(const QStyleOptionSlider *slider, const QRect &channel, const QRect &handle,
                                   QPainter *painter) const
{
    // In the Tics part's colour; the two end ticks 4px long, the rest 3,
    // starting 1px past the thumb, one per tickInterval at the thumb's
    // centre for that value.
    const bool horizontal = slider->orientation == Qt::Horizontal;
    const QColor colour = themeColour({QStringLiteral("trackbar"), horizontal ? 9 : 10, 1, {}}, u"color",
                                      slider->palette.color(QPalette::Active, QPalette::ButtonText));   // (disabled too)
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

QRect XPlasmaStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub,
                                   const QWidget *widget) const
{
    const ContextScope context(this, widget, option);
    if (control == CC_ComboBox && sub == SC_ComboBoxListBoxPopup && (m_renderer || classic()))
        // QComboBox drops its list from this rect's bottom-left: XP's list
        // starts under the combo's last row, not on it.
        return option->rect.adjusted(0, 0, 0, 1);
    if (control == CC_MdiControls && m_renderer && hasPart({QStringLiteral("window"), 20, 1, {}})) {
        // Themed (a real frame's menu bar): WP_MDI*BUTTON, SM_CXMENUSIZE by
        // the bar's height, flush right and touching.
        const QRect r = option->rect;
        const QSize b(sysMetric(u"menubarwidth", 18), sysMetric(u"menubarheight", 19));
        const QRect close(r.right() - b.width() + 1, r.top(), b.width(), b.height());
        switch (sub) {
        case SC_MdiCloseButton:
            return close;
        case SC_MdiNormalButton:
            return close.translated(-b.width(), 0);
        case SC_MdiMinButton:
            return close.translated(-2 * b.width(), 0);
        default:
            break;
        }
    }
    if (control == CC_MdiControls && (classic() || m_renderer)) {
        // A maximized MDI child's buttons in the menu bar: DFC_CAPTION
        // buttons SM_CXMENUSIZE - 2 by SM_CYMENU - 4, 2px down; minimize and
        // restore touching, close 2px apart and 2px in from the right.
        const QRect r = option->rect;
        const QSize b(sysMetric(u"menubarwidth", 18) - 2, sysMetric(u"menubarheight", 19) - 4);
        const QRect close(r.right() - 2 - b.width() + 1, r.top() + 2, b.width(), b.height());
        switch (sub) {
        case SC_MdiCloseButton:
            return close;
        case SC_MdiNormalButton:
            return close.translated(-2 - b.width(), 0);
        case SC_MdiMinButton:
            return close.translated(-2 - 2 * b.width(), 0);
        default:
            break;
        }
    }
    if (control == CC_ScrollBar && (classic() || m_renderer)
        && (sub == SC_ScrollBarSlider || sub == SC_ScrollBarSubPage || sub == SC_ScrollBarAddPage)) {
        // USER32's thumb: MulDiv(track, page, range + page) long -- rounded,
        // where Qt truncates (a 118px track at page 10 of 101: XP 12, Qt 11)
        // -- at MulDiv(track - thumb, value - min, range).
        if (const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option); bar && bar->maximum > bar->minimum) {
            const bool horizontal = bar->orientation == Qt::Horizontal;
            const QRect subLine = proxy()->subControlRect(control, option, SC_ScrollBarSubLine, widget);
            const QRect addLine = proxy()->subControlRect(control, option, SC_ScrollBarAddLine, widget);
            const int start = horizontal ? subLine.right() + 1 : subLine.bottom() + 1;
            const int end = horizontal ? addLine.left() : addLine.top();
            const int track = end - start;
            auto mulDiv = [](qint64 a, qint64 b, qint64 c) { return c ? int((a * b + c / 2) / c) : 0; };
            const qint64 range = qint64(bar->maximum) - bar->minimum;
            int thumb = qMax(mulDiv(track, bar->pageStep, range + bar->pageStep),
                             proxy()->pixelMetric(PM_ScrollBarSliderMin, option, widget));
            if (track > 0 && thumb <= track) {
                const qint64 value = bar->upsideDown ? qint64(bar->maximum) - bar->sliderPosition
                                                     : qint64(bar->sliderPosition) - bar->minimum;
                const int at = start + mulDiv(track - thumb, value, range);
                auto along = [&](int from, int to) {
                    return horizontal ? QRect(from, bar->rect.top(), to - from, bar->rect.height())
                                      : QRect(bar->rect.left(), from, bar->rect.width(), to - from);
                };
                if (sub == SC_ScrollBarSlider)
                    return along(at, at + thumb);
                return sub == SC_ScrollBarSubPage ? along(start, at) : along(at + thumb, end);
            }
        }
    }
    if (control == CC_TitleBar && (classic() || hasPart({QStringLiteral("window"), 1, 1, {}}))) {
        if (const auto *bar = qstyleoption_cast<const QStyleOptionTitleBar *>(option)) {
            const QRect r = xpTitleBarRect(bar, sub, widget);
            if (r.isValid() || sub != SC_TitleBarLabel)
                return r;
        }
    }
    if (control == CC_GroupBox && sub == SC_GroupBoxFrame && (classic() || hasPart({QStringLiteral("button"), 4, 1, {}}))) {
        // The frame's top runs through the caption's middle -- by GDI's
        // text height, which Qt 5 overstates by a row for Tahoma 8.
        const auto *box = qstyleoption_cast<const QStyleOptionGroupBox *>(option);
        QRect r = QProxyStyle::subControlRect(control, option, sub, widget);
        if (box && !box->text.isEmpty() && !(box->features & QStyleOptionFrame::Flat))
            r.setTop(option->rect.top() + xpTextHeight(widget ? widget->font() : QApplication::font()) / 2);
        return r;
    }
    if (control == CC_GroupBox && sub == SC_GroupBoxLabel && (classic() || hasPart({QStringLiteral("button"), 4, 1, {}}))) {
        // XP breaks the frame 1px before the caption; QCommonStyle cuts
        // the frame by exactly this rect. (Classic's caption also sits a
        // row lower than Qt's.)
        const auto *box = qstyleoption_cast<const QStyleOptionGroupBox *>(option);
        QRect r = QProxyStyle::subControlRect(control, option, sub, widget);
        if (box && !box->text.isEmpty() && !(box->subControls & SC_GroupBoxCheckBox) && (box->textAlignment & Qt::AlignLeft))
            r.setLeft(r.left() - 1);
        if (classic())
            r.translate(0, 1);
        return r;
    }
    if (classic() && control == CC_SpinBox) {
        const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
        if (spin && spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
            // XP's up-down (Classic, 80 x 21): SM_CXVSCROLL - 1 wide inside
            // the edit's 2px edge, two 8px buttons with a face row between.
            const QRect r = spin->rect;
            const int inset = spin->frame ? 2 : 0;
            const int bw = sysMetric(u"scrollbarwidth", 17) - 1;
            const int x = r.right() - inset - bw + 1;
            const int top = r.top() + inset, avail = r.height() - 2 * inset;
            const int upH = (avail - 1) / 2;
            switch (sub) {
            case SC_SpinBoxUp:
                return QRect(x, top, bw, upH);
            case SC_SpinBoxDown:
                return QRect(x, top + upH + 1, bw, avail - 1 - upH);
            case SC_SpinBoxEditField: {
                const int left = r.left() + (spin->frame ? 4 : 0);
                const int verticalInset = spin->frame ? 3 : 0;
                return QRect(left, r.top() + qMin(verticalInset, qMax(0, r.height())),
                             qMax(0, x - 2 - left), qMax(0, r.height() - 2 * verticalInset));
            }
            case SC_SpinBoxFrame:
                return r;
            default:
                break;
            }
        }
    }
    if (classic() && control == CC_ComboBox && (sub == SC_ComboBoxArrow || sub == SC_ComboBoxEditField)) {
        // The button inside the 2px client edge, SM_CXVSCROLL wide, the full
        // height; a drop-down list's field 1px inside that edge up to 1px
        // before the button (where the focus highlight goes); an editable
        // one's edit 1px right and up of Qt's.
        const QRect r = option->rect;
        const int w = sysMetric(u"scrollbarwidth", 17);
        const QRect arrow(r.right() - 1 - w, r.top() + 2, w, qMax(0, r.height() - 4));
        if (sub == SC_ComboBoxArrow)
            return arrow;
        const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option);
        if (combo && combo->editable)
            return QProxyStyle::subControlRect(control, option, sub, widget).translated(1, -1).adjusted(0, 0, -1, 0);
        return QRect(QPoint(r.left() + 3, r.top() + 3), QPoint(arrow.left() - 2, r.bottom() - 3));
    }
    if (m_renderer && control == CC_SpinBox) {
        const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
        if (spin && spin->buttonSymbols != QAbstractSpinBox::NoButtons && hasPart({QStringLiteral("spin"), 1, 1, {}})) {
            // XP's up-down docked in its buddy edit (UDS_ALIGNRIGHT, Luna,
            // 80 x 21): SM_CXVSCROLL + 1 wide over the edit's right edge.
            // Inside the edit's 1px border, two SM_CXVSCROLL x 9 buttons
            // (their art brings its own 1px white margin) with one row of
            // button face between. The text sits 4px into the edit's
            // client (EM_GETRECT 4,1), QLineEdit adding 2 of those.
            const QRect r = spin->rect;
            const int inset = spin->frame ? 1 : 0;
            const int bw = sysMetric(u"scrollbarwidth", 17);
            const int x = r.right() - inset - bw + 1;
            const int top = r.top() + inset, avail = r.height() - 2 * inset;
            const int upH = (avail - 1) / 2;
            switch (sub) {
            case SC_SpinBoxUp:
                return QRect(x, top, bw, upH);
            case SC_SpinBoxDown:
                return QRect(x, top + upH + 1, bw, avail - 1 - upH);
            case SC_SpinBoxEditField: {
                const int left = r.left() + (spin->frame ? 4 : 0);
                const int verticalInset = spin->frame ? 3 : 0;
                return QRect(left, r.top() + qMin(verticalInset, qMax(0, r.height())),
                             qMax(0, x - 2 - left), qMax(0, r.height() - 2 * verticalInset));
            }
            case SC_SpinBoxFrame:
                return r;
            default:
                break;
            }
        }
    }
    if ((m_renderer || classic()) && control == CC_Slider) {
        if (const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            const QSize thumb = sliderThumbSize(slider);
            if (thumb.isValid() && (classic() || hasPart({QStringLiteral("trackbar"), slider->orientation == Qt::Horizontal ? 1 : 2, 1, {}}))) {
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

int XPlasmaStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                            QStyleHintReturn *returnData) const
{
    switch (hint) {
    case SH_ComboBox_Popup:
        return 0;
    case SH_ComboBox_PopupFrameStyle:
        if (m_renderer || classic())
            return QFrame::StyledPanel | QFrame::Plain;   // drawn as XP's WS_BORDER (PE_Frame)
        break;
    case SH_MainWindow_SpaceBelowMenuBar:
        // The row under XP's menu bar is part of it (SM_CYMENU), drawn
        // with the bar.
        if (m_renderer || classic())
            return 0;
        break;
    case SH_WindowFrame_Mask:
        // An MDI child's shape: the caption art's transparent corners cut
        // away, as XP's window region does.
        if (auto *mask = qstyleoption_cast<QStyleHintReturnMask *>(returnData);
            mask && option && widget && widget->inherits("QMdiSubWindow") && m_renderer && hasPart({QStringLiteral("window"), 1, 1, {}})) {
            const int fw = proxy()->pixelMetric(PM_MdiSubWindowFrameWidth, option, widget);
            QStyleOptionTitleBar probe;
            probe.QStyleOption::operator=(*option);
            const int caption = proxy()->pixelMetric(PM_TitleBarHeight, &probe, widget) + fw;
            QRegion region(option->rect);
            if (const QImage *art = partImage({QStringLiteral("window"), 1, 1, {}}, QSize(option->rect.width(), caption), 1.0)) {
                // (Only the corners: rows until one is wholly opaque.)
                for (int y = 0; y < art->height(); ++y) {
                    bool opaque = true;
                    for (int x = 0; x < art->width(); ++x)
                        if (qAlpha(art->pixel(x, y)) == 0) {
                            region -= QRegion(option->rect.left() + x, option->rect.top() + y, 1, 1);
                            opaque = false;
                        }
                    if (opaque)
                        break;
                }
            }
            mask->region = region;
            return 1;
        }
        break;
    case SH_UnderlineShortcut:
        if (!m_safe)
            return m_cues ? 1 : 0;   // until Alt is held (see eventFilter)
        break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    case SH_SpinBox_SelectOnStep:
        // An up-down steps its buddy's value and leaves the caret be.
        if (m_renderer || classic())
            return 0;
        break;
#endif
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
