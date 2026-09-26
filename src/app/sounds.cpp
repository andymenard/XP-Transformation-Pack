// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sounds.h"

#include "app/media.h"
#include "app/paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>

namespace xpl::sounds {

namespace {

// WINDOWS\Media's sounds: the name XP showed, and the file on the media
// (LAYOUT.INF; I386/<NAME>, or CAB-compressed <NAM>_).
struct MediaSound
{
    const char *name;
    const char *file;
};
constexpr MediaSound kMedia[] = {
    {"chimes.wav", "chimes.wav"},
    {"chord.wav", "chord.wav"},
    {"ding.wav", "ding.wav"},
    {"notify.wav", "notify.wav"},
    {"recycle.wav", "recycle.wav"},
    {"ringin.wav", "ringin.wav"},
    {"ringout.wav", "ringout.wav"},
    {"tada.wav", "tada.wav"},
    {"Windows XP Balloon.wav", "xpballn.wav"},
    {"Windows XP Battery Critical.wav", "xpbatcrt.wav"},
    {"Windows XP Battery Low.wav", "xpbatlow.wav"},
    {"Windows XP Critical Stop.wav", "xpcrtstp.wav"},
    {"Windows XP Default.wav", "xpdef.wav"},
    {"Windows XP Ding.wav", "xpding.wav"},
    {"Windows XP Error.wav", "xperror.wav"},
    {"Windows XP Exclamation.wav", "xpexcl.wav"},
    {"Windows XP Hardware Fail.wav", "xphdfail.wav"},
    {"Windows XP Hardware Insert.wav", "xphdinst.wav"},
    {"Windows XP Hardware Remove.wav", "xphdrem.wav"},
    {"Windows XP Information Bar.wav", "xpinfbar.wav"},
    {"Windows XP Logoff Sound.wav", "xplogoff.wav"},
    {"Windows XP Logon Sound.wav", "xplogon.wav"},
    {"Windows XP Menu Command.wav", "xpmenu.wav"},
    {"Windows XP Minimize.wav", "xpmin.wav"},
    {"Windows XP Notify.wav", "xpnotify.wav"},
    {"Windows XP Pop-up Blocked.wav", "xpblkpop.wav"},
    {"Windows XP Print complete.wav", "xpprint.wav"},
    {"Windows XP Recycle.wav", "xprecycl.wav"},
    {"Windows XP Restore.wav", "xprestor.wav"},
    {"Windows XP Ringin.wav", "xpringin.wav"},
    {"Windows XP Ringout.wav", "xprngout.wav"},
    {"Windows XP Shutdown.wav", "xpshutdn.wav"},
    {"Windows XP Start.wav", "xpstart.wav"},
    {"Windows XP Startup.wav", "xpstartu.wav"},
};

// Sounds Plasma plays that XP never made (it said nothing on plugging the
// power in, or asking for a password): quiet under every scheme.
const char *const kNeverXp[] = {"power-plug", "power-unplug", "dialog-warning-auth"};

// The log-in and lock sounds Plasma has off: turned on for an event that
// has a sound. (file, group, the theme name it plays, the event.)
struct Switch
{
    const char *file;
    const char *group;
    const char *sound;   // written as its Sound=, or null to keep Plasma's
    const char *event;
};
constexpr Switch kSwitches[] = {
    {"plasma_workspace.notifyrc", "Event/startkde", nullptr, "SystemStart"},
    {"plasma_workspace.notifyrc", "Event/exitkde", nullptr, "SystemExit"},
    {"ksmserver.notifyrc", "Event/locked", "service-logout", "WindowsLogoff"},
    {"ksmserver.notifyrc", "Event/unlocked", "service-login", "WindowsLogon"},
};

QString soundsDir()
{
    return paths::localShare() + QLatin1String("/sounds");
}

// XPlasma's own notification events, for the window sounds Plasma has none
// for: the Start button sends them (XpWindowSounds.qml), each plays its
// sound theme name.
QString notifyrcPath()
{
    return paths::localShare() + QLatin1String("/knotifications6/xplasma.notifyrc");
}

QByteArray notifyrc()
{
    const std::pair<const char *, const char *> events[] = {
        {"windowOpen", "window-new"},           {"windowClose", "window-close"},
        {"windowMinimize", "window-minimized"}, {"windowMaximize", "window-maximized"},
        {"windowRestoreUp", "window-unminimized"}, {"windowRestoreDown", "window-unmaximized"},
    };
    QByteArray out = "# Written by XPlasma with a sound scheme: XP's window sounds.\n[Global]\nIconName=preferences-desktop-sound\nComment=XPlasma\n";
    for (const auto &[id, sound] : events)
        out += QByteArray("\n[Event/") + id + "]\nName=" + id + "\nSound=" + sound + "\nAction=Sound\n";
    return out;
}

bool write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

// A twentieth of a second of silence, as a WAV (22 kHz, 16-bit, mono).
QByteArray silence()
{
    const quint32 samples = 22050 / 20;
    const quint32 data = samples * 2;
    QByteArray wav(44 + int(data), '\0');
    char *p = wav.data();
    auto u32 = [&](int at, quint32 v) { qToLittleEndian(v, p + at); };
    auto u16 = [&](int at, quint16 v) { qToLittleEndian(v, p + at); };
    memcpy(p, "RIFF", 4);
    u32(4, 36 + data);
    memcpy(p + 8, "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);          // PCM
    u16(22, 1);          // mono
    u32(24, 22050);
    u32(28, 22050 * 2);
    u16(32, 2);
    u16(34, 16);
    memcpy(p + 36, "data", 4);
    u32(40, data);
    return wav;
}

} // namespace

const QList<Event> &events()
{
    // Plasma's names for each: KDE's message boxes (information, warning,
    // critical, question), powerdevil's battery alarms (low as
    // battery-caution, critical as battery-low), Plasma's device, trash,
    // log-in and log-out notices, the bell (and the volume's feedback,
    // XP's Default Beep), mail and messages; lock and unlock as XP's
    // Windows Logoff and Logon (XP played them switching users); and the
    // windows opening, closing, minimizing, maximizing and restoring, which
    // Plasma has no events for: XPlasma's Start button plays those
    // (XpWindowSounds.qml, through xplasma.notifyrc).
    static const QList<Event> list = [] {
        const QString windows = QStringLiteral("Windows");
        const QString explorer = QStringLiteral("Windows Explorer");
        auto e = [](const char *key, const char *label, const QString &program, std::initializer_list<const char *> names) {
            Event ev{QLatin1String(key), QLatin1String(label), program, {}};
            for (const char *n : names)
                ev.names << QLatin1String(n);
            return ev;
        };
        return QList<Event>{
            e("SystemAsterisk", "Asterisk", windows, {"dialog-information"}),
            e("Close", "Close program", windows, {"window-close"}),
            e("CriticalBatteryAlarm", "Critical Battery Alarm", windows, {"battery-low"}),
            e("SystemHand", "Critical Stop", windows, {"dialog-error-critical", "dialog-error"}),
            e(".Default", "Default Beep", windows, {"bell-window-system", "bell", "audio-volume-change"}),
            e("DeviceConnect", "Device Connect", windows, {"device-added"}),
            e("DeviceDisconnect", "Device Disconnect", windows, {"device-removed"}),
            e("DeviceFail", "Device Failed to Connect", windows, {"device-added-failed"}),
            e("SystemExclamation", "Exclamation", windows, {"dialog-warning"}),
            e("SystemExit", "Exit Windows", windows, {"desktop-logout"}),
            e("LowBatteryAlarm", "Low Battery Alarm", windows, {"battery-caution"}),
            e("Maximize", "Maximize", windows, {"window-maximized"}),
            e("Minimize", "Minimize", windows, {"window-minimized"}),
            e("MailBeep", "New Mail Notification", windows, {"message-new-email"}),
            e("Open", "Open program", windows, {"window-new"}),
            e("AppGPFault", "Program error", windows, {"dialog-error-serious"}),
            e("SystemQuestion", "Question", windows, {"dialog-question"}),
            e("RestoreDown", "Restore Down", windows, {"window-unmaximized"}),
            e("RestoreUp", "Restore Up", windows, {"window-unminimized"}),
            e("SystemStart", "Start Windows", windows, {"desktop-login"}),
            e("SystemNotification", "System Notification", windows, {"message", "message-new-instant"}),
            e("WindowsLogoff", "Windows Logoff", windows, {"service-logout"}),
            e("WindowsLogon", "Windows Logon", windows, {"service-login"}),
            e("EmptyRecycleBin", "Empty Recycle Bin", explorer, {"trash-empty"}),
        };
    }();
    return list;
}

QJsonObject Scheme::toJson() const
{
    QJsonObject map;
    for (auto it = sounds.cbegin(); it != sounds.cend(); ++it)
        map.insert(it.key(), it.value());
    return QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("sounds"), map}};
}

