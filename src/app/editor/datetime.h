#pragma once
// xplasma-edit --date-time: Windows XP's Date and Time Properties, for the
// computer's clock -- laid out from XP's own dialog templates (timedate.cpl,
// in dialog units), its words and its world map taken from the XP media.
//
//   Date & Time    the month, year and day, the time on XP's analog clock;
//                  Apply sets the computer's clock
//   Time Zone      XP's list of zones and its map; each stands for a Linux
//                  zone. Without daylight saving, the zone's standard time
//                  all year (a fixed Etc/GMT zone).
//   Internet Time  keeping the clock synchronized over the network
//
// All three go through systemd's timedated (org.freedesktop.timedate1),
// which asks for an administrator's password as XP asked for the right.
#include "app/timezones.h"

#include <QDateTime>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTimeEdit;
class QTimer;

namespace xpl::editor {

class CalendarGrid;
class AnalogClock;
class WorldMap;

class DateTimeProperties : public QDialog
{
    Q_OBJECT
public:
    explicit DateTimeProperties(QWidget *parent = nullptr);

    // Every tab rendered to DIR as PNGs (tests; the clock at 00:37:51 on
    // 24 September 2026 in Central Time, whatever the computer's -- a moment
    // captured on XP).
    bool saveScreenshots(const QString &dir);

private:
    QWidget *dateTimeTab();
    QWidget *timeZoneTab();
    QWidget *internetTimeTab();

    // XP's dialog units, in this dialog's font.
    QRect du(int x, int y, int w, int h) const;

    QDateTime shown() const;           // the date and time the dialog shows
    void showDateTime(const QDateTime &at);
    void dateEdited(const QDate &date);
    void timeEdited();
    void tick();
    void zonePicked();
    void updateZoneLabel();
    void readTimeService();
    void updateApply();
    bool apply();

    QTabWidget *m_tabs = nullptr;
    QPushButton *m_apply = nullptr;
    // Date & Time
    QComboBox *m_month = nullptr;
    QSpinBox *m_year = nullptr;
    CalendarGrid *m_calendar = nullptr;
    AnalogClock *m_clock = nullptr;
    QTimeEdit *m_time = nullptr;
    QLabel *m_zoneLabel = nullptr;
    // Time Zone
    QComboBox *m_zone = nullptr;
    WorldMap *m_map = nullptr;
    QCheckBox *m_daylight = nullptr;
    // Internet Time
    QCheckBox *m_sync = nullptr;
    QComboBox *m_server = nullptr;
    QPushButton *m_updateNow = nullptr;
    QLabel *m_syncStatus = nullptr;
    QLabel *m_nextSync = nullptr;

    QList<timezones::Zone> m_zones;
    QByteArray m_appliedZone;          // the computer's zone when opened / last applied
    bool m_appliedSync = false;
    qint64 m_offset = 0;               // ms the shown time is ahead of the real one
    bool m_timeEdited = false;
    bool m_zoneTouched = false;        // the zone list or its daylight box used, since opened / applied
    bool m_loading = false;
    QTimer *m_ticker = nullptr;
    QDateTime m_frozen;                // (screenshots: a fixed "now")
};

} // namespace xpl::editor
