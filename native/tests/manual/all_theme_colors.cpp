#include "app/editor/customluna.h"
#include "core/theme.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QTextStream>
int main(int argc,char**argv){QCoreApplication app(argc,argv);QTemporaryDir dir;QDirIterator it(argv[1],{"*.msstyles"},QDir::Files,QDirIterator::Subdirectories);int total=0,failed=0;while(it.hasNext()){QString source=it.next();if(source.contains("/Custom/")||source.contains("/__MACOSX/"))continue;auto t=xpl::Theme::load(source);if(!t)continue;for(const auto &variant:t->variants().iniNames){if(variant=="THEMES_INI")continue;QString error;auto out=xpl::editor::customLuna(source,{QColor("#123456"),QColor("#8a257a"),QColor("#409b52"),QColor("#ad7a10")},dir.path(),&error,variant);++total;if(out.isEmpty()){++failed;QTextStream(stdout)<<source<<":"<<variant<<" FAIL "<<error<<"\n";}else{auto result=xpl::Theme::load(out,variant);if(!result){++failed;QTextStream(stdout)<<"parse fail\n";}}}}QTextStream(stdout)<<"Checked "<<total<<" variants, failed "<<failed<<"\n";return failed>0;}
