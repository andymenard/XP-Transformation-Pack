// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The one way the app runs other programs (kwriteconfig6, the plasma-apply-*
// tools, systemctl...): resolved on PATH, output streamed line by line as it
// comes, cancellable, and a missing program an ordinary result (exit 127 with
// a hint on what to install), never an exception.
#include <QByteArray>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace xpl::host {

struct Result
{
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
    bool cancelled = false;
    bool ok() const { return exitCode == 0 && !cancelled; }
};

struct Options
{
    QByteArray input;                                  // written to stdin, then closed
    std::function<void(const QString &line)> onLine;   // each stdout/stderr line ('\r' ends one too)
    const std::atomic_bool *cancel = nullptr;           // set to stop: terminate, then kill after 3 s
    int timeoutMs = -1;                                // likewise, when it runs this long
    QString workingDirectory;
};

Result run(const QStringList &argv, const Options &options = {});

// The program's absolute path on PATH, or empty.
QString which(const QString &program);

// What to install for a missing program, or empty.
QString installHint(const QString &program);

} // namespace xpl::host
