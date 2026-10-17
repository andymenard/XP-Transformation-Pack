#include "app/editor/displayproperties.h"
#include "app/library.h"
#include "app/state.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QTabWidget>
#include <QTextStream>
#include <QDir>
#include <QFile>
#include <cassert>
int main(int argc,char**argv){QApplication app(argc,argv); xpl::editor::DisplayProperties dialog;
auto *styles=dialog.findChild<QComboBox*>("appearanceStyle");auto *colors=dialog.findChild<QComboBox*>("appearanceColor");auto *tabs=dialog.findChild<QTabWidget*>();QCheckBox *enabled=nullptr;for(auto*c:dialog.findChildren<QCheckBox*>())if(c->text()=="Use custom theme colors")enabled=c;assert(styles&&colors&&enabled);int checked=0;
for(int i=0;i<styles->count();++i){
 if(styles->itemData(i).toString().contains("/custom-luna/"))continue;
 tabs->setCurrentIndex(2);styles->setCurrentIndex(i);QMetaObject::invokeMethod(styles,"activated",Qt::DirectConnection,Q_ARG(int,i));
 const int variants=colors->count();for(int j=0;j<variants;++j){
  if(i==styles->count()-1 && j>1)break;
  colors->setCurrentIndex(j);QMetaObject::invokeMethod(colors,"activated",Qt::DirectConnection,Q_ARG(int,j));tabs->setCurrentIndex(3);
  const auto original=dialog.look();enabled->setChecked(true);const auto look=dialog.look();
  if(original.classic()){assert(look.classic()&&look.system.shellColors);assert(look.system.shellBaseName==original.system.name);QString error;assert(xpl::schemes::save(look.system,nullptr,&error));auto loaded=xpl::schemes::load(look.system.name);assert(loaded&&loaded->shellColors==look.system.shellColors);
 xpl::State state;state.appliedKind="classic";state.scheme=look.system.name;assert(state.save());
 xpl::editor::DisplayProperties reopened;assert(reopened.look().system.shellColors==look.system.shellColors);
 }
  else {assert(look.stylePath.contains("/custom-luna/"));assert(look.variant==original.variant);QFile f(look.stylePath+".json");assert(f.exists());}
  ++checked;enabled->setChecked(false);const auto reset=dialog.look();assert(reset.classic()==original.classic());if(!original.classic()){assert(reset.stylePath==original.stylePath);assert(reset.variant==original.variant);}tabs->setCurrentIndex(2);
 }
}
QTextStream(stdout)<<"UI checked "<<checked<<" base selections, generation, variants and reset, including Classic persistence.\n";
}
