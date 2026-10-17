// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/library.h"

#include "app/paths.h"
#include "core/theme.h"
#include "core/themefile.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace xpl {

const QList<FontSize> &fontSizes()
{
    static const QList<FontSize> sizes = {
        {"NORMAL", "Normal", ""},
        {"LARGEFONTS", "Large Fonts", " (large)"},
        {"EXTRALARGE", "Extra Large Fonts", " (extra large)"},
    };
    return sizes;
}

namespace {

// 'NORMALBLUE_INI' -> ("NORMAL", "BLUE"); nothing if it isn't a variant.
std::optional<std::pair<QString, QString>> splitIni(const QString &name)
{
    if (!name.endsWith(QLatin1String("_INI")))
        return std::nullopt;
    const QString stem = name.chopped(4);
    for (const FontSize &s : fontSizes()) {
        const QLatin1String key(s.key);
        if (stem.startsWith(key) && stem.size() > key.size())
            return std::pair(QString(key), stem.mid(key.size()));
    }
    return std::nullopt;
}

// XP's names for the Luna-era colours where THEMES_INI doesn't say; anything
// else is the token made readable (ROYALE -> Royale, DarkRoyale -> Dark Royale).
QString colourLabel(const QString &token)
{
    static const QHash<QString, QString> known = {
        {QStringLiteral("HOMESTEAD"), QStringLiteral("Olive Green")},
        {QStringLiteral("METALLIC"), QStringLiteral("Silver")},
        {QStringLiteral("DARKROYALE"), QStringLiteral("Royale Noir")},
    };
    if (known.contains(token))
        return known.value(token);
    if (token == token.toUpper())
        return token.left(1) + token.mid(1).toLower();
    static const QRegularExpression words(QStringLiteral("[A-Z][a-z0-9]*|[A-Z]+(?![a-z])"));
    QStringList out;
    for (auto it = words.globalMatch(token); it.hasNext();) {
        const QString w = it.next().captured();
        out << w.left(1).toUpper() + w.mid(1).toLower();
    }
    return out.isEmpty() ? token : out.join(QLatin1Char(' '));
}

bool isOfficial(const QString &path)
{
    static const QSet<QString> names = {
        QStringLiteral("luna.msstyles"), QStringLiteral("royale.msstyles"), QStringLiteral("royale noir.msstyles"),
        QStringLiteral("zune.msstyles"), QStringLiteral("embedded.msstyles"),
    };
    return names.contains(QFileInfo(path).fileName().toLower());
}

StyleInfo readStyle(const QString &path)
{
    StyleInfo info;
    info.path = path;
    info.official = isOfficial(path);
    info.label = QFileInfo(path).completeBaseName();
    QString error;
    const auto theme = Theme::load(path, QString(), &error);
    if (!theme) {
        info.error = error;
        return info;
    }
    const VariantTable &t = theme->variants();
    const QSet<QString> inis(t.iniNames.begin(), t.iniNames.end());

    // What XP shows, from THEMES_INI.
    const ThemeFile labels = ThemeFile::fromText(t.themesIni);
    if (const QString name = labels.value(u"documentation", u"DisplayName"); !name.isEmpty())
        info.label = name;
    auto shown = [&](const QString &kind, const QString &name) {
        return labels.value(QString(kind + QLatin1Char('.') + name), u"DisplayName");
    };

    QHash<QString, int> colourAt, sizeAt;
    auto addColour = [&](const QString &token, const QString &name) {
        if (colourAt.contains(token))
            return;
        const QString label = shown(QStringLiteral("ColorScheme"), name);
        colourAt.insert(token, int(info.colours.size()));
        info.colours.append({token, name, label.isEmpty() ? colourLabel(token) : label});
    };
    auto addSize = [&](const QString &key, const QString &name) {
        if (sizeAt.contains(key))
            return;
        QString label = shown(QStringLiteral("Size"), name);
        if (label.isEmpty())
            for (const FontSize &s : fontSizes())
                if (key == QLatin1String(s.key))
                    label = QLatin1String(s.label);
        sizeAt.insert(key, int(info.sizes.size()));
        info.sizes.append({key, name, label});
    };

    // FILERESNAMES is colour-major: [ci * sizes + si] is COLORNAMES[ci] at
    // SIZENAMES[si]. It is the only thing tying a .theme's "NormalColor" to
    // the resource's "BLUE".
    const auto &cn = t.colourNames, &sn = t.sizeNames, &fn = t.fileNames;
    if (!cn.isEmpty() && !sn.isEmpty() && fn.size() >= cn.size() * sn.size())
        for (int ci = 0; ci < cn.size(); ++ci)
            for (int si = 0; si < sn.size(); ++si) {
                const QString file = fn.at(ci * sn.size() + si).toUpper();
                const auto split = splitIni(file);
                if (!split || !inis.contains(file))
                    continue;
                addColour(split->second, cn.at(ci));
                addSize(split->first, sn.at(si));
            }
    QStringList sorted(t.iniNames);
    sorted.sort();
    for (const QString &ini : sorted)
        if (const auto split = splitIni(ini)) {
            addColour(split->second, split->second);
            addSize(split->first, split->first);
            info.variants.append({split->second, split->first, ini});
        }
    // Sizes in XP's order.
    std::sort(info.sizes.begin(), info.sizes.end(), [](const StyleOption &a, const StyleOption &b) {
        auto rank = [](const QString &k) {
            for (int i = 0; i < fontSizes().size(); ++i)
                if (k == QLatin1String(fontSizes().at(i).key))
                    return i;
            return 99;
        };
        return rank(a.key) < rank(b.key);
    });
    info.defaultVariant = theme->variant();
    if (info.variants.isEmpty())
        info.error = QStringLiteral("no colour schemes (not an XP-era style?)");
    return info;
}

// --- the style cache ----------------------------------------------------------------

struct StyleCache
{
    QMutex lock;
    bool loaded = false;
    bool dirty = false;
    QJsonObject entries;   // path -> {size, mtime, info}

