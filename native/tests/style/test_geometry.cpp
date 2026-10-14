// SPDX-License-Identifier: GPL-3.0-or-later
#include "style/xplasmastyle.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionMenuItem>
#include <QSpinBox>
#include <QStyleOptionSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <memory>

class GeometryTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString classicFile;
private Q_SLOTS:
    void initTestCase() {
        classicFile = directory.filePath(QStringLiteral("Classic.theme"));
        QFile file(classicFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Control Panel\\Colors]\nBackground=58 110 165\nWindow=255 255 255\nButtonFace=236 233 216\n");
    }
    void contextMenuUsesMenuFontAndTextOnlyIcons() {
        qputenv("XPLASMA_THEME", classicFile.toUtf8());
        XPlasmaStyle style;
        QMenu menu;
        menu.setFont(QFont(QStringLiteral("Tahoma"), 18));
        menu.setStyle(&style);
        menu.ensurePolished();
        QCOMPARE(menu.font().pointSize(), 8);
        QVERIFY(!menu.font().kerning());
        QPixmap icon(16,16); icon.fill(Qt::magenta);
        QStyleOptionMenuItem item;
        item.rect=QRect(0,0,140,17); item.font=menu.font();
        item.state=QStyle::State_Enabled;
        item.icon=QIcon(icon); item.text=QStringLiteral("Copy");
        item.checkType=QStyleOptionMenuItem::NonExclusive;
        auto render=[&] {
            QImage image(140,17,QImage::Format_ARGB32);image.fill(Qt::white);
            QPainter painter(&image);style.drawControl(QStyle::CE_MenuItem,&item,&painter,&menu);
            return image;
        };
        const QImage normal=render();
        for(int y=0;y<normal.height();++y)
            for(int x=0;x<normal.width();++x) QVERIFY(normal.pixelColor(x,y)!=QColor(Qt::magenta));
        item.checked=true;
        QVERIFY(render()!=normal); // Checkmarks survive even when actions have an icon.
        style.unpolish(&menu);
        QCOMPARE(menu.font().pointSize(),18);
    }
    void scrollbarHiDpiKeepsXpProportions() {
        const QString theme = qEnvironmentVariable("XPLASMA_TEST_THEME");
        if (theme.isEmpty()) QSKIP("Set XPLASMA_TEST_THEME to an XP visual style");
        qputenv("XPLASMA_THEME", theme.toUtf8());
        XPlasmaStyle style;
        QVERIFY(style.renderer());
        for (Qt::Orientation orientation : {Qt::Vertical, Qt::Horizontal}) {
            const QSize size = orientation == Qt::Vertical ? QSize(17,160) : QSize(160,17);
            QStyleOptionSlider option;
            option.rect = QRect(QPoint(0,0), size);
            option.orientation = orientation;
            option.state = QStyle::State_Enabled;
            option.minimum = 0; option.maximum = 100;
            option.pageStep = 40; option.sliderPosition = option.sliderValue = 50;
            option.subControls = QStyle::SC_All;
            auto render = [&](qreal dpr) {
                QImage image((QSizeF(size)*dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
                image.setDevicePixelRatio(dpr); image.fill(Qt::transparent);
                QPainter painter(&image);
                style.drawComplexControl(QStyle::CC_ScrollBar, &option, &painter, nullptr);
                painter.end(); return image;
            };
            const QImage normal = render(1);
            const QImage doubled = render(2);
            // Every original pixel (including arrows, grip and border) must
            // occupy exactly 2x2 device pixels, rather than stretching arrows.
            QImage expected = normal.scaled(size*2, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            expected.setDevicePixelRatio(2);
            QCOMPARE(doubled, expected);
            const QImage fractional = render(1.5);
            QVERIFY(!fractional.isNull());
            QCOMPARE(fractional.size(), (QSizeF(size)*1.5).toSize());
        }
    }
    void contents_data() {
        QTest::addColumn<QString>("theme");
        QTest::newRow("classic") << classicFile;
        const QString theme = qEnvironmentVariable("XPLASMA_TEST_THEME");
        if (!theme.isEmpty()) QTest::newRow("visual-style") << theme;
    }
    void contents() {
        QFETCH(QString, theme);
        qputenv("XPLASMA_THEME", theme.toUtf8());
        XPlasmaStyle style;
        QVERIFY(style.classic() || style.renderer());
        QLineEdit edit;
        for (int pointSize : {8, 12, 18}) {
            edit.setFont(QFont(QStringLiteral("Tahoma"), pointSize));
            for (auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
                edit.setLayoutDirection(direction);
                for (int height : {0, 1, 4, 6, 7, 21, 32, 60, 120}) {
                    for (int width : {0, 1, 4, 8, 9, 120}) {
                        QStyleOptionFrame opt;
                        opt.initFrom(&edit); opt.lineWidth=1; opt.rect=QRect(11, 17, width, height);
                        for (const QWidget *widget : {static_cast<QWidget *>(&edit), static_cast<QWidget *>(nullptr)}) {
                            const QRect content=style.subElementRect(QStyle::SE_LineEditContents, &opt, widget);
                            QCOMPARE(content.width(), qMax(0,width-8));
                            QCOMPARE(content.height(), qMax(0,height-6));
                            if (!content.isEmpty()) QVERIFY(opt.rect.contains(content));
                        }
                    }
                }
            }
        }
        // Native bridges derive a new outer height from these insets. It
        // must settle immediately rather than making the field grow forever.
        int height=21;
        for(int i=0;i<20;++i) {
            QStyleOptionFrame opt;opt.lineWidth=1;opt.rect=QRect(0,0,120,height);
            const QRect c=style.subElementRect(QStyle::SE_LineEditContents,&opt,&edit);
            const int next=13+(height-c.height());
            QCOMPARE(next,19);height=next;
        }
        QSpinBox spin;
        for (bool framed : {true,false}) {
            for (int h : {0,1,6,21,60,120}) {
                QStyleOptionSpinBox opt;opt.initFrom(&spin);opt.frame=framed;
                opt.buttonSymbols=QAbstractSpinBox::UpDownArrows;opt.rect=QRect(0,0,120,h);
                const QRect c=style.subControlRect(QStyle::CC_SpinBox,&opt,QStyle::SC_SpinBoxEditField,&spin);
                QCOMPARE(c.height(),qMax(0,h-(framed?6:0)));
            }
        }
        QComboBox combo;
        for(bool editable:{true,false}) {
            combo.setEditable(editable);
            for(int h:{21,60,120}) {
                QStyleOptionComboBox opt;opt.initFrom(&combo);opt.editable=editable;opt.rect=QRect(0,0,160,h);
                const QRect c=style.subControlRect(QStyle::CC_ComboBox,&opt,QStyle::SC_ComboBoxEditField,&combo);
                QVERIFY(!c.isEmpty());QVERIFY(opt.rect.contains(c));
                // Combo geometry already scales with the outer field.
                QCOMPARE(h-c.height(),6);
            }
        }
    }
    void preview() {
        const QString path=qEnvironmentVariable("XPLASMA_GEOMETRY_PREVIEW");
        if(path.isEmpty())return;
        const QString theme=qEnvironmentVariable("XPLASMA_TEST_THEME");
        qputenv("XPLASMA_THEME",(theme.isEmpty()?classicFile:theme).toUtf8());
        XPlasmaStyle style;
        QWidget window;window.setStyle(&style);window.setFont(QFont(QStringLiteral("Tahoma"),8));
        auto *layout=new QVBoxLayout(&window);
        auto *normal=new QLineEdit(QStringLiteral("Normal XP text field"));normal->setStyle(&style);layout->addWidget(normal);
        auto *tall=new QLineEdit(QStringLiteral("Resized text field"));tall->setStyle(&style);tall->setFixedHeight(60);layout->addWidget(tall);
        auto *spin=new QSpinBox;spin->setStyle(&style);spin->setFixedHeight(60);layout->addWidget(spin);
        auto *combo=new QComboBox;combo->setStyle(&style);combo->setEditable(true);combo->addItem(QStringLiteral("Editable combo"));layout->addWidget(combo);
        window.resize(320,240);window.show();QTest::qWait(100);QVERIFY(window.grab().save(path));
    }
};
QTEST_MAIN(GeometryTest)
#include "test_geometry.moc"
