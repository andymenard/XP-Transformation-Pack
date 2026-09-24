#include "app/icons.h"

#include "app/media.h"
#include "app/package.h"
#include "app/paths.h"
#include "core/icons.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>

#include <memory>

namespace xpl {

namespace {

// XP resource -> freedesktop names, one row per icon, by resource id as the
// catalogue verified them (ids, not icon indexes). Nothing of today's
// application branding is replaced.
struct Mapping
{
    const char *file;
    int group;
    const char *context;
    const char *names;
};
const Mapping kMap[] = {
    {"shell32.dll", 4, "Places", "folder inode-directory folder-yellow"},
    {"shell32.dll", 5, "Places", "folder-open"},
    {"shell32.dll", 37, "Places", "folder-programs"},
    {"shell32.dll", 21, "Places", "folder-recent-files"},
    {"shell32.dll", 235, "Places", "user-home folder-home folder-documents"},
    {"shell32.dll", 236, "Places", "folder-pictures"},
    {"shell32.dll", 237, "Places", "folder-music"},
    {"shell32.dll", 238, "Places", "folder-videos"},
    {"shell32.dll", 35, "Places", "user-desktop folder-desktop"},
    {"shell32.dll", 257, "Places", "folder-remote folder-network"},
    {"shell32.dll", 44, "Places", "user-bookmarks favorites"},
    {"shell32.dll", 32, "Places", "user-trash user-trash-empty"},
    {"shell32.dll", 33, "Places", "user-trash-full"},
    {"shell32.dll", 16, "Devices", "computer computer-desktop"},
    {"shell32.dll", 8, "Devices", "drive-harddisk drive-harddisk-root drive-harddisk-system"},
    {"shell32.dll", 9, "Devices", "drive-network"},
    {"shell32.dll", 12, "Devices", "drive-optical media-optical"},
    {"shell32.dll", 7, "Devices", "media-floppy drive-removable-media-floppy"},
    {"shell32.dll", 306, "Devices", "drive-removable-media drive-removable-media-usb drive-harddisk-usb"},
    {"shell32.dll", 314, "Devices", "media-flash media-flash-sd"},
    {"shell32.dll", 17, "Devices", "printer printer-network"},
    {"shell32.dll", 248, "Devices", "camera-photo"},
    {"shell32.dll", 317, "Devices", "camera-video"},
    {"shell32.dll", 316, "Devices", "scanner"},
    {"shell32.dll", 302, "Devices", "media-optical-audio"},
    {"shell32.dll", 18, "Places", "network-server network-workgroup network-network"},
    {"shell32.dll", 19, "Categories", "applications-internet"},
    {"shell32.dll", 22, "Categories", "preferences-system systemsettings"},
    {"shell32.dll", 274, "Categories", "applications-system preferences-other"},
    {"shell32.dll", 270, "Categories", "applications-graphics preferences-desktop-theme"},
    {"shell32.dll", 268, "Categories", "preferences-desktop-accessibility"},
    {"shell32.dll", 269, "Categories", "system-users preferences-system-users"},
    {"shell32.dll", 250, "Categories", "preferences-desktop-display"},
    {"shell32.dll", 39, "Categories", "preferences-desktop-font"},
    {"shell32.dll", 275, "Categories", "preferences-system-time"},
    {"shell32.dll", 4, "Applications", "system-file-manager"},
    {"shell32.dll", 152, "MimeTypes", "text-plain text-x-generic"},
    {"shell32.dll", 1, "MimeTypes", "application-x-executable application-octet-stream unknown"},
    {"shell32.dll", 224, "MimeTypes", "video-x-generic"},
    {"shell32.dll", 225, "MimeTypes", "audio-x-generic"},
    {"shell32.dll", 226, "MimeTypes", "image-x-generic"},
    {"shell32.dll", 155, "MimeTypes", "font-x-generic"},
    {"shell32.dll", 156, "MimeTypes", "application-x-font-ttf"},
    {"shell32.dll", 154, "MimeTypes", "application-x-sharedlib"},
    {"shell32.dll", 319, "Actions", "folder-new"},
    {"shell32.dll", 23, "Actions", "edit-find system-search"},
    {"shell32.dll", 160, "Actions", "system-run"},
    {"shell32.dll", 240, "Actions", "edit-delete"},
    {"shell32.dll", 133, "Actions", "edit-copy"},
    {"shell32.dll", 133, "Status", "klipper clipboard"},
    {"shell32.dll", 137, "Actions", "document-properties"},
    {"shell32.dll", 17, "Actions", "document-print"},
    {"shell32.dll", 24, "Actions", "help-contents help-browser"},
    {"user32.dll", 101, "Status", "dialog-warning"},
    {"user32.dll", 102, "Status", "dialog-question"},
    {"user32.dll", 103, "Status", "dialog-error"},
    {"user32.dll", 104, "Status", "dialog-information"},
    {"main.cpl", 100, "Devices", "input-mouse"},
    {"main.cpl", 200, "Devices", "input-keyboard"},
    {"mmsys.cpl", 4361, "Devices", "input-gaming"},
    {"mmsys.cpl", 4378, "Devices", "audio-input-microphone"},
    {"sndvol32.exe", 304, "Devices", "audio-card audio-speakers"},
    {"sndvol32.exe", 304, "Status", "audio-volume-high audio-volume-medium audio-volume-low"},
    {"sndvol32.exe", 303, "Status", "audio-volume-muted"},
    {"hotplug.dll", 100, "Status", "device-notifier"},
    // XP SP2 moved Bluetooth's art into xpsp2res.dll: the glossy glyph, with
    // its own 16px tray image. Off and blocked states inherit, rather than
    // show a disabled radio as on.
    {"xpsp2res.dll", 300, "Status", "network-bluetooth network-bluetooth-activated bluetooth-active"},
    {"xpsp2res.dll", 300, "Categories", "preferences-system-bluetooth preferences-system-bluetooth-symbolic"},
    {"xpsp2res.dll", 300, "Devices", "bluetooth"},
    {"netshell.dll", 190, "Status", "network-wired network-wired-activated network-wireless-connected network-wireless-available"},
    {"netshell.dll", 195, "Status", "network-wired-unavailable network-wireless-disconnected network-wireless-off"},
    {"netshell.dll", 196, "Status", "network-wired-activated-limited"},
};

struct Source
{
    QString file;
    int group = 0;           // an icon group, or...
    int batteryFrame = -1;   // ...batmeter.dll's image list frame
    bool charging = false;
    QString context;
};

QHash<QString, Source> mappings()
{
    QHash<QString, Source> out;
    for (const Mapping &m : kMap)
        for (const QString &name : QString::fromLatin1(m.names).split(QLatin1Char(' '))) {
            const Source s{QLatin1String(m.file), m.group, -1, false, QLatin1String(m.context)};
            out.insert(name, s);
            if (s.context == QLatin1String("Status"))
                out.insert(name + QLatin1String("-symbolic"), s);
        }
    // SP2's signal strengths (xpsp2res 2010..2015); limited stays a warning.
    for (const int level : {0, 20, 25, 40, 50, 60, 75, 80, 100})
        for (const QString &base : {QStringLiteral("network-wireless-%1").arg(level),
                                    QStringLiteral("network-wireless-connected-%1").arg(level, 2, 10, QLatin1Char('0'))})
            for (const char *suffix : {"", "-locked", "-limited"})
                for (const char *symbolic : {"", "-symbolic"}) {
                    const bool limited = QLatin1String(suffix) == QLatin1String("-limited");
                    out.insert(base + QLatin1String(suffix) + QLatin1String(symbolic),
                               {limited ? QStringLiteral("netshell.dll") : QStringLiteral("xpsp2res.dll"),
                                limited ? 196 : 2010 + (level + 10) / 20, -1, false, QStringLiteral("Status")});
                }
    // Batteries: Plasma asks by tenths, XP shows >66%, >33%, >9%, critical.
    for (int level = 0; level <= 100; level += 10) {
        const int frame = level > 66 ? 0 : level > 33 ? 1 : level > 9 ? 2 : 3;
        for (const bool charging : {false, true})
            for (const char *symbolic : {"", "-symbolic"})
                out.insert(QStringLiteral("battery-%1%2%3").arg(level, 3, 10, QLatin1Char('0')).arg(charging ? "-charging" : "").arg(QLatin1String(symbolic)),
                           {QStringLiteral("batmeter.dll"), 0, frame, charging, QStringLiteral("Status")});
    }
    const struct { const char *name; int frame; } states[] = {
        {"battery-missing", 5}, {"battery-caution", 3}, {"battery-low", 2}, {"battery-good", 1},
        {"battery-full", 0}, {"battery-empty", 3}, {"battery-unknown", 4}, {"ac-adapter", 6},
    };
    for (const auto &st : states)
        for (const char *symbolic : {"", "-symbolic"})
            out.insert(QLatin1String(st.name) + QLatin1String(symbolic),
                       {QStringLiteral("batmeter.dll"), 0, st.frame, false, QStringLiteral("Status")});
    return out;
}

// batmeter.dll's image lists: bitmap 301 is ten 16px frames, 300 ten 32px,
// magenta the transparent colour; frame 7 is the charging overlay.
QMap<int, QImage> batteryFrame(const QString &file, int frame, bool charging, QString *error)
{
    QMap<int, QImage> out;
    for (const auto &[size, id] : {std::pair(16, 301), std::pair(32, 300)}) {
        const auto strip = media::bitmap(file, id, error);
        if (!strip || strip->width() != 10 * size + 1 || strip->height() != size)
            return {};
        auto cell = [&](int f) {
            QImage img = strip->copy(f * size, 0, size, size).convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    if ((img.pixel(x, y) & 0xffffff) == 0xff00ff)
                        img.setPixel(x, y, 0);
            return img;
        };
        QImage img = cell(frame);
        if (charging) {
            QImage over = cell(7);
            QPainter p(&img);
            p.drawImage(0, 0, over);
        }
        out.insert(size, img);
    }
    return out;
}

} // namespace

QByteArray recolouredSvg(const QByteArray &svg, const QColor &text)
{
    QString s = QString::fromUtf8(svg);
    QHash<QString, QString> colours;
    static const QRegularExpression styleBlock(QStringLiteral("<style[^>]*id=\"current-color-scheme\"[^>]*>(.*?)</style>"),
                                               QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression rule(QStringLiteral("\\.ColorScheme-(\\w+)\\s*\\{\\s*color\\s*:\\s*(#[0-9a-fA-F]{3,8})"));
    if (const auto block = styleBlock.match(s); block.hasMatch()) {
        for (auto it = rule.globalMatch(block.captured(1)); it.hasNext();) {
            const auto m = it.next();
            colours.insert(m.captured(1), m.captured(2));
        }
        s.remove(block.capturedStart(), block.capturedLength());
    }
    colours.insert(QStringLiteral("Text"), text.name());
    static const QRegularExpression tag(QStringLiteral("<[^>]*class=\"[^\"]*ColorScheme-(\\w+)[^\"]*\"[^>]*>"));
    QString out;
    qsizetype at = 0;
    for (auto it = tag.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        out += s.mid(at, m.capturedStart() - at);
        QString element = m.captured(0);
        element.replace(QLatin1String("currentColor"), colours.value(m.captured(1), text.name()));
        out += element;
        at = m.capturedEnd();
    }
    out += s.mid(at);
    out.replace(QLatin1String("currentColor"), text.name());
    return out.toUtf8();
}

QStringList iconSourceFiles()
{
    QStringList files;
    for (const Mapping &m : kMap)
        if (!files.contains(QLatin1String(m.file)))
            files << QLatin1String(m.file);
    files << QStringLiteral("batmeter.dll");
    return files;
}

QString writeIconTheme(const QString &directory, const QString &inheritFrom, const QColor &trayGlyphs, QString *error)
{
    const QString inherit = (inheritFrom.isEmpty() || inheritFrom == QLatin1String(kIconThemeId)
                             || inheritFrom.contains(QRegularExpression(QStringLiteral("[\\n\\r,/]"))))
                                ? QStringLiteral("breeze")
                                : inheritFrom;
    Package pkg(QDir(directory).filePath(QLatin1String(kIconThemeId)));
    QHash<QString, std::shared_ptr<PeResources>> files;
    QMap<QString, std::pair<int, QString>> directories;   // "16x16/places" -> (size, context)
    QJsonObject provenance;
    const auto map = mappings();
    QStringList names = map.keys();
    names.sort();
    for (const QString &name : std::as_const(names)) {
        const Source &src = map.value(name);
        QMap<int, QImage> images;
        if (src.batteryFrame >= 0) {
            images = batteryFrame(src.file, src.batteryFrame, src.charging, error);
        } else {
            if (!files.contains(src.file)) {
                const auto bytes = media::systemFile(src.file, error);
                if (!bytes)
                    return QString();
                auto pe = PeResources::parse(*bytes, error);
                if (!pe)
                    return QString();
                files.insert(src.file, std::make_shared<PeResources>(*pe));
            }
            for (const auto &[size, icon] : iconGroup(*files.value(src.file), QString::number(src.group), error).asKeyValueRange())
                images.insert(size, icon.image);
        }
        // (shell32's 96px folder is a thumbnail backplate, not the folder.)
        images.removeIf([](auto it) { return it.key() > 48; });
        if (images.isEmpty()) {
            if (error && error->isEmpty())
                *error = QStringLiteral("%1 has nothing for %2").arg(src.file, name);
            return QString();
        }
        for (const auto &[size, image] : images.asKeyValueRange()) {
            const QString folder = QStringLiteral("%1x%1/%2").arg(size).arg(src.context.toLower());
            directories.insert(folder, {size, src.context});
            pkg.write(folder + QLatin1Char('/') + name + QLatin1String(".png"), image);
        }
        QJsonArray sizes;
        for (const int size : images.keys())
            sizes.append(size);
        provenance.insert(name, QJsonObject{{QStringLiteral("file"), src.file},
                                            {src.batteryFrame >= 0 ? QStringLiteral("frame") : QStringLiteral("group"),
                                             src.batteryFrame >= 0 ? src.batteryFrame : src.group},
                                            {QStringLiteral("sizes"), sizes}});
    }

    // The shortcut arrow (shell32's overlay, 30). XP lays the whole overlay
    // over the icon, the arrow in its bottom-left corner: 7px on 16px icons,
    // 11px on 32px ones. KDE paints an emblem in a square of its own in the
    // icon's bottom-right corner -- 8px for icons under 32, 16 to 48, 22 to
    // 96, then 32 and 64 -- so each square gets XP's arrow at XP's size (the
    // 11px one doubled and more for big icons), in that corner.
    {
        if (!files.contains(QStringLiteral("shell32.dll"))) {
            const auto bytes = media::systemFile(QStringLiteral("shell32.dll"), error);
            auto pe = bytes ? PeResources::parse(*bytes, error) : std::nullopt;
            if (!pe)
                return QString();
            files.insert(QStringLiteral("shell32.dll"), std::make_shared<PeResources>(*pe));
        }
        QMap<int, QImage> arrows;   // XP's overlay size -> the arrow alone
        for (const auto &[size, icon] : iconGroup(*files.value(QStringLiteral("shell32.dll")), QStringLiteral("30"), error).asKeyValueRange()) {
            QImage img = icon.image.convertToFormat(QImage::Format_ARGB32);
            QRect box;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 0)
                        box |= QRect(x, y, 1, 1);
            if (!box.isEmpty())
                arrows.insert(size, img.copy(box));
        }
        if (!arrows.contains(16) || !arrows.contains(32)) {
            if (error)
                *error = QStringLiteral("shell32.dll has no 16 and 32px shortcut overlay");
            return QString();
        }
        QJsonArray sizes;
        for (const int square : {8, 16, 22, 32, 64}) {
            const QImage arrow = square < 16 ? arrows.value(16) : arrows.value(32);
            const int scale = square < 16 ? 1 : qMax(1, square / arrow.width());
            QImage img(square, square, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            const QSize size = arrow.size() * scale;
            p.drawImage(QRect(QPoint(square - size.width(), square - size.height()), size), arrow);
            p.end();
            const QString folder = QStringLiteral("%1x%1/emblems").arg(square);
            directories.insert(folder, {square, QStringLiteral("Emblems")});
            for (const char *name : {"emblem-symbolic-link", "emblem-symbolic-link-symbolic"})
                pkg.write(folder + QLatin1Char('/') + QLatin1String(name) + QLatin1String(".png"), img);
            sizes.append(square);
        }
        for (const char *name : {"emblem-symbolic-link", "emblem-symbolic-link-symbolic"})
            provenance.insert(QLatin1String(name), QJsonObject{{QStringLiteral("file"), QStringLiteral("shell32.dll")},
                                                            {QStringLiteral("group"), 30}, {QStringLiteral("sizes"), sizes}});
    }

    // Names XP has nothing for would be shortened ("...-inactive-symbolic"
    // to one of ours) before inheritance reaches them: the states XP can't
    // show are pinned to the inherited theme's own art.
    QStringList pinned{QStringLiteral("network-bluetooth-inactive-symbolic")};
    for (int level = 0; level <= 100; level += 10)
        for (const char *charging : {"", "-charging"})
            for (const char *profile : {"powersave", "balanced", "performance"})
                pinned << QStringLiteral("battery-%1%2-profile-%3").arg(level, 3, 10, QLatin1Char('0')).arg(QLatin1String(charging), QLatin1String(profile));
    // The tray's icons XP has nothing for (the panel asks for "-symbolic"
    // ones; Discover's are plain) come in one colour, the desktop's text
    // colour -- black on Luna's blue. They're taken from the inherited theme
    // in the tray's own text colour instead.
    static const QRegularExpression trayName(QStringLiteral(
        "^((brightness|redshift-status|video-display-brightness|input-keyboard-brightness|notification|notifications"
        "|media-playback|audio-input-microphone|microphone-sensitivity|camera|network|bluetooth|kdeconnect|plasmavault"
        "|preferences-desktop-display-randr|input-touchpad|system-suspend-inhibited|airplane-mode|klipper|state"
        "|input-keyboard-virtual|battery)[a-z0-9-]*-symbolic"
        "|update-(none|low|medium|high)|system-software-update|system-reboot-update)$"));
    // The file for each: from the nearest theme along the inheritance
    // (breeze last), in the size nearest 16 (16, larger, then smaller).
    struct Found { int theme; int distance; QString path; };
    QMap<QString, Found> found;
    QStringList themes{inherit};
    for (int t = 0; t < themes.size(); ++t) {
        const QString theme = themes.at(t);
        QStringList bases{QDir::homePath() + QLatin1String("/.local/share/icons")};
        for (const QString &dir : paths::systemData())
            bases << dir + QLatin1String("/icons");
        for (const QString &base : std::as_const(bases)) {
            const QString root = base + QLatin1Char('/') + theme;
            QSettings index(root + QLatin1String("/index.theme"), QSettings::IniFormat);
            for (QString parent : index.value(QStringLiteral("Icon Theme/Inherits")).toStringList())
                if (parent = parent.trimmed(); !themes.contains(parent) && parent != QLatin1String("hicolor"))
                    themes << parent;
            for (const QString &dir : index.value(QStringLiteral("Icon Theme/Directories")).toStringList()) {
                const int size = index.value(dir + QLatin1String("/Size"), 0).toInt();
                const int distance = size >= 16 ? size - 16 : 1000 + 16 - size;
                for (const QFileInfo &file : QDir(root + QLatin1Char('/') + dir).entryInfoList({QStringLiteral("*.svg")}, QDir::Files)) {
                    const QString name = file.completeBaseName();
                    if (!pinned.contains(name) && !(trayGlyphs.isValid() && !map.contains(name) && trayName.match(name).hasMatch()))
                        continue;
                    const auto have = found.constFind(name);
                    if (have != found.cend() && (have->theme < t || (have->theme == t && have->distance <= distance)))
                        continue;
                    found.insert(name, {t, distance, file.filePath()});
                }
            }
        }
        if (t == themes.size() - 1 && !themes.contains(QStringLiteral("breeze")))
            themes << QStringLiteral("breeze");
    }
    QJsonObject fallbacks;
    for (const auto &[name, entry] : found.asKeyValueRange()) {
        QFile f(entry.path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = trayGlyphs.isValid() ? recolouredSvg(f.readAll(), trayGlyphs) : f.readAll();
        for (const int size : {16, 22, 24, 32, 48}) {
            const QString folder = QStringLiteral("%1x%1/status").arg(size);
            directories.insert(folder, {size, QStringLiteral("Status")});
            pkg.write(folder + QLatin1Char('/') + name + QLatin1String(".svg"), bytes);
        }
        fallbacks.insert(name, entry.path);
    }

    QString index = QStringLiteral("[Icon Theme]\nName=Windows XP\nComment=Windows XP's own icons, taken from your XP media by XPLasma\n"
                                   "Inherits=%1,hicolor\nDirectories=%2\n")
                        .arg(inherit, QStringList(directories.keys()).join(QLatin1Char(',')));
    for (const auto &[folder, spec] : directories.asKeyValueRange())
        index += QStringLiteral("\n[%1]\nSize=%2\nContext=%3\nType=Fixed\n").arg(folder).arg(spec.first).arg(spec.second);
    pkg.write(QStringLiteral("index.theme"), index.toUtf8());
    pkg.writeJson(QStringLiteral("xplasma-icons.json"),
                  QJsonObject{{QLatin1String(kGeneratedKey), true}, {QStringLiteral("icons"), provenance},
                              {QStringLiteral("pinned"), fallbacks}});
    if (!pkg.ok()) {
        if (error)
            *error = pkg.error();
        return QString();
    }
    return pkg.directory();
}

} // namespace xpl
