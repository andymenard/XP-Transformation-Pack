#include "app/media.h"

#include "app/archive.h"
#include "app/paths.h"
#include "app/state.h"
#include "core/dib.h"
#include "core/pe.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace xpl::media {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// "explorer.exe" -> "EXPLORER.EX_": how XP's setup media compresses it.
QString compressedName(const QString &name)
{
    QString packed = name.toUpper();
    packed[packed.size() - 1] = QLatin1Char('_');
    return packed;
}

// A file under a folder, found case-insensitively, at most two levels down
// (media folders are the CD root, or I386 itself).
QString findIn(const QString &root, const QString &name)
{
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    const int depth = QDir(root).absolutePath().count(QLatin1Char('/'));
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.count(QLatin1Char('/')) - depth > 2)
            continue;
        if (QFileInfo(path).fileName().compare(name, Qt::CaseInsensitive) == 0)
            return path;
    }
    return QString();
}

// A member of the media, as it is stored.
std::optional<QByteArray> fromMedia(const QString &media, const QString &member, QString *error)
{
    if (QFileInfo(media).isDir()) {
        const QString path = findIn(media, member);
        QFile f(path);
        if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) {
            fail(error, QStringLiteral("%1 isn't in %2").arg(member, media));
            return std::nullopt;
        }
        return f.readAll();
    }
    return archive::read(media, QLatin1String("I386/") + member, error);
}

} // namespace

std::optional<QByteArray> systemFile(const QString &name, QString *error)
{
    const QString cached = paths::extracted() + QLatin1Char('/') + name.toLower();
    if (QFile f(cached); f.open(QIODevice::ReadOnly))
        return f.readAll();

    const QString media = State::load().media;
    if (media.isEmpty() || !QFileInfo::exists(media)) {
        fail(error, QStringLiteral("no Windows XP media (run setup, or point XPLasma at the XP ISO)"));
        return std::nullopt;
    }
    QString why;
    std::optional<QByteArray> bytes = fromMedia(media, name.toUpper(), &why);
    if (!bytes) {
        const auto packed = fromMedia(media, compressedName(name), error);
        if (!packed)
            return std::nullopt;
        bytes = archive::expandCabinet(*packed, error);
        if (!bytes)
            return std::nullopt;
    }
    QDir().mkpath(paths::extracted());
    QSaveFile out(cached);
    if (out.open(QIODevice::WriteOnly) && out.write(*bytes) == bytes->size())
        out.commit();   // (a cache: failing to keep it isn't failing)
    return bytes;
}

std::optional<QImage> bitmap(const QString &file, int id, QString *error)
{
    const auto bytes = systemFile(file, error);
    if (!bytes)
        return std::nullopt;
    const auto pe = PeResources::parse(*bytes, error);
    if (!pe)
        return std::nullopt;
    const Resource *r = pe->find(u"2", QString::number(id));   // RT_BITMAP
    if (!r) {
        fail(error, QStringLiteral("%1 has no bitmap %2").arg(file).arg(id));
        return std::nullopt;
    }
    const Dib dib = decodeDib(r->data);
    if (dib.image.isNull()) {
        fail(error, QStringLiteral("%1's bitmap %2 doesn't decode").arg(file).arg(id));
        return std::nullopt;
    }
    return dib.image;
}

std::optional<QImage> startFlag(QString *error)
{
    return bitmap(QStringLiteral("explorer.exe"), 143, error);
}

std::optional<QImage> classicStartFlag(QString *error)
{
    return bitmap(QStringLiteral("explorer.exe"), 176, error);
}

} // namespace xpl::media
