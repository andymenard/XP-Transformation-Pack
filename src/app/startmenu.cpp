#include "app/startmenu.h"

#include "app/media.h"
#include "app/package.h"
#include "app/startbutton.h"
#include "app/startpanel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>

namespace xpl {

bool installStartPlasmoid(const QString &plasmoids, QString *error)
{
    const QString target = QDir(plasmoids).filePath(QLatin1String(kStartPlasmoidId));
    Package pkg(target);
    QDirIterator it(QStringLiteral(":/start-menu"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        pkg.write(file.mid(QStringLiteral(":/start-menu/").size()), f.readAll());
    }
    if (!pkg.ok() && error)
        *error = pkg.error();
    return pkg.ok();
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
    QSaveFile out(QDir(directory).filePath(QStringLiteral("theme.json")));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(theme).toJson()) < 0 || !out.commit()) {
        if (error)
            *error = out.errorString();
        return QJsonObject();
    }
    return theme;
}

} // namespace xpl
