#include "app/timezones.h"

#include "app/inf.h"
#include "app/media.h"
#include "core/dib.h"
#include "core/pe.h"

#include <QDateTime>
#include <QHash>
#include <QTimeZone>
#include <QtEndian>

#include <algorithm>

namespace xpl::timezones {

QList<Zone> parse(const QByteArray &hivesft)
{
    // HKLM,"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Time Zones\<key>","Display",0x00000002,"%GMT_MINUS_06_A_DESC%"
    static const QString root = QStringLiteral("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\");
    const Inf inf = Inf::parse(hivesft);
    QHash<QString, Zone> zones;
    for (const QString &section : inf.sectionNames())
        for (const QString &line : inf.lines(section)) {
            if (!line.contains(QLatin1String("Time Zones\\")))
                continue;
            const QStringList f = inf.fields(line);
            if (f.size() < 5 || f.at(0).compare(QLatin1String("HKLM"), Qt::CaseInsensitive) != 0
                || !f.at(1).startsWith(root, Qt::CaseInsensitive))
                continue;
            const QString key = f.at(1).mid(root.size());
            if (key.isEmpty() || key.contains(QLatin1Char('\\')))
                continue;
            Zone &z = zones[key];
            z.key = key;
            const QString name = f.at(2), value = f.at(4);
            if (name == QLatin1String("Display"))
                z.display = value;
            else if (name == QLatin1String("Std"))
                z.standard = value;
            else if (name == QLatin1String("Dlt"))
                z.daylight = value;
            else if (name == QLatin1String("Index"))
                z.index = value.toInt(nullptr, 0);
            else if (name == QLatin1String("MapID")) {
                const QStringList ids = value.split(QLatin1Char(','));   // "36,37": sea, land
                z.mapSea = ids.value(0, QStringLiteral("-1")).trimmed().toInt();
                z.mapLand = ids.value(1, QStringLiteral("-1")).trimmed().toInt();
            }
        }
    QList<Zone> out;
    for (Zone z : std::as_const(zones)) {
        if (z.display.isEmpty())
            continue;
        z.iana = QTimeZone::windowsIdToDefaultIanaId(z.key.toUtf8());
        // XP's names Windows has since retired.
        if (z.iana.isEmpty()) {
            static const QHash<QString, QByteArray> retired{
                {QStringLiteral("Mexico Standard Time"), "America/Mexico_City"},
                {QStringLiteral("Mexico Standard Time 2"), "America/Mazatlan"},
                {QStringLiteral("Mid-Atlantic Standard Time"), "Atlantic/South_Georgia"},
            };
            z.iana = retired.value(z.key);
        }
        out.append(z);
    }
    // XP's order: by offset from GMT, then by name -- its Index.
    std::sort(out.begin(), out.end(), [](const Zone &a, const Zone &b) { return a.index != b.index ? a.index < b.index : a.display < b.display; });
    return out;
}

std::optional<QList<Zone>> load(QString *error)
{
    const auto bytes = media::systemFile(QStringLiteral("hivesft.inf"), error);
    if (!bytes)
        return std::nullopt;
    QList<Zone> zones = parse(*bytes);
    if (zones.isEmpty()) {
        if (error)
            *error = QStringLiteral("HIVESFT.INF lists no time zones");
        return std::nullopt;
    }
    return zones;
}

int find(const QList<Zone> &zones, const QByteArray &iana)
{
    // The Windows zone the Linux one belongs to, then XP's zone of that name;
    // failing that, one that stands for the same Linux zone.
    const QByteArray windows = QTimeZone::ianaIdToWindowsId(iana);
    for (int i = 0; i < zones.size(); ++i)
        if (!windows.isEmpty() && zones.at(i).key.toUtf8() == windows)
            return i;
    for (int i = 0; i < zones.size(); ++i)
        if (zones.at(i).iana == iana)
            return i;
    // An alias ("US/Central") or a zone XP's list doesn't name: the one that
    // keeps the same time all year -- the same offsets, switched on the same
    // days.
    const QTimeZone wanted(iana);
    if (!wanted.isValid())
        return -1;
    const QDate year(QDate::currentDate().year(), 1, 1);
    for (int i = 0; i < zones.size(); ++i) {
        if (zones.at(i).iana.isEmpty())
            continue;
        const QTimeZone candidate(zones.at(i).iana);
        if (!candidate.isValid())
            continue;
        bool same = true;
        for (int day = 0; day < 366 && same; day += 3) {
            const QDateTime at(year.addDays(day).startOfDay(QTimeZone::UTC));
            same = wanted.offsetFromUtc(at) == candidate.offsetFromUtc(at);
        }
        if (same)
            return i;
    }
    return -1;
}

std::optional<Map> map(QString *error)
{
    const auto bytes = media::systemFile(QStringLiteral("timedate.cpl"), error);
    if (!bytes)
        return std::nullopt;
    const auto pe = PeResources::parse(*bytes, error);
    if (!pe)
        return std::nullopt;
    const Resource *r = pe->find(u"2", u"50");
    if (!r || r->data.size() < 40) {
        if (error)
            *error = QStringLiteral("timedate.cpl has no world map");
        return std::nullopt;
    }
    // The bitmap's colours only tell its palette indices apart: every one is
    // distinct, so each decoded pixel is looked up by colour.
    const auto *p = reinterpret_cast<const uchar *>(r->data.constData());
    const quint32 header = qFromLittleEndian<quint32>(p);
    const quint16 bpp = qFromLittleEndian<quint16>(p + 14);
    quint32 colours = qFromLittleEndian<quint32>(p + 32);
    if (!colours && bpp <= 8)
        colours = 1u << bpp;
    if (bpp != 8 || header + colours * 4 > quint32(r->data.size())) {
        if (error)
            *error = QStringLiteral("timedate.cpl's world map isn't the 256-colour bitmap XP's is");
        return std::nullopt;
    }
    QHash<QRgb, int> index;
    for (quint32 i = 0; i < colours; ++i) {
        const uchar *c = p + header + i * 4;
        if (const QRgb rgb = qRgb(c[2], c[1], c[0]); !index.contains(rgb))
            index.insert(rgb, int(i));   // (the palette's unused tail repeats colours: the first wins)
    }
    const QImage decoded = decodeDib(r->data).image;
    if (decoded.isNull()) {
        if (error)
            *error = QStringLiteral("timedate.cpl's world map doesn't decode");
        return std::nullopt;
    }
    Map m;
    m.indices = QImage(decoded.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < decoded.height(); ++y) {
        uchar *line = m.indices.scanLine(y);
        for (int x = 0; x < decoded.width(); ++x)
            line[x] = uchar(index.value(decoded.pixel(x, y) | 0xff000000u, 0));
    }
    return m;
}

} // namespace xpl::timezones
