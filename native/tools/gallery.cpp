// xplasma-gallery: every control XPLasma themes, in its interesting states.
//
//   xplasma-gallery [--style NAME] [--out FILE.png]
//
// The same layout as refharness's refdialog.exe (the Win32 twin, drawn by
// real XP), so the two can be compared side by side. With --out it renders
// once, offscreen-friendly, and exits.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyleFactory>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString styleName = QStringLiteral("XPLasma"), out;
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == QLatin1String("--style"))
            styleName = args[++i];
        else if (args[i] == QLatin1String("--out"))
            out = args[++i];
    }
    if (QStyle *style = QStyleFactory::create(styleName))
        app.setStyle(style);
    app.setPalette(app.style()->standardPalette());
    app.setFont(QFont(QStringLiteral("Tahoma"), 8));

    QWidget window;
    window.setWindowTitle(QStringLiteral("XPLasma gallery"));
    auto *root = new QVBoxLayout(&window);

    auto *toolbar = new QToolBar;
    for (const char *name : {"Back", "Forward", "Up"})
        toolbar->addAction(QString::fromLatin1(name));
    toolbar->actions().at(1)->setEnabled(false);
    root->addWidget(toolbar);

    auto *tabs = new QTabWidget;
    root->addWidget(tabs);
    auto *page = new QWidget;
    tabs->addTab(page, QStringLiteral("General"));
    tabs->addTab(new QWidget, QStringLiteral("Appearance"));
    tabs->addTab(new QWidget, QStringLiteral("Advanced"));
    auto *grid = new QGridLayout(page);

    auto *buttons = new QGroupBox(QStringLiteral("Buttons"));
    auto *bl = new QVBoxLayout(buttons);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    ok->setDefault(true);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    auto *off = new QPushButton(QStringLiteral("Apply"));
    off->setEnabled(false);
    for (QWidget *w : {static_cast<QWidget *>(ok), static_cast<QWidget *>(cancel), static_cast<QWidget *>(off)})
        bl->addWidget(w);
    grid->addWidget(buttons, 0, 0);

    auto *checks = new QGroupBox(QStringLiteral("Options"));
    auto *cl = new QVBoxLayout(checks);
    auto *c1 = new QCheckBox(QStringLiteral("Show window contents"));
    c1->setChecked(true);
    auto *c2 = new QCheckBox(QStringLiteral("Use drop shadows"));
    auto *c3 = new QCheckBox(QStringLiteral("Partly"));
    c3->setTristate(true);
    c3->setCheckState(Qt::PartiallyChecked);
    auto *c4 = new QCheckBox(QStringLiteral("Unavailable"));
    c4->setEnabled(false);
    c4->setChecked(true);
    auto *r1 = new QRadioButton(QStringLiteral("Center"));
    r1->setChecked(true);
    auto *r2 = new QRadioButton(QStringLiteral("Tile"));
    for (QWidget *w : std::initializer_list<QWidget *>{c1, c2, c3, c4, r1, r2})
        cl->addWidget(w);
    grid->addWidget(checks, 0, 1);

    auto *fields = new QGroupBox(QStringLiteral("Fields"));
    auto *fl = new QVBoxLayout(fields);
    auto *combo = new QComboBox;
    combo->addItems({QStringLiteral("Windows XP style"), QStringLiteral("Windows Classic style")});
    auto *editable = new QComboBox;
    editable->setEditable(true);
    editable->addItem(QStringLiteral("Tahoma"));
    auto *edit = new QLineEdit(QStringLiteral("Text"));
    auto *spin = new QSpinBox;
    spin->setValue(8);
    for (QWidget *w : std::initializer_list<QWidget *>{combo, editable, edit, spin})
        fl->addWidget(w);
    grid->addWidget(fields, 1, 0);

    auto *ranges = new QGroupBox(QStringLiteral("Ranges"));
    auto *rl = new QVBoxLayout(ranges);
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setValue(40);
    auto *progress = new QProgressBar;
    progress->setValue(60);
    auto *hbar = new QScrollBar(Qt::Horizontal);
    hbar->setRange(0, 100);
    hbar->setValue(30);
    for (QWidget *w : std::initializer_list<QWidget *>{slider, progress, hbar})
        rl->addWidget(w);
    grid->addWidget(ranges, 1, 1);

    auto *tree = new QTreeWidget;
    tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size"), QStringLiteral("Type")});
    for (int i = 0; i < 12; ++i)
        new QTreeWidgetItem(tree, {QStringLiteral("File %1").arg(i), QStringLiteral("%1 KB").arg(i * 7),
                                   QStringLiteral("Document")});
    tree->setFixedHeight(90);
    grid->addWidget(tree, 2, 0, 1, 2);

    window.resize(460, 440);
    window.show();
    if (!out.isEmpty()) {
        app.processEvents();
        return window.grab().save(out) ? 0 : 1;
    }
    return app.exec();
}
