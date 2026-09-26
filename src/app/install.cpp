// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/install.h"

#include "app/paths.h"
#include "app/state.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstdio>

namespace xpl::installer {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// `from` to `to` under a fresh name, then renamed into place.
bool copyFile(const QString &from, const QString &to, QString *error)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    const QString fresh = QFileInfo(to).absolutePath() + QLatin1String("/.") + QFileInfo(to).fileName() + QLatin1String(".new");
    QFile::remove(fresh);
    if (!QFile::copy(from, fresh))
        return fail(error, QStringLiteral("can't copy %1 to %2").arg(from, to));
    QFile::setPermissions(fresh, QFile(from).permissions());
    if (std::rename(QFile::encodeName(fresh).constData(), QFile::encodeName(to).constData()) != 0) {
        QFile::remove(fresh);
        return fail(error, QStringLiteral("can't put %1 in place").arg(to));
    }
    return true;
}

// A folder's tree copied beside `to`, then swapped in whole.
bool copyTree(const QString &from, const QString &to, QString *error)
{
    const QString fresh = to + QLatin1String(".new");
    QDir(fresh).removeRecursively();
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = fresh + file.mid(from.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(file, target)) {
            QDir(fresh).removeRecursively();
            return fail(error, QStringLiteral("can't copy %1").arg(file));
        }
        QFile::setPermissions(target, QFile(file).permissions());
    }
    QDir(to).removeRecursively();
    if (!QDir().rename(fresh, to))
        return fail(error, QStringLiteral("can't put %1 in place").arg(to));
    return true;
}

// (The Setup wizard is a program of its own, not installed: run again, it
// repairs or removes, as a Windows Installer package does.)
const QStringList kPrograms{QStringLiteral("xplasma"), QStringLiteral("xplasma-edit")};
// What runs at login (~/.config/autostart): the update check.
const char *const kAutostart[] = {"fyi.hotsocket.xplasma.updates.desktop"};
// Desktop entries earlier XPlasmas installed, gone since: removed on install.
const char *const kRetiredEntries[] = {"fyi.hotsocket.xplasma.sounds.desktop"};   // (now XPlasma Properties' Sounds tab)

QString autostartDir()
{
    return paths::config() + QLatin1String("/autostart");
}

} // namespace

QString packageRoot()
{
    return QDir(QCoreApplication::applicationDirPath() + QLatin1String("/..")).absolutePath();
}

bool isPackage(const QString &root)
{
    return QFileInfo(root + QLatin1String("/bin/xplasma")).isExecutable();
}

QString installedPrefix()
{
    const QString recorded = State::load().settings.value(QStringLiteral("installPrefix")).toString();
    if (!recorded.isEmpty() && QFileInfo(recorded + QLatin1String("/bin/xplasma")).exists())
        return recorded;
    const QString local = paths::home() + QLatin1String("/.local");
    return QFileInfo(local + QLatin1String("/bin/xplasma")).exists() ? local : QString();
}

int installedVersion(const QString &prefix)
{
    QProcess run;
    run.start(prefix + QLatin1String("/bin/xplasma"), {QStringLiteral("--version")});
    if (!run.waitForFinished(10000)) {
        run.kill();
        run.waitForFinished(1000);
        return 0;
    }
    // ("xplasma 2"; before release numbers, "xplasma 0.1.0")
    const QStringList words = QString::fromUtf8(run.readAllStandardOutput()).simplified().split(QLatin1Char(' '));
    bool whole = false;
    const int version = words.last().toInt(&whole);
    return whole ? version : 0;
}

QStringList installedPaths()
{
    QStringList out;
    for (const QString &p : kPrograms)
        out << QLatin1String("bin/") + p;
    out << QStringLiteral("lib/xplasma") << QStringLiteral("share/applications/fyi.hotsocket.xplasma.desktop")
        << QStringLiteral("share/applications/fyi.hotsocket.xplasma.datetime.desktop") << QStringLiteral("share/licenses/xplasma");
    for (const char *name : kRetiredEntries)
        out << QLatin1String("share/applications/") + QLatin1String(name);
    return out;
}