    static QString file() { return paths::cache() + QLatin1String("/style-index.json"); }
    static constexpr int kVersion = 1;

    void load()
    {
        if (loaded)
            return;
        loaded = true;
        QFile f(file());
        if (!f.open(QIODevice::ReadOnly))
            return;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        if (root.value(QLatin1String("version")).toInt() == kVersion)
            entries = root.value(QLatin1String("styles")).toObject();
    }

    void save()
    {
        if (!dirty)
            return;
        QDir().mkpath(paths::cache());
        QSaveFile f(file());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), kVersion}, {QStringLiteral("styles"), entries}})
                        .toJson(QJsonDocument::Compact));
            if (f.commit())
                dirty = false;
        }
    }
};

StyleCache &cache()
{
    static StyleCache c;
    return c;
}

StyleInfo cachedStyle(const QString &path)
{
    const QFileInfo fi(path);
    const QString key = fi.absoluteFilePath();
    StyleCache &c = cache();
    QMutexLocker locker(&c.lock);
    c.load();
    const QJsonObject hit = c.entries.value(key).toObject();
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
    if (!hit.isEmpty() && hit.value(QLatin1String("size")).toDouble() == double(fi.size())
        && hit.value(QLatin1String("mtime")).toDouble() == double(mtime))
        return StyleInfo::fromJson(hit.value(QLatin1String("info")).toObject());
    locker.unlock();
    StyleInfo info = readStyle(key);
    locker.relock();
    c.entries.insert(key, QJsonObject{{QStringLiteral("size"), double(fi.size())},
                                      {QStringLiteral("mtime"), double(mtime)},
                                      {QStringLiteral("info"), info.toJson()}});
    c.dirty = true;
    return info;
}

QString themeLabel(const ThemeFile &file, const QString &path)
{
    // XP's own .theme files name themselves through themeui.dll's strings.
    const QString name = file.value(u"Theme", u"DisplayName").trimmed();
    const QString compact = QString(name).remove(QLatin1Char(' ')).toLower();
    if (compact == QLatin1String("@themeui.dll,-2017"))
        return QStringLiteral("Windows XP");
    if (compact == QLatin1String("@themeui.dll,-2016"))
        return QStringLiteral("Windows Classic");
    if (!name.isEmpty() && !name.startsWith(QLatin1Char('@')))
        return name;
    return QFileInfo(path).completeBaseName();
}

QJsonArray optionsJson(const QList<StyleOption> &options)
{
    QJsonArray a;
    for (const StyleOption &o : options)
        a.append(QJsonObject{{QStringLiteral("key"), o.key}, {QStringLiteral("name"), o.name}, {QStringLiteral("label"), o.label}});
    return a;
}

