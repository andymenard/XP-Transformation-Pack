// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/startmenu.h"
#include "app/colors.h"
#include "app/paths.h"

#include "app/media.h"
#include "app/package.h"
#include "app/startbutton.h"
#include "app/startpanel.h"
#include "app/state.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include "app/session.h"
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
// menu shows; this is the one XPlasma's shows for a user with no picture of
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

QJsonObject writeStartTheme(const Look &look, const QString &directory, QString *error, const StartButtonExtras &extras)
{
    QJsonObject theme = writeStartPanel(look, directory, error);
    if (theme.isEmpty())
        return theme;
    const auto flag = look.classic() ? media::classicStartFlag(error) : media::startFlag(error);
    if (!flag)
        return QJsonObject();
    const auto button = startButton(look, *flag, extras, error);
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
    const char *states[]={"normal","hover","pressed"};
    for(int i=0;i<3;++i) {
        const QImage *layers[]={&button->backgrounds[i],&button->foregrounds[i],&button->textMasks[i],&button->shadowMasks[i]};
        const char *names[]={"background","foreground","textMask","shadowMask"};
        for(int j=0;j<4;++j) {
            const QString file=QDir(directory).filePath(QStringLiteral("button-%1-%2-%3.png").arg(stamp,QLatin1String(states[i]),QLatin1String(names[j])));
            if(!layers[j]->save(file)) {if(error)*error=QStringLiteral("Cannot write live Start layer: ")+file;return {};}
            images.insert(QLatin1String(states[i])+QLatin1Char('-')+QLatin1String(names[j]),QUrl::fromLocalFile(file).toString());
        }
    }
    images.insert(QStringLiteral("textColor"),button->textColor.name());
    images.insert(QStringLiteral("blueTaskbarEdge"),QFileInfo(look.stylePath).baseName().compare(QStringLiteral("luna"),Qt::CaseInsensitive)==0 && look.variant.contains(QStringLiteral("BLUE"),Qt::CaseInsensitive));
    theme.insert(QStringLiteral("button"), images);
    theme.insert(QStringLiteral("liveTintVersion"),1);
    // Measured once, used as the neutral luminance by the runtime shader.
    const auto tone=[](const QImage &image) {
        double sum=0,count=0;
        const QImage rgb=image.convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<rgb.height();++y)for(int x=0;x<rgb.width();++x){const QColor c=rgb.pixelColor(x,y);if(c.alpha()>200){sum+=.2126*c.redF()+.7152*c.greenF()+.0722*c.blueF();++count;}}
        return count ? qBound(.05,sum/count,.95) : .5;
    };
    images.insert(QStringLiteral("tone"),tone(button->backgrounds[0]));
    theme.insert(QStringLiteral("button"),images);
    QJsonObject parts=theme.value(QStringLiteral("parts")).toObject();
    for(auto it=parts.begin();it!=parts.end();++it) {
        QJsonObject spec=it.value().toObject();
        const QString name=it.key();
        if(!spec.contains(QStringLiteral("url")) || name.contains(QLatin1String("arrow")) || name.contains(QLatin1String("buttons")) || name==QLatin1String("userpicture"))continue;
        spec.insert(QStringLiteral("tone"),tone(QImage(QUrl(spec.value(QStringLiteral("url")).toString()).toLocalFile())));
        spec.insert(QStringLiteral("tintable"),true);it.value()=spec;
    }
    theme.insert(QStringLiteral("parts"),parts);
    theme.insert(QStringLiteral("look"), look.name);
    const QJsonObject features = State::load().settings.value(QStringLiteral("installerFeatures")).toObject();
    theme.insert(QStringLiteral("xpExplorerEnabled"), features.value(QStringLiteral("explorer")).toBool());
    theme.insert(QStringLiteral("xpControlPanelEnabled"), features.value(QStringLiteral("control_panel")).toBool());
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

bool applyLiveStartColors(Session &session,const QJsonObject &,QString *error)
{
    const QJsonObject colors; // Compatibility cleanup only: live rendering is retired.
    const QString json=QString::fromUtf8(QJsonDocument(colors).toJson(QJsonDocument::Compact));
    // Only configuration changes: no artwork writes, package replacements or restarts.
    const QString script=QStringLiteral("var n=0;panels().forEach(function(p){p.widgets('fyi.hotsocket.xplasma.start').forEach(function(w){w.currentConfigGroup=['General'];var t={};try{t=JSON.parse(w.readConfig('theme')||'{}');}catch(e){}if(!t.liveTintVersion)return;if(%1.enabled && String(w.readConfig('liveTintSupported',false))!=='true')return;w.writeConfig('liveColors',JSON.stringify(%1));++n;});});print(n);").arg(json);
    const auto result=session.script(script,error);
    if(!result || result->trimmed().toInt()<1){if(error && error->isEmpty())*error=QStringLiteral("Live tint needs the updated Start artwork and GPU rendering. Apply the base theme once first.");return false;}
    const QString path=QDir::homePath()+QStringLiteral("/.config/xplasma-live-colors.json");
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(colors).toJson(QJsonDocument::Compact))<0 || !file.commit()) {
        if(error)*error=file.errorString();return false;
    }
    bool paletteChanged=false;
    if(!applyLiveSystemColors(colors,paths::config(),&paletteChanged,error))return false;
    if(paletteChanged)session.signal(QStringLiteral("/KGlobalSettings"),QStringLiteral("org.kde.KGlobalSettings"),QStringLiteral("notifyChange"),{0,0});
    return true;
}

} // namespace xpl
