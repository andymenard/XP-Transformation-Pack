// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/displayproperties.h"
#include "app/editor/advanced.h"
#include "app/paths.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QTest>
#include <cassert>
#include <cstdio>
int main(int argc,char **argv){QApplication app(argc,argv);xpl::editor::DisplayProperties dialog;
 const QString before=dialog.look().stylePath;const QStringList files=QDir(xpl::paths::data()+"/custom-luna").entryList(QDir::Files);
 auto live=dialog.findChild<QCheckBox*>("liveStartColors");auto link=dialog.findChild<QCheckBox*>("linkTitleToTaskbar");assert(live&&link);
 live->setChecked(true);link->setChecked(true);
 auto task=dialog.findChild<xpl::editor::ColourButton*>("customColor1");auto title=dialog.findChild<xpl::editor::ColourButton*>("customColor2");assert(task&&title);
 task->setColour(QColor("#315c43"));QMetaObject::invokeMethod(task,"picked",Qt::DirectConnection);
 assert(task->colour()==title->colour());assert(!title->isEnabled());
 link->setChecked(false);assert(title->isEnabled());
 title->setColour(QColor("#644576"));QMetaObject::invokeMethod(title,"picked",Qt::DirectConnection);
 task->setColour(QColor("#804030"));QMetaObject::invokeMethod(task,"picked",Qt::DirectConnection);
 assert(title->colour()==QColor("#644576"));
 link->setChecked(true);assert(title->colour()==task->colour());
 auto text=dialog.findChild<QComboBox*>("customTextMode2");assert(text->isEnabled());text->setCurrentIndex(2);
 QTest::qWait(100);assert(dialog.look().stylePath==before);assert(QDir(xpl::paths::data()+"/custom-luna").entryList(QDir::Files)==files);
 assert(dialog.saveScreenshots("/tmp/linked-title-ui-shots"));dialog.reject();puts("Linked taskbar/titlebar colors, independent opt-out, and separate text controls passed; no artwork generation.");}
