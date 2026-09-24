// The Start menu's presentation (StartMenu.qml, pure Qt Quick) drawn
// offscreen from look data XPLasma generated: every part where the layout
// puts it, for a visual style and for Windows Classic.
//
//   test-startmenu [OUTDIR]   OUTDIR (or $XPLASMA_SHEETS) keeps the renders
#include "app/look.h"
#include "app/overrides.h"
#include "app/paths.h"
#include "app/startpanel.h"
#include "app/state.h"

#include <QDir>
#include <QApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace xpl;

class TestStartMenu : public QObject
{
    Q_OBJECT

    QString m_themes, m_iso, m_out;
    QTemporaryDir m_scratch;

    QImage render(const QJsonObject &theme, QObject **menuOut = nullptr, const QVariantMap &extra = {})
    {
        auto *engine = new QQmlEngine(this);
        // (The source tree, from build/<mode>-qt6/tests/app.)
        const QString qml = QCoreApplication::applicationDirPath() + QLatin1String("/../../../../src/app/start-menu/contents/ui/StartMenu.qml");
        QQmlComponent component(engine, QUrl::fromLocalFile(QFileInfo(qml).absoluteFilePath()));
        QVariantMap props{{QStringLiteral("themeData"), theme.toVariantMap()},
                          {QStringLiteral("userName"), QStringLiteral("sock")},
                          {QStringLiteral("pinnedItems"), QVariantList{QVariantMap{{QStringLiteral("label"), QStringLiteral("Internet")},
                                                                                   {QStringLiteral("description"), QStringLiteral("Internet Explorer")},
                                                                                   {QStringLiteral("action"), QStringLiteral("close")}}}},
                          {QStringLiteral("recentItems"), QVariantList{QVariantMap{{QStringLiteral("label"), QStringLiteral("Minesweeper")},
                                                                                   {QStringLiteral("action"), QStringLiteral("close")}}}},
                          {QStringLiteral("placeGroups"), QVariantList{QVariantList{QVariantMap{{QStringLiteral("label"), QStringLiteral("My Documents")},
                                                                                                {QStringLiteral("action"), QStringLiteral("close")}}}}}};
        props.insert(extra);
        QObject *object = component.createWithInitialProperties(props);
        if (!object) {
            qWarning() << component.errors();
            return QImage();
        }
        auto *item = qobject_cast<QQuickItem *>(object);
        auto *window = new QQuickWindow;
        window->setColor(Qt::magenta);
        item->setParentItem(window->contentItem());
        window->resize(int(item->width()), int(item->height()));
        window->show();
        QTest::qWait(200);
        const QImage image = window->grabWindow();
        if (menuOut)
            *menuOut = object;
        return image;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_out = qEnvironmentVariable("XPLASMA_SHEETS");
        if (QFileInfo::exists(paths::themes() + QLatin1String("/Luna/luna.msstyles")))
            m_themes = QFileInfo(paths::themes()).canonicalFilePath();
        m_iso = State::load().media;
        qputenv("HOME", QFile::encodeName(m_scratch.path()));
        if (!m_iso.isEmpty()) {
            State s;
            s.media = m_iso;
            s.save();
        }
    }

