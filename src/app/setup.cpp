#include "app/setup.h"

#include "app/archive.h"
#include "app/forks.h"
#include "app/host.h"
#include "app/inf.h"
#include "app/paths.h"
#include "app/project.h"
#include "app/state.h"
#include "app/apply.h"
#include "app/session.h"
#include "core/themefile.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <unistd.h>

namespace xpl::setup {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool cancelled(const std::atomic_bool *cancel)
{
    return cancel && cancel->load();
}

QString fontsDir()
{
    return paths::xpFonts();
}

// What a job added, so it can be taken away again -- and only that.
class Added
{
public:
    explicit Added(const QStringList &roots)
    {
        for (const QString &root : roots) {
            m_roots << root;
            m_before.insert(root, walk(root));
            m_existed.insert(root, QFileInfo::exists(root));
        }
    }
    void undo()
    {
        for (const QString &root : std::as_const(m_roots)) {
            if (!m_existed.value(root)) {
                QDir(root).removeRecursively();
                continue;
            }
            QStringList added = QStringList(walk(root).subtract(m_before.value(root)).values());
            // Files, then directories deepest first.
            std::sort(added.begin(), added.end(), [](const QString &a, const QString &b) {
                return a.count(QLatin1Char('/')) > b.count(QLatin1Char('/'));
            });
            for (const QString &p : std::as_const(added))
                if (QFileInfo(p).isDir())
                    QDir().rmdir(p);
                else
                    QFile::remove(p);
        }
    }

private:
    static QSet<QString> walk(const QString &root)
    {
        QSet<QString> found;
        QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext())
            found.insert(it.next());
        return found;
    }
    QStringList m_roots;
    QHash<QString, QSet<QString>> m_before;
    QHash<QString, bool> m_existed;
};

bool write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

// XP's install media is plain ISO 9660 with 8.3 names; its I386 files are
// CAB-compressed with the last letter of the extension an underscore.
// What doImport learnt about wallpapers, for tidying up after it.
struct WallpaperImport
{
    QHash<QString, QString> names;   // wallpaperNames()
    QSet<QString> onMedia;           // every image on the media, as an older XPLasma named it
};

