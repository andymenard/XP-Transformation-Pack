// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/paths.h"
#include <QApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickPaintedItem>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTest>
#include <cstdio>
int main(int argc,char **argv){
 qInstallMessageHandler([](QtMsgType,const QMessageLogContext &,const QString &msg){fprintf(stderr,"%s\n",qPrintable(msg));});
 QApplication app(argc,argv);
 if(argc<3)return 1;
 if(!QFileInfo::exists(QString::fromLocal8Bit(argv[2]))){fprintf(stderr,"Missing SVG fixture\n");return 10;}
 const QString ui=QString::fromLocal8Bit(argv[1]),svg=QString::fromLocal8Bit(argv[2]);const bool decoration=argc>3;
 QTemporaryDir home;qputenv("HOME",QFile::encodeName(home.path()));QDir().mkpath(home.path()+"/.config");
 const QString extra=decoration?QString("KSvg.FrameSvgItem {objectName:'closeButton';x:20;y:50;width:24;height:24;imagePath:'%1/close.svg';prefix:'active'} KSvg.FrameSvgItem {x:60;y:50;width:24;height:24;imagePath:'%1/minimize.svg';prefix:'active'} KSvg.FrameSvgItem {x:100;y:50;width:24;height:24;imagePath:'%1/maximize.svg';prefix:'active'}").arg(QFileInfo(svg).absolutePath()):QString();
 QQmlEngine engine;QQmlComponent component(&engine);
 component.setData(QString("import QtQuick\nimport org.kde.ksvg as KSvg\nimport '%1' as Xp\nItem {width:600;height:100; KSvg.FrameSvgItem{id:bg;objectName:'background';width:600;height:%6;imagePath:'%2';prefix:'%3'} Text{objectName:'xplasma-caption';x:80;y:8;text:'Taskbar text';color:'white'} Rectangle{x:8;y:4;width:20;height:20;color:'orange'} %5 Xp.LiveScope{id:tints;objectName:'scope';scene:parent;mode:'%4'} }").arg(QUrl::fromLocalFile(ui).toString(),svg,qEnvironmentVariable("LIVE_FRAME_PREFIX",decoration?"decoration":"south"),decoration?"decoration":"panel",extra,qEnvironmentVariable("LIVE_FRAME_HEIGHT","30")).toUtf8(),QUrl::fromLocalFile(ui+"/Probe.qml"));
 auto object=component.create();if(!object){for(const auto &e:component.errors())fprintf(stderr,"%s\n",qPrintable(e.toString()));return 2;}
 auto root=qobject_cast<QQuickItem*>(object);QQuickWindow window;window.setColor(Qt::black);root->setParentItem(window.contentItem());window.resize(600,qMax(100,qEnvironmentVariableIntValue("LIVE_FRAME_HEIGHT")));root->setHeight(window.height());window.show();QTest::qWait(250);
 if(!QTest::qWaitForWindowExposed(&window))return 15;
 QImage previous;int stable=0;
 for(int i=0;i<20 && stable<3;++i){QTest::qWait(100);const auto frame=window.grabWindow();stable=frame==previous?stable+1:0;previous=frame;}
 if(stable<3){fprintf(stderr,"Initial scene did not stabilize\n");return 16;}
 auto original=window.grabWindow();
 auto scope=root->findChild<QObject*>("scope");
 if(!scope || scope->property("surfaceCount").toInt()!=0){fprintf(stderr,"Disabled live tint created renderers\n");return 8;}
 auto write=[&](const QJsonObject &value){QFile f(home.path()+"/.config/xplasma-live-colors.json");f.open(QIODevice::WriteOnly);f.write(QJsonDocument(value).toJson());f.close();QTest::qWait(350);};
 write({{"enabled",true},{"passthrough",true},{"title","#315c43"},{"menu","#315c43"}});
 const auto neutral=window.grabWindow();
 if(neutral!=original){int changed=0;for(int y=0;y<original.height();++y)for(int x=0;x<original.width();++x)if(neutral.pixel(x,y)!=original.pixel(x,y))++changed;fprintf(stderr,"Untinted overlay differs from KSvg original: %d pixels\n",changed);return 5;}
 write({{"enabled",true},{"menu","#315c43"},{"menuText",2},{"title","#315c43"},{"titleText",2},{"buttons","#315c43"},{"buttonsText",1}});auto green=window.grabWindow();
 write({{"enabled",true},{"menu","#644576"},{"menuText",1},{"title","#644576"},{"titleText",1},{"buttons","#644576"},{"buttonsText",2}});auto purple=window.grabWindow();
 if(qEnvironmentVariableIntValue("LIVE_EXPECT_GPU")){
  int controllers=0;
  for(auto *item:scope->findChildren<QQuickItem*>()){
   if(item->property("gpu").toBool() && item->property("shaderActive").toBool())++controllers;
   if(item->inherits("TintSurface")){fprintf(stderr,"GPU path created a CPU tint renderer\n");return 13;}
  }
  if(controllers<1){fprintf(stderr,"No GPU tint controllers active\n");return 14;}
 }
 if(decoration){
  const auto scale=qreal(purple.width())/600.;int red=0;
  for(int y=52;y<72;++y)for(int x=22;x<42;++x){const QColor c=purple.pixelColor(qRound(x*scale),qRound(y*scale));if(c.red()>c.green()*1.4 && c.red()>c.blue()*1.3)++red;}
  if(red<20){fprintf(stderr,"Close button lost its red background\n");return 4;}
 }
 if(qEnvironmentVariableIntValue("LIVE_FRAME_HEIGHT")>100){
  QElapsedTimer timer;timer.start();
  for(int i=0;i<10;++i){for(auto *painted:root->findChildren<QQuickPaintedItem*>())painted->update();window.grabWindow();}
  fprintf(stderr,"Ten cached full-window redraws: %lld ms\n",timer.elapsed());
  const auto painted=root->findChildren<QQuickItem*>();QImage profiles[2];
  timer.restart();
  for(int i=0;i<20;++i){
   const QColor color(i%2?"#644576":"#315c43");
   for(auto *surface:painted)if(surface->inherits("TintSurface") || (surface->inherits("QQuickShaderEffect") && surface->property("closeButton").isValid() && surface->property("closeButton").toDouble()<0.5))surface->setProperty("tint",color);
   const auto frame=window.grabWindow();
   if(i<2)profiles[i]=frame;else if(frame!=profiles[i%2]){fprintf(stderr,"Repeated profile pixels changed\n");return 6;}
  }
  fprintf(stderr,"Twenty alternating full-window profile renders: %lld ms\n",timer.elapsed());
 }
 if(decoration && qEnvironmentVariableIntValue("LIVE_RESIZE_TEST")){
  auto *background=root->findChild<QQuickItem*>("background");
  write({{"enabled",true},{"passthrough",true},{"title","#315c43"}});
  for(int i=0;i<8;++i){
   background->setWidth(410+i*23);background->setHeight(180+i*31);
   const auto overlay=window.grabWindow();
   const qreal buttonScale=qreal(overlay.width())/600.;
   const QRect buttonRegion(qRound(60*buttonScale),qRound(50*buttonScale),qRound(64*buttonScale),qRound(24*buttonScale));
   if(overlay.copy(buttonRegion)!=neutral.copy(buttonRegion)){fprintf(stderr,"Minimize/maximize pixels changed during resize\n");return 12;}
   write({});const auto base=window.grabWindow();
   if(overlay!=base){overlay.save("/tmp/gpu-resize-overlay.png");base.save("/tmp/gpu-resize-base.png");fprintf(stderr,"Resize fidelity failed at step %d\n",i);return 7;}
   write({{"enabled",true},{"passthrough",true},{"title","#315c43"}});
  }
  write({{"enabled",true},{"title","#315c43"}});
  QElapsedTimer resizeTimer;resizeTimer.start();
  for(int i=0;i<40;++i){background->setWidth(400+i*4);background->setHeight(200+i*9);window.grabWindow();}
  fprintf(stderr,"Forty resize frames: %lld ms\n",resizeTimer.elapsed());
  background->setWidth(600);background->setHeight(qEnvironmentVariableIntValue("LIVE_FRAME_HEIGHT"));window.grabWindow();
 }
 write({});auto restored=window.grabWindow();
 if(scope->property("surfaceCount").toInt()!=0){fprintf(stderr,"Disabled live tint retained renderers\n");return 9;}
 for(auto *painted:root->findChildren<QQuickPaintedItem*>())if(painted->inherits("TintSurface")){fprintf(stderr,"Disabled renderer object survived\n");return 11;}
 if(decoration && qEnvironmentVariableIntValue("LIVE_RESIZE_TEST")){
  auto *background=root->findChild<QQuickItem*>("background");QElapsedTimer timer;timer.start();
  for(int i=0;i<40;++i){background->setWidth(400+i*4);background->setHeight(200+i*9);window.grabWindow();}
  fprintf(stderr,"Forty base-theme resize frames (no tint objects): %lld ms\n",timer.elapsed());
 }

 const auto scale=qreal(original.width())/600.;
 const QColor g=green.pixelColor(qRound(500*scale),qRound(15*scale)),p=purple.pixelColor(qRound(500*scale),qRound(15*scale));
 const QString out=qEnvironmentVariable("LIVE_TINT_OUTPUT","/tmp/live-surface-probe");QDir().mkpath(out);original.save(out+"/panel-original.png");green.save(out+"/panel-green.png");purple.save(out+"/panel-purple.png");
 if(original.isNull() || green==original || green==purple || original!=restored || g.green()<=g.red() || p.red()<=p.green() || green.pixelColor(qRound(15*scale),qRound(12*scale))!=original.pixelColor(qRound(15*scale),qRound(12*scale))){fprintf(stderr,"Surface tint/restore/icon test failed: green %s purple %s\n",qPrintable(g.name()),qPrintable(p.name()));return 3;}
 puts("Live surfaces passed: background and text changed, icon preserved, exact Cancel restoration.");
}
