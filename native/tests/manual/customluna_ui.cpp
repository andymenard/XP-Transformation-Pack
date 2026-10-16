#include "app/editor/displayproperties.h"
#include "app/editor/advanced.h"
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTabWidget>
#include <QFile>
#include <cassert>
int main(int argc, char **argv) {
 QApplication app(argc,argv);
 xpl::editor::DisplayProperties dialog;
 auto *tabs=dialog.findChild<QTabWidget*>(); assert(tabs && tabs->count()==7);
 QCheckBox *enabled=nullptr;
 for(auto *c:dialog.findChildren<QCheckBox*>()) if(c->text()=="Use Custom Luna colors") enabled=c;
 assert(enabled && enabled->isChecked());
 auto path=dialog.look().stylePath; assert(path.contains("/custom-luna/"));
 QPushButton *reset=nullptr;
 for(auto *b:dialog.findChildren<QPushButton*>()) if(b->text()=="Reset to original Luna") reset=b;
 assert(reset); reset->click(); assert(!enabled->isChecked());
 assert(dialog.look().variant=="NORMALBLUE_INI"); assert(!dialog.look().stylePath.contains("/custom-luna/"));
 enabled->setChecked(true); assert(dialog.look().stylePath.contains("/custom-luna/"));
 auto *page=tabs->widget(3);
 auto colours=page->findChildren<xpl::editor::ColourButton*>(); assert(colours.size()==4);
 colours[0]->setColour(QColor("#ff00aa")); QMetaObject::invokeMethod(colours[0],"picked",Qt::DirectConnection);
 assert(dialog.look().stylePath!=path); assert(QFile::exists(dialog.look().stylePath));
 dialog.reject();
}
