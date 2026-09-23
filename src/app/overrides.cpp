#include "app/overrides.h"

#include "app/package.h"

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>

namespace xpl::overrides {

namespace {

constexpr const char *kMarker = "X-XPLasma-Panel-Override";
constexpr const char *kLegacyMarker = "X-Xp2plasma-Panel-Override";   // xp2plasma's, and the Python app's
constexpr const char *kShadowMarker = "// xplasma: caption text shadow override";
constexpr const char *kLegacyShadowMarker = "// xp2plasma: caption text shadow override";
const QStringList kSystemRoots{QStringLiteral("/usr/local/share"), QStringLiteral("/usr/share")};

QString resource(const QString &name)
{
    QFile f(QStringLiteral(":/overrides/") + name);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString systemDir(const QString &relative)
{
    for (const QString &root : kSystemRoots)
        if (QFileInfo::exists(root + QLatin1Char('/') + relative))
            return root + QLatin1Char('/') + relative;
    return QString();
}

bool ownedWidget(const QString &dir)
{
    QFile f(dir + QLatin1String("/metadata.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject meta = QJsonDocument::fromJson(f.readAll()).object();
    return meta.value(QLatin1String(kMarker)).toBool() || meta.value(QLatin1String(kLegacyMarker)).toBool();
}

bool copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + file.mid(from.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(file, target))
            return false;
    }
    return true;
}

bool writeText(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(text.toUtf8()) >= 0 && f.commit();
}

} // namespace

std::optional<QString> patched(const QString &file, const QString &source, QString *error)
{
    static const QJsonObject rules = QJsonDocument::fromJson(resource(QStringLiteral("patches.json")).toUtf8()).object();
    if (!rules.contains(file)) {
        fail(error, QStringLiteral("no patches for %1").arg(file));
        return std::nullopt;
    }
    QString text = source;
    for (const QJsonValue &v : rules.value(file).toArray()) {
        const QJsonObject rule = v.toObject();
        const QString unless = rule.value(QStringLiteral("unlessContains")).toString();
        if (!unless.isEmpty() && text.contains(unless))
            continue;
        const QString find = rule.value(QStringLiteral("find")).toString();
        if (text.count(find) != 1) {
            fail(error, QStringLiteral("this Plasma's %1 isn't laid out as XPLasma expects (\"%2\"); not patching it")
                            .arg(file, find.left(60).trimmed()));
            return std::nullopt;
        }
        QString replace = rule.value(QStringLiteral("replace")).toString();
        // {{name}}: a snippet from overrides/name.qml.
        for (int at = int(replace.indexOf(QLatin1String("{{"))); at >= 0; at = int(replace.indexOf(QLatin1String("{{")))) {
            const int end = int(replace.indexOf(QLatin1String("}}"), at));
            if (end < 0)
                break;
            replace.replace(at, end + 2 - at, resource(replace.mid(at + 2, end - at - 2) + QLatin1String(".qml")));
        }
        text.replace(find, replace);
    }
    return text;
}

namespace {

struct Widget
{
    QString id, key;                 // plugin id, patches.json prefix
    QStringList files;               // what's patched, under contents/ui
    QHash<QString, QString> values;  // @KEY@ in the patched text -> value
};

const QList<Widget> kPanelWidgets{
    {QStringLiteral("org.kde.plasma.systemtray"), QStringLiteral("systemtray"),
     {QStringLiteral("main.qml"), QStringLiteral("items/AbstractItem.qml")}, {}},
    {QStringLiteral("org.kde.plasma.digitalclock"), QStringLiteral("digitalclock"),
     {QStringLiteral("main.qml"), QStringLiteral("DigitalClock.qml")}, {}},
    {QStringLiteral("org.kde.plasma.taskmanager"), QStringLiteral("taskmanager"),
     {QStringLiteral("main.qml"), QStringLiteral("Task.qml"), QStringLiteral("code/layoutmetrics.js")}, {}},
};
// (org.kde.plasma.folder, the desktop, is this package under another name.)
const QString kDesktopId = QStringLiteral("org.kde.desktopcontainment");

// Everything is checked before anything is replaced.
bool installWidgets(const QString &plasmoids, const QList<Widget> &widgets, QString *error)
{
    QList<QList<std::pair<QString, QString>>> prepared;
    for (const Widget &w : widgets) {
        const QString dest = plasmoids + QLatin1Char('/') + w.id;
        if (QFileInfo::exists(dest) && !ownedWidget(dest))
            return fail(error, QStringLiteral("%1 is someone else's override; not replacing it").arg(dest));
        const QString system = systemDir(QLatin1String("plasma/plasmoids/") + w.id);
        if (system.isEmpty())
            return fail(error, QStringLiteral("Plasma's %1 isn't installed").arg(w.id));
        QList<std::pair<QString, QString>> files;
        for (const QString &f : w.files) {
            auto text = patched(w.key + QLatin1Char('/') + f, readText(system + QLatin1String("/contents/ui/") + f), error);
            if (!text)
                return false;
            for (auto it = w.values.cbegin(); it != w.values.cend(); ++it)
                text->replace(QLatin1Char('@') + it.key() + QLatin1Char('@'), it.value());
            files.append({f, *text});
        }
        prepared.append(files);
    }
    for (int i = 0; i < widgets.size(); ++i) {
        const Widget &w = widgets.at(i);
        const QString system = systemDir(QLatin1String("plasma/plasmoids/") + w.id);
        const QString dest = plasmoids + QLatin1Char('/') + w.id;
        const QString staging = dest + QLatin1String(".xplasma-new");
        QDir(staging).removeRecursively();
        if (!copyTree(system + QLatin1String("/contents"), staging + QLatin1String("/contents")))
            return fail(error, QStringLiteral("can't copy %1").arg(system));
        QFile metaFile(system + QLatin1String("/metadata.json"));
        metaFile.open(QIODevice::ReadOnly);
        QJsonObject meta = QJsonDocument::fromJson(metaFile.readAll()).object();
        meta.insert(QLatin1String(kMarker), true);
        writeText(staging + QLatin1String("/metadata.json"), QString::fromUtf8(QJsonDocument(meta).toJson()));
        const QString ui = staging + QLatin1String("/contents/ui/");
        for (const auto &[f, text] : prepared.at(i))
            writeText(ui + f, text);
        if (w.key == QLatin1String("systemtray")) {
            // XP's show-hidden-icons button in place of KDE's arrow, which
            // stays as the fallback.
            QFile::rename(ui + QLatin1String("ExpanderArrow.qml"), ui + QLatin1String("PlasmaExpanderArrow.qml"));
            writeText(ui + QLatin1String("ExpanderArrow.qml"), resource(QStringLiteral("ExpanderArrow.qml")));
        }
        QDir(dest).removeRecursively();   // (ours: checked above)
        if (!QDir().rename(staging, dest))
            return fail(error, QStringLiteral("can't install %1").arg(dest));
    }
    return true;
}

void removeWidgets(const QString &plasmoids, const QStringList &ids)
{
    for (const QString &id : ids)
        for (const QString &suffix : {QString(), QStringLiteral(".xplasma-new"), QStringLiteral(".xp2plasma-new")}) {
            const QString dir = plasmoids + QLatin1Char('/') + id + suffix;
            if (ownedWidget(dir))
                QDir(dir).removeRecursively();
        }
}

// A shell command as a QML string literal: `editor` started as its own
// transient systemd unit, or (without systemd) directly.
QString launchLiteral(const QString &editor)
{
    QString quoted = editor;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    quoted = QLatin1Char('\'') + quoted + QLatin1Char('\'');
    const QString command = QStringLiteral("systemd-run --user --collect --quiet -- %1 || %1").arg(quoted);
    return QString::fromUtf8(QJsonDocument(QJsonArray{command}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

} // namespace

bool installPanelWidgets(const QString &plasmoids, QString *error)
{
    return installWidgets(plasmoids, kPanelWidgets, error);
}

void removePanelWidgets(const QString &plasmoids)
{
    QStringList ids;
    for (const Widget &w : kPanelWidgets)
        ids << w.id;
    removeWidgets(plasmoids, ids);
}

bool installDesktopMenu(const QString &plasmoids, const QString &editor, QString *error)
{
    return installWidgets(plasmoids,
                          {{kDesktopId, QStringLiteral("desktopcontainment"), {QStringLiteral("FolderViewLayer.qml")},
                            {{QStringLiteral("LAUNCH"), launchLiteral(editor)}}}},
                          error);
}

void removeDesktopMenu(const QString &plasmoids)
{
    removeWidgets(plasmoids, {kDesktopId});
}

bool installTitleShadows(const QString &themes, const QString &target, QString *error)
{
    const QString existing = readText(target);
    if (!existing.isEmpty() && !existing.startsWith(QLatin1String(kShadowMarker)) && !existing.startsWith(QLatin1String(kLegacyShadowMarker)))
        return fail(error, QStringLiteral("%1 is someone else's Aurorae override; not replacing it").arg(target));
    const QString system = systemDir(QStringLiteral("kwin/aurorae"));
    QString source = system.isEmpty() ? QString() : readText(system + QLatin1String("/aurorae.qml"));
    if (source.isEmpty())
        return fail(error, QStringLiteral("KWin's Aurorae renderer isn't installed"));

    // Which XPLasma themes want a shadow, from their rc files.
    QJsonObject shadows;
    for (const QFileInfo &pkg : QDir(themes).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString rcPath = pkg.filePath() + QLatin1Char('/') + pkg.fileName() + QLatin1String("rc");
        QSettings rc(rcPath, QSettings::IniFormat);
        if (!rc.contains(QLatin1String(kGeneratedKey) + QLatin1String("/Look")) && !readText(pkg.filePath() + QLatin1String("/metadata.desktop")).contains(QLatin1String("XPLasma")))
            continue;
        // (QSettings reads an INI file's [General] keys as top-level ones.)
        if (rc.value(QStringLiteral("UseTextShadow")).toString() != QLatin1String("true"))
            continue;
        auto colour = [&](const char *key) {
            const QStringList v = rc.value(QLatin1String(key)).toStringList().join(QLatin1Char(',')).split(QLatin1Char(','));
            if (v.size() < 3)
                return QStringLiteral("#00000000");
            return QColor(v.at(0).toInt(), v.at(1).toInt(), v.at(2).toInt(), v.value(3, QStringLiteral("255")).toInt()).name(QColor::HexArgb);
        };
        shadows.insert(pkg.fileName(), QJsonObject{{QStringLiteral("active"), colour("ActiveTextShadowColor")},
                                                   {QStringLiteral("inactive"), colour("InactiveTextShadowColor")},
                                                   {QStringLiteral("x"), rc.value(QStringLiteral("TextShadowOffsetX"), 1).toInt()},
                                                   {QStringLiteral("y"), rc.value(QStringLiteral("TextShadowOffsetY"), 1).toInt()}});
    }

    // KDE's renderer, importing its own components from where they are, with
    // a second, offset caption under the real one.
    const QString anchor = QStringLiteral("    Text {\n        id: caption");
    if (source.count(anchor) != 1 || source.count(QLatin1String("    id: root")) != 1)
        return fail(error, QStringLiteral("this KWin's aurorae.qml isn't laid out as XPLasma expects; no title shadows"));
    source.replace(source.indexOf(QLatin1String("import QtQuick")), int(qstrlen("import QtQuick")),
                   QStringLiteral("import \"%1\" as Upstream\nimport QtQuick").arg(QUrl::fromLocalFile(system).toString()));
    if (const int at = int(source.indexOf(QLatin1String("\nDecoration {"))); at >= 0)
        source.replace(at, int(qstrlen("\nDecoration {")), QStringLiteral("\nUpstream.Decoration {"));
    source.replace(QLatin1String("    AuroraeButtonGroup {"), QLatin1String("    Upstream.AuroraeButtonGroup {"));
    source.replace(source.indexOf(QLatin1String("    id: root")), int(qstrlen("    id: root")),
                   QStringLiteral("    id: root\n    property var xpTitleShadows: (")   // (bare braces would be a JS block)
                       + QString::fromUtf8(QJsonDocument(shadows).toJson(QJsonDocument::Compact)) + QLatin1Char(')'));
    source.replace(anchor, resource(QStringLiteral("title-shadow.qml")) + anchor);
    if (!writeText(target, QLatin1String(kShadowMarker) + QLatin1Char('\n') + source))
        return fail(error, QStringLiteral("can't write %1").arg(target));
    return true;
}

void removeTitleShadows(const QString &target)
{
    const QString text = readText(target);
    if (text.startsWith(QLatin1String(kShadowMarker)) || text.startsWith(QLatin1String(kLegacyShadowMarker)))
        QFile::remove(target);
}

} // namespace xpl::overrides
