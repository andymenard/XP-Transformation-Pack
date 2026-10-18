// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/paths.h"
#include "app/session.h"
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
namespace xpl::maintenance {
// Keep the retention policy self-contained in every native distribution.
// RecordingSession records this command without changing the real filesystem.
inline void run(Session &session, const QString &previousLook = {}, const QString &darkFolder = {}, bool throttled = false)
{
    static QElapsedTimer last;
    if(throttled && last.isValid() && last.elapsed()<60000)return;
    last.start();
    const QString python=QStandardPaths::findExecutable(QStringLiteral("python3"));
    QFile policy(QStringLiteral(":/maintenance/cache_maintenance.py"));
    if(python.isEmpty() || !policy.open(QIODevice::ReadOnly))return;
    QStringList args{python,QStringLiteral("-c"),QString::fromUtf8(policy.readAll()),QStringLiteral("--home"),paths::home(),QStringLiteral("--apply")};
    if(!previousLook.isEmpty())args<<QStringLiteral("--keep-look")<<previousLook;
    if(!darkFolder.isEmpty())args<<QStringLiteral("--keep-dark")<<darkFolder;
    session.run(args); // Cache cleanup cannot invalidate a successful theme apply.
}
}
