// xplasma-gallery: every control XPLasma themes, in its interesting states.
//
//   xplasma-gallery [--style NAME] [--out FILE.png]
//   xplasma-gallery --controls JOB OUTDIR [--style NAME]
//
// --controls reads uxdump's `control` lines and grabs the same Qt widget at
// the same size into OUTDIR/control_<kind>_<state>_<w>x<h>.png, for
// native/tests/controls_compare.py to hold against XP's own.
//
// The same layout as refharness's refdialog.exe (the Win32 twin, drawn by
// real XP), so the two can be compared side by side. With --out it renders
// once, offscreen-friendly, and exits.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
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
#include <QAbstractButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyleFactory>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QStatusBar>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>

static QWidget *controlFor(const QString &kind, const QString &label)
{
    const QString base = kind.section(QLatin1Char(':'), 0, 0);
    const int value = kind.section(QLatin1Char(':'), 1).toInt();
    if (base == QLatin1String("push") || base == QLatin1String("default")) {
        auto *b = new QPushButton(label);
        b->setDefault(base == QLatin1String("default"));
        return b;
    }
    if (base.startsWith(QLatin1String("check"))) {
        auto *c = new QCheckBox(label);
        c->setTristate(true);
        c->setCheckState(base == QLatin1String("check-on") ? Qt::Checked
                         : base == QLatin1String("check-mixed") ? Qt::PartiallyChecked : Qt::Unchecked);
        return c;
    }
    if (base.startsWith(QLatin1String("radio"))) {
        auto *r = new QRadioButton(label);
        r->setChecked(base == QLatin1String("radio-on"));
        return r;
    }
    if (base == QLatin1String("group"))
        return new QGroupBox(label);
    if (base == QLatin1String("edit"))
        return new QLineEdit(label);
    if (base == QLatin1String("combo") || base == QLatin1String("combo-edit")) {
        auto *c = new QComboBox;
        c->setEditable(base == QLatin1String("combo-edit"));
        c->addItem(label);
        return c;
    }
    if (base == QLatin1String("progress")) {
        auto *p = new QProgressBar;
        p->setValue(value);
        p->setTextVisible(false);   // XP's progress control has no label
        return p;
    }
    if (base == QLatin1String("trackbar")) {
        auto *t = new QSlider(Qt::Horizontal);
        t->setRange(0, 100);
        t->setTickInterval(10);
        t->setTickPosition(QSlider::TicksBelow);
        t->setValue(value);
        return t;
    }
    if (base == QLatin1String("tabs")) {
        auto *t = new QTabWidget;
        for (const char *name : {"General", "Appearance", "Advanced"})
            t->addTab(new QWidget, QString::fromLatin1(name));
        return t;
    }
    if (base == QLatin1String("listview")) {
        auto *t = new QTreeWidget;       // report-mode: a header and rows
        t->setRootIsDecorated(false);
        t->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size"), QStringLiteral("Type")});
        t->setColumnWidth(0, 100);
        t->setColumnWidth(1, 70);
        t->setColumnWidth(2, 80);
        for (int i = 0; i < 4; ++i)
            new QTreeWidgetItem(t, {QStringLiteral("File %1").arg(i)});
        return t;
    }
    if (base == QLatin1String("treeview")) {
        auto *t = new QTreeWidget;
        t->setHeaderHidden(true);
        auto *root = new QTreeWidgetItem(t, {QStringLiteral("Desktop")});
        new QTreeWidgetItem(root, {QStringLiteral("My Documents")});
        new QTreeWidgetItem(root, {QStringLiteral("My Computer")});
        root->setExpanded(true);
        return t;
    }
    if (base == QLatin1String("listbox")) {
        auto *l = new QListWidget;
        l->addItems({QStringLiteral("Ascent"), QStringLiteral("Autumn"), QStringLiteral("Azul")});
        return l;
    }
    if (base == QLatin1String("hscroll") || base == QLatin1String("vscroll")) {
        auto *s = new QScrollBar(base == QLatin1String("hscroll") ? Qt::Horizontal : Qt::Vertical);
        s->setRange(0, 91);   // XP: nMin 0, nMax 100, nPage 10
        s->setPageStep(10);
        s->setValue(30);
        return s;
    }
    if (base == QLatin1String("spin")) {
        auto *s = new QSpinBox;
        s->setRange(0, 100);
        s->setValue(42);
        return s;
    }
    if (base == QLatin1String("status")) {
        // A window's status bar: the rest of the window above it.
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        v->addStretch();
        auto *bar = new QStatusBar;
        bar->showMessage(label);
        v->addWidget(bar);
        return w;
    }
    if (base == QLatin1String("toolbar")) {
        auto *t = new QToolBar;
        t->setMovable(false);
        t->setToolButtonStyle(Qt::ToolButtonTextOnly);
        t->addAction(QStringLiteral("Back"));
        t->addAction(QStringLiteral("Forward"));
        t->addSeparator();
        QAction *folders = t->addAction(QStringLiteral("Folders"));
        folders->setCheckable(true);
        folders->setChecked(true);
        return t;
    }
    if (base == QLatin1String("menubar")) {
        auto *w = new QWidget;
        auto *bar = new QMenuBar(w);
        for (const char *name : {"File", "Edit", "View", "Help"})
            bar->addMenu(QString::fromLatin1(name));
        bar->setGeometry(0, 0, 10000, bar->sizeHint().height());
        return w;
    }
    return nullptr;
}

