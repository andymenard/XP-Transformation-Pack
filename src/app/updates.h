#pragma once
// Whether a newer XPlasma is out, as XP's Automatic Updates told of
// Windows': project.ini's UpdatesUrl names a small JSON file,
//
//   {"version": "0.2.0", "page": "https://...", "notes": "What's new, a line or two."}
//
// -- any host can serve it (a git host's raw file, a static page). Checked
// at most once a day, at login (xplasma-edit --check-updates); a newer one
// shows XP's globe in the tray. A version the user has said no to isn't
// shown again. No URL, no check.
#include "app/state.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace xpl::updates {

struct Release
{
    QString version;   // "0.2.0"
    QString page;      // where to get it
    QString notes;     // what's new, optional
};

// Where to look: XPLASMA_UPDATES_URL if set (tests), else project.ini's.
QString url();

// The file's contents, read; none with *error if it isn't one.
std::optional<Release> parse(const QByteArray &json, QString *error = nullptr);
// Whether `latest` is newer than `current` ("0.2.0" > "0.1.0").
bool newer(const QString &latest, const QString &current);

// Whether a check is due: none in the last day.
bool due(const State &state, const QDateTime &now = QDateTime::currentDateTimeUtc());
void checked(State &state, const QDateTime &now = QDateTime::currentDateTimeUtc());

// Whether to tell of `release`: newer than this XPlasma, and not a version
// the user has declined.
bool worthTelling(const State &state, const Release &release);
void decline(State &state, const Release &release);

// The file fetched (waited for, 15 s at most); none with *error.
std::optional<Release> fetch(const QString &url, QString *error = nullptr);

} // namespace xpl::updates
