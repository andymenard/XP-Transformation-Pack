// SPDX-License-Identifier: GPL-3.0-or-later
#include "customluna.h"
#include "app/paths.h"
#include "app/decoration.h"
#include "app/panel.h"
#include "core/pe.h"
#include "core/theme.h"
#include "core/render.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <QtMath>
#include <algorithm>

namespace xpl::editor {
namespace {
// The live shader's luminance mapping, baked once into resource pixels.
QRgb tint(QRgb rgb,const QColor &target,const QColor &base)
{
    if((rgb&0x00ffffff)==0x00ff00ff)return rgb;
    const QColor c=QColor::fromRgba(rgb);
    const qreal value=.2126*c.redF()+.7152*c.greenF()+.0722*c.blueF();
    const qreal tone=qBound(.05,qreal(base.redF()),.95);
    const qreal shade=value<tone?qPow(value/tone,1.1):1.;
    const qreal shine=value<tone?0.:.65*(value-tone)/(1.-tone);
    auto channel=[&](int v){return qBound(0,qRound(v*shade*(1.-shine)+255.*shine),255);};
    return qRgba(channel(target.red()),channel(target.green()),channel(target.blue()),qAlpha(rgb));
}
qreal imageTone(const QImage &input)
{
    const auto image=input.convertToFormat(QImage::Format_ARGB32);double sum=0,count=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(c.alpha()>200 && c.rgb()!=qRgb(255,0,255)){sum+=.2126*c.redF()+.7152*c.greenF()+.0722*c.blueF();++count;}}
    return count?qBound(.05,sum/count,.95):.5;
}
QColor menuSurface(const QColor &accent,const QString &) { return accent; }
QString rgbText(const QColor &c)
{
    return QStringLiteral("%1 %2 %3").arg(c.red()).arg(c.green()).arg(c.blue());
}
QColor accentText(const QColor &background)
{
    return .299*background.redF()+.587*background.greenF()+.114*background.blueF()<.5 ? QColor(Qt::white) : QColor(Qt::black);
}
QByteArray accentIni(const QByteArray &resource, const std::array<QColor,4> &colours, const std::array<int,4> &textModes, int menuTextMode, const QByteArray &metadata)
{
    const QColor accent = colours[1];
    const auto menuText=[&](const QColor &background){return menuTextMode==1 ? QColor(Qt::white) : menuTextMode==2 ? QColor(Qt::black) : accentText(background);};
    const auto textFor = [&textModes](const QColor &background, int part) {
        return textModes[part] == 1 ? QColor(Qt::white) : textModes[part] == 2 ? QColor(Qt::black) : accentText(background);
    };
    // Reserialize the parsed properties to make room without changing PE
    // resource sizes or offsets. Other variants retain their original INIs.
    const auto sections = Ini::parse(resource).sections();
    QStringList names;
    QHash<QString, Properties> ordered;
    for (auto it = sections.cbegin(); it != sections.cend(); ++it) {
        const QString name = it.key().toString();
        names.append(name); ordered.insert(name, it.value());
    }
    if (!ordered.contains(QStringLiteral("sysmetrics"))) return {};
    auto &system = ordered[QStringLiteral("sysmetrics")];
    system.insert(QStringLiteral("activecaption"),rgbText(colours[2]));
    system.insert(QStringLiteral("gradientactivecaption"),rgbText(colours[2].lighter(115)));
    system.insert(QStringLiteral("gradientinactivecaption"),rgbText(colours[2].lighter(110)));
    system.insert(QStringLiteral("menuhilight"),rgbText(accent));
    system.insert(QStringLiteral("inactivecaption"),rgbText(colours[2].darker(130)));
    system.insert(QStringLiteral("highlight"), rgbText(accent));
    system.insert(QStringLiteral("highlighttext"), rgbText(textFor(accent,1)));
    system.insert(QStringLiteral("hottracking"), rgbText(accent));
    system.insert(QStringLiteral("captiontext"), rgbText(textFor(colours[2],2)));
    system.insert(QStringLiteral("inactivecaptiontext"), rgbText(textFor(colours[2],2)));
    for (const QString &name : std::as_const(names)) {
        auto &props = ordered[name];
        // The user pane and Log Off bar are the taskbar's surface, so its
        // text; Start menu text is for the lists between them.
        const bool band = name == QLatin1String("startpanel.userpane") || name == QLatin1String("startpanel.logoff");
        if (band) {
            props.insert(QStringLiteral("textcolor"), rgbText(textFor(accent,1)));
            if (props.contains(QStringLiteral("textshadowcolor"))) props[QStringLiteral("textshadowcolor")] = rgbText(textFor(accent,1)==QColor(Qt::white)?QColor(Qt::black):QColor(Qt::white));
        }
        if (name.startsWith(QLatin1String("startpanel.")) || name.startsWith(QLatin1String("startmenu::"))) {
            const QColor surface = menuSurface(accent, name);
            if (props.contains(QStringLiteral("fillcolor"))) props[QStringLiteral("fillcolor")] = rgbText(surface);
            if (props.contains(QStringLiteral("bordercolor"))) props[QStringLiteral("bordercolor")] = rgbText(surface.darker(140));
            if (!band) props[QStringLiteral("textcolor")] = rgbText(menuText(surface));
            if (props.contains(QStringLiteral("captiontext"))) props[QStringLiteral("captiontext")] = rgbText(menuTextMode ? menuText(surface) : (menuText(surface) == QColor(Qt::white) ? QColor("#b8b8b8") : QColor("#555555")));
        }
        if ((name.startsWith(QLatin1String("window.caption")) || name.startsWith(QLatin1String("window.smallcaption"))) && !name.contains(QLatin1String("button"))) {
            if (props.contains(QStringLiteral("textcolor"))) props[QStringLiteral("textcolor")] = rgbText(textFor(colours[2],2));
            if (props.contains(QStringLiteral("textshadowcolor"))) props[QStringLiteral("textshadowcolor")] = rgbText(textFor(colours[2],2)==QColor(Qt::white)?QColor(Qt::black):QColor(Qt::white));
        }
        if (textModes[3] && name.startsWith(QLatin1String("window.")) && name.contains(QLatin1String("button")))
            props[QStringLiteral("glyphtransparentcolor")]=QStringLiteral("255 0 255");
        if (name.startsWith(QLatin1String("start::button"))) {
            props[QStringLiteral("textcolor")] = rgbText(textFor(colours[0],0));
            if(textFor(colours[0],0)==QColor(Qt::black))props[QStringLiteral("textshadowtype")]=QStringLiteral("none");
            if (props.contains(QStringLiteral("textshadowcolor"))) props[QStringLiteral("textshadowcolor")] = rgbText(QColor(Qt::black));
        }
        if (name.startsWith(QLatin1String("taskband::")) || name.startsWith(QLatin1String("traynotify")) || name.startsWith(QLatin1String("clock")))
            props[QStringLiteral("textcolor")] = rgbText(textFor(accent,1));
        if (name.startsWith(QLatin1String("startmenu::toolbar")) && name.endsWith(QLatin1String("(hot)")))
            props[QStringLiteral("textcolor")] = rgbText(menuText(accent));
    }
    names.sort();
    QString text;
    for (const QString &name : std::as_const(names)) {
        text += QLatin1Char('[') + name + QLatin1String("]\n");
        auto keys = ordered[name].keys(); keys.sort();
        for (const QString &key : std::as_const(keys)) text += key + QLatin1Char('=') + ordered[name][key] + QLatin1Char('\n');
    }
    text += QStringLiteral("[XPlasmaCustomLuna]\nAccent=") + rgbText(accent) + QStringLiteral("\nMenuText=") + rgbText(menuText(accent)) + QStringLiteral("\nProfile=") + QString::fromLatin1(metadata.toBase64()) + QLatin1Char('\n');
    QByteArray out(text.size() * 2, '\0');
    for (qsizetype i = 0; i < text.size(); ++i) qToLittleEndian<quint16>(text[i].unicode(), out.data() + i * 2);
    if (out.size() > resource.size()) return {};
    out.resize(resource.size(), '\0');
    return out;
}
bool recolour(QByteArray &dib, QColor target, QColor base, const QColor &backdrop = QColor(), bool glyphs = false, QSize frame = QSize(), QRgb keyColor = qRgb(255,0,255), bool keepRed = false, bool glyphOnly = false, int foregroundMode = 0)
{
    if (dib.size() < 40) return false;
    const auto *raw = reinterpret_cast<const uchar *>(dib.constData());
    const quint32 header = qFromLittleEndian<quint32>(raw);
    const qint32 width = qFromLittleEndian<qint32>(raw + 4), height = qFromLittleEndian<qint32>(raw + 8);
    const int bits = qFromLittleEndian<quint16>(raw + 14);
    const quint32 compression = qFromLittleEndian<quint32>(raw + 16);
    if (header < 40 || header > quint32(dib.size()) || width <= 0 || height == 0 || compression != 0) return false;
    auto pixel = [&](qsizetype offset, int x = -1, int y = -1) {
        auto *p = reinterpret_cast<uchar *>(dib.data()) + offset;
        const QRgb input = qRgb(p[2], p[1], p[0]);
        if (glyphOnly && input == keyColor) {p[0]=255;p[1]=0;p[2]=255;return;}
        if (input == keyColor || input == qRgb(255,0,255)) return;
        if (glyphOnly) { p[0]=target.blue(); p[1]=target.green(); p[2]=target.red(); return; }
        const QColor originalColor(input);
        if (glyphs) {
            const QColor c(input);
            const int fx = frame.width() > 0 && x >= 0 ? x % frame.width() : -1;
            const int fy = frame.height() > 0 && y >= 0 ? y % frame.height() : -1;
            const bool center = fx < 0 || (fx >= frame.width()/5 && fx < frame.width()*4/5
                                          && fy >= frame.height()/5 && fy < frame.height()*4/5);
            if (center && c.hsvSaturationF() < .12 && (c.valueF() > .96 || c.valueF() < .06)) {
                if(!foregroundMode)return;
                if ((foregroundMode && (c.valueF()>.88 || base.valueF()>.35)) || base.hsvSaturationF() > .2 || c.valueF() > .88) {
                    const QColor foreground = foregroundMode == 1 ? QColor(Qt::white) : foregroundMode == 2 ? QColor(Qt::black) : accentText(target);
                    p[0]=foreground.blue(); p[1]=foreground.green(); p[2]=foreground.red();
                }
                return;
            }
        }
        // Close keeps Luna's own artwork, inactive included; only its
        // symbol follows the buttons' text choice (above).
        if(keepRed)return;
        QColor destination = target, reference = base;
        // Luna antialiases the rounded Start edge against the blue taskbar.
        // These pixels belong to a mixture of the two surfaces.
        const QColor c(input);
        if (backdrop.isValid() && x >= width - 9 && c.hsvSaturationF() > .12
            && input != qRgb(255, 0, 255)) {
            const qreal t = qBound(0., (c.hsvHueF() * 360. - 125.) / 95., 1.);
            const auto mix = [t](QColor a, QColor b) {
                return QColor(qRound(a.red() * (1-t) + b.red() * t),
                              qRound(a.green() * (1-t) + b.green() * t),
                              qRound(a.blue() * (1-t) + b.blue() * t));
            };
            destination = mix(target, backdrop);
            reference = mix(base, QColor::fromRgbF(.354402745,.354402745,.354402745));
        }
        const QRgb out = tint(input, destination, reference);
        p[0] = qBlue(out); p[1] = qGreen(out); p[2] = qRed(out);
    };
    if (bits <= 8 && bits > 0) {
        quint32 count = qFromLittleEndian<quint32>(raw + 32);
        if (!count) count = 1u << bits;
        if (count > (1u << bits) || quint64(header) + count * 4 > quint64(dib.size())) return false;
        for (quint32 i = 0; i < count; ++i) pixel(header + i * 4);
    } else if (bits == 24 || bits == 32) {
        const qint64 stride = ((qint64(width) * bits + 31) / 32) * 4;
        const qint64 rows = qAbs(qint64(height));
        if (rows > (dib.size() - header) / stride) return false;
        for (qint64 y = 0; y < rows; ++y)
            for (int x = 0; x < width; ++x) pixel(header + y * stride + x * (bits / 8), x, int(y));
    } else return false;
    return true;
}
}
QString customLuna(const QString &source, const std::array<QColor, 4> &colours,
                   const QString &directory, QString *error, const QString &variant, int textMode, std::array<int,4> textModes, int menuTextMode)
{
    for (int &mode : textModes) mode=qBound(0,mode<0 ? textMode : mode,2);
    menuTextMode=menuTextMode<0 ? textModes[1] : qBound(0,menuTextMode,2);
    auto fail = [&](const QString &why) { if (error) *error = why; return QString(); };
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) return fail(input.errorString());
    QByteArray bytes = input.readAll();
    // A cache hit must not decode resources or render neutral artwork.
    QString selectedVariant=variant;
    if(selectedVariant.isEmpty()) {
        const auto theme=Theme::fromData(bytes,variant,error);
        if(!theme)return {};
        selectedVariant=theme->variant();
    }
    QJsonArray palette;
    for (const QColor &c : colours) palette.append(c.name());
    QJsonArray modes; for (int mode : textModes) modes.append(mode);
    const QByteArray metadata = QJsonDocument(QJsonObject{{"source", source}, {"sourceHash", QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}, {"colours", palette}, {"generator", kCustomLunaGenerator}, {"colorModel", "baked-live-luminance"}, {"startMenuText", menuTextMode}, {"textMode", textMode}, {"textModes", modes}, {"variant", selectedVariant}}).toJson();
    const QString id = QString::fromLatin1(QCryptographicHash::hash(QByteArray(bytes + metadata), QCryptographicHash::Sha256).toHex());
    const QString path = directory + QLatin1Char('/') + id + QStringLiteral(".msstyles");
    if (QFileInfo::exists(path) && QFileInfo::exists(path + QStringLiteral(".json"))) return path;
    const auto resources = PeResources::parse(bytes, error);
    if (!resources) return {};
    const auto selected = Theme::fromData(bytes, variant, error);
    if (!selected) return {};
    if(selected->property(u"xplasmacustomluna",u"",u"",u"accent"))return fail(QStringLiteral("Choose the original base style before changing colors; this style is already baked."));
    const auto baseLook=Look::fromStyle(source,selected->variant(),error);
    if(!baseLook)return {};
    const auto decoration=decorationArt(*baseLook);const auto panel=panelArt(*baseLook);
    auto elementTone=[](const auto &elements,const QStringList &ids){for(const auto &id:ids)for(const auto &entry:elements)if(entry.first==id && !entry.second.isNull())return imageTone(entry.second.scaled(32,32,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));return .5;};
    const qreal titleTone=imageTone(decoration.element("decoration-top").scaled(32,32,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));
    const qreal panelTone=elementTone(panel.panel,{"south-normal-center","south-center","center"});
    const qreal trayTone=elementTone(panel.tray,{"tray-center","clock-center","center"});
    const qreal taskTone=elementTone(panel.tasks,{"normal-center","center"});
    QHash<QString, int> roles;
    QHash<QString, QString> surfaces;
    QHash<QString, QSize> frames;
    QHash<QString, QRgb> keys;
    QSet<QString> untouched;
    QHash<QString,QRgb> glyphs;
    for (auto it = selected->ini().sections().cbegin(); it != selected->ini().sections().cend(); ++it) {
        const auto &key = it.key();
        int role = -1;
        if (key.cls == QLatin1String("window")) {
            if (key.part.contains(QLatin1String("closebutton"))) role = 4;
            else if (key.part.contains(QLatin1String("button"))) role = 3;
            else if (key.part.contains(QLatin1String("caption")) || key.part.contains(QLatin1String("frame"))) role = 2;
        }
        if (key.sub == QLatin1String("start") && key.cls == QLatin1String("button")) role = 0;
        if(key.cls==QLatin1String("scrollbar"))role=1;
        if (key.cls.startsWith(QLatin1String("startpanel")) || key.cls.startsWith(QLatin1String("taskbar"))
            || key.cls.startsWith(QLatin1String("traynotify")) || key.cls == QLatin1String("clock")
            || key.sub.startsWith(QLatin1String("task")) || key.sub.startsWith(QLatin1String("tray"))
            || key.sub.startsWith(QLatin1String("startmenu"))) role = 1;
        const bool artworkIcon = key.cls == QLatin1String("startpanel") &&
            (key.part.contains(QLatin1String("arrow")) || key.part.contains(QLatin1String("logoffbuttons")) || key.part == QLatin1String("userpicture"));
        if (role < 0) continue;
        for (auto prop = it.value().cbegin(); prop != it.value().cend(); ++prop)
            if (prop.key().startsWith(QLatin1String("imagefile")) || prop.key()==QLatin1String("glyphimagefile")) {
                const QString resource = Theme::resourceName(prop.value());
                if (resource.contains(QLatin1String("GLYPH")) || prop.key()==QLatin1String("glyphimagefile")) {
                    if (role==3 || role==4) {
                        QRgb transparent=qRgb(255,0,255);
                        parseColour(selected->property(key.cls,key.part,key.state,u"glyphtransparentcolor",key.sub).value_or(QString()),&transparent);
                        glyphs.insert(resource,transparent);
                    }
                    untouched.insert(resource);continue;
                }
                if (artworkIcon) { untouched.insert(resource); continue; }
                if (!roles.contains(resource) || role == 4) {
                    roles.insert(resource, role); surfaces.insert(resource, key.toString());
                    QRgb transparent = qRgb(255,0,255);
                    parseColour(selected->property(key.cls,key.part,key.state,u"transparentcolor",key.sub).value_or(QString()), &transparent);
                    keys.insert(resource,transparent);
                    const auto image = selected->bitmap(prop.value()).image;
                    const int count = qMax(1, selected->property(key.cls,key.part,key.state,u"imagecount",key.sub).value_or(QStringLiteral("1")).toInt());
                    const bool horizontal = selected->property(key.cls,key.part,key.state,u"imagelayout",key.sub).value_or(QString()).compare(QStringLiteral("horizontal"),Qt::CaseInsensitive)==0;
                    frames.insert(resource, QSize(image.width()/(horizontal ? count : 1),image.height()/(horizontal ? 1 : count)));
                }
            }
    }
    std::array<int, 4> changed{};
    for (const Resource &r : resources->all()) {
        if (r.type == QLatin1String("TEXTFILE") && r.name.compare(selected->variant(), Qt::CaseInsensitive) == 0) {
            const QByteArray ini = accentIni(r.data, colours, textModes, menuTextMode, metadata);
            const qsizetype offset = r.fileOffset;
            if (ini.isEmpty() || offset < 0) return fail(QStringLiteral("Cannot update Luna's selection colors."));
            bytes.replace(offset, ini.size(), ini);
            continue;
        }
        if (r.type != QLatin1String("2")) continue;
        const QString n = r.name.toUpper();
        if (glyphs.contains(n) && textModes[3]) {
            QByteArray bitmap=r.data;
            if (!recolour(bitmap,textModes[3]==1 ? QColor(Qt::white) : QColor(Qt::black),QColor("#808080"),QColor(),false,QSize(),glyphs[n],false,true))
                return fail(QStringLiteral("Unsupported button glyph: %1").arg(n));
            bytes.replace(r.fileOffset,bitmap.size(),bitmap);continue;
        }
        const int part = roles.value(n, -1);
        if (part < 0 || untouched.contains(n)) continue;
        QByteArray bitmap = r.data;
        const bool blueLuna = n.startsWith(QLatin1String("BLUE_")) && selected->variant().contains(QLatin1String("BLUE"));
        const auto image = decodeDib(bitmap).image;
        const QSize frame=frames.value(n,image.size());
        qreal tone=imageTone(image.copy(0,0,qMin(frame.width(),image.width()),qMin(frame.height(),image.height())));
        if(part==2)tone=titleTone;
        if(part==1){
            const auto section=surfaces.value(n);
            if(section.startsWith("taskbar"))tone=panelTone;
            else if(section.startsWith("traynotify") || section.startsWith("clock"))tone=trayTone;
            else if(section.startsWith("taskband") || section.startsWith("task::"))tone=taskTone;
        }
        if(part==3 || part==4){
            const QString name=part==4?QString("close"):n.contains("MIN")?QString("minimize"):n.contains("MAX")?QString("maximize"):n.contains("RESTORE")?QString("restore"):QString("help");
            for(const auto &button:decoration.buttons)if(button.first==name)tone=elementTone(button.second,{"active-center"});
        }
        const QColor base=QColor::fromRgbF(tone,tone,tone);
        QColor target=part==4 ? QColor("#d84427") : colours[part];
        if (part==1 && (surfaces[n].startsWith(QLatin1String("startpanel")) || surfaces[n].startsWith(QLatin1String("startmenu::"))))
            target=menuSurface(colours[1],surfaces[n]);
        if (!recolour(bitmap,target,base,part==0 && blueLuna ? colours[1] : QColor(),part==3 || part==4 || n.contains(QLatin1String("CHEVRON")),frames[n],keys.value(n,qRgb(255,0,255)),part==4,false,(part==3 || part==4) ? textModes[3] : 0))
            return fail(QStringLiteral("Unsupported theme bitmap: %1").arg(n));
        const qsizetype offset = r.fileOffset;
        if (offset < 0) return fail(QStringLiteral("Cannot locate Luna bitmap: %1").arg(n));
        bytes.replace(offset, bitmap.size(), bitmap);
        if (part < 4) ++changed[part];
    }
    if (!changed[0] || !changed[1] || !changed[2]) return fail(QStringLiteral("This theme is missing required shell artwork."));
    if (!Theme::fromData(bytes, selected->variant(), error)) return {};
    if (!QDir().mkpath(directory)) return fail(QStringLiteral("Cannot create Custom Luna folder."));
    QSaveFile out(path), meta(path + QStringLiteral(".json"));
    // (QSaveFile says "Unknown error" for a disk that filled up mid-write:
    // say which file, and what usually causes it.)
    auto writeFailed = [&](const QSaveFile &file) {
        return fail(QStringLiteral("Cannot save %1 (%2) -- is the disk full, or the folder read-only?").arg(file.fileName(), file.errorString()));
    };
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) return writeFailed(out);
    if (!meta.open(QIODevice::WriteOnly) || meta.write(metadata) != metadata.size() || !meta.commit()) return writeFailed(meta);
    return path;
}
QJsonObject colorMetadata(const QString &path,const QString &variant)
{
    const auto theme=Theme::load(path,variant);
    if(theme) {
        const auto encoded=theme->property(u"xplasmacustomluna",u"",u"",u"profile");
        if(encoded) {
            const auto profile=QJsonDocument::fromJson(QByteArray::fromBase64(encoded->toLatin1())).object();
            if(!profile.isEmpty())return profile;
        }
    }
    QFile sidecar(path+QStringLiteral(".json"));
    return sidecar.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(sidecar.readAll()).object():QJsonObject();
}

