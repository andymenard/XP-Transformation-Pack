#include "app/pointers.h"

#include "app/inf.h"
#include "app/media.h"
#include "app/package.h"
#include "app/paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>

#include <map>

namespace xpl::pointers {

namespace {

// user32.dll's cursor groups for Windows Default, by role.
constexpr int kDefaultGroups[RoleCount] = {100, 112, 111, 102, 103, 101, 113, 110, 108, 107, 105, 106, 109, 104, 114};

const PeResources *user32(QString *error)
{
    static std::optional<PeResources> pe;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (const auto file = media::systemFile(QStringLiteral("user32.dll"), error))
            pe = PeResources::parse(*file, error);
    }
    return pe ? &*pe : nullptr;
}

QString iconsDir()
{
    return paths::localShare() + QLatin1String("/icons");
}

bool write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

} // namespace

QString roleName(int role)
{
    static const char *const names[RoleCount] = {
        "Normal Select", "Help Select", "Working In Background", "Busy", "Precision Select", "Text Select",
        "Handwriting", "Unavailable", "Vertical Resize", "Horizontal Resize", "Diagonal Resize 1",
        "Diagonal Resize 2", "Move", "Alternate Select", "Link Select",
    };
    return role >= 0 && role < RoleCount ? QLatin1String(names[role]) : QString();
}

QStringList xcursorNames(int role)
{
    // The CSS names, X's core cursor names, and the hashes older toolkits
    // ask for -- and, so that nothing falls back to another theme, the
    // names of cursors XP didn't have, given the nearest XP one.
    static const std::map<int, QStringList> names = {
        {Arrow, {QStringLiteral("default"), QStringLiteral("left_ptr"), QStringLiteral("arrow"), QStringLiteral("top_left_arrow"),
                 QStringLiteral("left_arrow"), QStringLiteral("context-menu"), QStringLiteral("copy"), QStringLiteral("alias"),
                 QStringLiteral("dnd-copy"), QStringLiteral("dnd-link"), QStringLiteral("dnd-ask"), QStringLiteral("link"),
                 QStringLiteral("zoom-in"), QStringLiteral("zoom-out"), QStringLiteral("draft"),
                 QStringLiteral("1081e37283d90000800003c07f3ef6bf"), QStringLiteral("6407b0e94181790501fd1e167b474872"),
                 QStringLiteral("3085a0e285430894940527032f8b26df"), QStringLiteral("640fb0e74195791501fd1ed57b41487f")}},
        {Help, {QStringLiteral("help"), QStringLiteral("question_arrow"), QStringLiteral("whats_this"), QStringLiteral("left_ptr_help"),
                QStringLiteral("5c6cd98b3f3ebcb1f9c7f1c204630408"), QStringLiteral("d9ce0ab605698f320427677b458ad60b")}},
        {AppStarting, {QStringLiteral("progress"), QStringLiteral("left_ptr_watch"), QStringLiteral("half-busy"),
                       QStringLiteral("08e8e1c95fe2fc01f976f1e063a24ccd"), QStringLiteral("3ecb610c1bf2410f44200f48c40d3599")}},
        {Wait, {QStringLiteral("wait"), QStringLiteral("watch")}},
        {Crosshair, {QStringLiteral("crosshair"), QStringLiteral("cross"), QStringLiteral("tcross"), QStringLiteral("cross_reverse"),
                     QStringLiteral("diamond_cross"), QStringLiteral("cell"), QStringLiteral("plus")}},
        {IBeam, {QStringLiteral("text"), QStringLiteral("xterm"), QStringLiteral("ibeam"), QStringLiteral("vertical-text")}},
        {Pen, {QStringLiteral("pencil")}},
        {No, {QStringLiteral("not-allowed"), QStringLiteral("forbidden"), QStringLiteral("crossed_circle"), QStringLiteral("no-drop"),
              QStringLiteral("dnd-no-drop"), QStringLiteral("dnd-none"), QStringLiteral("circle"),
              QStringLiteral("03b6e0fcb3499374a867c041f52298f0")}},
        {SizeNS, {QStringLiteral("ns-resize"), QStringLiteral("size_ver"), QStringLiteral("n-resize"), QStringLiteral("s-resize"),
                  QStringLiteral("v_double_arrow"), QStringLiteral("sb_v_double_arrow"), QStringLiteral("top_side"),
                  QStringLiteral("bottom_side"), QStringLiteral("row-resize"), QStringLiteral("split_v"), QStringLiteral("size-ver"),
                  QStringLiteral("00008160000006810000408080010102"), QStringLiteral("2870a09082c103050810ffdffffe0204")}},
        {SizeWE, {QStringLiteral("ew-resize"), QStringLiteral("size_hor"), QStringLiteral("e-resize"), QStringLiteral("w-resize"),
                  QStringLiteral("h_double_arrow"), QStringLiteral("sb_h_double_arrow"), QStringLiteral("left_side"),
                  QStringLiteral("right_side"), QStringLiteral("col-resize"), QStringLiteral("split_h"), QStringLiteral("size-hor"),
                  QStringLiteral("028006030e0e7ebffc7f7070c0600140"), QStringLiteral("14fef782d02440884392942c11205230")}},
        {SizeNWSE, {QStringLiteral("nwse-resize"), QStringLiteral("size_fdiag"), QStringLiteral("nw-resize"), QStringLiteral("se-resize"),
                    QStringLiteral("top_left_corner"), QStringLiteral("bottom_right_corner"), QStringLiteral("size-fdiag"),
                    QStringLiteral("c7088f0f3e6c8088236ef8e1e3e70000"), QStringLiteral("38c5dff7c7b8962045400281044508d2")}},
        {SizeNESW, {QStringLiteral("nesw-resize"), QStringLiteral("size_bdiag"), QStringLiteral("ne-resize"), QStringLiteral("sw-resize"),
                    QStringLiteral("top_right_corner"), QStringLiteral("bottom_left_corner"), QStringLiteral("size-bdiag"),
                    QStringLiteral("fcf1c3c07f7ffe0f7ff8f8f8f8f80000"), QStringLiteral("50585d75b494802d0151028115016902")}},
        {SizeAll, {QStringLiteral("move"), QStringLiteral("size_all"), QStringLiteral("fleur"), QStringLiteral("all-scroll"),
                   QStringLiteral("grabbing"), QStringLiteral("closedhand"), QStringLiteral("dnd-move"), QStringLiteral("size-all"),
                   QStringLiteral("4498f0e0c1937ffe01fd06f973665830"), QStringLiteral("9081237383d90e509aa00f00170e968f"),
                   QStringLiteral("fcf21c00b30f7e3f83fe0dfd12e71cff")}},
        {UpArrow, {QStringLiteral("up-arrow"), QStringLiteral("center_ptr"), QStringLiteral("sb_up_arrow")}},
        {Hand, {QStringLiteral("pointer"), QStringLiteral("pointing_hand"), QStringLiteral("hand"), QStringLiteral("hand1"),
                QStringLiteral("hand2"), QStringLiteral("grab"), QStringLiteral("openhand"),
                QStringLiteral("e29285e634086352946a0e7090d73106"), QStringLiteral("9d800788f1b08800ae810202380a0822"),
                QStringLiteral("5aca4d189052212118709018842178c0"), QStringLiteral("9141b49c8149039304290b508d208c40")}},
    };
    const auto it = names.find(role);
    return it == names.end() ? QStringList() : it->second;
}