Scheme Scheme::fromJson(const QJsonObject &json)
{
    Scheme s;
    s.name = json.value(QLatin1String("name")).toString();
    const QJsonObject map = json.value(QLatin1String("sounds")).toObject();
    for (auto it = map.begin(); it != map.end(); ++it)
        if (!it.value().toString().isEmpty())
            s.sounds.insert(it.key(), it.value().toString());
    return s;
}

Scheme defaultScheme()
{
    // XP's registry defaults (HIVEUSD.INF, AppEvents\Schemes\Apps\*\*\.Default).
    Scheme s{QLatin1String(kDefaultScheme), {}};
    const std::pair<const char *, const char *> sounds[] = {
        {".Default", "Windows XP Ding.wav"},
        {"CriticalBatteryAlarm", "Windows XP Battery Critical.wav"},
        {"DeviceConnect", "Windows XP Hardware Insert.wav"},
        {"DeviceDisconnect", "Windows XP Hardware Remove.wav"},
        {"DeviceFail", "Windows XP Hardware Fail.wav"},
        {"LowBatteryAlarm", "Windows XP Battery Low.wav"},
        {"MailBeep", "Windows XP Notify.wav"},
        {"SystemAsterisk", "Windows XP Error.wav"},
        {"SystemExclamation", "Windows XP Exclamation.wav"},
        {"SystemExit", "Windows XP Shutdown.wav"},
        {"SystemHand", "Windows XP Critical Stop.wav"},
        {"SystemNotification", "Windows XP Balloon.wav"},
        {"SystemStart", "Windows XP Startup.wav"},
        {"WindowsLogoff", "Windows XP Logoff Sound.wav"},
        {"WindowsLogon", "Windows XP Logon Sound.wav"},
        {"EmptyRecycleBin", "Windows XP Recycle.wav"},
    };
    for (const auto &[key, file] : sounds)
        s.sounds.insert(QLatin1String(key), QLatin1String(file));
    return s;
}

