// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/themeimport.h"
#include "app/archive.h"
#include "app/library.h"
#include "app/paths.h"
#include "core/themefile.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUuid>

namespace xpl::themeimport {
bool removable(const QString &theme)
{
    const QFileInfo file(theme);
    const QFileInfo library(paths::themes() + QStringLiteral("/Custom"));
    if (library.isSymLink()) return false;
    const QString root = library.canonicalFilePath();
    return !root.isEmpty() && file.isFile() && !file.isSymLink()
        && file.suffix().compare(QLatin1String("theme"), Qt::CaseInsensitive) == 0
        && file.canonicalFilePath().startsWith(root + QLatin1Char('/'));
}
bool remove(const QString &theme, QString *error)
{
    if (!removable(theme)) {
        if (error) *error = QStringLiteral("Only imported themes in XPlasma's custom library can be deleted.");
        return false;
    }
    if (!QFile::remove(theme)) {
        if (error) *error = QStringLiteral("Could not delete the imported theme file.");
        return false;
    }
    return true;
}

bool install(const QString &source, Result *result, QString *error,
             const std::function<void(const QString &)> &say, const std::atomic_bool *cancel)
{
    auto fail = [&](const QString &s) { if (error) *error = s; return false; };
    auto stopped = [&] { return cancel && cancel->load(); };
    const QFileInfo input(source);
    if (!input.exists()) return fail(QStringLiteral("The selected theme does not exist."));
    const QString root = paths::themes() + QStringLiteral("/Custom");
    if (!QDir().mkpath(root)) return fail(QStringLiteral("Cannot create the custom theme library."));
    QTemporaryDir stage(root + QStringLiteral("/.import-XXXXXX"));
    if (!stage.isValid()) return fail(QStringLiteral("Cannot create the import folder."));
    constexpr qint64 limit = 512LL * 1024 * 1024;
    qint64 total = 0;
    int files = 0;
    auto copy = [&](const QString &from, const QString &relative) {
        if (stopped()) return fail(QStringLiteral("Import cancelled."));
        total += QFileInfo(from).size();
        if (total > limit || ++files > 10000) return fail(QStringLiteral("The theme pack is too large (limit: 512 MB / 10,000 files)."));
        const QString target = QDir(stage.path()).filePath(relative);
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(from, target))
            return fail(QStringLiteral("Cannot copy %1.").arg(QFileInfo(from).fileName()));
        return true;
    };
    if (say) say(QStringLiteral("Importing %1 ...").arg(input.fileName()));
    const QString suffix = input.suffix().toLower();
    if (input.isDir()) {
        QDirIterator it(source, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            // Do not follow links or copy resources outside the chosen folder.
            if (!QFileInfo(file).canonicalFilePath().startsWith(input.canonicalFilePath() + QLatin1Char('/'))) continue;
            if (!copy(file, QDir(source).relativeFilePath(file))) return false;
        }
    } else if (suffix == QLatin1String("msstyles")) {
        if (!copy(source, input.fileName())) return false;
    } else if (suffix == QLatin1String("theme")) {
        auto theme = ThemeFile::load(source, error);
        if (!theme) return false;
        // A loose .theme often points to files beside it. Copy just those assets,
        // not the whole Downloads folder, and rewrite their references locally.
        for (const QString &section : theme->sections())
            for (const QString &key : theme->keys(section)) {
                const QString asset = resolveAsset(source, theme->value(section, key));
                if (asset.isEmpty() || !QFileInfo(asset).isFile()) continue;
                const QString name = QStringLiteral("Assets/%1-%2").arg(files).arg(QFileInfo(asset).fileName());
                if (!copy(asset, name)) return false;
                theme->setValue(section, key, name);
            }
        if (!theme->save(QDir(stage.path()).filePath(input.fileName()), error)) return false;
    } else {
        const auto entries = archive::list(source, error);
        if (!entries) return fail(QStringLiteral("Cannot read this theme pack. Choose a ZIP, 7z, RAR, CAB, .theme, .msstyles, or an extracted folder. Windows installers and WindowBlinds skins are not supported."));
        for (const auto &entry : *entries) {
            total += qMax(qint64(0), entry.size);
            if (total > limit || ++files > 10000) return fail(QStringLiteral("The theme pack is too large (limit: 512 MB / 10,000 files)."));
        }
        if (!archive::extract(source, stage.path(), [&](qint64, qint64) { return !stopped(); }, error)) return false;
    }
    if (stopped()) return fail(QStringLiteral("Import cancelled."));
    const Library lib = Library::scan({stage.path()});
    QList<StyleInfo> styles;
    QStringList usable, represented;
    for (const auto &style : lib.styles)
        if (style.path.startsWith(stage.path() + QLatin1Char('/')) && style.readable()) styles.append(style);
    for (const auto &bundle : lib.themes) {
        if (!bundle.path.startsWith(stage.path() + QLatin1Char('/')) || !bundle.styleMissing.isEmpty()) continue;
        const auto file = ThemeFile::load(bundle.path);
        if (!file) continue;
        if (bundle.classic() && !file->hasSection(u"Control Panel\\Colors")) continue;
        if (!bundle.classic() && !Library::style(bundle.style).readable()) continue;
        usable.append(bundle.path);
        represented.append(bundle.style);
    }
    for (const auto &style : styles) {
        if (represented.contains(style.path)) continue;
        ThemeFile preset;
        preset.setValue(u"Theme", u"DisplayName", style.label);
        const QDir folder(QFileInfo(style.path).absolutePath());
        preset.setValue(u"VisualStyles", u"Path", QFileInfo(style.path).fileName());
        QString path = folder.filePath(QFileInfo(style.path).completeBaseName() + QStringLiteral(".theme"));
        if (QFileInfo::exists(path)) path = folder.filePath(QStringLiteral("XPlasma-%1.theme").arg(QUuid::createUuid().toString(QUuid::Id128)));
        if (!preset.save(path, error)) return false;
        usable.append(path);
    }
    if (usable.isEmpty()) return fail(QStringLiteral("No usable Windows XP theme was found. Include an XP .msstyles file or a Windows Classic .theme with its resources. Vista/Windows 7 styles and WindowBlinds skins use different formats."));
    if (stopped()) return fail(QStringLiteral("Import cancelled."));
    QString label = input.isDir() ? input.fileName() : input.completeBaseName();
    label.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 _.-]")), QStringLiteral("_"));
    label = label.left(60);
    const QString target = root + QLatin1Char('/') + label + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::Id128).left(8);
    const QString selected = QDir(stage.path()).relativeFilePath(usable.first());
    if (!QDir().rename(stage.path(), target)) return fail(QStringLiteral("Cannot save the imported theme."));
    stage.setAutoRemove(false);
    if (result) *result = {target, QDir(target).filePath(selected), int(styles.size()), int(usable.size())};
    if (say) say(QStringLiteral("Installed %1 theme(s). Choose Apply to use the selected theme.").arg(usable.size()));
    return true;
}
}
