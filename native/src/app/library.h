// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The theme library: everything XPlasma can apply, as XP's Display
// Properties presents it.
//
//   * Visual styles (.msstyles). Each has colour schemes and font sizes --
//     its layout INIs, named <SIZE><COLOUR>_INI (NORMALBLUE_INI,
//     LARGEFONTSHOMESTEAD_INI) -- and the names XP shows for them
//     (THEMES_INI: "Default (blue)", "Olive Green"). Indexing a style means
//     opening it, so the index is cached against each file's size and mtime.
//   * Theme bundles (.theme): a style and variant, or a Classic scheme, with a
//     wallpaper. Windows paths in them are found by name around the file.
//   * Windows Classic colour schemes: XP's 36 stock ones (shipped, from
//     HIVEDEF.INF) and the user's own, all .theme files.
#include "core/classic.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace xpl {

class ThemeFile;

// XP's three font sizes, as layout INI prefixes, and the suffix the matching
// stock Classic scheme carries.
struct FontSize
{
    const char *key;       // "NORMAL", "LARGEFONTS", "EXTRALARGE"
    const char *label;     // "Normal", "Large Fonts", "Extra Large Fonts"
    const char *suffix;    // "", " (large)", " (extra large)"
};
const QList<FontSize> &fontSizes();

struct StyleOption
{
    QString key;     // the INI token: "BLUE", "HOMESTEAD"; "NORMAL"
    QString name;    // what a .theme's ColorStyle= / Size= says: "NormalColor", "NormalSize"
    QString label;   // what XP shows: "Default (blue)", "Normal"
};

struct StyleVariant
{
    QString colour;  // StyleOption::key
    QString size;
    QString ini;     // "NORMALBLUE_INI"
};

struct StyleInfo
{
    QString path;
    QString label;              // XP's name for it ("Windows XP style"), or the file's
    bool official = false;      // one of Microsoft's own
    QList<StyleOption> colours; // in the style's order
    QList<StyleOption> sizes;
    QList<StyleVariant> variants;
    QString defaultVariant;     // what XP picks with no colour named
    QString error;              // why it can't be read (Vista-era binary layouts...)

    bool readable() const { return !variants.isEmpty(); }
    QList<StyleOption> sizesFor(const QString &colour) const;   // only the sizes that exist
    QString ini(const QString &colour, const QString &size) const;
    const StyleVariant *variant(const QString &ini) const;
    // A .theme's ColorStyle= and Size= as a layout INI; empty for the default.
    // Exact through the style's own tables; otherwise only an unambiguous
    // token match counts, never a guess.
    QString resolve(const QString &colorStyle, const QString &size) const;

    QJsonObject toJson() const;
    static StyleInfo fromJson(const QJsonObject &json);
};

struct ThemeBundle
{
    QString path;
    QString label;          // [Theme] DisplayName ("Windows XP" for XP's own)
    QString style;          // the .msstyles found, empty for Classic or not found
    QString variant;        // the layout INI it asks for, empty for the default
    QString styleMissing;   // [VisualStyles] Path when it couldn't be found
    QString wallpaper;      // found, or empty
    std::optional<int> fill;   // Plasma's FillMode for its WallpaperStyle, if it says
    bool classic() const { return style.isEmpty() && styleMissing.isEmpty(); }
    QJsonObject toJson() const;
};

class Library
{
public:
    // Everything under paths::themes() (and anything else in `roots`).
    static Library scan(const QStringList &roots = {});

    QList<StyleInfo> styles;     // readable or not; official first, then by label
    QList<ThemeBundle> themes;   // by label

    // One style, through the cache.
    static StyleInfo style(const QString &path);
    static ThemeBundle bundle(const QString &path);
    static ThemeBundle bundle(const ThemeFile &file, const QString &path);
};

// A Windows path from a .theme ("%WinDir%web\wallpaper\Bliss.bmp") as a file
// here: by name, in the bundle's own folder first (Foo.theme's Foo/), then
// `preferred` (where the name's stem is enough: bliss.jpg for Bliss.bmp),
// then around the .theme, then its parent's tree. An absolute Linux path is
// taken as is.
QString resolveAsset(const QString &themeFile, const QString &windowsPath, const QStringList &preferred = {});

// Plasma's wallpaper FillMode for a .theme's [Control Panel\Desktop], or
// nothing when it doesn't say.
std::optional<int> wallpaperFill(const ThemeFile &file);

namespace schemes {

struct Entry
{
    QString name;
    QString path;    // ":/schemes/..." for XP's own
    bool stock = false;
};

// XP's stock schemes and the user's, by name; a user scheme with a stock
// scheme's name hides it.
QList<Entry> list();
std::optional<ClassicScheme> load(const QString &name, QString *error = nullptr);

// "Windows Standard (large)" -> ("Windows Standard", "LARGEFONTS"). A scheme
// that only exists in one size ("Pumpkin (large)") is its own base.
std::pair<QString, QString> split(const QString &name);
QStringList bases();
bool sizeAvailable(const QString &base, const QString &sizeKey);

// Into paths::schemes() as <name>.theme; replaces a user scheme of that name.
bool save(const ClassicScheme &scheme, QString *path = nullptr, QString *error = nullptr);
bool remove(const QString &name);

// xp2plasma's saved-scheme JSON ({name: {colors, metrics, fonts}}).
QList<ClassicScheme> fromLegacyJson(const QByteArray &json);

} // namespace schemes

namespace presets {

struct Desktop
{
    QString wallpaper;       // empty: a solid colour
    int fill = 2;            // Plasma FillMode
    QColor colour = QColor(58, 110, 165);
};

struct Preset
{
    QString name;
    QString style;           // .msstyles, or empty for Classic
    QString colorStyle;      // the style's ColorStyle= / Size= names
    QString size;
    ClassicScheme scheme;    // for Classic
    Desktop desktop;
    QString base;            // a .theme to start from (its icons, sounds...), optional
};

// paths::themes()/Saved/<name>.theme; never replaces a file.
bool save(const Preset &preset, QString *path = nullptr, QString *error = nullptr);
// A preset XPlasma saved (and so may delete).
bool owned(const QString &path);

} // namespace presets

} // namespace xpl