QList<Scheme> builtinSchemes()
{
    return {defaultScheme(), Scheme{QLatin1String(kNoSounds), {}}};
}

QList<Scheme> savedSchemes(const State &state)
{
    QList<Scheme> out;
    const QJsonObject saved = state.settings.value(QStringLiteral("soundSchemes")).toObject();
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        Scheme s = Scheme::fromJson(it.value().toObject());
        s.name = it.key();
        out << s;
    }
    return out;
}

void saveScheme(State &state, const Scheme &scheme)
{
    QJsonObject saved = state.settings.value(QStringLiteral("soundSchemes")).toObject();
    saved.insert(scheme.name, scheme.toJson());
    state.settings.insert(QStringLiteral("soundSchemes"), saved);
}

void deleteScheme(State &state, const QString &name)
{
    QJsonObject saved = state.settings.value(QStringLiteral("soundSchemes")).toObject();
    saved.remove(name);
    if (saved.isEmpty())
        state.settings.remove(QStringLiteral("soundSchemes"));
    else
        state.settings.insert(QStringLiteral("soundSchemes"), saved);
}

QStringList mediaSounds()
{
    QStringList out;
    for (const MediaSound &m : kMedia)
        out << QLatin1String(m.name);
    return out;
}

QString soundFile(const QString &sound, QString *error)
{
    if (QFileInfo(sound).isAbsolute()) {
        if (QFileInfo::exists(sound))
            return sound;
        if (error)
            *error = QStringLiteral("Windows cannot find the file %1.").arg(sound);
        return QString();
    }
    for (const MediaSound &m : kMedia) {
        if (sound.compare(QLatin1String(m.name), Qt::CaseInsensitive) != 0)
            continue;
        const QString file = QLatin1String(m.file);
        // (systemFile keeps what it takes off the media in extracted())
        if (!media::systemFile(file, error))
            return QString();
        return paths::extracted() + QLatin1Char('/') + file;
    }
    if (error)
        *error = QStringLiteral("There is no sound %1 in Windows XP.").arg(sound);
    return QString();
}

bool writeTheme(const Scheme &scheme, const QString &dir, QString *error)
{
    QDir(dir).removeRecursively();
    const QString index = QStringLiteral("[Sound Theme]\nName=%1 (XP)\nComment=Windows XP's sounds, set by XPlasma Properties\n"
                                         "Directories=stereo\n\n[stereo]\nOutputProfile=stereo\n")
                              .arg(scheme.name);
    if (!write(dir + QLatin1String("/index.theme"), index.toUtf8())) {
        if (error)
            *error = QStringLiteral("can't write %1").arg(dir);
        return false;
    }
    const QString stereo = dir + QLatin1String("/stereo/");
    const QByteArray quiet = silence();
    for (const char *name : kNeverXp)
        write(stereo + QLatin1String(name) + QLatin1String(".wav"), quiet);
    for (const Event &e : events()) {
        const QString sound = scheme.sounds.value(e.key);
        QString file;
        if (!sound.isEmpty()) {
            QString why;
            file = soundFile(sound, &why);
            if (file.isEmpty()) {
                if (error)
                    *error = QStringLiteral("%1: %2").arg(e.label, why);
                return false;
            }
        }
        for (const QString &name : e.names) {
            // (the file's own kind, as the theme's readers go by the suffix)
            const QString suffix = file.isEmpty() ? QStringLiteral("wav") : QFileInfo(file).suffix().toLower();
            const QString out = stereo + name + QLatin1Char('.') + (suffix.isEmpty() ? QStringLiteral("wav") : suffix);
            bool ok;
            if (file.isEmpty()) {
                ok = write(out, quiet);
            } else {
                QDir().mkpath(stereo);
                QFile::remove(out);
                ok = QFile::copy(file, out);
            }
            if (!ok) {
                if (error)
                    *error = QStringLiteral("can't write %1").arg(out);
                return false;
            }
        }
    }
    return true;
}

