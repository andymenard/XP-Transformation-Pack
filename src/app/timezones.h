#pragma once
// XP's time zones, as its Date and Time Properties lists them: from the
// registry XP's setup writes (HIVESFT.INF, "Time Zones"), in XP's order and
// words -- "(GMT-06:00) Central Time (US & Canada)" -- each with the Linux
// zone it stands for (Qt's Windows-to-IANA table), and where it is on XP's
// world map (timedate.cpl's bitmap 50: every zone painted in colours of its
// own, a sea and a land one, which XP recolours).
#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

#include <optional>

namespace xpl::timezones {

struct Zone
{
    QString key;        // the registry key: XP's (Windows') zone id, "Central Standard Time"
    QString display;    // "(GMT-06:00) Central Time (US & Canada)"
    QString standard;   // "Central Standard Time"
    QString daylight;   // "Central Daylight Time"
    int index = 0;      // XP's order
    int mapSea = -1;    // the map's palette index for its sea, or -1
    int mapLand = -1;   // ...and for its land
    QByteArray iana;    // the Linux zone, "America/Chicago" (empty: none)
};

// XP's zones from a HIVESFT.INF, in XP's order.
QList<Zone> parse(const QByteArray &hivesft);
// The same, from the XP media.
std::optional<QList<Zone>> load(QString *error = nullptr);

// Which of `zones` is the Linux zone `iana` ("America/Chicago"), or -1.
int find(const QList<Zone> &zones, const QByteArray &iana);

// XP's world map: each pixel the palette index it was painted with (the
// bitmap's own palette colours are only tags), 356 x 184.
struct Map
{
    QImage indices;   // Format_Grayscale8: the palette index per pixel
};
std::optional<Map> map(QString *error = nullptr);

} // namespace xpl::timezones
