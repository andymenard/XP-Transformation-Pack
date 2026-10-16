#include "app/editor/customluna.h"
#include "app/editor/preview.h"
#include "app/startpanel.h"
#include "core/theme.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QTextStream>
int main(int argc,char**argv){QApplication app(argc,argv);QFile f(argv[1]);if(!f.open(QIODevice::ReadOnly))return 1;auto meta=QJsonDocument::fromJson(f.readAll()).object();std::array<QColor,4> colors;for(int i=0;i<4;++i)colors[i]=QColor(meta["colours"].toArray()[i].toString());QString error;QString generated=xpl::editor::customLuna(meta["source"].toString(),colors,"/tmp/tone-comparison/generated",&error,meta["variant"].toString());if(generated.isEmpty()){QTextStream(stderr)<<error;return 1;}QFile out("/tmp/tone-revised-path");out.open(QIODevice::WriteOnly);out.write(generated.toUtf8());
int n=0;for(const QString &path:{meta["source"].toString(),QString("/home/owner/.local/share/xplasma/assets/themes/Royale Noir/Royale Noir.msstyles"),QString(argv[1]).chopped(5),generated}){
 QString folder=QString("/tmp/tone-comparison/%1").arg(n++);QDir().mkpath(folder);auto look=xpl::Look::fromStyle(path);if(!look){QTextStream(stderr)<<"missing look "<<path;return 2;}
 auto theme=xpl::writeStartPanel(*look,folder,&error);if(theme.isEmpty()){QTextStream(stderr)<<error;return 3;}
 QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile("/home/owner/Documents/ChatGPT/XPlasma/native/src/app/start-menu/contents/ui/StartMenu.qml"));
 QVariantMap props{{"themeData",theme.toVariantMap()},{"userName",QString("owner")},{"pinnedItems",QVariantList{QVariantMap{{"label","Internet Explorer 6"},{"description","Internet Explorer 6"},{"action","close"}}}},{"recentItems",QVariantList{QVariantMap{{"label","System Settings"},{"description","Configure your system"},{"action","close"}}}},{"placeGroups",QVariantList{QVariantList{QVariantMap{{"label","My Documents"},{"action","close"}},QVariantMap{{"label","My Pictures"},{"action","close"}}}}}};
 auto *object=component.createWithInitialProperties(props);auto *item=qobject_cast<QQuickItem*>(object);if(!item){qWarning()<<component.errors();return 4;}QQuickWindow window;window.setColor(Qt::transparent);item->setParentItem(window.contentItem());window.resize(item->width(),item->height());window.show();QEventLoop loop;QTimer::singleShot(250,&loop,&QEventLoop::quit);loop.exec();window.grabWindow().save(folder+"/menu.png");
 xpl::preview::Previewer preview;preview.theme(*look,xpl::Wallpaper{QString(),2,QColor("#3a6ea5")},xpl::preview::kThemesSize).save(folder+"/appearance.png");
 QTextStream(stdout)<<folder<<" "<<path<<" palette "<<QString::fromUtf8(QJsonDocument(theme["palette"].toObject()).toJson(QJsonDocument::Compact))<<"\n";
 delete object;
}
}