std::optional<Scheme> applied(const State &state)
{
    if (!state.settings.contains(QStringLiteral("sounds")))
        return std::nullopt;
    return Scheme::fromJson(state.settings.value(QStringLiteral("sounds")).toObject());
}

bool apply(Session &session, State &state, const Scheme &scheme, QString *error)
{
    // A theme id per scheme, as the pointers': Plasma's players keep sounds
    // they've played by theme and name.
    const QByteArray key = QJsonDocument(scheme.toJson()).toJson(QJsonDocument::Compact);
    const QString id = QLatin1String(kThemeId) + QLatin1Char('-')
                       + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(8));
    if (!writeTheme(scheme, soundsDir() + QLatin1Char('/') + id, error))
        return false;
    const QStringList group{QStringLiteral("Sounds")};
    if (!state.settings.contains(QStringLiteral("previousSoundTheme"))) {
        const QString current = session.readConfig(QStringLiteral("kdeglobals"), group, QStringLiteral("Theme"));
        if (!current.startsWith(QLatin1String(kThemeId)))
            state.settings.insert(QStringLiteral("previousSoundTheme"), current);
    }
    session.writeConfig(QStringLiteral("kdeglobals"), group, QStringLiteral("Theme"), id);
    for (const Switch &s : kSwitches) {
        const QStringList event{QLatin1String(s.group)};
        if (scheme.sounds.value(QLatin1String(s.event)).isEmpty()) {
            session.deleteConfig(QLatin1String(s.file), event, QStringLiteral("Action"));
            continue;
        }
        session.writeConfig(QLatin1String(s.file), event, QStringLiteral("Action"), QStringLiteral("Sound"));
        if (s.sound)
            session.writeConfig(QLatin1String(s.file), event, QStringLiteral("Sound"), QLatin1String(s.sound));
    }
    // Earlier schemes' themes go.
    for (const QFileInfo &d : QDir(soundsDir()).entryInfoList({QLatin1String(kThemeId) + QLatin1String("-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        if (d.fileName() != id)
            QDir(d.absoluteFilePath()).removeRecursively();
    write(notifyrcPath(), notifyrc());
    state.settings.insert(QStringLiteral("sounds"), scheme.toJson());
    state.settings.remove(QStringLiteral("soundsOff"));
    return true;
}

void revert(Session &session, State &state)
{
    if (!state.settings.contains(QStringLiteral("sounds")) && !state.settings.contains(QStringLiteral("previousSoundTheme")))
        return;
    const QStringList group{QStringLiteral("Sounds")};
    const QString previous = state.settings.value(QStringLiteral("previousSoundTheme")).toString();
    if (previous.isEmpty() || previous.startsWith(QLatin1String(kThemeId)))
        session.deleteConfig(QStringLiteral("kdeglobals"), group, QStringLiteral("Theme"));
    else
        session.writeConfig(QStringLiteral("kdeglobals"), group, QStringLiteral("Theme"), previous);
    for (const Switch &s : kSwitches) {
        const QStringList event{QLatin1String(s.group)};
        session.deleteConfig(QLatin1String(s.file), event, QStringLiteral("Action"));
        if (s.sound)
            session.deleteConfig(QLatin1String(s.file), event, QStringLiteral("Sound"));
    }
    for (const QFileInfo &d : QDir(soundsDir()).entryInfoList({QLatin1String(kThemeId) + QLatin1String("-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(d.absoluteFilePath()).removeRecursively();
    QDir().rmdir(soundsDir());   // (if the user has no themes of their own)
    QFile::remove(notifyrcPath());
    QDir().rmdir(QFileInfo(notifyrcPath()).absolutePath());
    state.settings.remove(QStringLiteral("sounds"));
    state.settings.remove(QStringLiteral("previousSoundTheme"));
}

bool wantedWithLook(const State &state)
{
    return !applied(state) && !state.settings.value(QStringLiteral("soundsOff")).toBool();
}

void turnOff(Session &session, State &state)
{
    revert(session, state);
    state.settings.insert(QStringLiteral("soundsOff"), true);
}

} // namespace xpl::sounds
