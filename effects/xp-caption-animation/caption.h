// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QPainter>
#include <QIcon>
#include <QFont>
#include <QRectF>
#include <algorithm>
namespace XP {
constexpr int duration = 250;
inline QRectF interpolate(const QRectF &from, const QRectF &to, qreal progress) {
    const qreal t=std::clamp(progress,qreal(0),qreal(1));
    return {from.x()+(to.x()-from.x())*t,from.y()+(to.y()-from.y())*t,
            from.width()+(to.width()-from.width())*t,from.height()};
}
// The icon and title on a transparent strip: laid out once per animation
// (text shaping is most of a caption's cost), then cut to each frame's width.
inline QImage captionLabel(qreal width,qreal height,qreal scale,const QString &title,
                           const QIcon &icon,const QFont &font,const QColor &text) {
    QImage image(QSize(qMax(1,qRound(width*scale)),qMax(1,qRound(height*scale))),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);image.fill(Qt::transparent);
    QPainter p(&image);p.setFont(font);
    const int iconSize=16;const qreal iconBox=icon.isNull()?0:height;
    if(!icon.isNull())icon.paint(&p,QRect(qRound((height-iconSize)/2),qRound((height-iconSize)/2),iconSize,iconSize));
    p.setPen(text);
    p.drawText(QRectF(iconBox+1,0,qMax(qreal(0),width-iconBox-2),height),Qt::AlignLeft|Qt::AlignVCenter|Qt::TextSingleLine,title.left(255));
    return image;
}
// One frame: the gradient at `width`, the label over it with its text
// clipped to this width's text box.
inline QImage caption(qreal width,qreal height,qreal scale,const QImage &label,bool hasIcon,
                      const QColor &left,const QColor &right) {
    QImage image(QSize(qMax(1,qRound(width*scale)),qMax(1,qRound(height*scale))),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);image.fill(left);
    QPainter p(&image);
    const qreal iconBox=hasIcon?height:0;
    QLinearGradient gradient(iconBox,0,qMax(iconBox+1,width),0);gradient.setColorAt(0,left);gradient.setColorAt(1,right);
    p.fillRect(QRectF(iconBox,0,qMax(qreal(0),width-iconBox),height),gradient);
    p.setClipRect(QRectF(0,0,qMin(width,iconBox+1),height));p.drawImage(QPointF(0,0),label);
    p.setClipRect(QRectF(iconBox+1,0,qMax(qreal(0),width-iconBox-2),height));p.drawImage(QPointF(0,0),label);
    return image;
}
inline QImage caption(qreal width,qreal height,qreal scale,const QString &title,
                      const QIcon &icon,const QFont &font,const QColor &left,
                      const QColor &right,const QColor &text) {
    return caption(width,height,scale,captionLabel(width,height,scale,title,icon,font,text),!icon.isNull(),left,right);
}
}