QList<StyleOption> optionsFromJson(const QJsonArray &a)
{
    QList<StyleOption> options;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        options.append({o.value(QLatin1String("key")).toString(), o.value(QLatin1String("name")).toString(),
                        o.value(QLatin1String("label")).toString()});
    }
    return options;
}

} // namespace

// --- StyleInfo ----------------------------------------------------------------------

QList<StyleOption> StyleInfo::sizesFor(const QString &colour) const
{
    QList<StyleOption> out;
    for (const StyleOption &s : sizes)
        if (!ini(colour, s.key).isEmpty())
            out.append(s);
    return out;
}

QString StyleInfo::ini(const QString &colour, const QString &size) const
{
    for (const StyleVariant &v : variants)
        if (v.colour == colour && v.size == size)
            return v.ini;
    return QString();
}

const StyleVariant *StyleInfo::variant(const QString &iniName) const
{
    for (const StyleVariant &v : variants)
        if (v.ini.compare(iniName, Qt::CaseInsensitive) == 0)
            return &v;
    return nullptr;
}

QString StyleInfo::resolve(const QString &colorStyle, const QString &size) const
{
    const QString colour = colorStyle.trimmed().toLower();
    if (colour.isEmpty())
        return QString();
    QString token;
    for (const StyleOption &c : colours)
        if (c.name.toLower() == colour)
            token = c.key;
    if (token.isEmpty()) {
        QStringList hits;
        for (const StyleOption &c : colours)
            if (c.key.toLower() == colour || c.key.toLower().startsWith(colour))
                hits << c.key;
        if (hits.isEmpty())
            for (const StyleOption &c : colours)
                if (colour.startsWith(c.key.toLower()))
                    hits << c.key;
        if (hits.size() == 1)
            token = hits.first();
    }
    if (token.isEmpty())
        return QString();

    const QString wanted = size.trimmed().toLower();
    QString sizeKey;
    for (const StyleOption &s : sizes)
        if (s.name.toLower() == wanted)
            sizeKey = s.key;
    if (sizeKey.isEmpty()) {
        // Windows' own spellings when the style's tables don't say.
        static const QHash<QString, QString> spellings = {
            {QStringLiteral("normalsize"), QStringLiteral("NORMAL")},
            {QStringLiteral("largesize"), QStringLiteral("LARGEFONTS")},
            {QStringLiteral("extralargesize"), QStringLiteral("EXTRALARGE")},
            {QStringLiteral("extralargefonts"), QStringLiteral("EXTRALARGE")},
        };
        sizeKey = spellings.value(wanted, QStringLiteral("NORMAL"));
    }
    for (const QString &key : {sizeKey, QStringLiteral("NORMAL")})
        if (const QString found = ini(token, key); !found.isEmpty())
            return found;
    return QString();
}

QJsonObject StyleInfo::toJson() const
{
    QJsonArray vs;
    for (const StyleVariant &v : variants)
        vs.append(QJsonObject{{QStringLiteral("colour"), v.colour}, {QStringLiteral("size"), v.size}, {QStringLiteral("ini"), v.ini}});
    QJsonObject o{{QStringLiteral("path"), path},
                  {QStringLiteral("label"), label},
                  {QStringLiteral("official"), official},
                  {QStringLiteral("colours"), optionsJson(colours)},
                  {QStringLiteral("sizes"), optionsJson(sizes)},
                  {QStringLiteral("variants"), vs},
                  {QStringLiteral("default"), defaultVariant}};
    if (!error.isEmpty())
        o.insert(QStringLiteral("error"), error);
    return o;
}

StyleInfo StyleInfo::fromJson(const QJsonObject &o)
{
    StyleInfo info;
    info.path = o.value(QLatin1String("path")).toString();
    info.label = o.value(QLatin1String("label")).toString();
    info.official = o.value(QLatin1String("official")).toBool();
    info.colours = optionsFromJson(o.value(QLatin1String("colours")).toArray());
    info.sizes = optionsFromJson(o.value(QLatin1String("sizes")).toArray());
    for (const QJsonValue &v : o.value(QLatin1String("variants")).toArray()) {
        const QJsonObject vo = v.toObject();
        info.variants.append({vo.value(QLatin1String("colour")).toString(), vo.value(QLatin1String("size")).toString(),
                              vo.value(QLatin1String("ini")).toString()});
    }
    info.defaultVariant = o.value(QLatin1String("default")).toString();
    info.error = o.value(QLatin1String("error")).toString();
    return info;
}