bool installFiles(const QString &package, const QString &prefix, const Progress &progress, QString *error)
{
    auto say = [&](const QString &line) {
        if (progress)
            progress(line);
    };
    if (!isPackage(package))
        return fail(error, QStringLiteral("%1 isn't an XPlasma package (it has no bin/xplasma)").arg(package));
    say(QStringLiteral("Copying new files"));
    for (const QString &p : kPrograms) {
        const QString from = package + QLatin1String("/bin/") + p;
        if (QFileInfo::exists(from) && !copyFile(from, prefix + QLatin1String("/bin/") + p, error))
            return false;
    }
    for (const int major : {6, 5}) {
        const QString rel = QStringLiteral("/lib/xplasma/qt%1/styles/libxplasma.so").arg(major);
        if (QFileInfo::exists(package + rel) && !copyFile(package + rel, prefix + rel, error))
            return false;
    }
    say(QStringLiteral("Copying XPlasma's builds of Plasma's widgets"));
    if (QFileInfo::exists(package + QLatin1String("/lib/xplasma/forks"))
        && !copyTree(package + QLatin1String("/lib/xplasma/forks"), prefix + QLatin1String("/lib/xplasma/forks"), error))
        return false;
    // Desktop entries from `from` into `to`, their Exec the installed programs.
    auto entries = [&](const QString &from, const QString &to) {
        QDir().mkpath(to);
        for (const QFileInfo &desktop : QDir(from).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            QFile in(desktop.absoluteFilePath());
            if (!in.open(QIODevice::ReadOnly))
                continue;
            QByteArray text = in.readAll();
            for (const QString &p : kPrograms)
                for (const char end : {' ', '\n'}) {
                    const QByteArray from = QByteArray("\nExec=") + p.toUtf8() + end;
                    const QByteArray to = QByteArray("\nExec=") + (prefix + QLatin1String("/bin/") + p).toUtf8() + end;
                    text.replace(from, to);
                }
            QSaveFile out(to + QLatin1Char('/') + desktop.fileName());
            if (!out.open(QIODevice::WriteOnly) || out.write(text) < 0 || !out.commit())
                return fail(error, QStringLiteral("%1: %2").arg(out.fileName(), out.errorString()));
        }
        return true;
    };
    // (the licence and the third-party notices)
    const QString licenses = QStringLiteral("/share/licenses/xplasma");
    if (QFileInfo::exists(package + licenses) && !copyTree(package + licenses, prefix + licenses, error))
        return false;
    say(QStringLiteral("Adding XPlasma to the application menu"));
    const QString apps = prefix + QLatin1String("/share/applications");
    if (!entries(package + QLatin1String("/share/applications"), apps))
        return false;
    for (const char *name : kRetiredEntries)
        QFile::remove(apps + QLatin1Char('/') + QLatin1String(name));
    // (and what runs at login: the update check)
    if (!entries(package + QLatin1String("/share/xplasma/autostart"), autostartDir()))
        return false;
    if (const QString tool = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database")); !tool.isEmpty())
        QProcess::execute(tool, {QStringLiteral("-q"), apps});
    State state = State::load();
    state.settings.insert(QStringLiteral("installPrefix"), prefix);
    state.save();
    return true;
}

void removeFiles(const QString &prefix, bool extracted, bool downloads, const Progress &progress)
{
    auto say = [&](const QString &line) {
        if (progress)
            progress(line);
    };
    say(QStringLiteral("Removing XPlasma's files"));
    for (const char *name : kAutostart)
        QFile::remove(autostartDir() + QLatin1Char('/') + QLatin1String(name));
    for (const QString &rel : installedPaths()) {
        const QString path = prefix + QLatin1Char('/') + rel;
        if (QFileInfo(path).isDir())
            QDir(path).removeRecursively();
        else
            QFile::remove(path);
        paths::removeEmptyDirs(QFileInfo(path).absolutePath(), prefix);
    }
    if (const QString tool = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database")); !tool.isEmpty())
        QProcess::execute(tool, {QStringLiteral("-q"), prefix + QLatin1String("/share/applications")});
    if (downloads) {
        say(QStringLiteral("Removing the downloaded Windows XP CD image"));
        QDir(paths::downloads()).removeRecursively();
    }
    if (extracted) {
        say(QStringLiteral("Removing the Windows XP files XPlasma took out"));
        QDir(paths::data()).removeRecursively();
        QDir(paths::xpFonts()).removeRecursively();
        // (Its cache, less the downloads when they stay.)
        for (const QFileInfo &f : QDir(paths::cache()).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (f.absoluteFilePath() == QFileInfo(paths::downloads()).absoluteFilePath())
                continue;
            if (f.isDir())
                QDir(f.absoluteFilePath()).removeRecursively();
            else
                QFile::remove(f.absoluteFilePath());
        }
        QFile::remove(paths::config() + QLatin1String("/xplasmarc"));
        QDir().rmdir(paths::cache());   // (if the downloads went too)
        return;
    }
    State state = State::load();
    state.settings.remove(QStringLiteral("installPrefix"));
    state.save();
}

qint64 downloadsSize()
{
    qint64 total = 0;
    QDirIterator it(paths::downloads(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

} // namespace xpl::installer
