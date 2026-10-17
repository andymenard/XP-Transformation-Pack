// Measure the live apply pipeline without changing saved presets or theme artwork.
#include "app/startmenu.h"
#include "app/session.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
int main(int argc,char **argv){QCoreApplication app(argc,argv);QFile file(QDir::homePath()+"/.config/xplasma-live-colors.json");if(!file.open(QIODevice::ReadOnly))return 1;const auto doc=QJsonDocument::fromJson(file.readAll());if(!doc.isObject())return 2;const auto original=doc.object();xpl::LiveSession session;QString error;bool ok=true;
for(int i=0;i<6;++i){auto colors=original;const QString color=i%2?"#644576":"#315c43";colors.insert("menu",color);colors.insert("title",color);QElapsedTimer timer;timer.start();if(!xpl::applyLiveStartColors(session,colors,&error)){ok=false;break;}fprintf(stderr,"Apply %d: %.3f ms\n",i+1,timer.nsecsElapsed()/1000000.);QThread::msleep(180);}
const bool restored=xpl::applyLiveStartColors(session,original,&error);if(!ok || !restored){fprintf(stderr,"%s\n",qPrintable(error));return 3;}puts("Original active colors restored; saved state untouched.");}
