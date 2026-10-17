// SPDX-License-Identifier: GPL-3.0-or-later
// GPU smoke test: changes colors on the same QML scene without rewriting artwork.
#include "app/look.h"
#include "app/startmenu.h"
#include "app/state.h"
#include "app/paths.h"
#include <QApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <functional>

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QString error;
    const auto state=xpl::State::load();
    auto look=state.appliedKind==QLatin1String("style") ? xpl::Look::fromStyle(state.style,state.variant,&error) : std::optional<xpl::Look>(xpl::Look::fromScheme(xpl::ClassicScheme()));
    if(!look){qCritical()<<error;return 1;}
    QTemporaryDir temp;
    const auto theme=xpl::writeStartTheme(*look,temp.path(),&error);
    if(theme.isEmpty()){qCritical()<<error;return 2;}
    auto inventory=[&]{QByteArray b;QDirIterator it(temp.path(),QDir::Files);while(it.hasNext()){QFile f(it.next());f.open(QIODevice::ReadOnly);b+=f.readAll();}return QCryptographicHash::hash(b,QCryptographicHash::Sha256);};
    const auto before=inventory();
    QQmlEngine engine;
    QQmlComponent component(&engine);
    const QString ui=QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()+QStringLiteral("/../../src/app/start-menu/contents/ui");
    const QByteArray qml=QStringLiteral("import QtQuick\nimport '%1'\nimport '%1/LiveColors.js' as Colors\nItem{ id:root; property var base; property string computedMenuText: menu.themeData.palette.programText; property var live: ({}); width:menu.width; height:menu.height+40; StartMenu{id:menu;themeData:Colors.theme(root.base,root.live);userName:'Live color preview';pinnedItems:[{label:'Internet Explorer',description:'Web browser',action:'close'}]; recentItems:[{label:'Recent program',description:'App',action:'close'}]; placeGroups:[[{label:'My Documents',action:'close'}],[{label:'Search',action:'close'}]]} StartButtonArt{y:menu.height+5;art:root.base.button;live:root.live} }").arg(QUrl::fromLocalFile(ui).toString()).toUtf8();
    component.setData(qml,QUrl::fromLocalFile(ui+QStringLiteral("/LiveProbe.qml")));
    auto object=component.createWithInitialProperties({{QStringLiteral("base"),theme.toVariantMap()}});
    if(!object){qCritical()<<component.errors();return 3;}
    auto item=qobject_cast<QQuickItem*>(object);
    QQuickWindow window;window.setColor(Qt::transparent);item->setParentItem(window.contentItem());window.resize(item->width(),item->height());window.show();
    if(!QTest::qWaitForWindowExposed(&window))return 6;
    // Wayland's initial configure can change fractional scale after exposure.
    QSize previous;int stable=0;
    for(int i=0;i<30 && stable<5;++i){QTest::qWait(100);const auto size=window.grabWindow().size();stable=size==previous?stable+1:0;previous=size;}
    if(stable<5)return 7;
    const QString out=qEnvironmentVariable("LIVE_TINT_OUTPUT",QStringLiteral("/tmp/live-tint-probe"));QDir().mkpath(out);
    const auto original=window.grabWindow();original.save(out+"/original.png");
    auto separatorImages=[&]{QList<QImage> images;for(const auto &name:{"pinnedSeparator","allProgramsSeparator","placesSeparator"}){QQuickItem *line=nullptr;std::function<void(QQuickItem*)> find=[&](QQuickItem *node){if(node->objectName()==QLatin1String(name) && node->isVisible())line=node;for(auto *child:node->childItems())find(child);};find(item);if(!line || !line->isVisible())continue;const auto pos=line->mapToItem(item,QPointF());const qreal scale=qreal(window.grabWindow().width())/item->width();images.append(window.grabWindow().copy(QRect(qRound(pos.x()*scale),qRound(pos.y()*scale),qMax(1,qRound(line->width()*scale)),qMax(1,qRound(line->height()*scale)))));}return images;};
    const auto originalLines=separatorImages();
    auto tint=[&](const QString &color,int text){return QVariantMap{{"enabled",true},{"start",color},{"menu",color},{"startText",text},{"menuText",text}};};
    item->setProperty("live",tint("#315c43",1));QTest::qWait(250);const auto green=window.grabWindow();green.save(out+"/green.png");const auto greenLines=separatorImages();
    item->setProperty("live",tint("#644576",2));QTest::qWait(250);const auto purple=window.grabWindow();purple.save(out+"/purple.png");const auto purpleLines=separatorImages();
    if(originalLines.size()!=3 || greenLines.size()!=3 || purpleLines.size()!=3){fprintf(stderr,"Separator counts: %lld %lld %lld\n",originalLines.size(),greenLines.size(),purpleLines.size());return 8;}
    for(int i=0;i<3;++i)if(originalLines[i]==greenLines[i] || greenLines[i]==purpleLines[i]){fprintf(stderr,"Separator %d did not tint\n",i);return 9;}
    auto independent=tint("#315c43",2);independent.insert("startMenuText",1);
    item->setProperty("live",independent);QTest::qWait(250);
    if(item->property("computedMenuText").toString()!=QLatin1String("#ffffff"))return 10;
    window.grabWindow().save(out+"/menu-white-taskbar-black.png");
    auto mixedColors=tint("#f4d000",2);mixedColors.insert("menu","#644576");
    item->setProperty("live",mixedColors);QTest::qWait(250);const auto mixed=window.grabWindow();mixed.save(out+"/mixed-edge.png");
    if(theme.value("button").toObject().value("blueTaskbarEdge").toBool()){
        const auto button=theme.value("button").toObject();const qreal scale=qreal(mixed.width())/item->width();
        const auto edge=mixed.pixelColor(qRound((button.value("width").toInt()-2)*scale),qRound((item->height()-25)*scale));
        if(edge.blue()<=edge.green() || edge.red()<=edge.green()){qCritical()<<"Start edge did not follow purple taskbar"<<edge;return 5;}
    }
    item->setProperty("live",QVariantMap{});QTest::qWait(250);const auto restored=window.grabWindow();restored.save(out+"/restored.png");
    const QColor g=green.pixelColor(green.width()-25,100),p=purple.pixelColor(purple.width()-25,100);
    if(original.isNull() || green==original || purple==green || original!=restored || inventory()!=before || g.green()<=g.red() || p.red()<=p.green()) {fprintf(stderr,"Live tint/restore failed: null=%d unchanged=%d colorsame=%d restore=%d files=%d\n",original.isNull(),green==original,purple==green,original==restored,inventory()==before);qCritical()<<"Live tint/restore failed"<<g<<p;return 4;}
    qInfo()<<"Live colors changed on one scene; cancel restored pixels; artwork hashes unchanged."<<out;
    return 0;
}
