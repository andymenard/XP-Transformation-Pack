// The Start menu's presentation (StartMenu.qml, pure Qt Quick) drawn
// offscreen from look data XPLasma generated: every part where the layout
// puts it, for a visual style and for Windows Classic.
//
//   test-startmenu [OUTDIR]   OUTDIR (or $XPLASMA_SHEETS) keeps the renders
#include "app/look.h"
#include "app/paths.h"
#include "app/startpanel.h"
#include "app/state.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

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
    QGuiApplication app(argc, argv);
    TestStartMenu test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_startmenu.moc"