// uxdump's popup menu (build_menu), item for item.
static QMenu *xpMenu()
{
    auto *m = new QMenu;
    m->addAction(QStringLiteral("Open"))->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    m->addAction(QStringLiteral("Save As..."));
    m->addSeparator();
    QAction *status = m->addAction(QStringLiteral("Status Bar"));
    status->setCheckable(true);
    status->setChecked(true);
    m->addMenu(QStringLiteral("View"))->addAction(QStringLiteral("Thumbnails"));
    m->addSeparator();
    m->addAction(QStringLiteral("Print"))->setEnabled(false);
    m->addAction(QStringLiteral("Exit"));
    return m;
}

// A top-level popup (menu, tooltip) as XP shows it: at the capture's
// origin, over the host's button face.
static QImage overHost(QWidget &host, QWidget *popup)
{
    QImage image = host.grab().toImage();
    QPainter p(&image);
    p.drawPixmap(0, 0, popup->grab());
    return image;
}

static int controls(const QString &jobPath, const QString &outdir)
{
    QFile job(jobPath);
    if (!job.open(QIODevice::ReadOnly | QIODevice::Text))
        return 2;
    QDir().mkpath(outdir);
    while (!job.atEnd()) {
        const QStringList t = QString::fromUtf8(job.readLine()).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (t.size() < 5 || t.first() != QLatin1String("control"))
            continue;
        const QString kind = t.at(1), state = t.at(4), label = t.mid(5).join(QLatin1Char(' '));
        const QSize size(t.at(2).toInt(), t.at(3).toInt());
        QWidget host;
        host.setAutoFillBackground(true);
        host.setFixedSize(size);
        QString name = QStringLiteral("%1/control_%2_%3_%4x%5.png").arg(outdir, kind, state).arg(size.width()).arg(size.height());
        name.replace(QLatin1Char(':'), QLatin1Char('-'));
        if (kind == QLatin1String("menu")) {
            host.show();
            QMenu *menu = xpMenu();
            menu->popup(QPoint(400, 400));
            if (state == QLatin1String("hot"))
                menu->setActiveAction(menu->actions().first());
            QApplication::processEvents();
            overHost(host, menu).save(name);
            delete menu;
            continue;
        }
        if (kind == QLatin1String("tooltip")) {
            host.show();
            QToolTip::showText(QPoint(400, 400), label);
            QApplication::processEvents();
            for (QWidget *top : QApplication::topLevelWidgets())
                if (top->inherits("QTipLabel") && top->isVisible())
                    overHost(host, top).save(name);
            QToolTip::hideText();
            continue;
        }
        QWidget *w = controlFor(kind, label);
        if (!w)
            continue;
        w->setParent(&host);
        w->setGeometry(QRect(QPoint(0, 0), size));
        w->setEnabled(state != QLatin1String("disabled"));
        // Offscreen, the pointer sits over the widget: no hover unless asked,
        // and no focus unless asked (the first widget would otherwise take it).
        w->setAttribute(Qt::WA_Hover, false);
        if (state != QLatin1String("focused"))
            w->setFocusPolicy(Qt::NoFocus);
        host.move(400, 400);      // away from the offscreen pointer at (0, 0)
        host.show();
        if (state == QLatin1String("focused"))
            w->setFocus();
        if (state == QLatin1String("pressed"))
            if (auto *b = qobject_cast<QAbstractButton *>(w))
                b->setDown(true);
        QApplication::processEvents();
        if (state == QLatin1String("hot"))
            if (auto *bar = w->findChild<QMenuBar *>()) {
                // The pointer over the first title, as XP's HiliteMenuItem.
                const QPoint at = bar->actionGeometry(bar->actions().first()).center();
                QMouseEvent move(QEvent::MouseMove, at, bar->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(bar, &move);
                QApplication::processEvents();
            }
        host.grab().save(name);
    }
    return 0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString styleName = QStringLiteral("XPLasma"), out, controlsJob, controlsOut;
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == QLatin1String("--style"))
            styleName = args[++i];
        else if (args[i] == QLatin1String("--out"))
            out = args[++i];
        else if (args[i] == QLatin1String("--controls") && i + 2 < args.size()) {
            controlsJob = args[++i];
            controlsOut = args[++i];
        }
    }
    if (QStyle *style = QStyleFactory::create(styleName))
        app.setStyle(style);
    app.setPalette(app.style()->standardPalette());
    QFont font(QStringLiteral("Tahoma"), 8);
    if (qEnvironmentVariableIntValue("XPLASMA_XP_TEXT")) {
        // XP's "Standard" font smoothing: full hinting, no antialiasing.
        font.setHintingPreference(QFont::PreferFullHinting);
        font.setStyleStrategy(QFont::NoAntialias);
        font.setKerning(false);   // GDI's ExtTextOut doesn't kern
    }
    app.setFont(font);
    if (!controlsJob.isEmpty())
        return controls(controlsJob, controlsOut);

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