QJsonObject ThemeBundle::toJson() const
{
    QJsonObject o{{QStringLiteral("path"), path}, {QStringLiteral("label"), label}};
    if (classic())
        o.insert(QStringLiteral("classic"), true);
    if (!style.isEmpty())
        o.insert(QStringLiteral("style"), style);
    if (!variant.isEmpty())
        o.insert(QStringLiteral("variant"), variant);
    if (!styleMissing.isEmpty())
        o.insert(QStringLiteral("styleMissing"), styleMissing);
    if (!wallpaper.isEmpty())
        o.insert(QStringLiteral("wallpaper"), wallpaper);
    if (fill)
        o.insert(QStringLiteral("fill"), *fill);
    return o;
}

// --- the library --------------------------------------------------------------------

StyleInfo Library::style(const QString &path)
{
    StyleInfo info = cachedStyle(path);
    cache().save();
    return info;
}

ThemeBundle Library::bundle(const ThemeFile &file, const QString &path)
{
    ThemeBundle b;
    b.path = path;
    b.label = themeLabel(file, path);
    if (const QString vs = file.value(u"VisualStyles", u"Path"); !vs.isEmpty()) {
        b.style = resolveAsset(path, vs);
        if (b.style.isEmpty()) {
            b.styleMissing = vs;
        } else {
            const StyleInfo info = cachedStyle(b.style);
            b.variant = info.resolve(file.value(u"VisualStyles", u"ColorStyle"), file.value(u"VisualStyles", u"Size"));
        }
    }
    if (const QString wp = file.value(u"Control Panel\\Desktop", u"Wallpaper"); !wp.isEmpty())
        b.wallpaper = resolveAsset(path, wp, {paths::wallpapers()});
    b.fill = wallpaperFill(file);
    return b;
}

ThemeBundle Library::bundle(const QString &path)
{
    ThemeBundle b;
    b.path = path;
    b.label = QFileInfo(path).completeBaseName();
    if (const auto file = ThemeFile::load(path))
        b = bundle(*file, path);
    cache().save();
    return b;
}

Library Library::scan(const QStringList &roots)
{
    Library lib;
    QSet<QString> seen;
    QStringList styleFiles, themeFiles;
    for (const QString &root : QStringList{paths::themes()} + roots) {
        QDirIterator it(root, {QStringLiteral("*.msstyles"), QStringLiteral("*.theme")}, QDir::Files,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString file = it.next();
            if (file.contains(QLatin1String("/__MACOSX/")))
                continue;   // resource forks from Mac zips, not styles
            const QString real = QFileInfo(file).canonicalFilePath();
            if (seen.contains(real))
                continue;
            seen.insert(real);
            (file.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive) ? themeFiles : styleFiles) << file;
        }
    }
    styleFiles.sort();
    themeFiles.sort();
    for (const QString &f : std::as_const(styleFiles))
        lib.styles.append(cachedStyle(f));
    for (const QString &f : std::as_const(themeFiles))
        if (const auto file = ThemeFile::load(f))
            lib.themes.append(bundle(*file, f));
    cache().save();

    // Two styles XP would call the same are told apart by their folder.
    QHash<QString, int> counts;
    for (const StyleInfo &s : std::as_const(lib.styles))
        ++counts[s.label.toLower()];
    for (StyleInfo &s : lib.styles)
        if (counts.value(s.label.toLower()) > 1)
            s.label += QStringLiteral(" (%1)").arg(QFileInfo(s.path).dir().dirName());
    std::stable_sort(lib.styles.begin(), lib.styles.end(), [](const StyleInfo &a, const StyleInfo &b) {
        if (a.official != b.official)
            return a.official;
        return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
    });
    std::stable_sort(lib.themes.begin(), lib.themes.end(), [](const ThemeBundle &a, const ThemeBundle &b) {
        return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
    });
    return lib;
}

