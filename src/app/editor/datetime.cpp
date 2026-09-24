#include "app/editor/datetime.h"

#include "app/state.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QRegularExpression>
#include <QProcess>
#include <QFileInfo>
#include <QEventLoop>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimeEdit>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include <functional>

namespace xpl::editor {

namespace {

// GDI's MulDiv: a * b / c, rounded.
int mulDiv(int a, int b, int c)
{
    return int((qint64(a) * b + (a * b >= 0 ? c / 2 : -c / 2)) / c);
}

const QString kService = QStringLiteral("org.freedesktop.timedate1");
const QString kPath = QStringLiteral("/org/freedesktop/timedate1");

QVariant timedated(const char *property)
{
    QDBusInterface i(kService, kPath, kService, QDBusConnection::systemBus());
    return i.isValid() ? i.property(property) : QVariant();
}

// A command run as root through pkexec (the password dialog the desktop's
// polkit agent's), the event loop running meanwhile; empty, or why it failed.
QString asRoot(const QStringList &argv)
{
    QProcess process;
    QEventLoop loop;
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    process.start(QStringLiteral("pkexec"), argv);
    if (process.state() != QProcess::NotRunning)
        loop.exec();
    if (process.error() == QProcess::FailedToStart)
        return QStringLiteral("there's no way to change it here (neither systemd's time service nor pkexec).");
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString err = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return err.isEmpty() ? QStringLiteral("not allowed.") : err;
    }
    return QString();
}

// Without timedated (a system without systemd): what it would do, done
// directly as root -- the zone as /etc/localtime, the time into the system
// clock and the hardware clock. Synchronization is the system's own
// business there (ntpd, chrony...), not settable from here.
QString withoutTimedated(const QString &method, const QVariantList &args)
{
    if (method == QLatin1String("SetTimezone")) {
        const QString zone = args.value(0).toString();
        static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+)*$"));
        const QString file = QStringLiteral("/usr/share/zoneinfo/") + zone;
        if (!name.match(zone).hasMatch() || !QFileInfo(file).isFile())
            return QStringLiteral("this system has no time zone %1.").arg(zone);
        return asRoot({QStringLiteral("ln"), QStringLiteral("-sf"), file, QStringLiteral("/etc/localtime")});
    }
    if (method == QLatin1String("SetTime")) {
        const qint64 seconds = args.value(0).toLongLong() / 1000000;
        return asRoot({QStringLiteral("sh"), QStringLiteral("-c"),
                       QStringLiteral("date -u -s @%1 >/dev/null && { hwclock --systohc 2>/dev/null || true; }").arg(seconds)});
    }
    return QStringLiteral("this system keeps its clock synchronized its own way (no systemd time service).");
}

bool haveTimedated()
{
    return QDBusInterface(kService, kPath, kService, QDBusConnection::systemBus()).isValid();
}

// A timedated call, asking for authorization as needed (the password dialog
// is the desktop's polkit agent's); empty, or why it failed.
QString timedatedCall(const QString &method, const QVariantList &args)
{
    QDBusInterface i(kService, kPath, kService, QDBusConnection::systemBus());
    if (!i.isValid())
        return withoutTimedated(method, args);
    i.setTimeout(5 * 60 * 1000);   // (while someone types a password)
    const QDBusMessage reply = i.callWithArgumentList(QDBus::BlockWithGui, method, args);
    return reply.type() == QDBusMessage::ErrorMessage ? reply.errorMessage() : QString();
}

// The zone with its standard time all year: Etc/GMT+6 is six hours behind
// (POSIX's sign), only for whole hours.
QByteArray standardOnly(const QByteArray &iana)
{
    const QTimeZone tz(iana);
    if (!tz.isValid())
        return QByteArray();
    const int offset = tz.standardTimeOffset(QDateTime::currentDateTimeUtc());
    if (offset % 3600)
        return QByteArray();
    const int hours = -offset / 3600;
    return hours == 0 ? QByteArrayLiteral("Etc/GMT") : QByteArray("Etc/GMT") + (hours > 0 ? "+" : "") + QByteArray::number(hours);
}

} // namespace

// The look's 3D colours -- XP's button face, shadow, dark shadow, light and
// highlight. In a Plasma session an app's palette is KDE's colour scheme,
// whose shades are its own; XPLasma's style knows XP's (its standard
// palette), so under it they're taken from there.
QPalette bevels(const QWidget *widget)
{
    QPalette pal = widget->palette();
    if (widget->style()->name().compare(QLatin1String("xplasma"), Qt::CaseInsensitive) == 0) {
        const QPalette xp = widget->style()->standardPalette();
        for (const auto role : {QPalette::Button, QPalette::Dark, QPalette::Shadow, QPalette::Midlight, QPalette::Light})
            pal.setColor(role, xp.color(QPalette::Active, role));
    }
    return pal;
}