QJsonObject Scheme::toJson() const
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("files"), QJsonArray::fromStringList(files)}};
}

Scheme Scheme::fromJson(const QJsonObject &json)
{
    Scheme s;
    s.name = json.value(QLatin1String("name")).toString();
    for (const QJsonValue &v : json.value(QLatin1String("files")).toArray())
        s.files.append(v.toString());
    while (s.files.size() < RoleCount)
        s.files.append(QString());
    s.files = s.files.mid(0, RoleCount);
    return s;
}

Scheme defaultScheme()
{
    Scheme s;
    s.name = QLatin1String(kDefaultScheme);
    for (int i = 0; i < RoleCount; ++i)
        s.files.append(QString());
    return s;
}

QList<Scheme> schemes(QString *error)
{
    // HKLM,"...\Control Panel\Cursors\Schemes","<name>",,"<file>,<file>,..."
    // in XP's setup INFs, the files in XP's registry order (the roles', less
    // Link Select, which XP added without a scheme entry).
    QMap<QString, Scheme> found;
    found.insert(QLatin1String(kDefaultScheme), defaultScheme());
    for (const char *name : {"optional.inf", "accessor.inf"}) {
        const auto bytes = media::systemFile(QLatin1String(name), error);
        if (!bytes)
            continue;
        const Inf inf = Inf::parse(*bytes);
        for (const QString &section : inf.sectionNames())
            for (const QString &line : inf.lines(section)) {
                if (!line.startsWith(QLatin1String("HKLM"), Qt::CaseInsensitive))
                    continue;
                const QStringList f = inf.fields(line);
                if (f.size() < 5 || !f.at(1).endsWith(QLatin1String("Cursors\\Schemes"), Qt::CaseInsensitive))
                    continue;
                Scheme s;
                s.name = f.at(2).trimmed();
                QString value = f.mid(4).join(QLatin1Char(','));
                value.remove(QLatin1Char('"'));
                for (const QString &file : value.split(QLatin1Char(',')))
                    s.files.append(file.trimmed().section(QLatin1Char('\\'), -1).toLower());
                while (s.files.size() < RoleCount)
                    s.files.append(QString());
                s.files = s.files.mid(0, RoleCount);
                if (!s.name.isEmpty())
                    found.insert(s.name, s);
            }
    }
    if (found.size() == 1 && !user32(nullptr))
        return {};   // (no XP media at all)
    return found.values();
}

