// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/preview.h"
#include <cmath>

#include "app/media.h"
#include "app/decoration.h"
#include "app/gdifont.h"
#include "app/panel.h"
#include "app/paths.h"
#include "app/qttarget.h"
#include "app/startbutton.h"
#include "core/themefile.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>

namespace xpl::preview {

namespace {

// XP's monitor: its screen, measured against the bezel (a rectangle, no
// rounding), and the magenta it keys out.
constexpr QRect kMonitorScreen{15, 17, 153, 114};

} // namespace

Previewer::Previewer()
{
    // The style plugin: the one this XPlasma ships, and wherever it's installed.
    const QString shipped = paths::shippedStylePlugin(6);
    if (!shipped.isEmpty())
        QCoreApplication::addLibraryPath(QFileInfo(shipped).dir().absolutePath() + QLatin1String("/.."));
    QCoreApplication::addLibraryPath(paths::stylePluginRoot(6));
}

bool Previewer::styleAvailable() const
{
    return QStyleFactory::keys().contains(QLatin1String(qtstyle::kStyleKey), Qt::CaseInsensitive);
}

QStyle *Previewer::styleFor(const Look &look)
{
    // A look is known by what it draws from: the style and variant, or the
    // Classic scheme's contents (which Advanced edits).
    QString spec;
    if (look.classic()) {
        ThemeFile file;
        look.system.writeTo(file);
        const QByteArray bytes = file.toBytes();
        spec = m_scratch.filePath(QString::number(qHash(bytes), 16) + QLatin1String(".theme"));
        if (!QFileInfo::exists(spec)) {
            QFile f(spec);
            if (!f.open(QIODevice::WriteOnly))
                return nullptr;
            f.write(bytes);
        }
    } else {
        spec = look.stylePath + QLatin1Char(':') + look.variant;
    }
    if (QStyle *s = m_styles.value(spec))
        return s;
    // The plugin reads XPLASMA_THEME when it's made (and then doesn't watch
    // xplasmarc): one instance per look.
    const QByteArray old = qgetenv("XPLASMA_THEME");
    qputenv("XPLASMA_THEME", QFile::encodeName(spec));
    QStyle *style = QStyleFactory::create(QLatin1String(qtstyle::kStyleKey));
    if (old.isNull())
        qunsetenv("XPLASMA_THEME");
    else
        qputenv("XPLASMA_THEME", old);
    if (style)
        m_styles.insert(spec, style);
    return style;
}

namespace {
QColor liveText(const QColor &background,int mode){return mode==1?QColor(Qt::white):mode==2?QColor(Qt::black):(.299*background.redF()+.587*background.greenF()+.114*background.blueF()<.5?QColor(Qt::white):QColor(Qt::black));}
QImage liveImage(QImage image,const QColor &target,bool glyph=false,const QColor &edge=QColor()){
    if(image.isNull() || !target.isValid())return image;
    image=image.convertToFormat(QImage::Format_ARGB32);double sum=0,count=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(c.alpha()>200){sum+=.2126*c.redF()+.7152*c.greenF()+.0722*c.blueF();++count;}}
    const qreal tone=qBound(.05,count?sum/count:.5,.95);
    for(int y=0;y<image.height();++y){auto *line=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x){const auto pixel=line[x];if(!qAlpha(pixel))continue;const auto c=QColor::fromRgba(pixel);
        if(glyph && x>image.width()*.2 && x<image.width()*.8 && y>image.height()*.2 && y<image.height()*.8 && c.hsvSaturationF()<.12 && (c.valueF()>.96 || c.valueF()<.06))continue;
        qreal mix=0;if(edge.isValid() && x>=image.width()-9 && c.hsvSaturationF()>.12)mix=qBound(0.,(c.hsvHueF()*360.-125.)/95.,1.);
        const QColor tint(qRound(target.red()*(1-mix)+edge.red()*mix),qRound(target.green()*(1-mix)+edge.green()*mix),qRound(target.blue()*(1-mix)+edge.blue()*mix));
        const qreal center=tone*(1-mix)+.354402745*mix;
        const qreal value=.2126*c.redF()+.7152*c.greenF()+.0722*c.blueF();const qreal shade=value<center?std::pow(value/center,1.1):1;const qreal shine=value<center?0:.65*(value-center)/(1-center);
        auto channel=[&](int v){return qBound(0,qRound(v*shade*(1-shine)+255*shine),255);};line[x]=qRgba(channel(tint.red()),channel(tint.green()),channel(tint.blue()),qAlpha(pixel));
    }}return image;
}
QImage colorMask(QImage image,const QColor &color){QPainter p(&image);p.setCompositionMode(QPainter::CompositionMode_SourceIn);p.fillRect(image.rect(),color);return image;}
}

