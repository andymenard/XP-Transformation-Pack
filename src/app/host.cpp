// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/host.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>

namespace xpl::host {

QString which(const QString &program)
{
    if (program.contains(QLatin1Char('/')))
        return QFileInfo::exists(program) ? program : QString();
    return QStandardPaths::findExecutable(program);
}

QString installHint(const QString &program)
{
    static const QHash<QString, QString> hints = {
        {QStringLiteral("kwriteconfig6"), QStringLiteral("KDE Frameworks 6 (kconfig)")},
        {QStringLiteral("kreadconfig6"), QStringLiteral("KDE Frameworks 6 (kconfig)")},
        {QStringLiteral("plasma-apply-colorscheme"), QStringLiteral("Plasma 6 (plasma-workspace)")},
        {QStringLiteral("plasma-apply-desktoptheme"), QStringLiteral("Plasma 6 (plasma-workspace)")},
        {QStringLiteral("plasma-apply-wallpaperimage"), QStringLiteral("Plasma 6 (plasma-workspace)")},
        {QStringLiteral("plasma-apply-lookandfeel"), QStringLiteral("Plasma 6 (plasma-workspace)")},
        {QStringLiteral("systemctl"), QStringLiteral("systemd")},
        {QStringLiteral("fc-cache"), QStringLiteral("fontconfig")},
    };
    return hints.value(program);
}

namespace {
// Splits a stream into lines as it arrives; '\r' (progress meters) ends a
// line as '\n' does.
struct Lines
{
    QByteArray pending;
    void feed(const QByteArray &bytes, const std::function<void(const QString &)> &sink)
    {
        pending += bytes;
        pending.replace('\r', '\n');
        qsizetype at;
        while ((at = pending.indexOf('\n')) >= 0) {
            const QByteArray line = pending.left(at);
            pending.remove(0, at + 1);
            if (sink && !line.isEmpty())
                sink(QString::fromUtf8(line));
        }
    }
    void flush(const std::function<void(const QString &)> &sink)
    {
        if (sink && !pending.isEmpty())
            sink(QString::fromUtf8(pending));
        pending.clear();
    }
};
} // namespace

Result run(const QStringList &argv, const Options &options)
{
    Result result;
    if (argv.isEmpty())
        return result;
    const QString program = which(argv.first());
    if (program.isEmpty()) {
        result.exitCode = 127;
        const QString hint = installHint(argv.first());
        result.err = (argv.first() + QLatin1String(": not found")
                      + (hint.isEmpty() ? QString() : QLatin1String(" (install ") + hint + QLatin1Char(')')))
                         .toUtf8();
        return result;
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments(argv.mid(1));
    if (!options.workingDirectory.isEmpty())
        process.setWorkingDirectory(options.workingDirectory);
    process.start();
    if (!process.waitForStarted()) {
        result.exitCode = 127;
        result.err = process.errorString().toUtf8();
        return result;
    }
    if (!options.input.isEmpty())
        process.write(options.input);
    process.closeWriteChannel();

    Lines out, err;
    auto drain = [&] {
        const QByteArray o = process.readAllStandardOutput(), e = process.readAllStandardError();
        result.out += o;
        result.err += e;
        out.feed(o, options.onLine);
        err.feed(e, options.onLine);
    };
    QElapsedTimer clock;
    clock.start();
    auto stop = [&] {
        process.terminate();
        if (!process.waitForFinished(3000)) {
            process.kill();
            process.waitForFinished(1000);
        }
    };
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200);
        drain();
        const bool timedOut = options.timeoutMs >= 0 && clock.elapsed() > options.timeoutMs;
        if ((options.cancel && options.cancel->load()) || timedOut) {
            result.cancelled = true;
            stop();
            break;
        }
    }
    drain();
    out.flush(options.onLine);
    err.flush(options.onLine);
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    return result;
}

} // namespace xpl::host