QList<CursorFrame> frames(const Scheme &scheme, int role, QString *error)
{
    if (role < 0 || role >= RoleCount)
        return {};
    const QString file = scheme.files.value(role);
    if (!file.isEmpty()) {
        std::optional<QByteArray> bytes;
        if (QDir::isAbsolutePath(file)) {
            QFile f(file);
            if (f.open(QIODevice::ReadOnly))
                bytes = f.readAll();
        } else {
            bytes = media::systemFile(file, error);
        }
        if (bytes) {
            const QList<CursorFrame> out = decodeCursor(*bytes);
            if (!out.isEmpty())
                return out;
        }
    }
    // Windows Default: user32.dll's.
    if (const PeResources *pe = user32(error))
        if (const auto f = cursorGroup(*pe, QString::number(kDefaultGroups[role])))
            return {*f};
    return {};
}

QList<CursorFrame> withShadow(const QList<CursorFrame> &frames)
{
    // A soft copy of the cursor's shape, a little down and to the right,
    // under it.
    QList<CursorFrame> out;
    for (const CursorFrame &f : frames) {
        const QImage src = f.image.convertToFormat(QImage::Format_ARGB32);
        const int w = src.width(), h = src.height();
        QImage shadow(w, h, QImage::Format_ARGB32);
        shadow.fill(Qt::transparent);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int sum = 0, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = x - 2 + dx, sy = y - 2 + dy;
                        if (sx >= 0 && sy >= 0 && sx < w && sy < h)
                            sum += qAlpha(src.pixel(sx, sy));
                        ++n;
                    }
                shadow.setPixel(x, y, qRgba(0, 0, 0, sum / n / 2));
            }
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.drawImage(0, 0, shadow);
        p.drawImage(0, 0, src);
        p.end();
        out.append({img.convertToFormat(QImage::Format_ARGB32), f.hotspot, f.delayMs});
    }
    return out;
}

bool writeTheme(const Scheme &scheme, bool shadow, const QString &dir, int *size, QString *error)
{
    QDir(dir).removeRecursively();
    int nominal = 32;
    for (int role = 0; role < RoleCount; ++role) {
        QList<CursorFrame> f = frames(scheme, role, error);
        if (f.isEmpty()) {
            if (error && error->isEmpty())
                *error = QStringLiteral("no cursor for %1").arg(roleName(role));
            return false;
        }
        if (shadow)
            f = withShadow(f);
        if (role == Arrow)
            nominal = f.first().image.width();
        const QStringList names = xcursorNames(role);
        const QString main = dir + QLatin1String("/cursors/") + names.first();
        if (!write(main, xcursor(f, nominal))) {
            if (error)
                *error = QStringLiteral("can't write %1").arg(main);
            return false;
        }
        for (const QString &alias : names.mid(1))
            QFile::link(names.first(), dir + QLatin1String("/cursors/") + alias);
    }
    const QString inherits = !paths::findSystemData(QStringLiteral("icons/breeze_cursors")).isEmpty() ? QStringLiteral("breeze_cursors")
                                                                                                  : QStringLiteral("default");
    const QByteArray index = "[Icon Theme]\nName=" + scheme.name.toUtf8() + " (XP)\nComment=Windows XP's pointers, written by XPLasma\nInherits="
                             + inherits.toUtf8() + "\n" + kGeneratedKey + "=true\n";
    if (!write(dir + QLatin1String("/index.theme"), index)) {
        if (error)
            *error = QStringLiteral("can't write %1/index.theme").arg(dir);
        return false;
    }
    if (size)
        *size = nominal;
    return true;
}

std::optional<std::pair<Scheme, bool>> applied(const State &state)
{
    const QJsonObject o = state.settings.value(QStringLiteral("pointers")).toObject();
    if (o.isEmpty())
        return std::nullopt;
    return std::pair(Scheme::fromJson(o.value(QLatin1String("scheme")).toObject()), o.value(QLatin1String("shadow")).toBool());
}