bool doImport(const QString &path, Imported &n, WallpaperImport &wp, const Progress &say, const std::atomic_bool *cancel,
              QString *error)
{
    // What's wanted, and from where.
    QString media = path, i386;   // an ISO, or the folder
    const bool iso = QFileInfo(path).isFile();
    QList<std::pair<QString, QByteArray>> members;   // (path in the media, bytes)
    auto wanted = [](const QString &member) {
        const QString name = member.section(QLatin1Char('/'), -1).toUpper();
        const bool inI386 = member.toUpper().startsWith(QLatin1String("I386/")) && member.count(QLatin1Char('/')) == 1;
        return (inI386 && (name.endsWith(QLatin1String(".TT_")) || name.endsWith(QLatin1String(".BM_"))
                           || name.endsWith(QLatin1String(".JP_")) || name == QLatin1String("LUNA.MS_")
                           || name == QLatin1String("SHL_IMG.IN_") || name == QLatin1String("ACCESSOR.IN_")))
               || name.endsWith(QLatin1String(".TH_")) || name.endsWith(QLatin1String(".THEME"));
    };
    say(QStringLiteral("Reading %1 ...").arg(QFileInfo(path).fileName()));
    if (iso) {
        if (!archive::forEach(
                path, [&](const archive::Entry &e) { return wanted(e.path); },
                [&](const archive::Entry &e, const QByteArray &bytes) {
                    members.append({e.path, bytes});
                    return !cancelled(cancel);
                },
                error))
            return false;
    } else {
        // The folder may be I386 itself or hold it.
        QDir dir(path);
        if (!QFileInfo::exists(dir.filePath(QStringLiteral("I386"))) && dir.dirName().compare(QLatin1String("I386"), Qt::CaseInsensitive) == 0)
            dir.cdUp();
        media = dir.absolutePath();
        QDirIterator it(media, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            QString member = dir.relativeFilePath(file);
            if (member.section(QLatin1Char('/'), 0, 0).compare(QLatin1String("I386"), Qt::CaseInsensitive) == 0)
                member = QLatin1String("I386/") + member.section(QLatin1Char('/'), 1);
            if (!wanted(member))
                continue;
            QFile f(file);
            if (f.open(QIODevice::ReadOnly))
                members.append({member, f.readAll()});
        }
    }
    if (std::none_of(members.begin(), members.end(), [](const auto &m) { return m.first.toUpper().endsWith(QLatin1String(".TT_")); }))
        return fail(error, QStringLiteral("%1 doesn't look like Windows XP's install media (no I386 fonts)").arg(path));

    // XP's own wallpaper list first, from the setup INFs.
    QList<QByteArray> infs;
    for (const auto &[member, packed] : std::as_const(members))
        if (member.toUpper().endsWith(QLatin1String(".IN_")))
            if (const auto file = archive::expandCabinetFile(packed))
                infs.append(file->second);
    wp.names = wallpaperNames(infs);

    for (const auto &[member, packed] : std::as_const(members)) {
        if (cancelled(cancel))
            return fail(error, QStringLiteral("cancelled"));
        const QString name = member.section(QLatin1Char('/'), -1).toUpper();
        if (name.endsWith(QLatin1String(".IN_")))
            continue;
        const bool compressed = name.endsWith(QLatin1Char('_'));
        QString realName = member.section(QLatin1Char('/'), -1);
        QByteArray bytes = packed;
        if (compressed) {
            const auto file = archive::expandCabinetFile(packed);
            if (!file)
                continue;   // (SZDD, or damaged: not ours)
            realName = file->first.section(QLatin1Char('/'), -1);
            bytes = file->second;
        }
        if (name.endsWith(QLatin1String(".TT_"))) {
            n.fonts += write(fontsDir() + QLatin1Char('/') + realName.toLower(), bytes);
        } else if (name == QLatin1String("LUNA.MS_")) {
            n.styles += write(paths::themes() + QLatin1String("/Luna/luna.msstyles"), bytes);
        } else if (name.endsWith(QLatin1String(".BM_")) || name.endsWith(QLatin1String(".JP_"))) {
            wp.onMedia.insert(realName.toLower());
            if (!wp.names.isEmpty()) {
                // XP's list, by XP's names (I386 keeps toolbar bitmaps and
                // setup's art beside them).
                if (const QString to = wp.names.value(realName.toLower()); !to.isEmpty())
                    n.wallpapers += write(paths::wallpapers() + QLatin1Char('/') + to, bytes);
            } else {
                // (Media without XP's INFs: size tells wallpapers apart.)
                QBuffer buffer(&bytes);
                const QSize size = QImageReader(&buffer).size();
                if (size.width() >= 640 && size.height() >= 480)
                    n.wallpapers += write(paths::wallpapers() + QLatin1Char('/') + realName.toLower(), bytes);
            }
        } else {
            // .TH_ is ambiguous (NOISE.TH_ is noise.tha): a theme only if it reads as one.
            const ThemeFile theme = ThemeFile::fromBytes(bytes);
            if (!theme.hasSection(u"Theme"))
                continue;
            const QString stem = QFileInfo(realName).completeBaseName();
            const QString folder = member.section(QLatin1Char('/'), 0, -2);
            const bool root = folder.isEmpty() || folder.compare(QLatin1String("I386"), Qt::CaseInsensitive) == 0;
            QString target;
            if (root && stem.compare(QLatin1String("luna"), Qt::CaseInsensitive) == 0)
                target = paths::themes() + QLatin1String("/Luna/Luna.theme");
            else if (root && stem.compare(QLatin1String("classic"), Qt::CaseInsensitive) == 0)
                target = paths::themes() + QLatin1String("/Windows Classic.theme");
            else
                target = paths::themes() + QLatin1String("/Media/") + folder + QLatin1Char('/') + stem + QLatin1String(".theme");
            if (write(target, bytes)) {
                ++n.themes;
                say(QStringLiteral("  preset: %1").arg(QFileInfo(target).fileName()));
            }
        }
    }
    State state = State::load();
    state.media = QFileInfo(media).absoluteFilePath();
    state.save();
    host::run({QStringLiteral("fc-cache"), QStringLiteral("-f"), fontsDir()});
    return true;
}

