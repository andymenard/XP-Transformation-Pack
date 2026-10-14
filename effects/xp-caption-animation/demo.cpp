// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QTimer>
int main(int argc,char **argv){
 QApplication app(argc,argv);QMainWindow window;window.setWindowTitle("XP animation test");window.resize(500,300);window.setCentralWidget(new QLabel("Caption-only animation test",&window));window.show();
 QTimer::singleShot(800,&window,[&]{window.showMaximized();});
 QTimer::singleShot(1500,&window,[&]{window.showNormal();});
 QTimer::singleShot(2200,&window,[&]{window.showMinimized();});
 QTimer::singleShot(2900,&window,[&]{window.showNormal();});
 QTimer::singleShot(3500,&window,[&]{window.showMaximized();});
 QTimer::singleShot(3600,&window,[&]{window.showNormal();});
 if(app.arguments().contains("--close-during-animation"))QTimer::singleShot(2300,&window,&QWidget::close);
 QTimer::singleShot(4200,&app,&QCoreApplication::quit);
 return app.exec();
}