bool Library::stockTheme(const ThemeBundle &theme)
{
    if(presets::owned(theme.path) || QFileInfo(theme.path).dir().dirName()==QLatin1String("Saved"))return false;
    const QFileInfo file(theme.path);
    return (theme.label==QLatin1String("Windows XP") && file.fileName().compare("Luna.theme",Qt::CaseInsensitive)==0 && file.dir().dirName().compare("Luna",Qt::CaseInsensitive)==0)
        || (theme.label==QLatin1String("Windows Classic") && theme.classic() && file.fileName().compare("Windows Classic.theme",Qt::CaseInsensitive)==0);
}

QList<ThemeBundle> Library::themeChoices() const
{
    auto identity=[](const QString &path){if(path.isEmpty())return QString();const QFileInfo file(path);const auto real=file.canonicalFilePath();return real.isEmpty()?file.absoluteFilePath():real;};
    QString luna;
    for(const auto &theme:themes)if(stockTheme(theme) && theme.label==QLatin1String("Windows XP") && !theme.style.isEmpty())luna=identity(theme.style);
    QList<ThemeBundle> choices;
    for(const auto &theme:themes){
        const QFileInfo path(theme.path);
        bool redundant=false;
        if(!luna.isEmpty() && !theme.style.isEmpty() && identity(theme.style)==luna && path.dir().dirName()==QLatin1String("XP Presets")
            && QStringList{"Luna Blue","Luna Olive Green","Luna Silver","Windows Luna Blue","Windows Luna Olive Green","Windows Luna Silver"}.contains(theme.label)){
            // Only our minimal convenience presets are folded into Appearance.
            // Saved/imported themes with custom icons, sounds or colors remain.
            if(const auto file=ThemeFile::load(theme.path)){
                redundant=true;
                for(const auto &section:file->sections())if(!QStringList{"theme","visualstyles","control panel/desktop"}.contains(section.split(QLatin1Char('\\'),Qt::SkipEmptyParts).join(QLatin1Char('/')).toLower()))redundant=false;
                // A different desktop is a real theme, not a color shortcut.
                for(const auto &stock:themes)if(stockTheme(stock) && stock.label==QLatin1String("Windows XP"))
                    if((identity(theme.wallpaper)!=identity(stock.wallpaper)
                        && !(stock.wallpaper.isEmpty() && identity(theme.wallpaper)==identity(QFileInfo(stock.path).dir().absoluteFilePath("../../wallpapers/Bliss.jpg")))) || theme.fill!=stock.fill)redundant=false;
            }
        }
        if(!redundant)choices.append(theme);
    }
    QHash<QString,int> labels;
    for(const auto &theme:choices)++labels[theme.label.toCaseFolded()];
    for(auto &theme:choices)if(labels.value(theme.label.toCaseFolded())>1 && QFileInfo(theme.path).dir().dirName()==QLatin1String("Saved"))theme.label+=QStringLiteral(" (saved)");
    auto rank=[](const ThemeBundle &theme){return stockTheme(theme)?(theme.label==QLatin1String("Windows XP")?0:1):2;};
    std::stable_sort(choices.begin(),choices.end(),[&](const auto &a,const auto &b){const int ar=rank(a),br=rank(b);return ar!=br?ar<br:a.label.compare(b.label,Qt::CaseInsensitive)<0;});
    return choices;
}

// --- assets ---------------------------------------------------------------------------

