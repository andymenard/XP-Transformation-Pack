// xplasma-probe: load a style by name and render a few widgets with it.
//
//   xplasma-probe [--style NAME] [--out FILE.png]
//   xplasma-probe --watch SECONDS
//
// --watch never sets a style: it reports the style the platform theme (KDE's,
// in a Plasma session) gave the app, then every time it is replaced -- which
// is how we find out what KDE does on a StyleChanged broadcast.
//
// Prints which Qt it runs on (compile-time vs run-time) and which style Qt
// actually instantiated, so a load failure is visible rather than a silent
// fallback. Exit code 2 when the requested style could not be created.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString styleName = QStringLiteral("XPlasma");
    QString out;
    int watch = 0;
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == QLatin1String("--style"))
            styleName = args[++i];
        else if (args[i] == QLatin1String("--out"))
            out = args[++i];
        else if (args[i] == QLatin1String("--watch"))
            watch = args[++i].toInt();
    }

    QTextStream cout(stdout);
    if (watch > 0) {
        auto describe = [] {
            QStyle *s = QApplication::style();
            return QStringLiteral("%1 (%2) @%3").arg(s->objectName(), QLatin1String(s->metaObject()->className()))
                .arg(quintptr(s), 0, 16);
        };
        QString last = describe();
        cout << "start: " << last << '\n' << Qt::flush;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, [&] {
            const QString now = describe();
            if (now != last) {
                cout << "changed: " << now << '\n' << Qt::flush;
                last = now;
            }
        });
        poll.start(50);
        QTimer::singleShot(watch * 1000, &app, &QApplication::quit);
        return app.exec();
    }

    cout << "built against Qt " << QT_VERSION_STR << ", running on Qt " << qVersion() << '\n';
    cout << "available styles: " << QStyleFactory::keys().join(QLatin1String(", ")) << '\n';
    QStyle *style = QStyleFactory::create(styleName);
    if (!style) {
        cout << "could not create style " << styleName << '\n';
        return 2;
    }
    app.setStyle(style);
    cout << "style in use: " << app.style()->objectName() << " (" << app.style()->metaObject()->className() << ")\n";

    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    layout->addWidget(new QPushButton(QStringLiteral("OK")));
    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    cancel->setEnabled(false);
    layout->addWidget(cancel);
    auto *check = new QCheckBox(QStringLiteral("Show window contents while dragging"));
    check->setChecked(true);
    layout->addWidget(check);
    auto *combo = new QComboBox;
    combo->addItem(QStringLiteral("Windows XP style"));
    layout->addWidget(combo);
    window.resize(260, 140);
    window.show();
    app.processEvents();
    if (!out.isEmpty()) {
        window.grab().save(out);
        cout << "wrote " << out << '\n';
    }
    return 0;
}