std::optional<Look> staticColorLook(const Look &base,const QJsonObject &settings,QString *error)
{
    if(!settings.value("enabled").toBool())return base;
    std::array<QColor,4> colors;std::array<int,4> modes;
    const char *keys[]={"start","menu","title","buttons"};
    const char *textKeys[]={"startText","menuText","titleText","buttonsText"};
    for(int i=0;i<4;++i){const QString fallback=i==0?QString("#3c9b35"):i==1?QString("#245edb"):colors[1].name();colors[i]=QColor(settings.value(keys[i]).toString(fallback));modes[i]=qBound(0,settings.value(textKeys[i]).toInt(),2);if(!colors[i].isValid()){if(error)*error="Saved color profile contains an invalid color.";return std::nullopt;}}
    if(settings.value("linkTitleToTaskbar").toBool(true))colors[2]=colors[1];
    const int menuMode=qBound(0,settings.value("startMenuText").toInt(modes[1]),2);
    if(base.classic()){
        auto scheme=base.system;scheme.shellColors=colors;scheme.shellTextModes=modes;scheme.shellMenuTextMode=menuMode;
        scheme.shellBaseName=base.system.shellBaseName.isEmpty()?base.system.name:base.system.shellBaseName;
        scheme.name="Custom "+scheme.shellBaseName+' '+QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(settings).toJson(),QCryptographicHash::Sha256).toHex().left(10));
        scheme.setColour(u"ActiveTitle",colors[2]);scheme.setColour(u"GradientActiveTitle",colors[2].lighter(140));scheme.setColour(u"InactiveTitle",colors[2].darker(130));scheme.setColour(u"Hilight",colors[1]);
        auto text=[&](int part){return modes[part]==1?QColor(Qt::white):modes[part]==2?QColor(Qt::black):accentText(colors[part]);};
        scheme.setColour(u"TitleText",text(2));scheme.setColour(u"InactiveTitleText",text(2));scheme.setColour(u"HilightText",text(1));return Look::fromScheme(scheme);
    }
    QString source=base.stylePath;
    const auto metadata=colorMetadata(source,base.variant);
    if(!metadata.isEmpty()) {
        const QString original=metadata.value("source").toString();
        if(QFileInfo::exists(original) && QFileInfo(original).canonicalFilePath()!=QFileInfo(source).canonicalFilePath())source=original;
        else {
            bool same=metadata.value("colours").toArray().size()==4;
            const auto saved=metadata.value("colours").toArray(), savedModes=metadata.value("textModes").toArray();
            for(int i=0;same && i<4;++i)same=QColor(saved[i].toString())==colors[i] && (i<savedModes.size()?savedModes[i].toInt():0)==modes[i];
            same=same && metadata.value("startMenuText").toInt()==menuMode;
            if(same)return base;
            if(error)*error="The original base style is unavailable. Choose a base on Appearance before changing its colors.";
            return std::nullopt;
        }
    } else if(base.renderer->theme().property(u"xplasmacustomluna",u"",u"",u"accent")) {
        // Older exported baked styles may have lost their sidecar. Loading a
        // static profile is safe; a second recoloring of its pixels is not.
        if(settings.value("rendering").toString()==QLatin1String("static"))return base;
        if(error)*error="This baked style has no original-source metadata. Choose a base on Appearance first.";
        return std::nullopt;
    }
    const QString path=customLuna(source,colors,paths::data()+"/custom-luna",error,base.variant,0,modes,menuMode);
    if(path.isEmpty())return std::nullopt;
    return Look::fromStyle(path,base.variant,error);
}



} // namespace xpl::editor