QString resolveAsset(const QString &themeFile, const QString &windowsPath, const QStringList &preferred)
{
    // A preset XPlasma saved names files exactly: never reduce that to a
    // name and pick up some other file.
    if (QDir::isAbsolutePath(windowsPath) && !windowsPath.contains(QLatin1Char('\\')))
        return QFileInfo(windowsPath).isFile() ? windowsPath : QString();
    const QString target = QString(windowsPath).replace(QLatin1Char('/'), QLatin1Char('\\')).section(QLatin1Char('\\'), -1).trimmed().toLower();
    if (target.isEmpty())
        return QString();
    const QString targetStem = target.section(QLatin1Char('.'), 0, -2).isEmpty() ? target : target.section(QLatin1Char('.'), 0, -2);
    const QFileInfo theme(themeFile);
    const QDir base = theme.absoluteDir();
    // Collections keep Foo.theme next to a Foo/ holding its files; that
    // folder is the bundle proper and wins over its siblings.
    QStringList roots;
    for (const QString &d : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (d.compare(theme.completeBaseName(), Qt::CaseInsensitive) == 0)
            roots << base.filePath(d);
    roots << preferred << base.absolutePath();
    if (QDir parent = base; parent.cdUp())
        roots << parent.absolutePath();

    constexpr int kMaxDepth = 3;
    for (const QString &root : std::as_const(roots)) {
        const bool stemOk = preferred.contains(root);   // stock copies differ by format: bliss.bmp / bliss.jpg
        // Breadth first, each directory's entries sorted: the shallowest,
        // then alphabetically first, match wins.
        QList<std::pair<QString, int>> queue{{root, 0}};
        while (!queue.isEmpty()) {
            const auto [dir, depth] = queue.takeFirst();
            const QDir d(dir);
            for (const QString &f : d.entryList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
                const QString low = f.toLower();
                if (low == target || (stemOk && low.section(QLatin1Char('.'), 0, -2) == targetStem))
                    return d.filePath(f);
            }
            if (depth < kMaxDepth)
                for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
                    queue.append({d.filePath(sub), depth + 1});
        }
    }
    return QString();
}

std::optional<int> wallpaperFill(const ThemeFile &file)
{
    // Plasma's FillMode is Qt Quick's Image.fillMode: Stretch 0, PreserveAspectFit
    // 1, PreserveAspectCrop 2, Tile 3, Pad (centred) 6.
    const QString desktop = QStringLiteral("Control Panel\\Desktop");
    if (file.value(desktop, u"TileWallpaper").trimmed() == QLatin1String("1"))
        return 3;
    if (!file.contains(desktop, u"WallpaperStyle"))
        return std::nullopt;
    // XP knows 0 (centre) and 2 (stretch); later Windows added fit, fill, span.
    static const QHash<QString, int> styles = {
        {QStringLiteral("0"), 6}, {QStringLiteral("2"), 0}, {QStringLiteral("6"), 1},
        {QStringLiteral("10"), 2}, {QStringLiteral("22"), 2},
    };
    const QString style = file.value(desktop, u"WallpaperStyle").trimmed();
    if (!styles.contains(style))
        return std::nullopt;
    return styles.value(style);
}

// --- Classic schemes --------------------------------------------------------------------

namespace schemes {

namespace {
QString nameOf(const QString &path)
{
    if (const auto file = ThemeFile::load(path)) {
        const QString name = file->value(u"Theme", u"DisplayName").trimmed();
        if (!name.isEmpty() && !name.startsWith(QLatin1Char('@')))
            return name;
    }
    return QFileInfo(path).completeBaseName();
}

QString fileName(const QString &name)
{
    QString safe = name;
    safe.replace(QLatin1Char('/'), QLatin1Char('-'));
    return safe.trimmed() + QLatin1String(".theme");
}
} // namespace

QList<Entry> list()
{
    QList<Entry> user, out;
    const QDir own(paths::schemes());
    for (const QString &f : own.entryList({QStringLiteral("*.theme")}, QDir::Files, QDir::Name))
        user.append({nameOf(own.filePath(f)), own.filePath(f), false});
    const QDir stock(QStringLiteral(":/schemes"));
    for (const QString &f : stock.entryList({QStringLiteral("*.theme")}, QDir::Files, QDir::Name)) {
        const Entry e{nameOf(stock.filePath(f)), stock.filePath(f), true};
        const bool hidden = std::any_of(user.begin(), user.end(), [&](const Entry &u) {
            return u.name.compare(e.name, Qt::CaseInsensitive) == 0;
        });
        if (!hidden)
            out.append(e);
    }
    out += user;
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    return out;
}

std::optional<ClassicScheme> load(const QString &name, QString *error)
{
    for (const Entry &e : list())
        if (e.name.compare(name, Qt::CaseInsensitive) == 0) {
            const auto file = ThemeFile::load(e.path, error);
            if (!file)
                return std::nullopt;
            ClassicScheme s = ClassicScheme::fromThemeFile(*file);
            s.name = e.name;
            return s;
        }
    if (error)
        *error = QStringLiteral("no Classic scheme called %1").arg(name);
    return std::nullopt;
}

std::pair<QString, QString> split(const QString &name)
{
    QStringList names;
    for (const Entry &e : list())
        names << e.name;
    for (const FontSize &s : fontSizes()) {
        const QLatin1String suffix(s.suffix);
        if (suffix.size() && name.endsWith(suffix)) {
            const QString base = name.chopped(suffix.size());
            if (names.contains(base, Qt::CaseInsensitive))
                return {base, QLatin1String(s.key)};
            return {name, QStringLiteral("NORMAL")};
        }
    }
    return {name, QStringLiteral("NORMAL")};
}

QStringList bases()
{
    QStringList out;
    for (const Entry &e : list())
        if (split(e.name).first == e.name)
            out << e.name;
    return out;
}

bool sizeAvailable(const QString &base, const QString &sizeKey)
{
    for (const FontSize &s : fontSizes())
        if (sizeKey == QLatin1String(s.key)) {
            if (!*s.suffix)
                return true;
            const QString want = base + QLatin1String(s.suffix);
            for (const Entry &e : list())
                if (e.name.compare(want, Qt::CaseInsensitive) == 0)
                    return true;
        }
    return false;
}

bool save(const ClassicScheme &scheme, QString *path, QString *error)
{
    if (scheme.name.trimmed().isEmpty() || scheme.name.contains(QLatin1Char('\n')) || scheme.name.contains(QLatin1Char('\r'))) {
        if (error)
            *error = QStringLiteral("a scheme needs a one-line name");
        return false;
    }
    QDir().mkpath(paths::schemes());
    ThemeFile file;
    file.setValue(u"Theme", u"DisplayName", scheme.name.trimmed());
    scheme.writeTo(file);
    const QString target = QDir(paths::schemes()).filePath(fileName(scheme.name));
    if (!file.save(target, error))
        return false;
    if (path)
        *path = target;
    return true;
}

bool remove(const QString &name)
{
    for (const Entry &e : list())
        if (!e.stock && e.name.compare(name, Qt::CaseInsensitive) == 0)
            return QFile::remove(e.path);
    return false;
}

QList<ClassicScheme> fromLegacyJson(const QByteArray &json)
{
    QList<ClassicScheme> out;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        if (entry.isEmpty())
            continue;
        ClassicScheme s;
        s.name = it.key();
        const QJsonObject colours = entry.value(QLatin1String("colors")).toObject();
        for (auto c = colours.begin(); c != colours.end(); ++c) {
            const QJsonArray rgb = c.value().toArray();
            if (rgb.size() < 3)
                continue;
            // xp2plasma's names for the two XP left unnamed.
            QString key = c.key();
            if (key == QLatin1String("menuhilight_or_unused"))
                key = QStringLiteral("ButtonAlternateFace");
            else if (key == QLatin1String("hotlight"))
                key = QStringLiteral("HotTrackingColor");
            s.setColour(key, QColor(rgb.at(0).toInt(), rgb.at(1).toInt(), rgb.at(2).toInt()));
        }
        const QJsonObject m = entry.value(QLatin1String("metrics")).toObject();
        auto pair = [&](const char *key, int *a, int *b) {
            const QJsonArray v = m.value(QLatin1String(key)).toArray();
            if (v.size() >= 2) {
                *a = v.at(0).toInt(*a);
                *b = v.at(1).toInt(*b);
            }
        };
        s.metrics.borderWidth = m.value(QLatin1String("border")).toInt(s.metrics.borderWidth);
        pair("caption", &s.metrics.captionWidth, &s.metrics.captionHeight);
        pair("smcaption", &s.metrics.smCaptionWidth, &s.metrics.smCaptionHeight);
        pair("menu", &s.metrics.menuWidth, &s.metrics.menuHeight);
        pair("scroll", &s.metrics.scrollWidth, &s.metrics.scrollHeight);
        pair("icon", &s.icons.horzSpacing, &s.icons.vertSpacing);
        const QJsonObject fonts = entry.value(QLatin1String("fonts")).toObject();
        auto font = [&](const char *key, LogFont *lf) {
            const QJsonObject f = fonts.value(QLatin1String(key)).toObject();
            if (f.isEmpty())
                return;
            *lf = LogFont::fromPoints(f.value(QLatin1String("face")).toString(QStringLiteral("Tahoma")),
                                      f.value(QLatin1String("pt")).toInt(8), f.value(QLatin1String("bold")).toBool());
            lf->italic = f.value(QLatin1String("italic")).toBool();
        };
        font("caption", &s.metrics.captionFont);
        font("smcaption", &s.metrics.smCaptionFont);
        font("menu", &s.metrics.menuFont);
        font("status", &s.metrics.statusFont);
        font("message", &s.metrics.messageFont);
        font("icon", &s.icons.font);
        out.append(s);
    }
    return out;
}

} // namespace schemes

