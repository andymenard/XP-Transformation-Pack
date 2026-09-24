#include "app/startmenu.h"

#include "app/media.h"
#include "app/package.h"
#include "app/startbutton.h"
#include "app/startpanel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>

namespace xpl {

namespace {

// A plasmoid package shipped in the resources (:/<name>/...), written out whole.
bool installPackage(const QString &resource, const QString &plasmoids, const char *id, QString *error)
{
    const QString target = QDir(plasmoids).filePath(QLatin1String(id));
    Package pkg(target);
    const QString root = QStringLiteral(":/%1/").arg(resource);
    QDirIterator it(root.chopped(1), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        pkg.write(file.mid(root.size()), f.readAll());
    }
    if (!pkg.ok() && error)
        *error = pkg.error();
    return pkg.ok();
}

// XP gave every new account one of its pictures (All Users\Application
// Data\Microsoft\User Account Pictures\Default Pictures), which the Start
// menu shows; this is the one XPLasma's shows for a user with no picture of
// their own. Which one: from the user's name, so it stays theirs.
const char *const kUserPictures[] = {"AIRPLANE", "ASTRO", "BEACH", "BUTTERFL", "CAR", "CAT", "CHESS", "DIRTBIKE",
                                     "DOG", "DRIP", "DUCK", "FISH", "FROG", "GUITAR", "HORSES", "KICK", "LIFTOFF",
                                     "PALMTREE", "PNKFLOWR", "REDFLOWR", "SKATER", "SNWFLAKE", "SOCCER"};

QString userPictureName(const QString &user)
{
    quint32 hash = 0;
    for (const QChar c : user)
        hash = hash * 31 + c.unicode();
    return QString::fromLatin1(kUserPictures[hash % std::size(kUserPictures)]);
}

// Written into `directory`; its URL, or empty if the media hasn't it.
QString writeUserPicture(const QString &directory)
{
    const QString user = qEnvironmentVariable("USER", QDir::home().dirName());
    const QString name = userPictureName(user);
    const auto bytes = media::systemFile(name + QLatin1String(".BMP"));
    const QImage image = bytes ? QImage::fromData(*bytes, "BMP") : QImage();
    const QString file = QDir(directory).filePath(QStringLiteral("user-%1.png").arg(name.toLower()));
    if (image.isNull() || !image.save(file))
        return QString();
    return QUrl::fromLocalFile(file).toString();
}

} // namespace

bool installStartPlasmoid(const QString &plasmoids, QString *error)
{
    return installPackage(QStringLiteral("start-menu"), plasmoids, kStartPlasmoidId, error);
}

bool installClockPlasmoid(const QString &plasmoids, QString *error)
{
    return installPackage(QStringLiteral("clock"), plasmoids, kClockPlasmoidId, error);
}

QJsonObject writeStartTheme(const Look &look, const QString &directory, QString *error)
{
    QJsonObject theme = writeStartPanel(look, directory, error);
    if (theme.isEmpty())
        return theme;
    const auto flag = look.classic() ? media::classicStartFlag(error) : media::startFlag(error);
    if (!flag)
        return QJsonObject();
    const auto button = startButton(look, *flag, look.classic() ? QStringLiteral("Start") : QStringLiteral("start"), error);
    if (!button)
        return QJsonObject();
    // (A new name per content, so no image cache ever shows the old one.)
    const QString stamp = QString::number(qHash(QByteArray(reinterpret_cast<const char *>(button->normal.constBits()),
                                                            int(button->normal.sizeInBytes()))),
                                          16);
    QJsonObject images{{QStringLiteral("width"), button->normal.width()}, {QStringLiteral("height"), button->normal.height()}};
    for (const auto &[state, image] : {std::pair("normal", &button->normal), std::pair("hover", &button->hover),
                                       std::pair("pressed", &button->pressed)}) {
        const QString file = QDir(directory).filePath(QStringLiteral("button-%1-%2.png").arg(stamp, QLatin1String(state)));
        if (!image->convertToFormat(QImage::Format_ARGB32).save(file)) {
            if (error)
                *error = QStringLiteral("can't write %1").arg(file);
            return QJsonObject();
        }
        images.insert(QLatin1String(state), QUrl::fromLocalFile(file).toString());
    }
    theme.insert(QStringLiteral("button"), images);
    theme.insert(QStringLiteral("look"), look.name);
    if (const QString picture = writeUserPicture(directory); !picture.isEmpty())
        theme.insert(QStringLiteral("userPicture"), picture);
    QSaveFile out(QDir(directory).filePath(QStringLiteral("theme.json")));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(theme).toJson()) < 0 || !out.commit()) {
        if (error)
            *error = out.errorString();
        return QJsonObject();
    }
    return theme;
}

} // namespace xpl
