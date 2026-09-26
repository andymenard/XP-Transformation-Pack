// SPDX-License-Identifier: GPL-3.0-or-later
// xplasma-schemes: XP's stock Windows Classic schemes, from the install ISO
// (or an HIVEDEF.INF), as the .theme files XPlasma ships.
//
//   xplasma-schemes XP.iso|HIVEDEF.INF OUTDIR
//
// Regenerates src/app/data/schemes; the app tests check it against the ISO.
#include "app/archive.h"
#include "app/hivedef.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QTextStream err(stderr);
    if (args.size() != 3) {
        err << "usage: xplasma-schemes XP.iso|HIVEDEF.INF OUTDIR\n";
        return 2;
    }
    QByteArray inf;
    QString error;
    if (args[1].endsWith(QLatin1String(".inf"), Qt::CaseInsensitive)) {
        QFile f(args[1]);
        if (!f.open(QIODevice::ReadOnly)) {
            err << args[1] << ": " << f.errorString() << '\n';
            return 1;
        }
        inf = f.readAll();
    } else if (auto bytes = xpl::archive::read(args[1], QStringLiteral("I386/HIVEDEF.INF"), &error)) {
        inf = *bytes;
    } else {
        err << error << '\n';
        return 1;
    }
    const auto schemes = xpl::hivedef::schemes(inf);
    if (schemes.isEmpty()) {
        err << "no schemes in " << args[1] << '\n';
        return 1;
    }
    QDir().mkpath(args[2]);
    for (const auto &s : schemes) {
        QFile out(QDir(args[2]).filePath(s.name + QLatin1String(".theme")));
        if (!out.open(QIODevice::WriteOnly) || out.write(xpl::hivedef::themeBytes(s)) < 0) {
            err << out.fileName() << ": " << out.errorString() << '\n';
            return 1;
        }
    }
    QTextStream(stdout) << schemes.size() << " schemes into " << args[2] << '\n';
    return 0;
}
