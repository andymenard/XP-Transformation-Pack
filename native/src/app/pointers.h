// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Mouse pointers, as XP's Mouse Properties > Pointers has them: a scheme
// names a cursor file for each of 15 roles (Normal Select, Help Select, ...,
// Link Select); an empty one is Windows Default's, which is user32.dll's
// own. XP's schemes come from its setup INFs (OPTIONAL.INF's Windows Black,
// Inverted, large and extra large; ACCESSOR.INF's 3D-White, Dinosaur,
// Hands, ...), their cursors from the media -- all read from it as needed
// (media::systemFile), nothing kept of Microsoft's in XPlasma.
//
// Applying one writes an Xcursor theme (~/.local/share/icons/xplasma-pointers)
// with each role under every name X and Wayland apps ask for, and switches
// Plasma to it at the scheme's size.
#include "app/session.h"
#include "app/state.h"
#include "core/cursors.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace xpl::pointers {

enum Role {
    Arrow, Help, AppStarting, Wait, Crosshair, IBeam, Pen, No,
    SizeNS, SizeWE, SizeNWSE, SizeNESW, SizeAll, UpArrow, Hand,
    RoleCount
};
// XP's names for them ("Normal Select", ...).
QString roleName(int role);
// The names apps ask a cursor theme for, the first the file's own.
QStringList xcursorNames(int role);

struct Scheme
{
    QString name;
    QStringList files;   // RoleCount entries: an XP cursor's file name, an absolute path, or empty (Windows Default)

    QJsonObject toJson() const;
    static Scheme fromJson(const QJsonObject &json);
    bool operator==(const Scheme &other) const { return name == other.name && files == other.files; }
};

inline constexpr const char *kDefaultScheme = "Windows Default";
inline constexpr const char *kThemeId = "xplasma-pointers";

// XP's schemes, by name (Windows Default among them); empty without XP media.
QList<Scheme> schemes(QString *error = nullptr);
Scheme defaultScheme();

// A role's cursor in a scheme (Windows Default's where it names none).
QList<CursorFrame> frames(const Scheme &scheme, int role, QString *error = nullptr);
// XP's pointer shadow under a cursor's frames.
QList<CursorFrame> withShadow(const QList<CursorFrame> &frames);

// The theme, into `dir` (its index.theme and cursors/); `size` is what it
// should be shown at (the Normal Select cursor's).
bool writeTheme(const Scheme &scheme, bool shadow, const QString &dir, int *size, QString *error = nullptr);

// What's applied (the scheme, and whether pointers have shadows), from state.
std::optional<std::pair<Scheme, bool>> applied(const State &state);
// Stable installed theme id; color profiles do not change this identity.
QString themeId(const Scheme &scheme, bool shadow);
bool apply(Session &session, State &state, const Scheme &scheme, bool shadow, QString *error = nullptr);
// The user's own pointers back.
void revert(Session &session, State &state);

// Every XP scheme as a cursor theme of its own, for Plasma's Cursors settings
// to offer (~/.local/share/icons/xplasma-cursors-<scheme>, "<scheme> (XP)"),
// none applied -- the Setup wizard's. Their names, or none with *error.
QStringList installSchemes(QString *error = nullptr);
void removeSchemes();

} // namespace xpl::pointers
