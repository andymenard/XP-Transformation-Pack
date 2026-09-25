#pragma once
// Sounds, as XP's Sounds and Audio Devices > Sounds has them: a sound scheme
// names a sound for each program event (Asterisk, Critical Stop, Device
// Connect, ..., Windows Logon), or none. XP's own scheme, Windows Default,
// is its registry's defaults (HIVEUSD.INF); its sounds are the WAVs setup
// copied into WINDOWS\Media, read from the media as needed
// (media::systemFile) -- nothing kept of Microsoft's in XPLasma.
//
// Plasma plays a sound for an event by a freedesktop sound theme's names
// (dialog-information, device-added, desktop-login...), from the theme
// kdeglobals names. Applying a scheme writes it as such a theme
// (~/.local/share/sounds/xplasma-sounds-*), each event's sound under the
// names Plasma plays for it -- and silence under those of events the scheme
// leaves quiet, so no other theme's sound stands in -- and switches Plasma
// to it; the log-in, log-out, lock and unlock sounds Plasma has off are
// turned on for the events that have one.
#include "app/session.h"
#include "app/state.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace xpl::sounds {

struct Event
{
    QString key;          // XP's (AppEvents\EventLabels): "SystemAsterisk"
    QString label;        // "Asterisk"
    QString program;      // the list's group: "Windows", "Windows Explorer"
    QStringList names;    // the sound theme names Plasma plays it as
};
// XP's events Plasma has a sound for, in XP's order (by program, then label).
const QList<Event> &events();

struct Scheme
{
    QString name;
    QMap<QString, QString> sounds;   // event key -> a media sound ("Windows XP Ding.wav") or an absolute path; none absent

    QJsonObject toJson() const;
    static Scheme fromJson(const QJsonObject &json);
    bool operator==(const Scheme &other) const { return name == other.name && sounds == other.sounds; }
    bool operator!=(const Scheme &other) const { return !(*this == other); }
};

inline constexpr const char *kDefaultScheme = "Windows Default";
inline constexpr const char *kNoSounds = "No Sounds";
inline constexpr const char *kThemeId = "xplasma-sounds";

// XP's two: Windows Default and No Sounds.
QList<Scheme> builtinSchemes();
Scheme defaultScheme();
// Schemes the user saved (Save As...), by name.
QList<Scheme> savedSchemes(const State &state);
void saveScheme(State &state, const Scheme &scheme);
void deleteScheme(State &state, const QString &name);

// The sounds in XP's WINDOWS\Media ("chimes.wav", "Windows XP Balloon.wav",
// ...), by the names XP showed them under, sorted as its list was.
QStringList mediaSounds();
// A scheme's sound as a file to play: a media sound taken off the media
// (kept in paths::extracted()), or the path it is; empty with *error.
QString soundFile(const QString &sound, QString *error = nullptr);

// The theme, into `dir` (its index.theme and stereo/*.wav).
bool writeTheme(const Scheme &scheme, const QString &dir, QString *error = nullptr);

// What's applied, from state.
std::optional<Scheme> applied(const State &state);
bool apply(Session &session, State &state, const Scheme &scheme, QString *error = nullptr);
// The sound theme the user had, and Plasma's own log-in and lock sounds.
void revert(Session &session, State &state);

} // namespace xpl::sounds
