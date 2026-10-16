// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated generation/decoration preparation timing. Does not measure compositor
// presentation and does not apply anything to the desktop.
#include "app/editor/customluna.h"
#include "app/paths.h"
#include "app/decoration.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    if(argc<2)return 1;
    QTemporaryDir home;if(!home.isValid())return 2;
    qputenv("HOME",QFile::encodeName(home.path()));
    QString error;
    const auto base=xpl::Look::fromStyle(QString::fromLocal8Bit(argv[1]),argc>2?QString::fromLocal8Bit(argv[2]):QString(),&error);
    if(!base){qCritical()<<error;return 3;}
    const QJsonObject colors{{"enabled",true},{"start","#eb9d00"},{"menu","#7c7c5c"},{"buttons","#d99100"},{"startMenuText",2}};
    QElapsedTimer clock;clock.start();
    const auto first=xpl::editor::staticColorLook(*base,colors,&error);
    if(!first){qCritical()<<error;return 4;}
    const double cold=clock.nsecsElapsed()/1e6;
    clock.restart();
    for(int i=0;i<30;++i)if(!xpl::editor::staticColorLook(*base,colors,&error))return 5;
    const double warm=clock.nsecsElapsed()/30e6;
    QFile::remove(first->stylePath+".json");
    const auto imported=xpl::editor::staticColorLook(*first,colors,&error);
    if(!imported){qCritical()<<error;return 6;}
    const auto before=xpl::decorationArt(*first).element("decoration-top");
    if(before!=xpl::decorationArt(*imported).element("decoration-top"))return 7;
    clock.restart();const auto decoration=xpl::decorationArt(*first);
    const double prepare=clock.nsecsElapsed()/1e6;
    clock.restart();for(int i=0;i<30;++i)xpl::decorationArt(*first);
    QTextStream(stdout)<<"Cold color build: "<<cold<<" ms; warm color lookup: "<<warm<<" ms\n"
        <<"Missing sidecar: identical titlebar pixels\n"
        <<"Decoration preparation: "<<prepare<<" ms; repeated: "<<clock.nsecsElapsed()/30e6<<" ms (excludes compositor presentation)\n";
}