// --- presets ----------------------------------------------------------------------------

namespace presets {

namespace {
QString savedDir()
{
    return paths::themes() + QLatin1String("/Saved");
}

QString slug(const QString &name)
{
    QString s;
    for (const QChar c : name)
        s += c.isLetterOrNumber() ? c : QLatin1Char('-');
    while (s.startsWith(QLatin1Char('-')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('-')))
        s.chop(1);
    return s.isEmpty() ? QStringLiteral("Theme") : s;
}
} // namespace

bool save(const Preset &preset, QString *path, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (preset.name.contains(QLatin1Char('\n')) || preset.name.contains(QLatin1Char('\r')))
        return fail(QStringLiteral("theme names can't contain line breaks"));
    ThemeFile file;
    if (!preset.base.isEmpty()) {
        const auto base = ThemeFile::load(preset.base, error);
        if (!base)
            return false;
        file = *base;
        // The copy lives elsewhere: point what it names at the files found.
        for (const QString &section : file.sections())
            for (const QString &key : file.keys(section)) {
                const QString value = file.value(section, key);
                if (value.contains(QLatin1Char('\\')) || value.contains(QLatin1Char('/')))
                    if (const QString found = resolveAsset(preset.base, value); !found.isEmpty())
                        file.setValue(section, key, found);
            }
    }
    file.setValue(u"Theme", u"DisplayName", preset.name);
    file.setValue(u"XPlasma", u"Saved", QStringLiteral("1"));
    file.setValue(u"XPlasma",u"LiveColors",QString::fromUtf8(QJsonDocument(preset.liveColors).toJson(QJsonDocument::Compact)));
    if (preset.style.isEmpty()) {
        file.removeSection(u"VisualStyles");
        preset.scheme.writeTo(file);
    } else {
        file.setValue(u"VisualStyles", u"Path", QFileInfo(preset.style).absoluteFilePath());
        file.setValue(u"VisualStyles", u"ColorStyle", preset.colorStyle);
        file.setValue(u"VisualStyles", u"Size", preset.size);
    }
    const Desktop &d = preset.desktop;
    file.setValue(u"Control Panel\\Colors", u"Background",
                  QStringLiteral("%1 %2 %3").arg(d.colour.red()).arg(d.colour.green()).arg(d.colour.blue()));
    // Plasma FillMode -> WallpaperStyle (XP's centre and stretch, later
    // Windows' fit and fill) and TileWallpaper.
    static const QHash<int, QString> styles = {{0, QStringLiteral("2")}, {1, QStringLiteral("6")}, {2, QStringLiteral("10")},
                                               {3, QStringLiteral("0")}, {6, QStringLiteral("0")}};
    const QString desktop = QStringLiteral("Control Panel\\Desktop");
    file.setValue(desktop, u"Wallpaper", d.wallpaper);
    file.setValue(desktop, u"WallpaperStyle", styles.value(d.fill, QStringLiteral("10")));
    file.setValue(desktop, u"TileWallpaper", d.fill == 3 ? QStringLiteral("1") : QStringLiteral("0"));

    QDir().mkpath(savedDir());
    const QString target = savedDir() + QLatin1Char('/') + slug(preset.name) + QLatin1String(".theme");
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(QFileInfo::exists(target) ? QStringLiteral("%1 already exists").arg(target) : out.errorString());
    if (out.write(file.toBytes()) < 0 || !out.flush())
        return fail(out.errorString());
    if (path)
        *path = target;
    return true;
}

bool owned(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isSymLink() || fi.absolutePath() != QDir(savedDir()).absolutePath()
        || fi.suffix().compare(QLatin1String("theme"), Qt::CaseInsensitive) != 0)
        return false;
    const auto file = ThemeFile::load(path);
    return file && file->value(u"XPlasma", u"Saved") == QLatin1String("1");
}

} // namespace presets

} // namespace xpl