// An older XPLasma imported wallpapers by their 8.3 names, and some that
// aren't wallpapers: those go, or become XP's names, and the desktop and
// saved themes that name them follow.
void tidyWallpapers(const WallpaperImport &wp, const Progress &say)
{
    if (wp.names.isEmpty())
        return;
    const QDir dir(paths::wallpapers());
    QHash<QString, QString> moved;   // old path -> new path
    int removed = 0;
    for (const QString &f : dir.entryList(QDir::Files)) {
        const QString low = f.toLower();
        if (!wp.onMedia.contains(low) || f == wp.names.value(low))
            continue;   // (not the old import's, or already XP's name)
        const QString to = wp.names.value(low);
        if (!to.isEmpty() && !QFileInfo::exists(dir.filePath(to)))
            continue;   // (never drop one that has no replacement)
        if (!QFile::remove(dir.filePath(f)))
            continue;
        if (to.isEmpty())
            ++removed;
        else
            moved.insert(dir.filePath(f), dir.filePath(to));
    }
    if (moved.isEmpty() && removed == 0)
        return;
    say(QStringLiteral("Wallpapers: %1 renamed as XP named them, %2 that XP didn't list removed.").arg(moved.size()).arg(removed));
    // Saved themes name their files outright.
    QDirIterator it(paths::themes(), {QStringLiteral("*.theme")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString theme = it.next();
        QFile f(theme);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QByteArray text = f.readAll();
        f.close();
        const QByteArray before = text;
        for (auto m = moved.cbegin(); m != moved.cend(); ++m)
            text.replace(m.key().toUtf8(), m.value().toUtf8());
        if (text != before)
            write(theme, text);
    }
    // The desktop, when it shows one of them.
    QFile layout(paths::config() + QLatin1String("/plasma-org.kde.plasma.desktop-appletsrc"));
    const QByteArray desktop = layout.open(QIODevice::ReadOnly) ? layout.readAll() : QByteArray();
    QVariantMap renames;
    for (auto m = moved.cbegin(); m != moved.cend(); ++m)
        if (desktop.contains(QUrl::fromLocalFile(m.key()).toEncoded()) || desktop.contains(m.key().toUtf8()))
            renames.insert(QUrl::fromLocalFile(m.key()).toString(), QUrl::fromLocalFile(m.value()).toString());
    if (!renames.isEmpty() && plasmaSession()) {
        LiveSession session;
        session.script(scriptFrom(QStringLiteral("rename-wallpapers"), {{QStringLiteral("RENAMES"), renames}}));
    }
}

QString archiveBase()
{
    const QString env = qEnvironmentVariable("XPLASMA_ARCHIVE_URL");
    return env.isEmpty() ? QString::fromUtf8(project::kArchiveUrl) : env;
}

// A GET, waited for; `bytes` hears of progress, `cancel` stops it.
std::optional<QByteArray> get(const QUrl &url, const QString &saveTo, const Bytes &bytes, const std::atomic_bool *cancel,
                              QString *error)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("XPLasma (KDE theme converter)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    QFile out(saveTo + QLatin1String(".part"));
    if (!saveTo.isEmpty() && !out.open(QIODevice::WriteOnly)) {
        fail(error, out.errorString());
        return std::nullopt;
    }
    QByteArray body;
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        const QByteArray chunk = reply->readAll();
        if (saveTo.isEmpty())
            body += chunk;
        else
            out.write(chunk);
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 done, qint64 total) {
        if (bytes)
            bytes(done, qMax<qint64>(0, total));
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cancelled(cancel))
            reply->abort();
    });
    poll.start(200);
    loop.exec();
    const QByteArray rest = reply->readAll();
    if (saveTo.isEmpty())
        body += rest;
    else
        out.write(rest);
    const bool ok = reply->error() == QNetworkReply::NoError;
    const QString why = cancelled(cancel) ? QStringLiteral("cancelled") : reply->errorString();
    reply->deleteLater();
    if (!saveTo.isEmpty()) {
        out.close();
        if (!ok) {
            out.remove();
        } else {
            QFile::remove(saveTo);
            out.rename(saveTo);
        }
    }
    if (!ok) {
        fail(error, why);
        return std::nullopt;
    }
    return body;
}

} // namespace

bool plasmaSession(QString *detail)
{
    // The session says what it is -- or, started outside it (a terminal
    // over SSH, a timer), this user's plasmashell is running. Its version
    // from libPlasma's file name, not by starting plasmashell (which, in
    // this program's offscreen platform, Plasma 6.6's crashes).
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").trimmed();
    auto say = [&](const QString &what) {
        if (detail)
            *detail = what;
    };
    if (!desktop.isEmpty() && !desktop.contains(QLatin1String("KDE"), Qt::CaseInsensitive)) {
        say(desktop);
        return false;
    }
    if (desktop.isEmpty()
        && !host::run({QStringLiteral("pgrep"), QStringLiteral("-u"), QString::number(getuid()), QStringLiteral("-x"), QStringLiteral("plasmashell")}).ok()) {
        say(QStringLiteral("no desktop session found"));
        return false;
    }
    LiveSession session;
    const QString plasma = forks::runningPlasma(session);
    if (plasma.isEmpty()) {
        say(desktop.isEmpty() ? QStringLiteral("Plasma, of an unknown version") : desktop);
        return true;   // (a KDE session whose version couldn't be read is still one)
    }
    say(QStringLiteral("Plasma %1").arg(plasma));
    return plasma.section(QLatin1Char('.'), 0, 0).toInt() >= 6;
}

