// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/preview.h"
#include "app/editor/jobs.h"
#include <QTimer>
#include <QThread>
#include <QWidget>
#include "app/editor/wallpapercatalogue.h"
#include "app/decoration.h"
#include "app/panel.h"
#include <QTest>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
class TestPreview : public QObject {
 Q_OBJECT
 QTemporaryDir scratch;
private Q_SLOTS:
 void initTestCase() {
  QVERIFY(scratch.isValid());
  qputenv("HOME",QFile::encodeName(scratch.path()));
 }
 void responsiveApplyKeepsTimersRunningAndPreservesErrors() {
  class PaintProbe:public QWidget {public:int paints=0;protected:void paintEvent(QPaintEvent *) override {++paints;}};
  PaintProbe window;window.show();int ticks=0;QTimer heartbeat;
  connect(&heartbeat,&QTimer::timeout,&window,[&]{++ticks;window.update();window.close();});heartbeat.start(5);
  bool workerThread=false;QString error;
  QVERIFY(!xpl::editor::runResponsiveTask(&window,[&](QString *message){workerThread=QThread::currentThread()!=qApp->thread();QThread::msleep(70);*message="Expected test error";return false;},&error));
  QVERIFY(workerThread);QVERIFY(ticks>2);QVERIFY(window.paints>1);QVERIFY(window.isVisible());QCOMPARE(error,QString("Expected test error"));
  heartbeat.stop();window.close();QVERIFY(!window.isVisible());
 }

 void wallpaperOrderAndVariants() {
  QFileInfoList files={QFileInfo("Plus! — Plus! Space.jpg"),QFileInfo("Bliss (1920x1080).jpg"),QFileInfo("Sample Pictures — Winter.jpg"),QFileInfo("Azul.jpg"),QFileInfo("Bliss.jpg")};
  xpl::editor::sortWallpapers(files);
  QCOMPARE(files.size(),4);
  QCOMPARE(files[0].fileName(),QString("Azul.jpg"));
  QCOMPARE(files[1].fileName(),QString("Bliss.jpg"));
  QCOMPARE(files[2].fileName(),QString("Plus! — Plus! Space.jpg"));
 }
 void fillWallpaperPreservesAspectAndCropsCenter() {
  QImage image(4,2,QImage::Format_ARGB32);
  for(int y=0;y<2;++y) for(int x=0;x<4;++x) image.setPixelColor(x,y,QList<QColor>{Qt::red,Qt::green,Qt::blue,Qt::yellow}[x]);
  const QImage filled=xpl::preview::placeWallpaper(image,2,Qt::black,QSize(2,2));
  QCOMPARE(filled.pixelColor(0,0),QColor(Qt::green));
  QCOMPARE(filled.pixelColor(1,0),QColor(Qt::blue));
 }

 void liveTintsAppearInPreviewsAndRestore() {
  const QString root=qEnvironmentVariable("XPLASMA_PREVIEW_THEMES");if(root.isEmpty())QSKIP("Set XPLASMA_PREVIEW_THEMES");
  const auto look=xpl::Look::fromStyle(QDir(root).filePath("Luna/luna.msstyles"),"NORMALBLUE_INI");QVERIFY(look);
  xpl::preview::Previewer preview;xpl::Wallpaper wallpaper;
  const auto original=preview.theme(*look,wallpaper,xpl::preview::kThemesSize);
  const QJsonObject green{{"enabled",true},{"start","#f4d000"},{"menu","#315c43"},{"title","#315c43"},{"buttons","#315c43"},{"startText",2},{"titleText",1}};
  auto purple=green;purple.insert("menu","#644576");purple.insert("title","#644576");
  const auto g=preview.theme(*look,wallpaper,xpl::preview::kThemesSize,{},green);
  const auto p=preview.theme(*look,wallpaper,xpl::preview::kThemesSize,{},purple);
  QVERIFY(g!=original);QVERIFY(p!=g);QCOMPARE(preview.theme(*look,wallpaper,xpl::preview::kThemesSize),original);
  const QColor gc=g.pixelColor(g.width()-10,g.height()-5),pc=p.pixelColor(p.width()-10,p.height()-5);
  QVERIFY(gc.green()>gc.red());QVERIFY(pc.red()>pc.green());
  const auto out=qEnvironmentVariable("XPLASMA_PREVIEW_OUTPUT");if(!out.isEmpty()){QDir().mkpath(out);original.save(out+"/original.png");g.save(out+"/green.png");p.save(out+"/purple.png");}
 }

 void matchesGeneratedWindowBorders() {
  const QString root = qEnvironmentVariable("XPLASMA_PREVIEW_THEMES");
  if (root.isEmpty()) QSKIP("Set XPLASMA_PREVIEW_THEMES to an extracted XP theme library");
  int checked=0;
  for (const auto &spec : QList<QPair<QString,QString>>{{"Luna/luna.msstyles","NORMALBLUE_INI"},{"Luna/luna.msstyles","NORMALMETALLIC_INI"},{"Zune/Zune.msstyles",""},{"Royale Noir/Royale Noir.msstyles",""}}) {
   const QString path=QDir(root).filePath(spec.first);
   if (!QFileInfo::exists(path)) continue;
   const auto look=xpl::Look::fromStyle(path,spec.second);QVERIFY(look);++checked;
   const QString output=qEnvironmentVariable("XPLASMA_PREVIEW_OUTPUT");
   if (!output.isEmpty()) {QDir().mkpath(output);xpl::preview::Previewer preview;QVERIFY(preview.appearance(*look,QSize(347,180),look->system.colour(u"Background")).save(QDir(output).filePath(look->id+QStringLiteral(".png"))));}
   for (bool active : {true,false}) {
    const QSize size(307,140);
    const QImage expected=xpl::composeFrame(xpl::decorationArt(*look).frame,active?QStringLiteral("decoration-"):QStringLiteral("decoration-inactive-"),size);
    const QImage actual=xpl::preview::windowDecoration(*look,size,QStringLiteral("Active Window"),active);
    QCOMPARE(actual.copy(0,0,size.width(),3),expected.copy(0,0,size.width(),3));
    QCOMPARE(actual.copy(0,size.height()-4,size.width(),4),expected.copy(0,size.height()-4,size.width(),4));
    QCOMPARE(actual.copy(0,40,4,70),expected.copy(0,40,4,70));
   }
  }
  QVERIFY(checked>=4);
 }
};
QTEST_MAIN(TestPreview)
#include "test_preview.moc"