    void luna()
    {
        if (m_themes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_themes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(look);
        QString error;
        const QJsonObject theme = writeStartPanel(*look, m_scratch.filePath(QStringLiteral("luna")), &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        QObject *menu = nullptr;
        const QImage image = render(theme, &menu);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(380, 478));
        // The header is the user pane's art, from the top-left.
        const QImage userpane(m_scratch.filePath(QStringLiteral("luna/userpane.png")));
        if (!m_out.isEmpty())
            image.save(m_out + QLatin1String("/startmenu-luna.png"));
        QCOMPARE(image.pixel(3, 40), userpane.pixel(3, 40));   // (in its fixed left margin)
        QVERIFY(menu->findChild<QQuickItem *>(QStringLiteral("footer")));
        if (!m_out.isEmpty())
            image.save(m_out + QLatin1String("/startmenu-luna.png"));
    }

    void everyFileCompiles()
    {
        // Every QML file of the package parses (and, where its imports are
        // here, compiles). Some modules exist only inside Plasma's shell
        // (org.kde.plasma.plasmoid): a file that needs one can't be
        // compiled here, but Qt parses it before resolving imports, so a
        // syntax error still shows.
        const QDir ui(QCoreApplication::applicationDirPath() + QLatin1String("/../../../../src/app/start-menu/contents/ui"));
        QQmlEngine engine;
        const QStringList files = ui.entryList({QStringLiteral("*.qml")});
        QVERIFY(files.size() >= 7);
        for (const QString &file : files) {
            QQmlComponent component(&engine, QUrl::fromLocalFile(ui.absoluteFilePath(file)));
            QStringList wrong;
            for (const QQmlError &e : component.errors())
                if (!e.description().contains(QLatin1String("is not installed")) && !e.description().endsWith(QLatin1String("unavailable")))
                    wrong << e.toString();
            QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QLatin1Char('\n'))));
        }
    }

    void balloonsCompile()
    {
        // XPLasma's QML inside KDE's notifications and tray, installed as
        // Plasma would load it, compiles (as far as the imports here reach).
        if (!QFileInfo::exists(QStringLiteral("/usr/share/plasma/plasmoids/org.kde.plasma.notifications")))
            QSKIP("no Plasma here");
        const QString plasmoids = m_scratch.filePath(QStringLiteral("plasmoids"));
        QString error;
        QVERIFY2(xpl::overrides::installPanelWidgets(plasmoids, QStringLiteral("/bin/true"), &error), qPrintable(error));
        QVERIFY2(xpl::overrides::installDesktopMenu(plasmoids, QStringLiteral("/bin/true"), &error), qPrintable(error));
        QQmlEngine engine;
        for (const char *file : {"org.kde.plasma.notifications/contents/ui/XpButton.qml",
                                 "org.kde.plasma.notifications/contents/ui/XpBalloon.qml",
                                 "org.kde.plasma.notifications/contents/ui/XpCopyDialog.qml",
                                 "org.kde.plasma.notifications/contents/ui/XpPopup.qml",
                                 "org.kde.plasma.notifications/contents/ui/global/Globals.qml",
                                 "org.kde.plasma.notifications/contents/ui/main.qml",
                                 "org.kde.plasma.systemtray/contents/ui/XpNotificationIcons.qml",
                                 "org.kde.plasma.systemtray/contents/ui/main.qml",
                                 "org.kde.desktopcontainment/contents/ui/FolderView.qml"}) {
            QQmlComponent component(&engine, QUrl::fromLocalFile(plasmoids + QLatin1Char('/') + QLatin1String(file)));
            QStringList wrong;
            for (const QQmlError &e : component.errors())
                if (!e.description().contains(QLatin1String("is not installed")) && !e.description().endsWith(QLatin1String("unavailable")))
                    wrong << e.toString();
            QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QLatin1Char('\n'))));
        }
    }

    void rightClickAgainAndAgain()
    {
        // The right-click menu opens every time, not only the first: its
        // previous items belong to Plasma's menu, which deletes them itself.
        const QString qml = QCoreApplication::applicationDirPath() + QLatin1String("/../../../../src/app/start-menu/contents/ui/ActionMenu.qml");
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(qml).absoluteFilePath()));
        if (component.isError() && component.errorString().contains(QLatin1String("is not installed")))
            QSKIP("KDE's QML modules aren't here");
        std::unique_ptr<QObject> menu(component.create());
        QVERIFY2(menu, qPrintable(component.errorString()));
        QQuickWindow window;
        qobject_cast<QQuickItem *>(menu.get())->setParentItem(window.contentItem());
        window.resize(100, 100);
        window.show();
        static QStringList warnings;
        warnings.clear();
        const auto previous = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &message) {
            if (type != QtDebugMsg)
                warnings << message;
        });
        const QVariantList entries{QVariantMap{{QStringLiteral("text"), QStringLiteral("Open")}, {QStringLiteral("actionId"), QStringLiteral("_xplasma_open")}},
                                   QVariantMap{{QStringLiteral("type"), QStringLiteral("separator")}},
                                   QVariantMap{{QStringLiteral("text"), QStringLiteral("Pin to Start menu")}}};
        for (int i = 0; i < 3; ++i) {
            QVERIFY(QMetaObject::invokeMethod(menu.get(), "show", Q_ARG(QVariant, entries), Q_ARG(QVariant, QVariant::fromValue(menu.get())),
                                              Q_ARG(QVariant, 5), Q_ARG(QVariant, 5)));
            QTRY_VERIFY2(menu->property("opened").toBool(), qPrintable(QStringLiteral("round %1: %2").arg(i).arg(warnings.join(QLatin1Char('\n')))));
        }
        qInstallMessageHandler(previous);
        QVERIFY2(warnings.filter(QLatin1String("ActionMenu")).isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void dragToRearrange()
    {
        // XP's program list: a pinned program dropped at another place moves
        // there; a recent one dropped among the pinned is pinned there, and
        // among the recent ones nothing happens. (The drag itself is a
        // system drag: it carries the program's id and launcher.)
        if (m_themes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_themes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(look);
        QString error;
        const QJsonObject theme = writeStartPanel(*look, m_scratch.filePath(QStringLiteral("luna-drag")), &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        auto items = [](const char *name, int n) {
            QVariantList list;
            for (int i = 0; i < n; ++i) {
                const QString id = QStringLiteral("%1-%2.desktop").arg(QLatin1String(name)).arg(i);
                list << QVariantMap{{QStringLiteral("label"), id}, {QStringLiteral("identity"), id},
                                    {QStringLiteral("favoriteId"), id}, {QStringLiteral("action"), QStringLiteral("close")}};
            }
            return list;
        };
        QObject *menu = nullptr;
        render(theme, &menu, {{QStringLiteral("pinnedItems"), items("pinned", 3)}, {QStringLiteral("recentItems"), items("recent", 2)}});
        auto *list = menu->findChild<QQuickItem *>(QStringLiteral("programEntries"));
        QVERIFY(list);
        QQuickWindow *window = list->window();
        const QJsonObject provisional = theme.value(QStringLiteral("provisional")).toObject();
        const qreal pinnedRow = provisional.value(QStringLiteral("pinnedRowHeight")).toDouble();
        const qreal recentRow = provisional.value(QStringLiteral("recentRowHeight")).toDouble();
        const qreal separator = menu->property("separatorHeight").toReal();
        QSignalSpy spy(menu, SIGNAL(pinIdRequested(QString,int)));
        auto drop = [&](const QString &id, qreal y) {
            QMimeData data;
            data.setData(QStringLiteral("application/x-xplasma-favorite"), id.toUtf8());
            const QPoint at = list->mapToScene(QPointF(40, y)).toPoint();
            QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            QDragMoveEvent move(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &move);
            const int slot = menu->property("dropSlot").toInt();
            QDropEvent dropped(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &dropped);
            return slot;
        };
        QCOMPARE(drop(QStringLiteral("pinned-0.desktop"), pinnedRow * 2.9), 3);   // the first, below the last
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("pinned-0.desktop"));
        QCOMPARE(spy.at(0).at(1).toInt(), 3);
        QCOMPARE(drop(QStringLiteral("recent-0.desktop"), pinnedRow * 0.2), 0);   // a recent one, to the top
        QCOMPARE(spy.size(), 2);
        QCOMPARE(spy.at(1).at(1).toInt(), 0);
        QCOMPARE(drop(QStringLiteral("recent-0.desktop"), pinnedRow * 3 + separator + recentRow * 1.5), -1);   // among the recent
        QCOMPARE(spy.size(), 2);
        QCOMPARE(drop(QStringLiteral("pinned-1.desktop"), pinnedRow * 3 + separator + recentRow * 1.5), 3);   // pinned, dropped low: the end
    }

    void dropFromAllPrograms()
    {
        // A program dragged out of All Programs (another window) and dropped
        // on the program list is pinned where it lands.
        if (m_themes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_themes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(look);
        QString error;
        const QJsonObject theme = writeStartPanel(*look, m_scratch.filePath(QStringLiteral("luna-drop")), &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        QVariantList pinned;
        for (int i = 0; i < 3; ++i)
            pinned << QVariantMap{{QStringLiteral("label"), QStringLiteral("pinned %1").arg(i)}, {QStringLiteral("action"), QStringLiteral("close")}};
        QObject *menu = nullptr;
        render(theme, &menu, {{QStringLiteral("pinnedItems"), pinned}});
        auto *list = menu->findChild<QQuickItem *>(QStringLiteral("programEntries"));
        QVERIFY(list);
        QQuickWindow *window = list->window();
        const qreal row = theme.value(QStringLiteral("provisional")).toObject().value(QStringLiteral("pinnedRowHeight")).toDouble();
        QSignalSpy spy(menu, SIGNAL(pinIdRequested(QString,int)));
        QMimeData data;
        data.setData(QStringLiteral("application/x-xplasma-favorite"), "org.kde.konsole.desktop");
        const QPointF over = list->mapToScene(QPointF(40, row * 1.1));   // between the first and second
        QDragEnterEvent enter(over.toPoint(), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QDragMoveEvent move(over.toPoint(), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &move);
        QCOMPARE(menu->property("dropSlot").toInt(), 1);
        QDropEvent drop(over, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("org.kde.konsole.desktop"));
        QCOMPARE(spy.at(0).at(1).toInt(), 1);
        QCOMPARE(menu->property("dropSlot").toInt(), -1);
    }

    void grows()
    {
        // XP's way with a long list: the menu gets taller, and only when the
        // screen runs out do recent programs go, from the bottom.
        if (m_themes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_themes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(look);
        QString error;
        const QJsonObject theme = writeStartPanel(*look, m_scratch.filePath(QStringLiteral("luna-tall")), &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        auto items = [](const char *name, int n) {
            QVariantList list;
            for (int i = 0; i < n; ++i)
                list << QVariantMap{{QStringLiteral("label"), QStringLiteral("%1 %2").arg(QLatin1String(name)).arg(i)},
                                    {QStringLiteral("action"), QStringLiteral("close")}};
            return list;
        };
        const QVariantMap many{{QStringLiteral("pinnedItems"), items("Pinned", 8)}, {QStringLiteral("recentItems"), items("Recent", 6)}};
        QObject *menu = nullptr;
        const QImage tall = render(theme, &menu, many);
        QVERIFY(tall.height() > 478);
        QCOMPARE(menu->property("recentShown").toInt(), 6);
        const auto *scroll = menu->findChild<QQuickItem *>(QStringLiteral("programScroll"));
        QVERIFY(scroll);
        QVERIFY(scroll->property("contentHeight").toReal() <= scroll->height());   // all of it shown, no scrolling
        if (!m_out.isEmpty())
            tall.save(m_out + QLatin1String("/startmenu-tall.png"));

        QVariantMap cramped = many;
        cramped.insert(QStringLiteral("maximumHeight"), tall.height() - 100);
        const QImage fitted = render(theme, &menu, cramped);
        QVERIFY(fitted.height() <= tall.height() - 100);
        QVERIFY(menu->property("recentShown").toInt() < 6);
        // Few items: never shorter than XP's measured menu.
        QCOMPARE(render(theme).height(), 478);
    }

    void classic()
    {
        if (m_iso.isEmpty())
            QSKIP("no XP media on this machine (the footer's buttons come from it)");
        const Look look = Look::fromScheme(ClassicScheme());
        QString error;
        const QJsonObject theme = writeStartPanel(look, m_scratch.filePath(QStringLiteral("classic")), &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        const QImage image = render(theme);
        QVERIFY(!image.isNull());
        // Windows Standard's frame: Light3D, then the highlight, round it all;
        // the header's gradient from the active caption colour 3px in.
        QCOMPARE(QColor(image.pixel(0, 100)), QColor(212, 208, 200));
        QCOMPARE(QColor(image.pixel(1, 100)), QColor(255, 255, 255));
        QCOMPARE(QColor(image.pixel(379, 100)), QColor(64, 64, 64));
        QCOMPARE(QColor(image.pixel(3, 20)), QColor(10, 36, 106));
        // The etched line between the columns.
        QCOMPARE(QColor(image.pixel(3 + 190, 200)), QColor(128, 128, 128));
        QCOMPARE(QColor(image.pixel(3 + 191, 200)), QColor(255, 255, 255));
        if (!m_out.isEmpty())
            image.save(m_out + QLatin1String("/startmenu-classic.png"));
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // (A QApplication: the right-click menu is a real QMenu.)
    QApplication app(argc, argv);
    TestStartMenu test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_startmenu.moc"