bool checkMedia(const QString &iso, QString *why)
{
    if (!QFileInfo(iso).isFile())
        return fail(why, QStringLiteral("There's no file %1.").arg(iso));
    QString error;
    const auto entries = archive::list(iso, &error);
    if (!entries)
        return fail(why, QStringLiteral("%1 isn't a disc image Setup can read (%2).").arg(QFileInfo(iso).fileName(), error));
    static const QRegularExpression tag(QStringLiteral("^WIN51[A-Z]{1,2}\\.SP(\\d)$"), QRegularExpression::CaseInsensitiveOption);
    bool i386 = false;
    int pack = 0;
    for (const archive::Entry &e : *entries) {
        if (e.path.startsWith(QLatin1String("I386/"), Qt::CaseInsensitive))
            i386 = true;
        if (const auto m = tag.match(e.path); m.hasMatch())
            pack = qMax(pack, m.captured(1).toInt());
    }
    if (!i386)
        return fail(why, QStringLiteral("%1 isn't a Windows XP installation CD (it has no I386 folder).").arg(QFileInfo(iso).fileName()));
    if (pack < 3)
        return fail(why, pack ? QStringLiteral("%1 is Windows XP with Service Pack %2; XPLasma needs one that includes Service Pack 3.")
                                    .arg(QFileInfo(iso).fileName())
                                    .arg(pack)
                              : QStringLiteral("%1 is a Windows XP CD without a service pack; XPLasma needs one that includes Service Pack 3.")
                                    .arg(QFileInfo(iso).fileName()));
    return true;
}

QList<Check> preflight()
{
    const struct { const char *tool, *purpose; bool apply; const char *alternative; } tools[] = {
        {"kwriteconfig6", "write KDE settings", true, nullptr},
        {"kreadconfig6", "read KDE settings", true, nullptr},
        {"plasma-apply-colorscheme", "set the colour scheme", true, nullptr},
        {"plasma-apply-desktoptheme", "set the taskbar's theme", true, nullptr},
        {"plasma-apply-lookandfeel", "reset to Plasma's look", false, nullptr},
        {"systemctl", "restart Plasma's shell", true, "kquitapp6"},   // (without systemd: quit over D-Bus, started again)
        {"fc-cache", "make the XP fonts visible", false, nullptr},
    };
    QList<Check> out;
    for (const auto &t : tools) {
        QString found = host::which(QLatin1String(t.tool));
        if (found.isEmpty() && t.alternative)
            found = host::which(QLatin1String(t.alternative));
        out.append({QLatin1String(t.tool), QLatin1String(t.purpose), found, t.apply});
    }
    return out;
}

QHash<QString, QString> wallpaperNames(const QList<QByteArray> &infs)
{
    // The copy lists XP's setup puts wallpapers with: Web\Wallpaper's, and
    // the Accessories' "Desktop Wallpaper" component (into %windir%).
    static const QStringList kLists{QStringLiteral("wallpaper.copyfiles"), QStringLiteral("deskpapercopyfilessys")};
    QHash<QString, QString> out;
    for (const QByteArray &bytes : infs) {
        const Inf inf = Inf::parse(bytes);
        for (const QString &list : kLists) {
            for (const QString &line : inf.lines(list)) {
                const QStringList f = inf.fields(line);
                const QString dest = f.value(0), source = f.size() > 1 ? f.at(1) : dest;
                if (source.isEmpty() || dest.isEmpty())
                    continue;
                QString name = dest;
                // default.jpg is what XP calls its default wallpaper (Bliss)
                // on disk; its own name is in the strings.
                if (QFileInfo(dest).completeBaseName().compare(QLatin1String("default"), Qt::CaseInsensitive) == 0)
                    for (const QString &s : inf.strings())
                        if (s.compare(source, Qt::CaseInsensitive) == 0)
                            name = s;
                out.insert(source.toLower(), name);
            }
        }
    }
    return out;
}