QImage windowDecoration(const Look &look, QSize size, const QString &title, bool active, bool dialog, const QJsonObject &live)
{
    const DecorationLayout layout = decorationLayout(look);
    const DecorationArt art = decorationArt(look);
    QImage image = composeFrame(art.frame, active ? QStringLiteral("decoration-") : QStringLiteral("decoration-inactive-"), size);
    const bool tinted=live.value("enabled").toBool();
    const QColor titleColor(live.value("title").toString(live.value("menu").toString()));
    if(tinted)image=liveImage(image,titleColor);
    QPainter painter(&image);
    int right = size.width() - layout.frame - 2;
    for (const QString &button : {QStringLiteral("close"), QStringLiteral("maximize"), QStringLiteral("minimize")}) {
        if (dialog && button != QLatin1String("close")) continue;
        for (const auto &[name, states] : art.buttons) {
            if (name != button) continue;
            for (const auto &[state, pixels] : states)
                if (state == (active ? QStringLiteral("active-center") : QStringLiteral("inactive-center"))) {
                    right -= layout.button.width();
                    QImage buttonPixels=pixels;
                    if(tinted){
                        buttonPixels=liveImage(buttonPixels,name==QLatin1String("close")?QColor("#d84427"):QColor(live.value("buttons").toString()),true);
                        const int mode=live.value("buttonsText").toInt();
                        if(mode)for(int y=buttonPixels.height()/5;y<buttonPixels.height()*4/5;++y)for(int x=buttonPixels.width()/5;x<buttonPixels.width()*4/5;++x){const auto c=pixels.pixelColor(x,y);if(c.hsvSaturationF()<.12 && (c.valueF()>.96 || c.valueF()<.06))buttonPixels.setPixelColor(x,y,QColor(mode==1?Qt::white:Qt::black));}
                    }
                    painter.drawImage(QPoint(right, layout.frame + 2), buttonPixels);
                    right -= look.classic() && button == QLatin1String("maximize") ? 0 : layout.buttonGap;
                    break;
                }
        }
    }
    const LogFont &spec = look.system.metrics.captionFont;
    QFont font(spec.face); font.setPixelSize(qAbs(spec.height)); font.setBold(spec.bold()); font.setItalic(spec.italic);
    painter.setFont(gdiFont(font));
    const QRect label(layout.frame + 4, layout.frame, qMax(0, right - layout.frame - 6), layout.caption);
    const QString text = QFontMetrics(font).elidedText(title, Qt::ElideRight, label.width());
    const auto flags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine;
    if (look.renderer) {
        const PartRef caption{QStringLiteral("window"), 1, active ? 1 : 2, {}};
        if (look.renderer->property(caption, u"textshadowtype").value_or(QString()).compare(QLatin1String("single"), Qt::CaseInsensitive) == 0) {
            const QStringList xy = look.renderer->property(caption, u"textshadowoffset").value_or(QStringLiteral("1,1")).split(QLatin1Char(','));
            const QPoint offset(xy.value(0).trimmed().toInt(), xy.value(1, xy.value(0)).trimmed().toInt());
            QRgb shadow = qRgb(128, 128, 128);
            parseColour(look.renderer->property(caption, u"textshadowcolor").value_or(QStringLiteral("128 128 128")), &shadow);
            painter.setPen(tinted ? QColor("#77000000") : QColor::fromRgb(shadow));
            painter.drawText(label.translated(offset), flags, text);
        }
    }
    painter.setPen(tinted ? liveText(titleColor,live.value("titleText").toInt()) : look.system.colour(active ? u"TitleText" : u"InactiveTitleText"));
    painter.drawText(label, flags, text);
    return image;
}

