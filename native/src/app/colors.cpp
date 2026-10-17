// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/colors.h"
#include "core/crispfont.h"

#include <QFont>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QJsonDocument>

namespace xpl {

namespace {
QString rgb(const QColor &c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}
} // namespace

QByteArray colorScheme(const ClassicScheme &s, const QString &name, const std::pair<QColor, QColor> &complementary)
{
    QString out;
    auto group = [&](const char *group, const char *bg, const char *fg) {
        const QString b = rgb(s.colour(QString::fromLatin1(bg))), f = rgb(s.colour(QString::fromLatin1(fg)));
        out += QStringLiteral("[Colors:%1]\n").arg(QLatin1String(group));
        out += QStringLiteral("BackgroundNormal=%1\nBackgroundAlternate=%1\n").arg(b);
        out += QStringLiteral("ForegroundNormal=%1\nForegroundActive=%1\n").arg(f);
        out += QStringLiteral("ForegroundInactive=%1\n").arg(rgb(s.colour(u"GrayText")));
        out += QStringLiteral("ForegroundLink=%1\n").arg(rgb(s.colour(u"HotTrackingColor")));
        out += QStringLiteral("DecorationFocus=%1\nDecorationHover=%1\n\n").arg(rgb(s.colour(u"Hilight")));
    };
    // XP greys disabled text to GrayText and nothing else: no KDE effects.
    out += QStringLiteral("[ColorEffects:Disabled]\nColorEffect=0\nContrastEffect=0\nIntensityEffect=0\n\n");
    out += QStringLiteral("[ColorEffects:Inactive]\nEnable=false\n\n");
    group("Window", "ButtonFace", "ButtonText");
    group("Button", "ButtonFace", "ButtonText");
    group("Header", "ButtonFace", "ButtonText");
    if (complementary.first.isValid() && complementary.second.isValid()) {
        out += QStringLiteral("[Colors:Complementary]\n");
        out += QStringLiteral("BackgroundNormal=%1\nBackgroundAlternate=%1\n").arg(rgb(complementary.first));
        out += QStringLiteral("ForegroundNormal=%1\nForegroundActive=%1\nForegroundInactive=%1\n").arg(rgb(complementary.second));
        out += QStringLiteral("DecorationFocus=%1\nDecorationHover=%1\n\n").arg(rgb(s.colour(u"Hilight")));
    } else {
        group("Complementary", "ButtonFace", "ButtonText");
    }
    group("View", "Window", "WindowText");
    group("Selection", "Hilight", "HilightText");
    group("Tooltip", "InfoWindow", "InfoText");
    out += QStringLiteral("[General]\nColorScheme=%1\nName=%1\nshadeSortColumn=false\n\n").arg(name);
    out += QStringLiteral("[KDE]\ncontrast=4\n\n");
    out += QStringLiteral("[WM]\nactiveBackground=%1\nactiveBlend=%2\nactiveForeground=%3\n")
               .arg(rgb(s.colour(u"ActiveTitle")), rgb(s.colour(u"GradientActiveTitle")), rgb(s.colour(u"TitleText")));
    out += QStringLiteral("inactiveBackground=%1\ninactiveBlend=%2\ninactiveForeground=%3\n\n")
               .arg(rgb(s.colour(u"InactiveTitle")), rgb(s.colour(u"GradientInactiveTitle")), rgb(s.colour(u"InactiveTitleText")));
    out += QStringLiteral("[%1]\nScheme=%2\n").arg(QLatin1String("X-XPlasma-Generated"), name);
    return out.toUtf8();
}

bool applyLiveSystemColors(const QJsonObject &colors,const QString &directory,bool *changed,QString *error)
{
    if(changed)*changed=false;
    const bool enabled=colors.value(QStringLiteral("enabled")).toBool();
    const QColor accent(colors.value(QStringLiteral("menu")).toString());
    if(enabled && !accent.isValid()){if(error)*error=QStringLiteral("Invalid system accent color");return false;}
    QDir().mkpath(directory);
    const QString backupPath=directory+QStringLiteral("/xplasma-live-system-base.json");
    QFile previous(backupPath);QJsonObject backup;
    if(previous.open(QIODevice::ReadOnly)){
        QJsonParseError parse;const auto doc=QJsonDocument::fromJson(previous.readAll(),&parse);
        if(parse.error!=QJsonParseError::NoError || !doc.isObject()){if(error)*error=QStringLiteral("Cannot read the original system palette backup");return false;}
        backup=doc.object();
    }
    // KConfig's INI dialect is not QSettings' dialect: colons in group names
    // must remain literal, and RGB triplets must not acquire quotes or spaces.
    const QString palettePath=directory+QStringLiteral("/kdeglobals");
    QFile source(palettePath);QByteArray original;
    if(source.exists()){
        if(!source.open(QIODevice::ReadOnly)){if(error)*error=source.errorString();return false;}
        original=source.readAll();
    }
    auto lines=original.split('\n');
    bool legacyGroups=false;
    for(const auto &line:lines)if(line.trimmed().startsWith('[') && line.contains('%'))legacyGroups=true;
    if(legacyGroups){
        // Repair files left by an older Qt INI writer, including duplicate
        // encoded/literal groups, before capturing or restoring palette keys.
        QList<QByteArray> order;QMap<QByteArray,QList<QByteArray>> groups;QByteArray section;
        order.append(section);
        for(const auto &raw:lines){const auto line=raw.trimmed();
            if(line.startsWith('[') && line.endsWith(']')){
                section=QUrl::fromPercentEncoding(line.mid(1,line.size()-2)).toUtf8();
                if(!order.contains(section))order.append(section);
            }else{
                auto &content=groups[section];const int equals=line.indexOf('=');bool replaced=false;
                if(equals>=0 && !line.startsWith('#') && !line.startsWith(';')){
                    const auto key=line.left(equals).trimmed();
                    for(auto &prior:content)if(prior.indexOf('=')>=0 && prior.left(prior.indexOf('=')).trimmed()==key){prior=raw;replaced=true;break;}
                }
                if(!replaced)content.append(raw);
            }
        }
        lines.clear();for(const auto &group:order){if(!group.isEmpty())lines.append('['+group+']');lines.append(groups.value(group));}
    }
    auto indexOf=[&](const QString &key){
        const int slash=key.indexOf('/');const QByteArray group=key.left(slash).toUtf8(),name=key.mid(slash+1).toUtf8();
        QByteArray section;int found=-1;
        for(int i=0;i<lines.size();++i){const auto line=lines[i].trimmed();
            if(line.startsWith('[') && line.endsWith(']'))section=line.mid(1,line.size()-2);
            else if(section==group && line.indexOf('=')>=0 && line.left(line.indexOf('=')).trimmed()==name)found=i;
        }return found;
    };
    auto currentValue=[&](const QString &key){const int i=indexOf(key);if(i<0)return QString();auto value=QString::fromUtf8(lines[i].mid(lines[i].indexOf('=')+1).trimmed());const auto components=value.split(',');if(components.size()==3){bool rgb=true;QStringList clean;for(const auto &part:components){bool ok=false;const int number=part.trimmed().toInt(&ok);rgb=rgb && ok && number>=0 && number<=255;clean.append(part.trimmed());}if(rgb)value=clean.join(',');}return value;};
    auto setColor=[&](const QString &key,const QString &value){
        const int slash=key.indexOf('/');const QByteArray group=key.left(slash).toUtf8(),entry=key.mid(slash+1).toUtf8()+'='+value.toUtf8();
        const int existing=indexOf(key);
        if(existing>=0){lines[existing]=entry+(lines[existing].endsWith('\r')?QByteArray("\r"):QByteArray());return;}
        const QByteArray header='['+group+']';
        for(int i=0;i<lines.size();++i)if(lines[i].trimmed()==header){lines.insert(i+1,entry);return;}
        if(!lines.isEmpty() && !lines.last().isEmpty())lines.append(QByteArray());
        lines.append(header);lines.append(entry);lines.append(QByteArray());
    };
    auto entries=backup.value(QStringLiteral("entries")).toObject();
    auto last=backup.value(QStringLiteral("last")).toObject();
    QMap<QString,QString> desired;
    auto foreground=[](QColor color,int mode){return mode==1?QColor(Qt::white):mode==2?QColor(Qt::black):(.299*color.redF()+.587*color.greenF()+.114*color.blueF()<.5?QColor(Qt::white):QColor(Qt::black));};
    if(enabled){
        const QColor title(colors.value(QStringLiteral("title")).toString(accent.name()));
        const QColor text=foreground(accent,0);
        for(const QString &group:QStringList{"Window","Button","Header","View","Tooltip","Complementary","Selection"}){
            desired["Colors:"+group+"/DecorationFocus"]=rgb(accent);
            desired["Colors:"+group+"/DecorationHover"]=rgb(accent.lighter(115));
            if(group!="Selection")desired["Colors:"+group+"/ForegroundLink"]=rgb(accent);
        }
        for(const QString &key:QStringList{"BackgroundNormal","BackgroundAlternate"})desired["Colors:Selection/"+key]=rgb(accent);
        for(const QString &key:QStringList{"ForegroundNormal","ForegroundActive","ForegroundInactive"})desired["Colors:Selection/"+key]=rgb(text);
        desired["Colors:Complementary/BackgroundNormal"]=rgb(accent);
        desired["Colors:Complementary/BackgroundAlternate"]=rgb(accent);
        desired["Colors:Complementary/ForegroundNormal"]=rgb(foreground(accent,colors.value("menuText").toInt()));
        desired["Colors:Complementary/ForegroundActive"]=desired["Colors:Complementary/ForegroundNormal"];
        desired["WM/activeBackground"]=rgb(title);desired["WM/activeBlend"]=rgb(title.lighter(115));
        desired["WM/activeForeground"]=rgb(foreground(title,colors.value("titleText").toInt()));
        for(auto it=desired.cbegin();it!=desired.cend();++it){
            const bool exists=indexOf(it.key())>=0;const QString current=currentValue(it.key());
            // A newly applied base theme or an external edit becomes the new baseline.
            if(!entries.contains(it.key()) || (last.contains(it.key()) && current!=last.value(it.key()).toString()))
                entries.insert(it.key(),QJsonObject{{"exists",exists},{"value",current}});
            last.insert(it.key(),it.value());
        }
        const QJsonObject snapshot{{"entries",entries},{"last",last}};
        if(snapshot!=backup){
            QSaveFile file(backupPath);
            if(!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(snapshot).toJson())<0 || !file.commit()){
                if(error)*error=file.errorString();return false;
            }
        }
        for(auto it=desired.cbegin();it!=desired.cend();++it)if(currentValue(it.key())!=it.value()){
            setColor(it.key(),it.value());if(changed)*changed=true;
        }
    }else{
        for(auto it=entries.constBegin();it!=entries.constEnd();++it){
            if(currentValue(it.key())!=last.value(it.key()).toString())continue;
            const auto original=it.value().toObject();
            if(original.value("exists").toBool())setColor(it.key(),original.value("value").toString());
            else {const int i=indexOf(it.key());if(i>=0)lines.removeAt(i);}
            if(changed)*changed=true;
        }
    }
    const auto updated=lines.join('\n');
    if(updated!=original){
        if(changed)*changed=true;
        QSaveFile destination(palettePath);
        if(!destination.open(QIODevice::WriteOnly) || destination.write(updated)!=updated.size() || !destination.commit()){
            if(error)*error=destination.errorString();return false;
        }
    }
    if(!enabled)QFile::remove(backupPath);
    return true;
}

QString qtFontString(const LogFont &lf)
{
    QFont font(osUiFamily(lf.face));
    font.setPointSize(lf.points());
    font.setBold(lf.bold());
    font.setItalic(lf.italic);
    font.setUnderline(lf.underline);
    return font.toString();
}

QList<ConfigValue> fontSettings(const ClassicScheme &s)
{
    const QString kdeglobals = QStringLiteral("kdeglobals");
    const QString ui = qtFontString(s.metrics.messageFont), caption = qtFontString(s.metrics.captionFont);
    return {
        {kdeglobals, QStringLiteral("General"), QStringLiteral("font"), ui},
        {kdeglobals, QStringLiteral("General"), QStringLiteral("menuFont"), qtFontString(s.metrics.menuFont)},
        {kdeglobals, QStringLiteral("General"), QStringLiteral("toolBarFont"), ui},
        {kdeglobals, QStringLiteral("General"), QStringLiteral("smallestReadableFont"), qtFontString(s.metrics.statusFont)},
        {kdeglobals, QStringLiteral("WM"), QStringLiteral("activeFont"), caption},
        {kdeglobals, QStringLiteral("WM"), QStringLiteral("inactiveFont"), caption},
    };
}

} // namespace xpl