bool importMedia(const QString &path, Imported *counts, const Progress &progress, const std::atomic_bool *cancel, QString *error)
{
    const Progress say = progress ? progress : [](const QString &) {};
    Added added({fontsDir(), paths::themes(), paths::wallpapers()});
    const QString previousMedia = State::load().media;
    Imported n;
    WallpaperImport wp;
    if (!doImport(path, n, wp, say, cancel, error) || n.fonts == 0 || n.styles == 0 || n.wallpapers == 0) {
        if (error && error->isEmpty())
            *error = QStringLiteral("the import is incomplete (%1 fonts, %2 styles, %3 wallpapers)")
                         .arg(n.fonts).arg(n.styles).arg(n.wallpapers);
        say(QStringLiteral("Rolling back ..."));
        added.undo();
        State state = State::load();
        state.media = previousMedia;
        state.save();
        return false;
    }
    say(QStringLiteral("XP media: %1 fonts, %2 style, %3 presets, %4 wallpapers.")
            .arg(n.fonts).arg(n.styles).arg(n.themes).arg(n.wallpapers));
    tidyWallpapers(wp, say);
    if (counts)
        *counts = n;
    return true;
}

QList<Download> downloads()
{
    return {
        {QStringLiteral("WinXPProSP3x86"), QStringLiteral("*.iso"), QStringLiteral("Windows XP Professional SP3 install media (589 MB)"),
         false, true},
        {QStringLiteral("windows-xp-official-themes"), QStringLiteral("XP_Themes.zip"),
         QStringLiteral("the official theme packs (Royale, Royale Noir, Zune, Embedded)")},
        {QStringLiteral("windows-xp-official-themes"), QStringLiteral("Winter Fun Packs 2003.zip"), QStringLiteral("the official wallpaper packs"),
         true},
    };
}

bool fetch(const Download &d, const Progress &progress, const Bytes &bytes, const std::atomic_bool *cancel, QString *error)
{
    const Progress say = progress ? progress : [](const QString &) {};
    // A copy in the cache means no network at all, not even to look it up.
    const QString wanted = d.file.toLower();
    QString local;
    for (const QFileInfo &f : QDir(paths::downloads()).entryInfoList(QDir::Files, QDir::Name)) {
        const QString name = f.fileName().toLower();
        if (!name.endsWith(QLatin1String(".part"))
            && ((wanted.startsWith(QLatin1String("*.")) && name.endsWith(wanted.mid(1))) || name == wanted))
            local = f.absoluteFilePath();
    }
    if (!local.isEmpty()) {
        say(QStringLiteral("Using the downloaded %1 (%2) ...").arg(d.label, QFileInfo(local).fileName()));
    } else {
        say(QStringLiteral("Fetching %1 ...").arg(d.label));
        const auto meta = get(QUrl(archiveBase() + QLatin1String("/metadata/") + d.item), QString(), {}, cancel, error);
        if (!meta)
            return fail(error, QStringLiteral("couldn't reach archive.org for %1: %2").arg(d.label, error ? *error : QString()));
        QString name;
        qint64 largest = -1;
        for (const QJsonValue &v : QJsonDocument::fromJson(*meta).object().value(QStringLiteral("files")).toArray()) {
            const QString candidate = v.toObject().value(QStringLiteral("name")).toString();
            const qint64 size = v.toObject().value(QStringLiteral("size")).toVariant().toLongLong();
            if (wanted.startsWith(QLatin1String("*.")) ? candidate.toLower().endsWith(wanted.mid(1)) && size > largest
                                                        : candidate.toLower() == wanted) {
                name = candidate;
                largest = size;
            }
        }
        if (name.isEmpty())
            return fail(error, QStringLiteral("archive.org's %1 has no %2 any more").arg(d.item, d.file));
        QDir().mkpath(paths::downloads());
        local = paths::downloads() + QLatin1Char('/') + QFileInfo(name).fileName();
        const QUrl url(archiveBase() + QLatin1String("/download/") + d.item + QLatin1Char('/') + QString::fromUtf8(QUrl::toPercentEncoding(name)));
        if (!get(url, local, bytes, cancel, error))
            return false;
    }
    if (cancelled(cancel))
        return fail(error, QStringLiteral("cancelled"));
    if (d.media)
        return importMedia(local, nullptr, say, cancel, error);
    say(QStringLiteral("Unpacking into %1 ...").arg(paths::themes()));
    Added added({paths::themes()});
    if (!archive::extract(local, paths::themes(), [&](qint64, qint64) { return !cancelled(cancel); }, error)) {
        added.undo();
        return false;
    }
    return true;
}

} // namespace xpl::setup
