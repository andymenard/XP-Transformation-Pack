// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/preview.h"
#include "app/decoration.h"
#include "app/panel.h"
#include <QTest>
#include <QDir>
#include <QFileInfo>
class TestPreview : public QObject {
 Q_OBJECT
private Q_SLOTS:
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