bool apply(Session &session, State &state, const Scheme &scheme, bool shadow, QString *error)
{
    // A theme id per scheme: Plasma's tool won't re-apply the name that's
    // set, and the compositor keeps cursors by it.
    const QByteArray key = QJsonDocument(scheme.toJson()).toJson(QJsonDocument::Compact) + (shadow ? "+shadow" : "");
    const QString id = QLatin1String(kThemeId) + QLatin1Char('-')
                       + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(8));
    int size = 32;
    if (!writeTheme(scheme, shadow, iconsDir() + QLatin1Char('/') + id, &size, error))
        return false;
    const QStringList mouse{QStringLiteral("Mouse")};
    if (!state.settings.contains(QStringLiteral("previousCursor"))) {
        state.settings.insert(QStringLiteral("previousCursor"),
                              QJsonObject{{QStringLiteral("theme"), session.readConfig(QStringLiteral("kcminputrc"), mouse, QStringLiteral("cursorTheme"))},
                                          {QStringLiteral("size"), session.readConfig(QStringLiteral("kcminputrc"), mouse, QStringLiteral("cursorSize"))}});
    }
    // (Plasma's tool takes --size but doesn't use it: the size goes in first.)
    session.writeConfig(QStringLiteral("kcminputrc"), mouse, QStringLiteral("cursorSize"), QString::number(size));
    const host::Result r = session.run({QStringLiteral("plasma-apply-cursortheme"), QStringLiteral("--size"), QString::number(size), id});
    if (!r.ok()) {
        if (error)
            *error = QStringLiteral("plasma-apply-cursortheme: %1").arg(QString::fromUtf8(r.err + r.out).trimmed());
        return false;
    }
    // Earlier schemes' themes go.
    for (const QFileInfo &d : QDir(iconsDir()).entryInfoList({QLatin1String(kThemeId) + QLatin1String("-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        if (d.fileName() != id)
            QDir(d.absoluteFilePath()).removeRecursively();
    state.settings.insert(QStringLiteral("pointers"), QJsonObject{{QStringLiteral("scheme"), scheme.toJson()}, {QStringLiteral("shadow"), shadow}});
    return true;
}

void revert(Session &session, State &state)
{
    if (!state.settings.contains(QStringLiteral("pointers")) && !state.settings.contains(QStringLiteral("previousCursor")))
        return;
    const QJsonObject previous = state.settings.value(QStringLiteral("previousCursor")).toObject();
    QString theme = previous.value(QLatin1String("theme")).toString();
    if (theme.isEmpty() || theme.startsWith(QLatin1String(kThemeId)))
        theme = QStringLiteral("breeze_cursors");
    const QStringList mouse{QStringLiteral("Mouse")};
    const QString size = previous.value(QLatin1String("size")).toString();
    if (size.toInt() > 0)
        session.writeConfig(QStringLiteral("kcminputrc"), mouse, QStringLiteral("cursorSize"), size);
    else
        session.deleteConfig(QStringLiteral("kcminputrc"), mouse, QStringLiteral("cursorSize"));
    session.run({QStringLiteral("plasma-apply-cursortheme"), theme});
    for (const QFileInfo &d : QDir(iconsDir()).entryInfoList({QLatin1String(kThemeId) + QLatin1String("-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(d.absoluteFilePath()).removeRecursively();
    state.settings.remove(QStringLiteral("pointers"));
    state.settings.remove(QStringLiteral("previousCursor"));
}

QStringList installSchemes(QString *error)
{
    QStringList names;
    const QList<Scheme> all = schemes(error);
    if (all.isEmpty())
        return names;
    const QString icons = paths::localShare() + QLatin1String("/icons/");
    for (const Scheme &scheme : all) {
        QString slug = scheme.name.toLower();
        slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        while (slug.endsWith(QLatin1Char('-')))
            slug.chop(1);
        int size = 0;
        const QString dir = icons + QLatin1String("xplasma-cursors-") + slug;
        QDir(dir).removeRecursively();
        if (!writeTheme(scheme, true, dir, &size, error))
            return {};
        names << scheme.name;
    }
    return names;
}

void removeSchemes()
{
    const QDir icons(paths::localShare() + QLatin1String("/icons"));
    for (const QString &dir : icons.entryList({QStringLiteral("xplasma-cursors-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(icons.filePath(dir)).removeRecursively();
}

} // namespace xpl::pointers
