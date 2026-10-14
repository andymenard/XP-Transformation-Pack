// SPDX-License-Identifier: GPL-3.0-or-later
#include "caption.h"
#include <QGuiApplication>
#include <QDebug>
int main(int argc,char **argv){
 QGuiApplication app(argc,argv);
 const QRectF a(100,100,800,26),b(500,800,160,32);
 if(XP::interpolate(a,b,0)!=a)return 1;
 const auto mid=XP::interpolate(a,b,.5);
 if(mid!=QRectF(300,450,480,26))return 2;
 if(XP::interpolate(a,b,1)!=QRectF(500,800,160,26))return 3;
 for(auto scale:{1.,1.5,2.5,3.}){
  const auto image=XP::caption(480,26,scale,"Caption test",QIcon(),QFont("Tahoma",10,QFont::Bold),QColor(0,84,227),QColor(61,149,255),Qt::white);
  if(image.size()!=QSize(qRound(480*scale),qRound(26*scale))||image.pixelColor(1,1).alpha()!=255)return 4;
 }
 qInfo()<<"250ms linear, constant-height geometry and opaque HiDPI caption rendering passed";
 return 0;
}
