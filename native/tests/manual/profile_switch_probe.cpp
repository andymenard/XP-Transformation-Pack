// Manual UI regression probe. Expose internals only in this test translation unit.
#include <QtWidgets>
#include <QTest>
#include <QTemporaryDir>
#include <sstream>
#include "app/apply.h"
#include "app/library.h"
#include "app/pointers.h"
#include "app/paths.h"
#include "app/editor/customluna.h"
#include "app/editor/preview.h"
#include "app/editor/advanced.h"
#include "core/themefile.h"
#define private public
#include "app/editor/displayproperties.h"
#undef private
#include <cassert>
#include <cstdio>
int main(int argc,char **argv){
 // All generated profiles and editor state stay in a temporary home.
 const QString assets=xpl::paths::assets();const auto initial=xpl::State::load();
 QTemporaryDir isolated;assert(isolated.isValid());qputenv("HOME",QFile::encodeName(isolated.path()));
 QDir().mkpath(xpl::paths::data());assert(QFile::link(assets,xpl::paths::assets()));assert(initial.save());
 QApplication app(argc,argv);xpl::editor::DisplayProperties dialog;
 assert(dialog.m_appliedPointers && !dialog.pointersDiffer());
 const auto applied=*dialog.m_appliedPointers;const auto look=dialog.look();const auto wallpaper=dialog.wallpaper();
 QTemporaryDir temp;
 for(int i=0;i<6;++i){
  xpl::ThemeFile file;file.setValue(u"Control Panel\\Colors",u"Background",QString("%1 %2 %3").arg(wallpaper.colour.red()).arg(wallpaper.colour.green()).arg(wallpaper.colour.blue()));
  file.setValue(u"Control Panel\\Cursors",u"SchemeName",applied.first.name);
  QJsonObject colors{{"enabled",true},{"rendering","static"},{"startMenuText",2},{"menuText",1}};colors.insert("menu",i%2?"#644576":"#315c43");colors.insert("title",colors.value("menu"));
  QString error;const auto baked=xpl::editor::staticColorLook(look,colors,&error);assert(baked);
  file.setValue(u"XPlasma",u"LiveColors",QString::fromUtf8(QJsonDocument(colors).toJson(QJsonDocument::Compact)));
  xpl::ThemeBundle bundle;bundle.path=temp.filePath(QString("Profile-%1.theme").arg(i));bundle.label=QString("Profile %1").arg(i);bundle.style=baked->stylePath;bundle.variant=baked->variant;bundle.wallpaper=wallpaper.image;bundle.fill=wallpaper.fill;
  QFile output(bundle.path);assert(output.open(QIODevice::WriteOnly));output.write(file.toBytes());output.close();
  dialog.showBundle(bundle);
  assert(dialog.m_appliedPointers && *dialog.m_appliedPointers==applied);
  assert(!dialog.pointersDiffer());
  assert(dialog.m_themeFile==bundle.path);
  assert(dialog.look().stylePath==baked->stylePath);
  assert(dialog.m_lunaColours[1]->colour()==QColor(colors.value("menu").toString()));
  assert(dialog.m_startMenuTextMode->currentIndex()==2 && dialog.m_customTextModes[1]->currentIndex()==1);
  dialog.showTab("Custom Colors");dialog.showTab("Themes");assert(dialog.m_themeFile==bundle.path);
  assert(dialog.liveStartColors().isEmpty());
 }
 dialog.m_pointerShadow=!applied.second;assert(dialog.pointersDiffer());dialog.m_pointerShadow=applied.second;
 dialog.m_pointers->files[0]="changed-arrow.cur";assert(dialog.pointersDiffer());dialog.m_pointers=applied.first;
 dialog.m_pointers->name="Different scheme";assert(dialog.pointersDiffer());dialog.m_pointers=applied.first;
 dialog.m_pointers.reset();assert(dialog.pointersDiffer());dialog.m_pointers=applied.first;
 dialog.m_liveStartTimer->stop();dialog.m_pointerTimer->stop();
 puts("Six baked profiles retain their selected theme file, pixels, independent text colors and applied cursors. Scheme, file, shadow and None changes remain detectable.");
}
