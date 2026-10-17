#include "app/editor/displayproperties.h"
#include "app/state.h"
#include <QApplication>
#include <QComboBox>
#include <QTextStream>
#include <cassert>
int main(int argc,char**argv){QApplication app(argc,argv);xpl::editor::DisplayProperties dialog;int values[4]={1,2,1,2};for(int i=0;i<4;++i){auto*mode=dialog.findChild<QComboBox*>(QString("customTextMode%1").arg(i));assert(mode&&mode->count()==3);mode->setCurrentIndex(values[i]);QMetaObject::invokeMethod(mode,"activated",Qt::DirectConnection,Q_ARG(int,values[i]));}auto look=dialog.look();assert(!look.classic());assert(look.system.colour(u"TitleText")==QColor(Qt::white));assert(look.system.colour(u"HilightText")==QColor(Qt::black));auto state=xpl::State::load();state.appliedKind="style";state.style=look.stylePath;state.variant=look.variant;assert(state.save());xpl::editor::DisplayProperties reopened;for(int i=0;i<4;++i){auto*restored=reopened.findChild<QComboBox*>(QString("customTextMode%1").arg(i));assert(restored&&restored->currentIndex()==values[i]);}assert(reopened.look().stylePath==look.stylePath);assert(reopened.saveScreenshots("/tmp/per-text-preview"));QTextStream(stdout)<<"Four independent row selections and reopening passed.\n";}