QImage Previewer::windows(const Look &look, QSize size, const QImage &behind, const QJsonObject &live)
{
    QStyle *style = styleFor(look);
    if(style && style->metaObject()->indexOfMethod("setLiveColors(QVariantMap)")>=0)
        QMetaObject::invokeMethod(style,"setLiveColors",Q_ARG(QVariantMap,live.toVariantMap()));
    QImage out = behind.isNull() ? QImage(size, QImage::Format_ARGB32_Premultiplied) : behind;
    if (behind.isNull()) out.fill(look.system.colour(u"Background"));
    const auto frame = decorationLayout(look);
    QPainter painter(&out);
    auto window = [&](const QString &title, QWidget *content, const QRect &geometry, bool active, bool dialog = false) {
        const QRect client(frame.frame, frame.frame + frame.caption, geometry.width() - 2 * frame.frame,
                           geometry.height() - frame.caption - 2 * frame.frame);
        const LogFont &spec = look.system.metrics.messageFont;
        QFont font(spec.face);font.setPixelSize(qAbs(spec.height));font.setBold(spec.bold());
        QList<QWidget *> widgets = content->findChildren<QWidget *>();widgets << content;
        for (QWidget *widget : std::as_const(widgets)) {
            if (style) { widget->setStyle(style);widget->setPalette(style->standardPalette()); }
            widget->setFont(gdiFont(font));
        }
        content->setAttribute(Qt::WA_DontShowOnScreen);
        content->resize(client.size());content->show();
        if (content->layout()) content->layout()->activate();
        painter.fillRect(client.translated(geometry.topLeft()), look.system.colour(u"ButtonFace"));
        content->render(&painter, geometry.topLeft() + client.topLeft());
        painter.drawImage(geometry.topLeft(), windowDecoration(look, geometry.size(), title, active, dialog,live));
        delete content;
    };
    auto text = [](const QString &s) {
        auto *edit = new QPlainTextEdit(s);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        return edit;
    };
    const int w = size.width(), h = size.height();
    window(QStringLiteral("Inactive Window"), text(QString()), QRect(8, 8, w - 60, h - 40), false);
    window(QStringLiteral("Active Window"), text(QStringLiteral("Window Text")), QRect(18, 34, w - 40, h - 44), true);
    auto *box = new QWidget;
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(6, 4, 6, 4);layout->setSpacing(2);
    layout->addWidget(new QLabel(QStringLiteral("Message Text")));
    auto *ok = new QPushButton(QStringLiteral("OK"));ok->setDefault(true);
    layout->addWidget(ok, 0, Qt::AlignHCenter);
    window(QStringLiteral("Message Box"), box, QRect(w / 2 - 70, h / 2 - 10, 150, 90), true, true);
    return out;
}

QImage Previewer::appearance(const Look &look, QSize size, const QColor &background,const QJsonObject &live)
{
    QImage behind(size, QImage::Format_ARGB32_Premultiplied);
    behind.fill(background);
    return windows(look, size, behind,live);
}

QImage placeWallpaper(const QImage &image, int fill, const QColor &colour, QSize size)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(colour);
    if (image.isNull())
        return out;
    // (The sample is the screen scaled down: a centred image keeps its
    // proportion of the screen, as it would on the desktop.)
    const QSize screen(1024, 768);
    const double scale = double(size.width()) / screen.width();
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    switch (fill) {
    case 0:   // stretch
        p.drawImage(out.rect(), image);
        break;
    case 1:   // fit
    case 2: { // crop
        const QSize fitted = image.size().scaled(size, fill == 1 ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding);
        p.drawImage(QRect(QPoint((size.width() - fitted.width()) / 2, (size.height() - fitted.height()) / 2), fitted), image);
        break;
    }
    case 3: { // tile
        const QImage tile = image.scaled(image.size() * scale, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int y = 0; y < size.height(); y += qMax(1, tile.height()))
            for (int x = 0; x < size.width(); x += qMax(1, tile.width()))
                p.drawImage(x, y, tile);
        break;
    }
    default: { // centred
        const QSize scaled = image.size() * scale;
        p.drawImage(QRect(QPoint((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2), scaled), image);
        break;
    }
    }
    return out;
}

