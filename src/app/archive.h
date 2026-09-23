#pragma once
// Reading archives, linked in rather than shelled out to.
//
// XP's install ISO is read here (ISO 9660, Joliet names when present):
// libarchive lists nothing from it, its root directory record being shorter
// than a sector. Everything else -- ZIP, RAR, 7z and TAR downloads, and XP's
// CAB-compressed I386 files (EXPLORER.EX_, THEMEUI.DL_...) -- goes through
// libarchive.
//
// A damaged or hostile archive is an error string, never a crash or a write
// outside the destination.
#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace xpl::archive {

struct Entry
{
    QString path;          // '/'-separated, relative
    qint64 size = 0;
    bool directory = false;
};

// Called as bytes are extracted; returning false cancels.
using Progress = std::function<bool(qint64 done, qint64 total)>;

std::optional<QList<Entry>> list(const QString &file, QString *error = nullptr);

// One member's bytes. `member` matches case-insensitively, with '/' or '\\'.
std::optional<QByteArray> read(const QString &file, const QString &member, QString *error = nullptr);

// In one pass, every member `want` accepts, with its bytes; `take`
// returning false stops (as a cancel).
bool forEach(const QString &file, const std::function<bool(const Entry &)> &want,
             const std::function<bool(const Entry &, const QByteArray &)> &take, QString *error = nullptr);

// Every member into `directory` (created if need be).
bool extract(const QString &file, const QString &directory, const Progress &progress = {}, QString *error = nullptr);

// A Microsoft cabinet held in memory -- an XP "*.EX_" / "*.DL_" -- expanded:
// the (first) file it holds.
std::optional<QByteArray> expandCabinet(const QByteArray &cab, QString *error = nullptr);
// ...and the name it had ("bliss.jpg" out of BLISS.JP_).
std::optional<std::pair<QString, QByteArray>> expandCabinetFile(const QByteArray &cab, QString *error = nullptr);

} // namespace xpl::archive