// XP's 3D edges: EDGE_SUNKEN, 2px, in the look's colours.
void sunkenEdge(QPainter &p, const QRect &r, const QPalette &pal)
{
    auto line = [&](int x1, int y1, int x2, int y2, QPalette::ColorRole role) {
        p.setPen(pal.color(role));
        p.drawLine(x1, y1, x2, y2);
    };
    line(r.left(), r.top(), r.right() - 1, r.top(), QPalette::Dark);          // outer: shadow...
    line(r.left(), r.top(), r.left(), r.bottom() - 1, QPalette::Dark);
    line(r.left(), r.bottom(), r.right(), r.bottom(), QPalette::Light);       // ...and highlight
    line(r.right(), r.top(), r.right(), r.bottom(), QPalette::Light);
    line(r.left() + 1, r.top() + 1, r.right() - 2, r.top() + 1, QPalette::Shadow);   // inner: dark shadow...
    line(r.left() + 1, r.top() + 1, r.left() + 1, r.bottom() - 2, QPalette::Shadow);
    line(r.left() + 1, r.bottom() - 1, r.right() - 1, r.bottom() - 1, QPalette::Midlight);   // ...and light
    line(r.right() - 1, r.top() + 1, r.right() - 1, r.bottom() - 1, QPalette::Midlight);
}

// The window manager's caption colours (kdeglobals [WM], which the look
// sets from XP's): XP's calendar draws with them.
struct Captions
{
    QColor active{0, 84, 227}, activeText{Qt::white}, inactive{122, 150, 223}, inactiveText{216, 228, 248};
};
Captions captions()
{
    Captions c;
    QSettings kdeglobals(QDir::homePath() + QLatin1String("/.config/kdeglobals"), QSettings::IniFormat);
    auto colour = [&](const char *key, QColor &out) {
        const QStringList rgb = kdeglobals.value(QLatin1String("WM/") + QLatin1String(key)).toStringList().join(QLatin1Char(',')).split(QLatin1Char(','));
        if (rgb.size() >= 3)
            out = QColor(rgb.at(0).toInt(), rgb.at(1).toInt(), rgb.at(2).toInt());
    };
    colour("activeBackground", c.active);
    colour("activeForeground", c.activeText);
    colour("inactiveBackground", c.inactive);
    colour("inactiveForeground", c.inactiveText);
    return c;
}

// --- the calendar ---------------------------------------------------------------

// The calendar's locale: the user's -- or, for the C locale (a machine
// with none set), English, as XP's: C's narrow day names are "1".."7" and
// its week starts on Monday.
QLocale calendarLocale()
{
    const QLocale locale;
    return locale.language() == QLocale::C ? QLocale(QLocale::English, QLocale::UnitedStates) : locale;
}

// XP's month (CalWndMain), as measured on XP: a sunken edge; the days of the
// week on the inactive caption's colour, 18px rows; each column a field two
// digits wide on its centre, the day's name at its left, the day's number at
// its right; the chosen day in the active caption's colour, just round the
// number. Click or arrow keys to pick.
class CalendarGrid : public QWidget
{
public:
    std::function<void(const QDate &)> picked;