QImage Previewer::desktop(const Wallpaper &wallpaper, QSize size)
{
    if (!m_monitorTried) {
        m_monitorTried = true;
        if (const auto bitmap = media::bitmap(QStringLiteral("themeui.dll"), 126)) {
            m_monitor = bitmap->convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < m_monitor.height(); ++y)
                for (int x = 0; x < m_monitor.width(); ++x)
                    if ((m_monitor.pixel(x, y) & 0xffffff) == 0xff00ff)
                        m_monitor.setPixel(x, y, 0);
        }
    }
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    const QImage image = wallpaper.image.isEmpty() ? QImage() : QImageReader(wallpaper.image).read();
    if (m_monitor.isNull()) {   // (without the XP media: just the screen)
        p.drawImage(0, 0, placeWallpaper(image, wallpaper.fill.value_or(0), wallpaper.colour, size));
        return out;
    }
    // XP's monitor, with the wallpaper in its screen.
    const QPoint at((size.width() - m_monitor.width()) / 2, (size.height() - m_monitor.height()) / 2);
    p.drawImage(at, m_monitor);
    p.drawImage(kMonitorScreen.translated(at).topLeft(), placeWallpaper(image, wallpaper.fill.value_or(0), wallpaper.colour, kMonitorScreen.size()));
    return out;
}

QImage Previewer::theme(const Look &look, const Wallpaper &desktop, QSize size, const StartButtonExtras &start,const QJsonObject &live)
{
    // The screen scaled down: the wallpaper, the windows, the taskbar with
    // the Start button along the bottom.
    const PanelArt art = panelArt(look);
    const int barHeight = qMax(12, art.height * size.width() / 800);
    const QSize screen(size.width(), size.height() - barHeight);
    const QImage image = desktop.image.isEmpty() ? QImage() : QImageReader(desktop.image).read();
    QImage full(size, QImage::Format_ARGB32_Premultiplied);
    full.fill(Qt::black);
    QPainter p(&full);
    p.drawImage(0, 0, windows(look, screen, placeWallpaper(image, desktop.fill.value_or(0), desktop.colour, screen),live));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QImage panel=composeFrame(art.panel,QString(),QSize(800,art.height));
    if(live.value("enabled").toBool())panel=liveImage(panel,QColor(live.value("menu").toString()));
    p.drawImage(QRect(0,screen.height(),size.width(),barHeight),panel);
    const auto flag = look.classic() ? media::classicStartFlag() : media::startFlag();
    if (flag)
        if (const auto button = startButton(look, *flag, start)){
            QImage pixels=button->normal;
            if(live.value("enabled").toBool()){
                const QColor tint(live.value("start").toString());
                const bool edge=QFileInfo(look.stylePath).baseName().compare("luna",Qt::CaseInsensitive)==0 && look.variant.contains("BLUE",Qt::CaseInsensitive);
                pixels=liveImage(button->backgrounds[0],tint,false,edge?QColor(live.value("menu").toString()):QColor());
                QPainter layers(&pixels);const QColor text=liveText(tint,live.value("startText").toInt());
                if(text==QColor(Qt::white))layers.drawImage(0,0,colorMask(button->shadowMasks[0],QColor("#73000000")));
                layers.drawImage(0,0,button->foregrounds[0]);layers.drawImage(0,0,colorMask(button->textMasks[0],text));
            }
            p.drawImage(QRect(0,screen.height(),button->normal.width()*barHeight/button->normal.height(),barHeight),pixels);
        }
    return full;
}

} // namespace xpl::preview
