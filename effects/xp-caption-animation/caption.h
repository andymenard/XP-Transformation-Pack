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
inline QImage caption(qreal width,qreal height,qreal scale,const QString &title,
                      const QIcon &icon,const QFont &font,const QColor &left,
                      const QColor &right,const QColor &text) {
    QImage image(QSize(qMax(1,qRound(width*scale)),qMax(1,qRound(height*scale))),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);image.fill(left);
    QPainter p(&image);p.setFont(font);
    const int iconSize=16;const qreal iconBox=icon.isNull()?0:height;
    QLinearGradient gradient(iconBox,0,qMax(iconBox+1,width),0);gradient.setColorAt(0,left);gradient.setColorAt(1,right);
    p.fillRect(QRectF(iconBox,0,qMax(qreal(0),width-iconBox),height),gradient);
    if(!icon.isNull())icon.paint(&p,QRect(qRound((height-iconSize)/2),qRound((height-iconSize)/2),iconSize,iconSize));
    p.setPen(text);p.setClipRect(QRectF(iconBox+1,0,qMax(qreal(0),width-iconBox-2),height));
    p.drawText(QRectF(iconBox+1,0,qMax(qreal(0),width-iconBox-2),height),Qt::AlignLeft|Qt::AlignVCenter|Qt::TextSingleLine,title.left(255));
    return image;
}
}