    explicit CalendarGrid(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_captions(captions())
    {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }
    void setDate(const QDate &date)
    {
        if (date != m_date) {
            m_date = date;
            update();
        }
    }

protected:
    static constexpr int kEdge = 2, kRow = 18;
    // A column's field: two digits wide, on the column's centre.
    QRect field(int row, int column) const
    {
        const qreal w = (width() - 2 * kEdge) / 7.0;
        const int centre = kEdge + int(w * column + w / 2);
        const int digits = fontMetrics().horizontalAdvance(QStringLiteral("00"));
        return QRect(centre - digits / 2, kEdge + row * kRow, digits, kRow);
    }
    int firstColumn() const
    {
        const int first = calendarLocale().firstDayOfWeek();
        return (QDate(m_date.year(), m_date.month(), 1).dayOfWeek() - first + 7) % 7;
    }
    QRect numberRect(int day) const
    {
        const int slot = firstColumn() + day - 1;
        const QRect f = field(1 + slot / 7, slot % 7);
        const int w = fontMetrics().horizontalAdvance(QString::number(day));
        const int h = fontMetrics().height();
        return QRect(f.right() + 1 - w, f.top() + (kRow - h) / 2, w, h);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QPalette pal = palette();
        p.fillRect(rect(), pal.color(QPalette::Base));
        p.fillRect(QRect(kEdge, kEdge, width() - 2 * kEdge, kRow), m_captions.inactive);
        sunkenEdge(p, rect(), bevels(this));
        const QLocale locale = calendarLocale();
        p.setPen(m_captions.inactiveText);
        for (int c = 0; c < 7; ++c) {
            const int day = (locale.firstDayOfWeek() - 1 + c) % 7 + 1;
            p.drawText(field(0, c), Qt::AlignLeft | Qt::AlignVCenter, locale.dayName(day, QLocale::NarrowFormat));
        }
        for (int d = 1; d <= m_date.daysInMonth(); ++d) {
            const QRect r = numberRect(d);
            if (d == m_date.day()) {
                const QRect box = r.adjusted(-1, 0, 1, 0);
                p.fillRect(box, m_captions.active);
                p.setPen(m_captions.activeText);
                p.drawText(r, Qt::AlignRight | Qt::AlignVCenter, QString::number(d));
                if (hasFocus()) {
                    p.setPen(QPen(m_captions.activeText, 1, Qt::DotLine));
                    p.drawRect(box.adjusted(0, 0, -1, -1));
                }
            } else {
                p.setPen(pal.color(QPalette::Text));
                p.drawText(r, Qt::AlignRight | Qt::AlignVCenter, QString::number(d));
            }
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (int d = 1; d <= m_date.daysInMonth(); ++d) {
            const int slot = firstColumn() + d - 1;
            const QRect f = field(1 + slot / 7, slot % 7);
            const qreal w = (width() - 2 * kEdge) / 7.0;
            const QRect cell(int(kEdge + w * (slot % 7)), f.top(), int(w), kRow);
            if (cell.contains(e->position().toPoint())) {
                choose(QDate(m_date.year(), m_date.month(), d));
                return;
            }
        }
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        int step = 0;
        switch (e->key()) {
        case Qt::Key_Left: step = -1; break;
        case Qt::Key_Right: step = 1; break;
        case Qt::Key_Up: step = -7; break;
        case Qt::Key_Down: step = 7; break;
        default: QWidget::keyPressEvent(e); return;
        }
        const QDate next = m_date.addDays(step);
        if (next.month() == m_date.month())   // (XP's stays in its month)
            choose(next);
    }
    void focusInEvent(QFocusEvent *) override { update(); }
    void focusOutEvent(QFocusEvent *) override { update(); }
    void changeEvent(QEvent *event) override
    {
        // Another look put on while it's open: its captions, as XP's
        // calendar takes the new system colours.
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
            m_captions = captions();
            update();
        }
        QWidget::changeEvent(event);
    }

private:
    void choose(const QDate &date)
    {
        setDate(date);
        if (picked)
            picked(date);
    }
    QDate m_date = QDate::currentDate();
    Captions m_captions;
};

// --- the clock --------------------------------------------------------------------

// XP's analog clock (ClockWndMain), pixel for pixel as XP draws it: the
// hours as raised 4px squares in teal, the minutes as sunken 3px dots in the
// look's 3D colours, on a circle 2px in; the hour and minute hands teal
// kites lit from the top left (the 3D highlight) with the 3D shadow on
// their bottom right; the second hand a black line.
class AnalogClock : public QWidget
{
public:
    using QWidget::QWidget;
    void setTime(const QTime &time)
    {
        if (time != m_time) {
            m_time = time;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QPalette pal = bevels(this);
        const QColor teal(0, 128, 128), cyan(0, 255, 255), black(0, 0, 0);
        const int side = qMin(width(), height()) - 1;
        const QPointF c((width() - 1) / 2.0, (height() - 1) / 2.0);
        const qreal r = qRound(side * 0.45);   // (where the marks' centres are: 61px in XP's 135)
        for (int i = 0; i < 60; ++i) {
            const qreal a = i * M_PI / 30;
            const QPoint at(qRound(c.x() + qSin(a) * r), qRound(c.y() - qCos(a) * r));
            if (i % 5 == 0) {
                const QPoint o = at - QPoint(2, 2);   // CCC# / Ctt# / Ctt# / C###
                p.fillRect(QRect(o, QSize(4, 4)), black);
                p.fillRect(QRect(o, QSize(3, 1)), cyan);
                p.fillRect(QRect(o, QSize(1, 4)), cyan);
                p.fillRect(QRect(o + QPoint(1, 1), QSize(2, 2)), teal);
            } else {
                const QPoint o = at - QPoint(1, 1);   // gg. / gfW / .WW
                p.fillRect(QRect(o, QSize(2, 1)), pal.color(QPalette::Dark));
                p.fillRect(QRect(o + QPoint(0, 1), QSize(1, 1)), pal.color(QPalette::Dark));
                p.fillRect(QRect(o + QPoint(1, 1), QSize(1, 1)), pal.color(QPalette::Button));
                p.fillRect(QRect(o + QPoint(2, 1), QSize(1, 2)), pal.color(QPalette::Light));
                p.fillRect(QRect(o + QPoint(1, 2), QSize(1, 1)), pal.color(QPalette::Light));
            }
        }
        // A hand: a slim kite, widest at the centre, from a short tail
        // behind it to its tip; the highlight and shadow are the kite moved
        // up-left and down-right under it.
        auto kite = [&](qreal turns, qreal length, qreal halfWidth, const QPointF &shift) {
            const qreal a = turns * 2 * M_PI;
            const QPointF dir(qSin(a), -qCos(a)), across(-dir.y(), dir.x());
            QPolygonF poly;
            poly << c + shift + dir * length << c + shift + across * halfWidth
                 << c + shift - dir * 4 << c + shift - across * halfWidth;
            return poly;
        };
        auto hand = [&](qreal turns, qreal length, qreal halfWidth) {
            p.setPen(Qt::NoPen);
            p.setBrush(pal.color(QPalette::Dark));
            p.drawPolygon(kite(turns, length, halfWidth, QPointF(2, 2)));
            p.setBrush(pal.color(QPalette::Light));
            p.drawPolygon(kite(turns, length, halfWidth, QPointF(-1, -1)));
            p.setBrush(teal);
            p.drawPolygon(kite(turns, length, halfWidth, QPointF(0, 0)));
        };
        const qreal seconds = m_time.second(), minutes = m_time.minute() + seconds / 60, hours = m_time.hour() % 12 + minutes / 60;
        hand(hours / 12, r * 0.6, 3);
        hand(minutes / 60, r * 0.78, 3);
        const qreal a = seconds * M_PI / 30;
        p.setPen(black);
        p.drawLine(c.toPoint(), QPoint(qRound(c.x() + qSin(a) * r * 0.72), qRound(c.y() - qCos(a) * r * 0.72)));
    }

private:
    QTime m_time;
};

// --- the map ------------------------------------------------------------------------

// XP's world map: land green, sea navy, in a sunken edge, turned round the
// world so the zone is in the middle -- as XP scrolls it, by the zone's
// standard offset: 1/24 of the width an hour, from 338px at GMT (measured on
// XP for Central, Brasilia and India). Without the XP media, nothing (the
// list works on its own).
class WorldMap : public QWidget
{
public:
    explicit WorldMap(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        if (const auto m = timezones::map())
            m_indices = m->indices;
    }
    QSize mapSize() const { return m_indices.isNull() ? QSize() : m_indices.size() + QSize(4, 4); }
    void setOffset(int minutes)
    {
        if (m_indices.isNull())
            return;
        const int w = m_indices.width();
        m_shift = ((338 + minutes * w / 1440) % w + w) % w;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (m_indices.isNull())
            return;
        auto isSea = [](int i) { return (i < 48 && i % 2 == 0) || i == 78 || i == 88; };
        const int w = m_indices.width(), h = m_indices.height();
        QImage out(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; ++y) {
            const uchar *in = m_indices.constScanLine(y);
            auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; ++x)
                line[x] = isSea(in[(x + m_shift) % w]) ? qRgb(0, 0, 127) : qRgb(0, 127, 0);
        }
        const QRect edge(QPoint((width() - w - 4) / 2, 0), QSize(w + 4, h + 4));
        p.drawImage(edge.topLeft() + QPoint(2, 2), out);
        sunkenEdge(p, edge, bevels(this));
    }

private:
    QImage m_indices;
    int m_shift = 338;
};

// --- the dialog -----------------------------------------------------------------------

DateTimeProperties::DateTimeProperties(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Date and Time Properties"));
    if (const auto zones = timezones::load())
        m_zones = *zones;
    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    m_tabs->addTab(dateTimeTab(), QStringLiteral("Date && Time"));
    m_tabs->addTab(timeZoneTab(), QStringLiteral("Time Zone"));
    m_tabs->addTab(internetTimeTab(), QStringLiteral("Internet Time"));
    root->addWidget(m_tabs);
    root->setSizeConstraint(QLayout::SetFixedSize);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    m_apply = new QPushButton(QStringLiteral("&Apply"));
    ok->setDefault(true);
    for (QPushButton *b : {ok, cancel, m_apply})
        row->addWidget(b);
    root->addLayout(row);
    connect(ok, &QPushButton::clicked, this, [this] {
        if (apply())
            accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &DateTimeProperties::apply);

    // The computer's zone and synchronization, as they are.
    m_loading = true;
    m_appliedZone = timedated("Timezone").toString().toUtf8();
    if (m_appliedZone.isEmpty())
        m_appliedZone = QTimeZone::systemTimeZoneId();
    int zone = -1;
    bool daylight = true;
    if (m_appliedZone.startsWith("Etc/GMT")) {
        // A standard time all year: the zone XPLasma set it for, or the first
        // with that standard time.
        daylight = false;
        const QString remembered = State::load().settings.value(QStringLiteral("dateTimeZone")).toString();
        for (int i = 0; i < m_zones.size() && zone < 0; ++i)
            if (m_zones.at(i).key == remembered && standardOnly(m_zones.at(i).iana) == m_appliedZone)
                zone = i;
        for (int i = 0; i < m_zones.size() && zone < 0; ++i)
            if (standardOnly(m_zones.at(i).iana) == m_appliedZone && QTimeZone(m_zones.at(i).iana).hasDaylightTime())
                zone = i;
    } else {
        zone = timezones::find(m_zones, m_appliedZone);
    }
    m_zone->setCurrentIndex(zone);
    m_daylight->setChecked(daylight);
    zonePicked();
    readTimeService();
    m_loading = false;

    m_ticker = new QTimer(this);
    connect(m_ticker, &QTimer::timeout, this, &DateTimeProperties::tick);
    m_ticker->start(250);
    tick();
    updateApply();
}

QRect DateTimeProperties::du(int x, int y, int w, int h) const
{
    // The dialog's base units, as GDI works them out for its font.
    const QFontMetrics fm(font());
    const int cx = (fm.horizontalAdvance(QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")) / 26 + 1) / 2;
    const int cy = fm.height();
    return QRect(mulDiv(x, cx, 4), mulDiv(y, cy, 8), mulDiv(w, cx, 4), mulDiv(h, cy, 8));
}

QWidget *DateTimeProperties::dateTimeTab()
{
    auto *page = new QWidget;
    page->setFixedSize(du(0, 0, 252, 152).size());
    auto *date = new QGroupBox(QStringLiteral("&Date"), page);
    date->setGeometry(du(4, 2, 122, 125));
    auto *time = new QGroupBox(QStringLiteral("&Time"), page);
    time->setGeometry(du(132, 2, 113, 125));
    auto place = [&](QWidget *w, QRect r) {
        w->setParent(page);
        w->setGeometry(r);
        w->raise();
    };

    m_month = new QComboBox;
    const QLocale locale;
    for (int m = 1; m <= 12; ++m)
        m_month->addItem(locale.standaloneMonthName(m));
    QRect r = du(11, 17, 50, 12);
    place(m_month, QRect(r.topLeft(), QSize(r.width(), m_month->sizeHint().height())));
    m_year = new QSpinBox;
    m_year->setRange(1980, 2099);   // (XP's range)
    m_year->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    r = du(68, 17, 50, 13);
    place(m_year, QRect(r.topLeft(), QSize(r.width(), m_year->sizeHint().height())));
    m_calendar = new CalendarGrid;
    place(m_calendar, du(11, 37, 108, 80));

    m_clock = new AnalogClock;
    place(m_clock, du(144, 13, 90, 90));
    m_time = new QTimeEdit;
    const bool twelveHour = locale.timeFormat(QLocale::ShortFormat).contains(QLatin1Char('a'), Qt::CaseInsensitive);
    m_time->setDisplayFormat(twelveHour ? QStringLiteral("h:mm:ss AP") : QStringLiteral("HH:mm:ss"));
    m_time->setWrapping(true);
    r = du(159, 105, 65, 12);
    place(m_time, QRect(r.topLeft(), QSize(r.width(), m_time->sizeHint().height())));

    m_zoneLabel = new QLabel;
    place(m_zoneLabel, du(4, 136, 243, 8));

    connect(m_month, &QComboBox::activated, this, [this](int m) {
        const QDate d = shown().date();
        dateEdited(QDate(d.year(), m + 1, qMin(d.day(), QDate(d.year(), m + 1, 1).daysInMonth())));
    });
    connect(m_year, &QSpinBox::valueChanged, this, [this](int y) {
        if (m_loading)
            return;
        const QDate d = shown().date();
        if (y != d.year())
            dateEdited(QDate(y, d.month(), qMin(d.day(), QDate(y, d.month(), 1).daysInMonth())));
    });
    m_calendar->picked = [this](const QDate &d) { dateEdited(d); };
    connect(m_time, &QTimeEdit::timeChanged, this, [this] {
        if (!m_loading)
            timeEdited();
    });
    return page;
}

QWidget *DateTimeProperties::timeZoneTab()
{
    auto *page = new QWidget;
    page->setFixedSize(du(0, 0, 252, 152).size());
    m_zone = new QComboBox(page);
    for (const timezones::Zone &z : std::as_const(m_zones))
        m_zone->addItem(z.display);
    const QRect r = du(5, 4, 241, 12);
    m_zone->setGeometry(QRect(r.topLeft(), QSize(r.width(), m_zone->sizeHint().height())));
    m_map = new WorldMap(page);
    // (XP's control is the bitmap in its edge, which runs past the template's
    // height by a few pixels.)
    const QRect mapRect = du(5, 22, 241, 113);
    m_map->setGeometry(QRect(mapRect.topLeft(), m_map->mapSize().isValid() ? QSize(mapRect.width(), m_map->mapSize().height()) : mapRect.size()));
    m_daylight = new QCheckBox(QStringLiteral("A&utomatically adjust clock for daylight saving changes"), page);
    m_daylight->setGeometry(du(5, 139, 242, 10));
    connect(m_zone, &QComboBox::currentIndexChanged, this, &DateTimeProperties::zonePicked);
    connect(m_daylight, &QCheckBox::toggled, this, [this] {
        updateZoneLabel();
        updateApply();
    });
    // (Only the user's own picks change the zone: the one shown for the
    // computer's can be another name for it -- Etc/UTC shows as XP's
    // Greenwich Standard Time, Atlantic/Reykjavik.)
    connect(m_zone, &QComboBox::activated, this, [this] {
        m_zoneTouched = true;
        updateApply();
    });
    connect(m_daylight, &QCheckBox::clicked, this, [this] {
        m_zoneTouched = true;
        updateApply();
    });
    return page;
}

QWidget *DateTimeProperties::internetTimeTab()
{
    auto *page = new QWidget;
    page->setFixedSize(du(0, 0, 252, 152).size());
    m_sync = new QCheckBox(QStringLiteral("Automatically &synchronize with an Internet time server"), page);
    m_sync->setGeometry(du(8, 10, 242, 10));
    auto *label = new QLabel(QStringLiteral("Server:"), page);
    label->setGeometry(du(31, 26, 28, 8));
    m_server = new QComboBox(page);
    const QRect r = du(60, 24, 115, 12);
    m_server->setGeometry(QRect(r.topLeft(), QSize(r.width(), m_server->sizeHint().height())));
    // (Which server is the system's time service's to say; shown, not set.)
    m_server->setEnabled(false);
    m_updateNow = new QPushButton(QStringLiteral("&Update Now"), page);
    m_updateNow->setGeometry(du(180, 24, 55, 14));
    m_syncStatus = new QLabel(page);
    m_syncStatus->setGeometry(du(8, 52, 235, 48));
    m_syncStatus->setWordWrap(true);
    m_syncStatus->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_nextSync = new QLabel(page);
    m_nextSync->setGeometry(du(8, 108, 235, 8));
    auto *note = new QLabel(QStringLiteral("Synchronization can occur only when your computer is connected to the Internet."), page);
    note->setGeometry(du(8, 120, 235, 24));
    note->setWordWrap(true);
    note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    connect(m_sync, &QCheckBox::toggled, this, [this](bool on) {
        m_server->setEnabled(false);
        m_updateNow->setEnabled(on && m_appliedSync);
        updateApply();
    });
    connect(m_updateNow, &QPushButton::clicked, this, [this] {
        // (A resynchronization: the service off and on again.)
        m_syncStatus->setText(QStringLiteral("Please wait while your computer synchronizes with %1").arg(m_server->currentText()));
        QString why = timedatedCall(QStringLiteral("SetNTP"), {false, true});
        if (why.isEmpty())
            why = timedatedCall(QStringLiteral("SetNTP"), {true, true});
        if (!why.isEmpty())
            m_syncStatus->setText(QStringLiteral("An error occurred while your computer was synchronizing with %1.  %2").arg(m_server->currentText(), why));
        QTimer::singleShot(3000, this, &DateTimeProperties::readTimeService);
    });
    return page;
}

void DateTimeProperties::readTimeService()
{
    const bool can = timedated("CanNTP").toBool();
    m_appliedSync = timedated("NTP").toBool();
    const bool synchronized = timedated("NTPSynchronized").toBool();
    const bool wasLoading = m_loading;
    m_loading = true;
    m_sync->setChecked(m_appliedSync);
    m_loading = wasLoading;
    m_sync->setEnabled(can);
    m_updateNow->setEnabled(can && m_appliedSync);
    // The server, if the service is systemd's own (timesyncd).
    QDBusInterface sync(QStringLiteral("org.freedesktop.timesync1"), QStringLiteral("/org/freedesktop/timesync1"),
                        QStringLiteral("org.freedesktop.timesync1.Manager"), QDBusConnection::systemBus());
    const QString server = sync.isValid() ? sync.property("ServerName").toString() : QString();
    m_server->clear();
    m_server->addItem(server.isEmpty() ? QStringLiteral("(your system's time service)") : server);
    if (!haveTimedated())
        m_syncStatus->setText(QStringLiteral("This computer keeps its time synchronized its own way, not through systemd's time service."));
    else if (!can)
        m_syncStatus->setText(QStringLiteral("This computer has no time service that can synchronize it."));
    else if (m_appliedSync && synchronized)
        m_syncStatus->setText(server.isEmpty() ? QStringLiteral("The time has been successfully synchronized.")
                                               : QStringLiteral("The time has been successfully synchronized with %1.").arg(server));
    else if (m_appliedSync)
        m_syncStatus->setText(QStringLiteral("Please wait while your computer synchronizes with %1").arg(m_server->currentText()));
    else
        m_syncStatus->setText(QString());
    m_nextSync->clear();
}

QDateTime DateTimeProperties::shown() const
{
    const QDateTime now = m_frozen.isValid() ? m_frozen : QDateTime::currentDateTime();
    return now.addMSecs(m_offset);
}

void DateTimeProperties::showDateTime(const QDateTime &at)
{
    const bool wasLoading = m_loading;
    m_loading = true;
    m_month->setCurrentIndex(at.date().month() - 1);
    if (m_year->value() != at.date().year())
        m_year->setValue(at.date().year());
    m_calendar->setDate(at.date());
    m_clock->setTime(at.time());
    if (!m_time->hasFocus() || !m_timeEdited)
        m_time->setTime(at.time());
    m_loading = wasLoading;
}

void DateTimeProperties::dateEdited(const QDate &date)
{
    const QDateTime now = shown();
    m_offset += now.msecsTo(QDateTime(date, now.time()));
    showDateTime(shown());
    updateApply();
}

void DateTimeProperties::timeEdited()
{
    // Editing stops the clock, as XP's did, until Apply.
    m_timeEdited = true;
    const QDateTime now = shown();
    m_offset += now.msecsTo(QDateTime(now.date(), m_time->time()));
    m_clock->setTime(m_time->time());
    updateApply();
}

void DateTimeProperties::tick()
{
    if (m_timeEdited) {
        // (held where it was set)
        m_offset = (m_frozen.isValid() ? m_frozen : QDateTime::currentDateTime()).msecsTo(QDateTime(shown().date(), m_time->time()));
        return;
    }
    showDateTime(shown());
}

void DateTimeProperties::zonePicked()
{
    const int i = m_zone->currentIndex();
    const bool known = i >= 0 && i < m_zones.size();
    const QTimeZone tz(known ? m_zones.at(i).iana : QByteArray());
    // Daylight saving: only for zones that have it, and can do without it.
    const bool hasDaylight = tz.isValid() && tz.hasDaylightTime();
    m_daylight->setEnabled(hasDaylight && !standardOnly(m_zones.value(i).iana).isEmpty());
    if (!hasDaylight) {
        const bool wasLoading = m_loading;
        m_loading = true;
        m_daylight->setChecked(false);
        m_loading = wasLoading;
    } else if (!m_loading) {
        m_daylight->setChecked(true);
    }
    if (tz.isValid())
        m_map->setOffset(tz.standardTimeOffset(QDateTime::currentDateTimeUtc()) / 60);
    updateZoneLabel();
    updateApply();
}

void DateTimeProperties::updateZoneLabel()
{
    const int i = m_zone->currentIndex();
    QString name;
    if (i >= 0 && i < m_zones.size()) {
        const timezones::Zone &z = m_zones.at(i);
        const QTimeZone tz(z.iana);
        const bool daylight = m_daylight->isChecked() && tz.isValid() && tz.isDaylightTime(shown());
        name = daylight ? z.daylight : z.standard;
    } else {
        name = QString::fromUtf8(m_appliedZone);
    }
    m_zoneLabel->setText(QStringLiteral("Current time zone:  %1").arg(name));
}

void DateTimeProperties::updateApply()
{
    if (!m_apply)
        return;
    const int i = m_zone ? m_zone->currentIndex() : -1;
    QByteArray zone = i >= 0 && i < m_zones.size() ? m_zones.at(i).iana : m_appliedZone;
    if (i >= 0 && m_daylight && !m_daylight->isChecked() && m_daylight->isEnabled())
        zone = standardOnly(zone);
    const bool changed = m_timeEdited || std::abs(m_offset) > 1000 || (m_zoneTouched && zone != m_appliedZone)
                         || (m_sync && m_sync->isChecked() != m_appliedSync);
    m_apply->setEnabled(changed);
}

bool DateTimeProperties::apply()
{
    if (!m_apply->isEnabled())
        return true;
    QStringList errors;
    // Synchronization first: a clock set by hand needs it off.
    if (m_sync->isEnabled() && m_sync->isChecked() != m_appliedSync) {
        if (const QString why = timedatedCall(QStringLiteral("SetNTP"), {m_sync->isChecked(), true}); !why.isEmpty())
            errors << why;
    }
    // The zone (if the user picked one: see m_zoneTouched).
    const int i = m_zone->currentIndex();
    if (m_zoneTouched && i >= 0 && i < m_zones.size()) {
        QByteArray zone = m_zones.at(i).iana;
        if (!m_daylight->isChecked() && m_daylight->isEnabled())
            zone = standardOnly(zone);
        if (!zone.isEmpty() && zone != m_appliedZone) {
            if (const QString why = timedatedCall(QStringLiteral("SetTimezone"), {QString::fromUtf8(zone), true}); !why.isEmpty()) {
                errors << why;
            } else {
                m_appliedZone = zone;
                m_zoneTouched = false;
                State state = State::load();
                state.settings.insert(QStringLiteral("dateTimeZone"), m_zones.at(i).key);
                state.save();
            }
        }
    }
    // The clock.
    if (m_timeEdited || std::abs(m_offset) > 1000) {
        if (timedated("NTP").toBool()) {
            errors << QStringLiteral("The time can't be set by hand while it's synchronized with an Internet time server. "
                                     "Turn that off on the Internet Time tab first.");
        } else {
            const qint64 usec = shown().toUTC().toMSecsSinceEpoch() * 1000;
            if (const QString why = timedatedCall(QStringLiteral("SetTime"), {usec, false, true}); !why.isEmpty())
                errors << why;
            else {
                m_offset = 0;
                m_timeEdited = false;
            }
        }
    }
    readTimeService();
    updateApply();
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Date and Time Properties"), errors.join(QStringLiteral("\n\n")));
        return false;
    }
    return true;
}

bool DateTimeProperties::saveScreenshots(const QString &dir)
{
    QDir().mkpath(dir);
    // A fixed moment, in Central Time.
    m_loading = true;
    m_frozen = QDateTime(QDate(2026, 9, 24), QTime(0, 37, 51));   // (a moment captured on XP)
    m_offset = 0;
    m_timeEdited = false;
    m_zone->setCurrentIndex(timezones::find(m_zones, "America/Chicago"));
    m_daylight->setChecked(true);
    m_loading = false;
    zonePicked();
    tick();
    setAttribute(Qt::WA_DontShowOnScreen);
    show();
    bool ok = true;
    const char *const names[] = {"datetime", "timezone", "internettime"};
    for (int i = 0; i < m_tabs->count(); ++i) {
        m_tabs->setCurrentIndex(i);
        QApplication::processEvents();
        ok &= grab().save(QDir(dir).filePath(QLatin1String(names[i]) + QLatin1String(".png")));
    }
    return ok;
}

} // namespace xpl::editor
