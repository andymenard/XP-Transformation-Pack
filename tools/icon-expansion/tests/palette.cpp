// SPDX-License-Identifier: GPL-3.0-or-later
// Render argv[1] through KDE in white and black; optional argv[2] selects the theme.
#include <QApplication>
#include <QImage>
#include <QDebug>
#include <cstdio>
#include <KIconLoader>
#include <KIconTheme>
#include <KIconColors>
int main(int argc,char**argv){QApplication app(argc,argv);KIconTheme::forceThemeForTests(argc>2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("breeze"));KIconLoader loader;
 for(QColor ink:{QColor(Qt::white),QColor(Qt::black)}){
  const auto pix=loader.loadScaledIcon(QString::fromLocal8Bit(argv[1]),KIconLoader::Small,1,QSize(32,32),KIconLoader::DefaultState,{},nullptr,true,KIconColors(ink));
  auto im=pix.toImage().convertToFormat(QImage::Format_ARGB32);int n=0;
  for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)if(im.pixelColor(x,y)==ink)++n;
  printf("color %s size %d %d pixels %d\n",qPrintable(ink.name()),im.width(),im.height(),n); if(n<20)return 1;
 }return 0;}
