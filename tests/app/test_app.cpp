// QtTest for the app library: archives, host programs, state, adoption.
//
// Every test runs with HOME moved to a scratch directory. The XP install ISO,
// when xp2plasma or XPLasma downloaded one on this machine, is used for the
// real-media tests (skipped otherwise).
#include "app/adopt.h"
#include "app/apply.h"
#include "app/controls.h"
#include "app/overrides.h"
#include "app/session.h"
#include "app/setup.h"
#include "app/decoration.h"
#include "app/look.h"
#include "app/icons.h"
#include "app/media.h"
#include "app/inf.h"
#include "app/panel.h"
#include "app/flatpak.h"
#include "app/forks.h"
#include "app/gtk.h"
#include "app/install.h"
#include "app/pointers.h"
#include "app/sounds.h"
#include "app/timezones.h"
#include "app/startbutton.h"
#include "app/startmenu.h"
#include "app/startpanel.h"
#include "app/archive.h"
#include "app/hivedef.h"
#include "app/host.h"
#include "app/library.h"
#include "app/paths.h"
#include "app/state.h"
#include "app/xptext.h"
#include "core/themefile.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QApplication>
#include <QGuiApplication>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QProcess>
#include <QTest>
#include <QTimeZone>
#include <QtEndian>

#include <archive.h>
#include <archive_entry.h>

#include <thread>

using namespace xpl;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// An archive written with libarchive: `format` is "zip" or "tar.gz".
bool writeArchive(const QString &path, const QString &format, const QList<std::pair<QByteArray, QByteArray>> &members)
{
    ::archive *a = archive_write_new();
    if (format == QLatin1String("zip")) {
        archive_write_set_format_zip(a);
    } else {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_gzip(a);
    }
    if (archive_write_open_filename(a, QFile::encodeName(path).constData()) != ARCHIVE_OK)
        return false;
    for (const auto &[name, data] : members) {
        archive_entry *e = archive_entry_new();
        archive_entry_set_pathname(e, name.constData());
        archive_entry_set_size(e, data.size());
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_write_header(a, e);
        archive_write_data(a, data.constData(), size_t(data.size()));
        archive_entry_free(e);
    }
    archive_write_close(a);
    archive_write_free(a);
    return true;
}

// A minimal ISO 9660 image: sector 16 the primary volume descriptor, 17 the
// terminator, 18 the root directory, 19 "I386/", then file data.
struct IsoBuilder
{
    static constexpr int kSector = 2048;
    QByteArray image = QByteArray(22 * kSector, '\0');

    static QByteArray record(quint32 lba, quint32 size, bool dir, const QByteArray &name, quint8 flags = 0)
    {
        QByteArray r(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0), '\0');
        r[0] = char(r.size());
        qToLittleEndian<quint32>(lba, r.data() + 2);
        qToBigEndian<quint32>(lba, r.data() + 6);
        qToLittleEndian<quint32>(size, r.data() + 10);
        qToBigEndian<quint32>(size, r.data() + 14);
        r[25] = char((dir ? 0x02 : 0) | flags);
        r[32] = char(name.size());
        r.replace(33, name.size(), name);
        return r;
    }
    void put(int sector, const QByteArray &bytes) { image.replace(sector * kSector, bytes.size(), bytes); }

    IsoBuilder(bool loop = false)
    {
        QByteArray pvd(kSector, '\0');
        pvd[0] = 1;
        pvd.replace(1, 5, "CD001");
        pvd[6] = 1;
        pvd.replace(156, 34, record(18, kSector, true, QByteArray(1, '\0')));
        put(16, pvd);
        QByteArray term(kSector, '\0');
        term[0] = char(255);
        term.replace(1, 5, "CD001");
        put(17, term);
        // The root: ".", "..", HELLO.TXT;1, I386/ -- 564 bytes, as XP's own is.
        put(18, record(18, kSector, true, QByteArray(1, '\0')) + record(18, kSector, true, QByteArray(1, '\1'))
                    + record(20, 5, false, "HELLO.TXT;1") + record(19, kSector, true, "I386"));
        QByteArray i386 = record(19, kSector, true, QByteArray(1, '\0')) + record(18, kSector, true, QByteArray(1, '\1'))
                          // a multi-extent file: two records, the first flagged 0x80
                          + record(21, 3, false, "SPLIT.BIN;1", 0x80) + record(21, 3, false, "SPLIT.BIN;1")
                          + record(20, 5, false, "README.;1");
        if (loop)   // a directory claiming to be the root again
            i386 += record(18, kSector, true, "LOOP");
        put(19, i386);
        put(20, "hello");
        put(21, "abc");
    }
};

// A session that only records: what apply would run, script and signal.
class RecordingSession : public Session
{
public:
    QList<QStringList> runs;
    QStringList scripts;
    QStringList calls;
    QHash<QString, QString> config;   // "file|group/group|key" -> value
    QString layoutReply = QStringLiteral("{\"log\":[\"laid out\"],\"panels\":[{\"panel\":1,\"order\":[3,4,5]}]}");
    bool systemd = true;   // (the session's plasmashell a systemd user unit)
    QList<QStringList> detached;

    host::Result run(const QStringList &argv) override
    {
        runs << argv;
        host::Result r;
        r.exitCode = 0;
        if (argv.value(0) == QLatin1String("systemctl") && argv.contains(QStringLiteral("LoadState")))
            r.out = systemd ? "loaded\n" : "not-found\n";
        if (argv.value(0) == QLatin1String("kreadconfig6") || argv.value(0) == QLatin1String("kwriteconfig6")) {
            const int file = int(argv.indexOf(QStringLiteral("--file"))), key = int(argv.indexOf(QStringLiteral("--key")));
            QStringList groups;
            for (int i = 0; i < argv.size() - 1; ++i)
                if (argv.at(i) == QLatin1String("--group"))
                    groups << argv.at(i + 1);
            const QString id = argv.value(file + 1) + QLatin1Char('|') + groups.join(QLatin1Char('/')) + QLatin1Char('|') + argv.value(key + 1);
            if (argv.value(0) == QLatin1String("kreadconfig6"))
                r.out = config.value(id).toUtf8();
            else if (argv.contains(QStringLiteral("--delete")))
                config.remove(id);
            else
                config.insert(id, argv.value(key + 2));
        }
        return r;
    }
    std::optional<QString> script(const QString &js, QString *) override
    {
        scripts << js;
        if (js.contains(QLatin1String("print('up')")))
            return QStringLiteral("up");
        if (js.contains(QLatin1String("LAUNCHER_KEYS")))
            return layoutReply;
        return QString();
    }
    bool call(const QString &, const QString &path, const QString &, const QString &method, const QVariantList &) override
    {
        calls << path + QLatin1Char(' ') + method;
        return true;
    }
    void signal(const QString &path, const QString &, const QString &name, const QVariantList &) override
    {
        calls << path + QLatin1Char(' ') + name;
    }
    bool startDetached(const QStringList &argv, const QStringList & = {}) override
    {
        detached << argv;
        return true;
    }
    bool ran(const QString &program) const
    {
        return std::any_of(runs.begin(), runs.end(), [&](const QStringList &a) { return a.value(0) == program; });
    }
};

} // namespace

class TestApp : public QObject
{
    Q_OBJECT

    QTemporaryDir m_home;
    QString m_realIso;
    QString m_realThemes;   // the asset library's themes, for the real-style tests
    QString m_realFonts;    // XP's fonts, as setup imported them

private Q_SLOTS:
    void initTestCase()
    {
        // The real XP ISO, if this machine has one (looked up before HOME moves).
        for (const QString &dir : QStringList{QDir::homePath() + QLatin1String("/.cache/xplasma/downloads"),
                                   QDir::homePath() + QLatin1String("/.cache/xp2plasma/downloads")}) {
            const auto isos = QDir(dir).entryInfoList({QStringLiteral("*.iso")}, QDir::Files, QDir::Size);
            if (!isos.isEmpty() && m_realIso.isEmpty())
                m_realIso = isos.first().absoluteFilePath();
        }
        if (QFileInfo::exists(paths::themes() + QLatin1String("/Luna/luna.msstyles")))
            m_realThemes = QFileInfo(paths::themes()).canonicalFilePath();
        if (QFileInfo::exists(paths::xpFonts() + QLatin1String("/tahoma.ttf")))
            m_realFonts = paths::xpFonts();
        QVERIFY(m_home.isValid());
        qputenv("HOME", QFile::encodeName(m_home.path()));
        QCOMPARE(paths::home(), m_home.path());
    }

    void init()
    {
        // A fresh home per test.
        QDir(m_home.path()).removeRecursively();
        QDir().mkpath(m_home.path());
    }

    // --- paths ----------------------------------------------------------------

    void shippedStylePlugin()
    {
        // This is build/<mode>-qt6/tests/app/test-app; build.sh installs the
        // plugins into build/<mode>.
        QDir tree(QDir::cleanPath(QCoreApplication::applicationDirPath() + QLatin1String("/../..")));
        const QString mode = tree.dirName().section(QLatin1String("-qt"), 0, 0);
        const QString expected = QFileInfo(tree.filePath(QLatin1String("../") + mode + QLatin1String("/lib/xplasma/qt6/styles/libxplasma.so")))
                                     .absoluteFilePath();
        if (!QFileInfo::exists(expected))
            QSKIP("build.sh hasn't installed this build yet");
        QCOMPARE(paths::shippedStylePlugin(6), expected);
    }

    // --- archives -------------------------------------------------------------

    void zipAndTar_data()
    {
        QTest::addColumn<QString>("format");
        QTest::newRow("zip") << QStringLiteral("zip");
        QTest::newRow("tar.gz") << QStringLiteral("tar.gz");
    }

    void zipAndTar()
    {
        QFETCH(QString, format);
        const QString file = m_home.filePath(QStringLiteral("pack.") + format);
        QVERIFY(writeArchive(file, format,
                             {{"Luna/luna.msstyles", "MZ-style"}, {"Luna/Shell/NormalColor/shellstyle.dll", "MZ-shell"},
                              {"readme.txt", QByteArray(200000, 'x')}}));

        const auto entries = xpl::archive::list(file);
        QVERIFY(entries);
        QCOMPARE(entries->size(), 3);
        QCOMPARE(entries->at(0).path, QStringLiteral("Luna/luna.msstyles"));
        QCOMPARE(entries->at(2).size, 200000);

        QCOMPARE(xpl::archive::read(file, QStringLiteral("luna\\LUNA.MSSTYLES")).value_or(QByteArray()), QByteArray("MZ-style"));
        QString error;
        QVERIFY(!xpl::archive::read(file, QStringLiteral("nope"), &error));
        QVERIFY(error.contains(QLatin1String("not in")));

        const QString out = m_home.filePath(QStringLiteral("out"));
        qint64 last = -1;
        QVERIFY2(xpl::archive::extract(file, out, [&](qint64 done, qint64) { return (last = done), true; }, &error),
                 qPrintable(error));
        QVERIFY(last > 0);
        QFile f(out + QLatin1String("/Luna/Shell/NormalColor/shellstyle.dll"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("MZ-shell"));

        // Cancelling stops the extraction and says so.
        QVERIFY(!xpl::archive::extract(file, m_home.filePath(QStringLiteral("out2")), [](qint64, qint64) { return false; }, &error));
        QCOMPARE(error, QStringLiteral("cancelled"));
    }

    void refusesTraversal()
    {
        const QString file = m_home.filePath(QStringLiteral("evil.zip"));
        QVERIFY(writeArchive(file, QStringLiteral("zip"), {{"ok.txt", "fine"}, {"../../escaped.txt", "bad"}}));
        QString error;
        QVERIFY(!xpl::archive::extract(file, m_home.filePath(QStringLiteral("dest/inner")), {}, &error));
        QVERIFY(error.contains(QLatin1String("outside")));
        QVERIFY(!QFileInfo::exists(m_home.filePath(QStringLiteral("escaped.txt"))));
    }

    void garbage()
    {
        const QString file = m_home.filePath(QStringLiteral("junk.zip"));
        writeFile(file, QByteArray(5000, '\x5a'));
        QString error;
        QVERIFY(!xpl::archive::list(file, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!xpl::archive::list(m_home.filePath(QStringLiteral("missing.zip")), &error));
        QVERIFY(!xpl::archive::expandCabinet(QByteArray("MSCF but not really"), &error));
    }

    void iso()
    {
        const QString file = m_home.filePath(QStringLiteral("tiny.iso"));
        writeFile(file, IsoBuilder().image);
        QString error;
        const auto entries = xpl::archive::list(file, &error);
        QVERIFY2(entries, qPrintable(error));
        QStringList names;
        for (const auto &e : *entries)
            names << e.path + (e.directory ? QStringLiteral("/") : QString());
        QCOMPARE(names, (QStringList{QStringLiteral("HELLO.TXT"), QStringLiteral("I386/"), QStringLiteral("I386/SPLIT.BIN"),
                                     QStringLiteral("I386/README")}));
        QCOMPARE(xpl::archive::read(file, QStringLiteral("hello.txt")).value_or(QByteArray()), QByteArray("hello"));
        QCOMPARE(xpl::archive::read(file, QStringLiteral("i386\\split.bin")).value_or(QByteArray()), QByteArray("abcabc"));

        const QString out = m_home.filePath(QStringLiteral("iso-out"));
        QVERIFY2(xpl::archive::extract(file, out, {}, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(out + QLatin1String("/I386/README")));
    }

    void isoLoop()
    {
        // A directory pointing back at the root is walked once, not forever.
        const QString file = m_home.filePath(QStringLiteral("loop.iso"));
        writeFile(file, IsoBuilder(true).image);
        const auto entries = xpl::archive::list(file);
        QVERIFY(entries);
        QVERIFY(entries->size() < 10);
    }

    void isoTruncated()
    {
        // Cut off mid-directory: an error or a partial list, never a crash.
        const QString file = m_home.filePath(QStringLiteral("cut.iso"));
        writeFile(file, IsoBuilder().image.left(18 * 2048 + 100));
        xpl::archive::list(file);
        xpl::archive::read(file, QStringLiteral("HELLO.TXT"));
    }

    void realMedia()
    {
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        QString error;
        const auto entries = xpl::archive::list(m_realIso, &error);
        QVERIFY2(entries, qPrintable(error));
        QVERIFY(entries->size() > 1000);
        // The Start flag's source: explorer.exe, CAB-compressed.
        const auto packed = xpl::archive::read(m_realIso, QStringLiteral("I386/EXPLORER.EX_"), &error);
        QVERIFY2(packed, qPrintable(error));
        QVERIFY(packed->startsWith("MSCF"));
        const auto explorer = xpl::archive::expandCabinet(*packed, &error);
        QVERIFY2(explorer, qPrintable(error));
        QVERIFY(explorer->startsWith("MZ"));
        QVERIFY(explorer->size() > 900000);
    }


    // --- the library ----------------------------------------------------------

    void stockSchemes()
    {
        const auto list = schemes::list();
        QCOMPARE(list.size(), 36);
        QVERIFY(std::all_of(list.begin(), list.end(), [](const schemes::Entry &e) { return e.stock; }));
        const auto standard = schemes::load(QStringLiteral("windows standard"));
        QVERIFY(standard);
        QCOMPARE(standard->name, QStringLiteral("Windows Standard"));
        ClassicScheme fresh;   // XP's defaults, which are Windows Standard
        fresh.name = standard->name;
        QCOMPARE(standard->colours, fresh.colours);
        QVERIFY(standard->metrics == fresh.metrics);
        const auto brick = schemes::load(QStringLiteral("Brick"));
        QVERIFY(brick);
        QCOMPARE(brick->colour(u"ActiveTitle"), QColor(128, 0, 0));
        QCOMPARE(brick->metrics.captionFont.face, QStringLiteral("Tahoma"));
        QCOMPARE(brick->metrics.captionFont.points(), 9);
        QVERIFY(!schemes::load(QStringLiteral("Nope")));

        using Split = std::pair<QString, QString>;
        QCOMPARE(schemes::split(QStringLiteral("Windows Standard (large)")),
                 Split(QStringLiteral("Windows Standard"), QStringLiteral("LARGEFONTS")));
        QCOMPARE(schemes::split(QStringLiteral("Pumpkin (large)")), Split(QStringLiteral("Pumpkin (large)"), QStringLiteral("NORMAL")));
        const QStringList bases = schemes::bases();
        QCOMPARE(bases.size(), 22);
        QVERIFY(bases.contains(QStringLiteral("Pumpkin (large)")));
        QVERIFY(!bases.contains(QStringLiteral("Lilac (large)")));
        QVERIFY(schemes::sizeAvailable(QStringLiteral("Lilac"), QStringLiteral("LARGEFONTS")));
        QVERIFY(!schemes::sizeAvailable(QStringLiteral("Brick"), QStringLiteral("LARGEFONTS")));
        QVERIFY(schemes::sizeAvailable(QStringLiteral("Windows Classic"), QStringLiteral("EXTRALARGE")));
    }

    void stockSchemesAreXps()
    {
        // The shipped files are exactly what HIVEDEF.INF says.
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        const auto inf = xpl::archive::read(m_realIso, QStringLiteral("I386/HIVEDEF.INF"));
        QVERIFY(inf);
        const auto fromXp = hivedef::schemes(*inf);
        QCOMPARE(fromXp.size(), 36);
        for (const ClassicScheme &s : fromXp) {
            QFile shipped(QStringLiteral(":/schemes/") + s.name + QLatin1String(".theme"));
            QVERIFY2(shipped.open(QIODevice::ReadOnly), qPrintable(s.name));
            QCOMPARE(shipped.readAll(), hivedef::themeBytes(s));
        }
    }

    void userSchemes()
    {
        ClassicScheme mine = *schemes::load(QStringLiteral("Brick"));
        mine.name = QStringLiteral("Brick");
        mine.setColour(u"Background", QColor(1, 2, 3));
        QString path, error;
        QVERIFY2(schemes::save(mine, &path, &error), qPrintable(error));
        QVERIFY(path.startsWith(paths::schemes()));
        // Yours hides XP's of the same name.
        const auto list = schemes::list();
        QCOMPARE(list.size(), 36);
        QCOMPARE(schemes::load(QStringLiteral("Brick"))->colour(u"Background"), QColor(1, 2, 3));
        mine.name = QStringLiteral("My / Own");
        QVERIFY(schemes::save(mine));
        QCOMPARE(schemes::list().size(), 37);
        QVERIFY(schemes::load(QStringLiteral("My / Own")));
        QVERIFY(schemes::remove(QStringLiteral("Brick")));
        QVERIFY(!schemes::remove(QStringLiteral("Desert")));   // XP's own can't be deleted
        QCOMPARE(schemes::load(QStringLiteral("Brick"))->colour(u"Background"), QColor(66, 0, 0));
        mine.name = QStringLiteral("two\nlines");
        QVERIFY(!schemes::save(mine, nullptr, &error));
    }

    void legacySchemes()
    {
        // xp2plasma's JSON for Brick, as its editor saved it.
        const QByteArray json = R"({"Brick": {
            "metrics": {"border": 1, "scroll": [13, 13], "caption": [18, 18], "smcaption": [15, 15], "menu": [18, 18]},
            "fonts": {"caption": {"face": "Tahoma", "pt": 9, "bold": true, "italic": false},
                      "menu": {"face": "Microsoft Sans Serif", "pt": 8, "bold": false, "italic": false}},
            "colors": {"background": [66, 0, 0], "activetitle": [128, 0, 0], "hotlight": [128, 0, 0],
                       "menuhilight_or_unused": [192, 192, 192]}}})";
        const auto converted = schemes::fromLegacyJson(json);
        QCOMPARE(converted.size(), 1);
        const ClassicScheme &s = converted.first();
        const ClassicScheme xp = *schemes::load(QStringLiteral("Brick"));
        QCOMPARE(s.name, QStringLiteral("Brick"));
        for (const char *c : {"Background", "ActiveTitle", "HotTrackingColor", "ButtonAlternateFace"})
            QCOMPARE(s.colour(QString::fromLatin1(c)), xp.colour(QString::fromLatin1(c)));
        QCOMPARE(s.metrics.scrollWidth, 13);
        QCOMPARE(s.metrics.captionFont, xp.metrics.captionFont);
        QCOMPARE(s.metrics.menuFont.face, QStringLiteral("Microsoft Sans Serif"));
        QVERIFY(schemes::fromLegacyJson("not json").isEmpty());

        // Adoption converts them.
        writeFile(paths::legacyData() + QLatin1String("/schemes/user/brick.json"),
                  QByteArray(json).replace("\"Brick\"", "\"Old Brick\""));
        adoptLegacy();
        QVERIFY(schemes::load(QStringLiteral("Old Brick")));
    }

    void assets()
    {
        const QString root = m_home.filePath(QStringLiteral("lib"));
        writeFile(root + QLatin1String("/Foo.theme"), "[Theme]\r\n");
        writeFile(root + QLatin1String("/Foo/Wall.BMP"), "mine");
        writeFile(root + QLatin1String("/Bar/wall.bmp"), "sibling's");
        writeFile(root + QLatin1String("/Only/deep/a/b/c/far.bmp"), "too deep");
        writeFile(m_home.filePath(QStringLiteral("stock/bliss.jpg")), "jpg");
        const QString theme = root + QLatin1String("/Foo.theme");

        // The bundle's own folder first, by name, any case.
        QCOMPARE(resolveAsset(theme, QStringLiteral("%WinDir%\\Web\\Wallpaper\\wall.bmp")), root + QLatin1String("/Foo/Wall.BMP"));
        // Stock copies may differ in format, but only in the preferred places.
        QCOMPARE(resolveAsset(theme, QStringLiteral("%WinDir%web\\wallpaper\\Bliss.bmp"), {m_home.filePath(QStringLiteral("stock"))}),
                 m_home.filePath(QStringLiteral("stock/bliss.jpg")));
        QCOMPARE(resolveAsset(theme, QStringLiteral("Bliss.bmp")), QString());
        QCOMPARE(resolveAsset(theme, QStringLiteral("far.bmp")), QString());   // deeper than 3 below any root
        // A saved preset's exact path is taken as it is.
        QCOMPARE(resolveAsset(theme, root + QLatin1String("/Bar/wall.bmp")), root + QLatin1String("/Bar/wall.bmp"));
        QCOMPARE(resolveAsset(theme, root + QLatin1String("/Bar/gone.bmp")), QString());
        QCOMPARE(resolveAsset(theme, QString()), QString());
    }

    void fill_data()
    {
        QTest::addColumn<QByteArray>("desktop");
        QTest::addColumn<int>("fill");   // -1: says nothing
        QTest::newRow("stretch") << QByteArray("WallpaperStyle=2") << 0;
        QTest::newRow("centre") << QByteArray("WallpaperStyle=0") << 6;
        QTest::newRow("tile") << QByteArray("WallpaperStyle=0\nTileWallpaper=1") << 3;
        QTest::newRow("fill") << QByteArray("WallpaperStyle=10") << 2;
        QTest::newRow("unknown") << QByteArray("WallpaperStyle=7") << -1;
        QTest::newRow("absent") << QByteArray("Wallpaper=x.bmp") << -1;
    }

    void fill()
    {
        QFETCH(QByteArray, desktop);
        QFETCH(int, fill);
        const auto got = wallpaperFill(ThemeFile::fromBytes("[Control Panel\\Desktop]\n" + desktop));
        QCOMPARE(got.value_or(-1), fill);
    }

    void presetsSave()
    {
        presets::Preset p;
        p.name = QStringLiteral("My Classic");
        p.scheme = *schemes::load(QStringLiteral("Rose"));
        p.desktop.wallpaper = QStringLiteral("/x/bliss.jpg");
        p.desktop.fill = 3;
        QString path, error;
        QVERIFY2(presets::save(p, &path, &error), qPrintable(error));
        QCOMPARE(path, paths::themes() + QLatin1String("/Saved/My-Classic.theme"));
        QVERIFY(presets::owned(path));
        const ThemeBundle b = Library::bundle(path);
        QCOMPARE(b.label, QStringLiteral("My Classic"));
        QVERIFY(b.classic());
        QCOMPARE(b.fill.value_or(-1), 3);
        const ClassicScheme back = ClassicScheme::fromThemeFile(*ThemeFile::load(path));
        QCOMPARE(back.colour(u"ActiveTitle"), p.scheme.colour(u"ActiveTitle"));
        QCOMPARE(back.colour(u"Background"), p.desktop.colour);   // the desktop's colour wins
        // Never replaced.
        QVERIFY(!presets::save(p, nullptr, &error));
        QVERIFY(error.contains(QLatin1String("exists")));
        p.name = QStringLiteral("bad\nname");
        QVERIFY(!presets::save(p));

        // A style preset starting from a bundle: its files are found and
        // written as the paths they are here.
        writeFile(m_home.filePath(QStringLiteral("b/Base.theme")),
                  "[Theme]\r\nDisplayName=Base\r\n[Control Panel\\Cursors]\r\nArrow=%WinDir%cursors\\arrow.cur\r\n");
        writeFile(m_home.filePath(QStringLiteral("b/Base/arrow.cur")), "cur");
        presets::Preset s;
        s.name = QStringLiteral("Styled");
        s.style = QStringLiteral("/x/luna.msstyles");
        s.colorStyle = QStringLiteral("Metallic");
        s.size = QStringLiteral("NormalSize");
        s.base = m_home.filePath(QStringLiteral("b/Base.theme"));
        QVERIFY2(presets::save(s, &path, &error), qPrintable(error));
        const ThemeFile saved = *ThemeFile::load(path);
        QCOMPARE(saved.value(u"VisualStyles", u"ColorStyle"), QStringLiteral("Metallic"));
        QCOMPARE(saved.value(u"Theme", u"DisplayName"), QStringLiteral("Styled"));
        QCOMPARE(saved.value(u"Control Panel\\Cursors", u"Arrow"), m_home.filePath(QStringLiteral("b/Base/arrow.cur")));
        QVERIFY(!presets::owned(s.base));
    }

    void realStyles()
    {
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        const StyleInfo luna = Library::style(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY2(luna.readable(), qPrintable(luna.error));
        QCOMPARE(luna.label, QStringLiteral("Windows XP style"));
        QVERIFY(luna.official);
        QStringList colours;
        for (const StyleOption &c : luna.colours)
            colours << c.label;
        QCOMPARE(colours, (QStringList{QStringLiteral("Default (blue)"), QStringLiteral("Silver"), QStringLiteral("Olive Green")}));
        QCOMPARE(luna.defaultVariant, QStringLiteral("NORMALBLUE_INI"));
        QCOMPARE(luna.resolve(QStringLiteral("Metallic"), QStringLiteral("NormalSize")), QStringLiteral("NORMALMETALLIC_INI"));
        QCOMPARE(luna.resolve(QStringLiteral("homestead"), QString()), QStringLiteral("NORMALHOMESTEAD_INI"));
        QCOMPARE(luna.resolve(QStringLiteral("NormalColor"), QStringLiteral("ExtraLargeFonts")), QStringLiteral("EXTRALARGEBLUE_INI"));
        QCOMPARE(luna.resolve(QStringLiteral("Purple"), QString()), QString());
        QCOMPARE(luna.sizesFor(QStringLiteral("BLUE")).size(), 3);
        // Cached: the second look doesn't open the file (same answer, and the
        // index is on disk).
        QVERIFY(QFileInfo::exists(paths::cache() + QLatin1String("/style-index.json")));
        QCOMPARE(Library::style(luna.path).toJson(), luna.toJson());

        // (Royale Noir is a theme pack's, not the XP disc's: where it's been fetched.)
        if (const QString noirPath = m_realThemes + QLatin1String("/Royale Noir/Royale Noir.msstyles"); QFileInfo::exists(noirPath)) {
            const StyleInfo noir = Library::style(noirPath);
            QCOMPARE(noir.resolve(QStringLiteral("Metallic"), QStringLiteral("NormalSize")), QStringLiteral("NORMALDARKROYALE_INI"));
        }

        const ThemeBundle xp = Library::bundle(m_realThemes + QLatin1String("/Luna/Luna.theme"));
        QCOMPARE(xp.label, QStringLiteral("Windows XP"));
        QCOMPARE(QFileInfo(xp.style).fileName(), QStringLiteral("luna.msstyles"));
        QCOMPARE(xp.variant, QStringLiteral("NORMALBLUE_INI"));

        // (Luna from the disc; four more where the official packs are fetched.)
        const Library lib = Library::scan({m_realThemes});
        QVERIFY(!lib.styles.isEmpty());
        if (QFileInfo::exists(m_realThemes + QLatin1String("/Royale Noir")))
            QVERIFY(lib.styles.size() >= 5);
        QVERIFY(lib.styles.first().official);
    }

    // --- generators -------------------------------------------------------------

    void decoration_data()
    {
        QTest::addColumn<QString>("style");
        QTest::addColumn<QString>("variant");
        for (const char *v : {"NORMALBLUE_INI", "NORMALMETALLIC_INI", "NORMALHOMESTEAD_INI"})
            QTest::newRow(v) << QStringLiteral("Luna/luna.msstyles") << QString::fromLatin1(v);
        QTest::newRow("royale") << QStringLiteral("Royale/Royale.msstyles") << QString();
        QTest::newRow("royale-noir") << QStringLiteral("Royale Noir/Royale Noir.msstyles") << QStringLiteral("NORMALDARKROYALE_INI");
        QTest::newRow("zune") << QStringLiteral("Zune/Zune.msstyles") << QString();
        QTest::newRow("embedded") << QStringLiteral("Embedded/Embedded.msstyles") << QString();
    }

    void decoration()
    {
        // Aurorae's frame, as KSvg lays it out, is XP's own caption and
        // frames drawn at that window size -- every pixel of the border.
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        QFETCH(QString, style);
        QFETCH(QString, variant);
        if (!QFileInfo::exists(m_realThemes + QLatin1Char('/') + style))
            QSKIP("its theme pack isn't fetched on this machine");
        QString error;
        const auto look = Look::fromStyle(m_realThemes + QLatin1Char('/') + style, variant, &error);
        QVERIFY2(look, qPrintable(error));
        const DecorationArt art = decorationArt(*look);
        const DecorationLayout l = decorationLayout(*look);
        const Renderer &r = *look->renderer;
        const int F = l.frame, H = l.frame + l.caption;
        // (Stretched edges come from an 800 x 600 window: exact there; at
        // other sizes only what XP keeps fixed is, and what it stretches
        // unevenly -- Luna's bottom and sides -- is within a few levels.)
        const QMargins capM = r.margins({QStringLiteral("window"), 1, 1, {}}, u"sizingmargins");
        const QMargins botM = r.margins({QStringLiteral("window"), 9, 1, {}}, u"sizingmargins");
        for (const QSize size : {QSize(800, 600), QSize(160, 120), QSize(437, 311), QSize(1024, 700)})
            for (const bool active : {true, false}) {
                const int state = active ? 1 : 2;
                QImage xp(size, QImage::Format_ARGB32_Premultiplied);
                xp.fill(Qt::transparent);
                {
                    QPainter p(&xp);
                    p.drawImage(0, 0, r.background({QStringLiteral("window"), 1, state, {}}, QSize(size.width(), H)));
                    p.drawImage(0, H, r.background({QStringLiteral("window"), 7, state, {}}, QSize(F, size.height() - H - F)));
                    p.drawImage(size.width() - F, H, r.background({QStringLiteral("window"), 8, state, {}}, QSize(F, size.height() - H - F)));
                    p.drawImage(0, size.height() - F, r.background({QStringLiteral("window"), 9, state, {}}, QSize(size.width(), F)));
                }
                const QImage ours = composeFrame(art.frame, active ? QStringLiteral("decoration-") : QStringLiteral("decoration-inactive-"), size);
                const bool reference = size == QSize(800, 600);
                int fixedDiffer = 0, worst = 0;
                for (int y = 0; y < size.height(); ++y)
                    for (int x = 0; x < size.width(); ++x) {
                        const bool top = y < H, bottom = y >= size.height() - F;
                        if (!top && !bottom && x >= F && x < size.width() - F)
                            continue;   // the client's
                        const QMargins &m = top ? capM : botM;
                        const bool fixed = reference || ((top || bottom) && (x < m.left() || x >= size.width() - m.right()));
                        const QRgb a = xp.pixel(x, y), b = ours.pixel(x, y);
                        if (a == b)
                            continue;
                        if (fixed)
                            ++fixedDiffer;
                        worst = std::max({worst, qAbs(qRed(a) - qRed(b)), qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b)),
                                      qAbs(qAlpha(a) - qAlpha(b))});
                    }
                const QString where = QStringLiteral("%1x%2 %3").arg(size.width()).arg(size.height()).arg(active ? "active" : "inactive");
                QVERIFY2(fixedDiffer == 0, qPrintable(QStringLiteral("%1 fixed px differ at %2").arg(fixedDiffer).arg(where)));
                QVERIFY2(worst <= 16, qPrintable(QStringLiteral("stretched edge off by %1 at %2").arg(worst).arg(where)));
            }
        // Buttons at XP's size, every state.
        QCOMPARE(art.buttons.size(), r.hasPart({QStringLiteral("window"), 23, 1, {}}) ? 5 : 4);
        for (const auto &[file, states] : art.buttons) {
            QCOMPARE(states.size(), 8);
            for (const auto &[state, image] : states)
                QCOMPARE(image.size(), l.button);
        }
    }

    void panel_data()
    {
        QTest::addColumn<QString>("style");
        QTest::addColumn<QString>("variant");
        for (const char *v : {"NORMALBLUE_INI", "NORMALMETALLIC_INI", "NORMALHOMESTEAD_INI"})
            QTest::newRow(v) << QStringLiteral("Luna/luna.msstyles") << QString::fromLatin1(v);
        QTest::newRow("royale") << QStringLiteral("Royale/Royale.msstyles") << QString();
        QTest::newRow("zune") << QStringLiteral("Zune/Zune.msstyles") << QString();
        QTest::newRow("embedded") << QStringLiteral("Embedded/Embedded.msstyles") << QString();
    }

    void panel()
    {
        // The taskbar, laid out as KSvg does, is XP's at any width; a task
        // button is too wherever XP doesn't stretch it.
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        QFETCH(QString, style);
        QFETCH(QString, variant);
        if (!QFileInfo::exists(m_realThemes + QLatin1Char('/') + style))
            QSKIP("its theme pack isn't fetched on this machine");
        QString error;
        const auto look = Look::fromStyle(m_realThemes + QLatin1Char('/') + style, variant, &error);
        QVERIFY2(look, qPrintable(error));
        const PanelArt art = panelArt(*look);
        const Renderer &r = *look->renderer;
        QCOMPARE(art.height, 30);
        // Text on the tray is the clock's: Royale's is black on its light
        // tray (its taskbar text is white); Luna Blue's white.
        if (style.startsWith(QLatin1String("Royale/")) || variant == QLatin1String("NORMALMETALLIC_INI"))
            QCOMPARE(art.trayTextColour, QColor(Qt::black));
        else if (variant == QLatin1String("NORMALBLUE_INI"))
            QCOMPARE(art.trayTextColour, QColor(Qt::white));
        QVERIFY(art.trayTextColour.isValid());
        for (const int width : {640, 800, 1280, 1366, 1920}) {
            const QImage xp = r.background({QStringLiteral("taskbar"), 1, 0, {}}, QSize(width, 30));
            const QImage ours = composeFrame(art.panel, QString(), QSize(width, 30));
            int differ = 0;
            for (int y = 0; y < 30; ++y)
                for (int x = 0; x < width; ++x)
                    differ += xp.pixel(x, y) != ours.pixel(x, y);
            QVERIFY2(differ == 0, qPrintable(QStringLiteral("bar: %1 px differ at %2").arg(differ).arg(width)));
        }
        const PartRef button{QStringLiteral("toolbar"), 1, 5, QStringLiteral("taskband")};
        const QMargins m = r.margins(button, u"sizingmargins");
        for (const int width : {60, 160, 200}) {
            if (width <= m.left() + m.right())
                continue;   // (narrower than its ends, XP squeezes them; FrameSvg can't)
            const QImage xp = r.background(button, QSize(width, 30));
            const QImage ours = composeFrame(art.tasks, QStringLiteral("focus-"), QSize(width, 30));
            int differ = 0;
            for (int y = 0; y < 30; ++y)
                for (int x = 0; x < width; ++x)
                    if (width == 160 || x < m.left() || x >= width - m.right())
                        differ += xp.pixel(x, y) != ours.pixel(x, y);
            QVERIFY2(differ == 0, qPrintable(QStringLiteral("task button: %1 px differ at %2").arg(differ).arg(width)));
        }
    }

    void startFlag()
    {
        // The flag comes out of explorer.exe on the XP media, once.
        QString error;
        QVERIFY(!media::startFlag(&error));   // no media recorded yet
        QVERIFY(error.contains(QLatin1String("media")));
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const auto flag = media::startFlag(&error);
        QVERIFY2(flag, qPrintable(error));
        QCOMPARE(flag->size(), QSize(25, 20));
        QVERIFY(flag->hasAlphaChannel());
        QVERIFY(QFileInfo::exists(paths::extracted() + QLatin1String("/explorer.exe")));
        state.media = QStringLiteral("/nowhere.iso");
        QVERIFY(state.save());
        QVERIFY(media::startFlag());   // cached now
    }

    void startButton()
    {
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"), QStringLiteral("NORMALMETALLIC_INI"));
        QVERIFY(look);
        QString error;
        QVERIFY(!xpl::startButton(*look, QImage(), QStringLiteral("start"), &error));   // never without its flag
        QVERIFY(error.contains(QLatin1String("flag")));

        QImage flag(25, 20, QImage::Format_ARGB32);
        flag.fill(qRgba(255, 0, 0, 255));
        const auto art = xpl::startButton(*look, flag, QStringLiteral("start"), &error);
        QVERIFY2(art, qPrintable(error));
        // Measured on XP (Luna Silver, English): 99 x 30, the flag at (10, 5);
        // right of the label (and its shadow), XP's own button.
        QCOMPARE(art->normal.size(), QSize(99, 30));
        QCOMPARE(art->normal.pixel(10, 5), qRgb(255, 0, 0));
        QCOMPARE(art->normal.pixel(34, 24), qRgb(255, 0, 0));
        QVERIFY(art->normal.pixel(9, 5) != qRgb(255, 0, 0));
        const QImage xp = look->renderer->background({QStringLiteral("button"), 1, 2, QStringLiteral("start")}, QSize(99, 32));
        for (int y = 0; y < 30; ++y)
            for (int x = 80; x < 99; ++x)
                QCOMPARE(art->hover.pixel(x, y), xp.pixel(x, y));
        QVERIFY(art->pressed != art->normal);

        const Look classic = Look::fromScheme(ClassicScheme());
        const auto classicArt = xpl::startButton(classic, flag, QStringLiteral("start"), &error);
        QVERIFY2(classicArt, qPrintable(error));
        QCOMPARE(classicArt->normal.height(), 30);

        // Extras: none is XP's own button; a longer label, a wider one; a
        // logo of characters or a picture in the flag's place, its height.
        const auto plain = xpl::startButton(*look, flag, StartButtonExtras{}, &error);
        QVERIFY2(plain, qPrintable(error));
        QCOMPARE(plain->normal, art->normal);
        const auto longer = xpl::startButton(*look, flag, StartButtonExtras{QStringLiteral("start me up"), {}, {}}, &error);
        QVERIFY(longer && longer->normal.width() > art->normal.width());
        const auto emoji = xpl::startButton(*look, flag, StartButtonExtras{QStringLiteral("fart"), QStringLiteral("\U0001F427"), {}}, &error);
        QVERIFY(emoji);
        QVERIFY(emoji->normal.pixel(10, 5) != qRgb(255, 0, 0));   // no flag
        QImage picture(40, 40, QImage::Format_ARGB32);
        picture.fill(qRgba(0, 255, 0, 255));
        const QString file = m_home.filePath(QStringLiteral("logo.png"));
        QVERIFY(picture.save(file));
        const auto pictured = xpl::startButton(*look, flag, StartButtonExtras{{}, QStringLiteral("ignored"), file}, &error);
        QVERIFY(pictured);
        QCOMPARE(pictured->normal.pixel(10, 5), qRgb(0, 255, 0));
        QCOMPARE(pictured->normal.pixel(29, 24), qRgb(0, 255, 0));   // 20 tall and wide, where the flag was 25 x 20
        // A picture that's gone: XP's flag.
        const auto gone = xpl::startButton(*look, flag, StartButtonExtras{{}, {}, file + QLatin1String(".gone")}, &error);
        QVERIFY(gone && gone->normal == art->normal);
        // Kept as JSON, only what's set.
        const StartButtonExtras e{QStringLiteral("fart"), QStringLiteral("\U0001F427"), {}};
        QCOMPARE(StartButtonExtras::fromJson(e.toJson()), e);
        QVERIFY(StartButtonExtras{}.toJson().isEmpty());
    }

    void iconTheme()
    {
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        QString error;
        QImage drawnIcon(13, 13, QImage::Format_ARGB32_Premultiplied);
        drawnIcon.fill(Qt::red);
        const QString theme = writeIconTheme(m_home.filePath(QStringLiteral("out")), QStringLiteral("breeze"), QColor(Qt::white), &error,
                                             {{QStringLiteral("value-increase"), {{13, drawnIcon}}}});
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        // Icons drawn for the look, at their own sizes, as actions.
        QCOMPARE(QImage(theme + QLatin1String("/13x13/actions/value-increase.png")).size(), QSize(13, 13));
        // XP's own sizes only: shell32's folder at 16, 32 and 48, never larger.
        for (const char *size : {"16x16", "32x32", "48x48"})
            QVERIFY(QFileInfo::exists(theme + QLatin1Char('/') + QLatin1String(size) + QLatin1String("/places/folder.png")));
        QVERIFY(!QFileInfo::exists(theme + QLatin1String("/96x96/places/folder.png")));
        QCOMPARE(QImage(theme + QLatin1String("/32x32/places/folder.png")).size(), QSize(32, 32));
        // Battery frames come from batmeter's image lists, keyed.
        const QImage battery(theme + QLatin1String("/16x16/status/battery-050.png"));
        QCOMPARE(battery.size(), QSize(16, 16));
        QCOMPARE(qAlpha(battery.pixel(0, 0)), 0);
        QFile index(theme + QLatin1String("/index.theme"));
        QVERIFY(index.open(QIODevice::ReadOnly));
        {
            const QByteArray text = index.readAll();
            QVERIFY(text.contains("Inherits=breeze,hicolor"));
            QVERIFY(text.contains("[13x13/actions]\nSize=13\nContext=Actions"));
        }
        // The shortcut arrow in KDE's emblem squares, bottom-right, XP's size:
        // the 7px arrow for small icons, the 11px one at 32.
        for (const auto &[square, arrow] : {std::pair(8, 7), std::pair(16, 11), std::pair(22, 22)}) {
            const QImage emblem(theme + QStringLiteral("/%1x%1/emblems/emblem-symbolic-link.png").arg(square));
            QCOMPARE(emblem.size(), QSize(square, square));
            QRect box;
            for (int y = 0; y < square; ++y)
                for (int x = 0; x < square; ++x)
                    if (qAlpha(emblem.pixel(x, y)) > 0)
                        box |= QRect(x, y, 1, 1);
            QCOMPARE(box, QRect(square - arrow, square - arrow, arrow, arrow));
        }
        // Updates as Automatic Updates' shields; brightness as Power Options'
        // monitor, 32px only on XP, so scaled for the tray's 16.
        QCOMPARE(QImage(theme + QLatin1String("/16x16/status/update-none-symbolic.png")).size(), QSize(16, 16));
        QVERIFY(QImage(theme + QLatin1String("/16x16/status/update-high.png")) != QImage(theme + QLatin1String("/16x16/status/update-none.png")));
        QCOMPARE(QImage(theme + QLatin1String("/16x16/status/brightness-high-symbolic.png")).size(), QSize(16, 16));
        QCOMPARE(QImage(theme + QLatin1String("/32x32/status/brightness-high-symbolic.png")).size(), QSize(32, 32));
        // Charging: the level's frame, the plug's head over its bottom-right
        // corner only.
        {
            const QImage idle(theme + QLatin1String("/16x16/status/battery-050.png"));
            const QImage charging(theme + QLatin1String("/16x16/status/battery-050-charging.png"));
            QCOMPARE(idle.copy(0, 0, 8, 16), charging.copy(0, 0, 8, 16));
            QVERIFY(idle.copy(8, 9, 8, 7) != charging.copy(8, 9, 8, 7));
        }
        // The tray's icons XP has none for: the inherited theme's, in the
        // tray's text colour, beyond the icon loader's recolouring.
        if (QFileInfo::exists(QStringLiteral("/usr/share/icons/breeze/index.theme"))) {
            QFile sun(theme + QLatin1String("/16x16/status/redshift-status-on-symbolic.svg"));
            QVERIFY(sun.open(QIODevice::ReadOnly));
            const QByteArray svg = sun.readAll();
            QVERIFY2(svg.contains("#ffffff") && !svg.contains("currentColor") && !svg.contains("current-color-scheme"), svg.left(400).constData());
            QVERIFY(!QFileInfo::exists(theme + QLatin1String("/16x16/status/folder-symbolic.svg")));   // (not the tray's)
        }
    }

    void recolouredSymbolic()
    {
        const QByteArray in = R"(<svg><defs><style id="current-color-scheme" type="text/css">.ColorScheme-Text { color:#232629; } .ColorScheme-Highlight { color:#3daee9; }</style></defs>)"
                              R"(<path class="ColorScheme-Text" fill="currentColor" d="m0 0"/><rect class="ColorScheme-Highlight" style="fill:currentColor"/><g fill="currentColor"/></svg>)";
        const QByteArray out = xpl::recolouredSvg(in, QColor(0x41, 0x40, 0x0a));
        QVERIFY2(out == R"(<svg><defs></defs><path class="ColorScheme-Text" fill="#41400a" d="m0 0"/><rect class="ColorScheme-Highlight" style="fill:#3daee9"/><g fill="#41400a"/></svg>)",
                 out.constData());
    }

    void startPanel()
    {
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(look);
        QString error;
        const QString dir = m_home.filePath(QStringLiteral("menu"));
        const QJsonObject theme = writeStartPanel(*look, dir, &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        const QJsonObject parts = theme.value(QStringLiteral("parts")).toObject();
        QVERIFY(parts.contains(QStringLiteral("moreprogramsarrow-hot")));
        QCOMPARE(parts.value(QStringLiteral("logoff")).toObject().value(QStringLiteral("sizing")).toArray().at(0).toInt(), 48);
        QCOMPARE(theme.value(QStringLiteral("layout")).toObject().value(QStringLiteral("headerHeight")).toInt(), 64);
        QVERIFY(QFileInfo::exists(dir + QLatin1String("/button-shutdown-hot.png")));
        QVERIFY(QFileInfo::exists(dir + QLatin1String("/theme.json")));
        QVERIFY(writeStartPanel(Look::fromScheme(ClassicScheme()), dir, &error).isEmpty());
    }

    void startPlasmoid()
    {
        // The package ships inside XPLasma; installing replaces what had the
        // id (xp2plasma's Kickoff fork) entirely.
        const QString plasmoids = m_home.filePath(QStringLiteral("plasmoids"));
        writeFile(plasmoids + QLatin1String("/fyi.hotsocket.xplasma.start/contents/ui/Kickoff-leftover.qml"), "x");
        QString error;
        QVERIFY2(installStartPlasmoid(plasmoids, &error), qPrintable(error));
        const QString pkg = plasmoids + QLatin1String("/fyi.hotsocket.xplasma.start");
        QVERIFY(!QFileInfo::exists(pkg + QLatin1String("/contents/ui/Kickoff-leftover.qml")));
        for (const char *file : {"metadata.json", "contents/config/main.xml", "contents/ui/main.qml", "contents/ui/StartButton.qml",
                                 "contents/ui/StartMenu.qml", "contents/ui/XpFullRepresentation.qml", "contents/ui/ActionMenu.qml"})
            QVERIFY2(QFileInfo::exists(pkg + QLatin1Char('/') + QLatin1String(file)), file);
        QFile meta(pkg + QLatin1String("/metadata.json"));
        QVERIFY(meta.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(meta.readAll()).object();
        QCOMPARE(json.value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Id")).toString(), QStringLiteral("fyi.hotsocket.xplasma.start"));
        QVERIFY(json.value(QStringLiteral("X-XPLasma-Generated")).toBool());
    }

    void plasmaControlsDrawn()
    {
        // Plasma's popup controls, drawn by XPLasma's style: every SVG, for a
        // visual style and for Classic, with the states Plasma lays over.
        if (m_realThemes.isEmpty())
            QSKIP("needs the asset library");
        const auto luna = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(luna);
        if (!xpStyle(*luna))
            QSKIP("XPLasma's style plugin isn't where this test can load it");
        const auto classic = Look::fromScheme(ClassicScheme());
        for (const Look *look : {&*luna, &classic}) {
            const auto controls = plasmaControls(*look);
            for (const char *svg : {"button", "checkmarks", "radiobutton", "lineedit", "listitem", "viewitem",
                                    "bar_meter_horizontal", "slider", "scrollbar", "tabbar", "switch", "arrows"})
                QVERIFY2(controls.contains(QLatin1String(svg)), svg);
            auto has = [&](const char *svg, const char *element) {
                const auto &elements = controls.value(QLatin1String(svg));
                return std::any_of(elements.begin(), elements.end(), [&](const auto &e) {
                    return e.first == QLatin1String(element) && !e.second.isNull();
                });
            };
            QVERIFY(has("button", "normal-center"));
            QVERIFY(has("button", "pressed-center"));
            QCOMPARE(has("button", "hover-center"), !look->classic());   // (Classic's buttons don't light up)
            QVERIFY(has("radiobutton", "symbol"));
            QVERIFY(has("slider", "horizontal-slider-handle"));
            QVERIFY(has("scrollbar", "hint-scrollbar-size"));
            QVERIFY(has("tabbar", "north-active-tab-center"));
            // A pushed button isn't an unpushed one.
            const auto &button = controls.value(QStringLiteral("button"));
            auto element = [&](const char *name) {
                return std::find_if(button.begin(), button.end(), [&](const auto &e) { return e.first == QLatin1String(name); })->second;
            };
            QVERIFY(element("normal-topleft") != element("pressed-topleft"));
            // A check box's empty box: the button shadow's 16px variant only
            // (Plasma draws a check box as a button frame, the shadow over
            // it); the shadow itself nothing, under buttons and combo boxes.
            const QImage box = element("16-16-shadow-center");
            QCOMPARE(box.size(), QSize(16, 16));
            QCOMPARE(qAlpha(box.pixel(0, 0)), 255);   // square, filling it: no button frame round it
            QCOMPARE(qAlpha(box.pixel(15, 15)), 255);
            const QImage none = element("shadow-center");
            for (int y = 0; y < none.height(); ++y)
                for (int x = 0; x < none.width(); ++x)
                    QCOMPARE(qAlpha(none.pixel(x, y)), 0);
            // The ticked box on it: the same box, the tick added.
            const auto &marks = controls.value(QStringLiteral("checkmarks"));
            const QImage ticked = std::find_if(marks.begin(), marks.end(), [](const auto &e) { return e.first == QLatin1String("checkbox"); })->second;
            QCOMPARE(ticked.size(), QSize(16, 16));
            QCOMPARE(ticked.pixel(0, 0), box.pixel(0, 0));
            QVERIFY(ticked != box);
            // A switch (XP's check box): the active bar, drawn from the bar's
            // left to the handle's right edge (Plasma 6.4 even when off), is
            // one piece and the handle 3px, so off it has no width to draw in.
            const auto &sw = controls.value(QStringLiteral("switch"));
            auto swElement = [&](const char *name) {
                const auto it = std::find_if(sw.begin(), sw.end(), [&](const auto &e) { return e.first == QLatin1String(name); });
                return it == sw.end() ? QImage() : it->second;
            };
            QCOMPARE(swElement("handle").width(), 3);
            QVERIFY(!swElement("active-center").isNull());
            QVERIFY(swElement("active-topleft").isNull() && swElement("active-left").isNull());
            QCOMPARE(swElement("active-center").width(), swElement("hint-bar-size").width() - 3);   // the box (bar less 2) less its left edge
            // Focus (and hover) only what they change of the button: not over
            // a check box's middle, where the tick is.
            QCOMPARE(qAlpha(element("focus-center").pixel(element("focus-center").width() / 2, element("focus-center").height() / 2)), 0);

            // The spin box's buttons, XP's up-down on its side: each size a
            // label might ask for, drawn at that size, left unlike right.
            const auto spin = spinIcons(*look);
            for (const char *name : {"value-decrease", "value-increase"}) {
                const auto sizes = spin.value(QLatin1String(name));
                for (const int size : {12, 13, 16, 24, 32}) {
                    QVERIFY2(sizes.contains(size), name);
                    QCOMPARE(sizes.value(size).size(), QSize(size, size));
                }
            }
            QVERIFY(spin.value(QStringLiteral("value-decrease")).value(13) != spin.value(QStringLiteral("value-increase")).value(13));
        }
    }

    void startTheme()
    {
        if (m_realThemes.isEmpty() || m_realIso.isEmpty())
            QSKIP("needs the asset library and the XP ISO");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QString error;
        const QJsonObject theme = writeStartTheme(*look, m_home.filePath(QStringLiteral("start")), &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        const QJsonObject button = theme.value(QStringLiteral("button")).toObject();
        QCOMPARE(button.value(QStringLiteral("height")).toInt(), 30);
        for (const char *state : {"normal", "hover", "pressed"})
            QVERIFY(QFileInfo::exists(QUrl(button.value(QLatin1String(state)).toString()).toLocalFile()));
        QVERIFY(theme.value(QStringLiteral("parts")).toObject().value(QStringLiteral("userpane")).toObject()
                    .value(QStringLiteral("url")).toString().startsWith(QLatin1String("file:///")));
        // One of XP's account pictures, for a user without their own: 48x48.
        const QImage picture(QUrl(theme.value(QStringLiteral("userPicture")).toString()).toLocalFile());
        QCOMPARE(picture.size(), QSize(48, 48));
    }

    void flatpakStyle()
    {
        // The style as a KDE runtime extension, one per org.kde.Platform 6
        // branch; the override letting apps read the look, added to the
        // user's own and taken back out alone.
        struct FlatpakSession : RecordingSession {
            QByteArray runtimes;
            host::Result run(const QStringList &argv) override
            {
                host::Result r = RecordingSession::run(argv);
                if (argv.value(0) == QLatin1String("flatpak") && argv.contains(QStringLiteral("--runtime")))
                    r.out = runtimes;
                else if (argv == QStringList{QStringLiteral("flatpak"), QStringLiteral("--default-arch")})
                    r.out = "x86_64\n";
                return r;
            }
        } session;
        session.runtimes = "org.kde.Platform\t6.10\norg.kde.Sdk\t6.10\norg.kde.Platform\t6.9\norg.kde.Platform\t6.10\n"
                           "org.kde.Platform\t5.15-24.08\norg.freedesktop.Platform\t24.08\n";
        QCOMPARE(flatpak::qtRuntimes(session), (QStringList{QStringLiteral("6.10"), QStringLiteral("6.9")}));
        const QString plugin = m_home.filePath(QStringLiteral("libxplasma.so"));
        writeFile(plugin, "not really a plugin");
        // (With what an earlier XPLasma let apps read, to be taken out.)
        writeFile(flatpak::overrideFile(), "[Context]\nfilesystems=home;xdg-config/xplasmarc:ro;\n\n[Environment]\nFOO=1\n");
        // (An empty stand-in Flatpak left in an app's own config folder goes;
        // a real file there stays.)
        writeFile(paths::home() + QLatin1String("/.var/app/org.example.A/config/xplasmarc"), "");
        writeFile(paths::home() + QLatin1String("/.var/app/org.example.B/config/xplasmarc"), "[Theme]\n");
        QStringList log;
        QString error;
        QVERIFY2(flatpak::install(session, plugin, &log, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(paths::home() + QLatin1String("/.var/app/org.example.A/config/xplasmarc")));
        QVERIFY(QFileInfo::exists(paths::home() + QLatin1String("/.var/app/org.example.B/config/xplasmarc")));
        for (const char *branch : {"6.10", "6.9"})
            QVERIFY(QFileInfo::exists(flatpak::extensionRoot() + QLatin1String("/x86_64/") + QLatin1String(branch) + QLatin1String("/styles/libxplasma.so")));
        auto overrides = [] {
            QFile f(flatpak::overrideFile());
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QVERIFY2(overrides().contains(QLatin1String("filesystems=home;xdg-data/xplasma/active:ro;")), qPrintable(overrides()));
        QVERIFY(overrides().contains(QLatin1String("[Environment]\nFOO=1")));
        // Again: nothing doubled. A runtime gone: its extension too.
        session.runtimes = "org.kde.Platform\t6.10\n";
        QVERIFY(flatpak::install(session, plugin, &log, &error));
        QCOMPARE(overrides().count(QLatin1String("xplasma/active")), 1);
        QVERIFY(!QFileInfo::exists(flatpak::extensionRoot() + QLatin1String("/x86_64/6.9")));
        // Gone: ours only.
        flatpak::remove(&log);
        QVERIFY(!QFileInfo::exists(flatpak::extensionRoot()));
        QVERIFY2(overrides().contains(QLatin1String("filesystems=home;")) && !overrides().contains(QLatin1String("xplasma")), qPrintable(overrides()));
        QVERIFY(overrides().contains(QLatin1String("FOO=1")));
        // An override that was only ours goes whole; no Flatpak: nothing done.
        QFile::remove(flatpak::overrideFile());
        session.runtimes = "org.kde.Platform\t6.10\n";
        QVERIFY(flatpak::install(session, plugin, &log, &error));
        flatpak::remove();
        QVERIFY(!QFileInfo::exists(flatpak::overrideFile()));
        session.runtimes.clear();
        QVERIFY(flatpak::install(session, plugin, &log, &error));
        QVERIFY(!QFileInfo::exists(flatpak::extensionRoot()) && !QFileInfo::exists(flatpak::overrideFile()));
    }

    void installPackage()
    {
        // A package (as the Setup wizard unpacks its payload) into a prefix:
        // the programs, the plugins, the builds of Plasma's widgets, the menu
        // entries pointed at the installed editor; and out again.
        const QString src = m_home.filePath(QStringLiteral("pkg-src"));
        writeFile(src + QLatin1String("/bin/xplasma"), "#!/bin/sh\n");
        writeFile(src + QLatin1String("/bin/xplasma-edit"), "#!/bin/sh\n");
        QFile::setPermissions(src + QLatin1String("/bin/xplasma"), QFile::permissions(src + QLatin1String("/bin/xplasma")) | QFile::ExeOwner);
        QFile::setPermissions(src + QLatin1String("/bin/xplasma-edit"), QFile::permissions(src + QLatin1String("/bin/xplasma-edit")) | QFile::ExeOwner);
        writeFile(src + QLatin1String("/lib/xplasma/qt6/styles/libxplasma.so"), "plugin");
        writeFile(src + QLatin1String("/lib/xplasma/forks/6.7/plasma/applets/org.kde.panel.so"), "fork");
        writeFile(src + QLatin1String("/share/applications/fyi.hotsocket.xplasma.desktop"), "[Desktop Entry]\nName=Display Properties\nExec=xplasma-edit\n");
        const QString tar = m_home.filePath(QStringLiteral("pkg.tar"));
        QCOMPARE(QProcess::execute(QStringLiteral("tar"), {QStringLiteral("-C"), src, QStringLiteral("-cf"), tar, QStringLiteral("bin"), QStringLiteral("lib"),
                                                          QStringLiteral("share")}),
                 0);
        const QString pkg = m_home.filePath(QStringLiteral("pkg"));
        QString error;
        QVERIFY2(archive::extract(tar, pkg, {}, &error), qPrintable(error));
        QVERIFY(installer::isPackage(pkg) || QFileInfo::exists(pkg + QLatin1String("/bin/xplasma")));
        QFile::setPermissions(pkg + QLatin1String("/bin/xplasma"), QFile::permissions(pkg + QLatin1String("/bin/xplasma")) | QFile::ExeOwner);
        const QString prefix = m_home.filePath(QStringLiteral("prefix"));
        QVERIFY2(installer::installFiles(pkg, prefix, {}, &error), qPrintable(error));
        QVERIFY(QFileInfo(prefix + QLatin1String("/bin/xplasma")).isExecutable());
        QVERIFY(QFileInfo::exists(prefix + QLatin1String("/lib/xplasma/qt6/styles/libxplasma.so")));
        QVERIFY(QFileInfo::exists(prefix + QLatin1String("/lib/xplasma/forks/6.7/plasma/applets/org.kde.panel.so")));
        QFile desktop(prefix + QLatin1String("/share/applications/fyi.hotsocket.xplasma.desktop"));
        QVERIFY(desktop.open(QIODevice::ReadOnly));
        QVERIFY(desktop.readAll().contains(("Exec=" + prefix + "/bin/xplasma-edit\n").toUtf8()));
        QCOMPARE(installer::installedPrefix(), prefix);
        // (The downloads stay unless asked, apart from what was taken out.)
        writeFile(paths::downloads() + QLatin1String("/xp.iso"), "iso");
        QCOMPARE(installer::downloadsSize(), qint64(3));
        installer::removeFiles(prefix, false, false, {});
        QVERIFY(QFileInfo::exists(paths::downloads() + QLatin1String("/xp.iso")));
        QVERIFY(!QFileInfo::exists(prefix + QLatin1String("/bin/xplasma")) && !QFileInfo::exists(prefix + QLatin1String("/lib/xplasma")));
        QVERIFY(!QFileInfo::exists(prefix + QLatin1String("/lib")));   // (its folders too, emptied)
        QVERIFY(QFileInfo::exists(prefix));
        QVERIFY(installer::installedPrefix().isEmpty());
        // What was taken out gone, the image kept; then the image too.
        writeFile(paths::xpFonts() + QLatin1String("/tahoma.ttf"), "font");
        installer::removeFiles(prefix, true, false, {});
        QVERIFY(!QFileInfo::exists(paths::xpFonts()) && QFileInfo::exists(paths::downloads() + QLatin1String("/xp.iso")));
        installer::removeFiles(prefix, false, true, {});
        QVERIFY(!QFileInfo::exists(paths::downloads() + QLatin1String("/xp.iso")));
    }

    void gtkTheme()
    {
        // The booster: every selector's subject outranks Adwaita's; @-rules
        // and comments as they were.
        const QString css = gtk::boosted(QStringLiteral("@define-color a #fff;\n/* note */\nbutton, notebook > header tab:checked { x: 1; }\n"));
        QVERIFY2(css.contains(QLatin1String("@define-color a #fff;")) && css.contains(QLatin1String("/* note */")), qPrintable(css));
        QVERIFY2(css.contains(QLatin1String("button:not(.xp-1):not(.xp-2):not(.xp-3):not(.xp-4),\nnotebook > header tab:checked:not(.xp-1)")),
                 qPrintable(css));

        if (m_realThemes.isEmpty())
            QSKIP("needs the asset library");
        const auto luna = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(luna);
        if (!xpStyle(*luna))
            QSKIP("XPLasma's style plugin isn't where this test can load it");
        const Look classic = Look::fromScheme(ClassicScheme());
        for (const Look *look : {&*luna, &classic}) {
            QString error;
            const QString root = gtk::writeTheme(*look, m_home.filePath(QStringLiteral("gtk-out")), &error);
            QVERIFY2(!root.isEmpty(), qPrintable(error));
            QFile gtk3(root + QLatin1String("/gtk-3.0/gtk.css")), layer(root + QLatin1String("/gtk-4.0/xplasma.css")), gtk4(root + QLatin1String("/gtk-4.0/gtk.css"));
            QVERIFY(gtk3.open(QIODevice::ReadOnly) && layer.open(QIODevice::ReadOnly) && gtk4.open(QIODevice::ReadOnly));
            const QString text3 = QString::fromUtf8(gtk3.readAll()), text4 = QString::fromUtf8(layer.readAll());
            // On Adwaita, every placeholder filled, every image there.
            QVERIFY(text3.startsWith(QLatin1String("@import url(\"resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css\");")));
            QVERIFY(QString::fromUtf8(gtk4.readAll()).contains(QLatin1String("@import url(\"xplasma.css\");")));
            for (const QString &t : {text3, text4}) {
                QVERIFY2(!t.contains(QLatin1String("${")), qPrintable(t.mid(t.indexOf(QLatin1String("${")) - 40, 80)));
                static const QRegularExpression url(QStringLiteral("url\\(\"\\.\\./assets/([^\"]+)\"\\)"));
                for (auto it = url.globalMatch(t); it.hasNext();) {
                    const QString file = it.next().captured(1);
                    QVERIFY2(QFileInfo::exists(root + QLatin1String("/assets/") + file), qPrintable(file));
                }
            }
            // XP's scroll bar arrows in GTK 3 (steppers), none asked of GTK 4.
            QVERIFY(text3.contains(QLatin1String("-GtkScrollbar-has-backward-stepper: true")));
            QVERIFY(!text4.contains(QLatin1String("Stepper")));
            // A button's middle as the background (GTK 3 leaves a border-image's undrawn).
            QVERIFY(QFileInfo::exists(root + QLatin1String("/assets/button-normal-fill.png")));
        }

        // Applied: GTK switched, overlay scroll bars off, the libadwaita line
        // added after KDE's own; reset takes back exactly that.
        RecordingSession session;
        session.config.insert(QStringLiteral("gtk-3.0/settings.ini|Settings|gtk-theme-name"), QStringLiteral("Breeze"));
        const QString user = paths::config() + QLatin1String("/gtk-4.0/gtk.css");
        writeFile(user, "@import 'colors.css';");
        State state;
        QStringList log;
        QString error;
        QVERIFY2(gtk::apply(session, *luna, state, &log, &error), qPrintable(error));
        QCOMPARE(session.config.value(QStringLiteral("gtk-3.0/settings.ini|Settings|gtk-theme-name")), gtk::themeName(*luna));
        QCOMPARE(session.config.value(QStringLiteral("gtk-4.0/settings.ini|Settings|gtk-theme-name")), gtk::themeName(*luna));
        QVERIFY(session.calls.join(QLatin1Char(' ')).contains(QLatin1String("setGtkTheme")));
        QCOMPARE(state.settings.value(QStringLiteral("previousGtkTheme")).toString(), QStringLiteral("Breeze"));
        QVERIFY(QFileInfo::exists(paths::localShare() + QLatin1String("/themes/") + gtk::themeName(*luna) + QLatin1String("/gtk-3.0/gtk.css")));
        auto userText = [&] {
            QFile f(user);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QVERIFY2(userText().startsWith(QLatin1String("@import 'colors.css';\n@import url(\"file://")) && userText().contains(QLatin1String("xplasma.css")),
                 qPrintable(userText()));
        // Flatpak's GTK apps: the theme as an org.gtk.Gtk3theme extension (the
        // theme's gtk-3.0 folder, its images inside), and the GTK 4 stylesheet
        // and the themes readable, for libadwaita apps.
        {
            const QString ext = flatpak::gtkExtensionRoot(gtk::themeName(*luna)) + QLatin1String("/x86_64/3.22");
            QFile css(ext + QLatin1String("/gtk.css"));
            QVERIFY(css.open(QIODevice::ReadOnly));
            const QByteArray text = css.readAll();
            QVERIFY(text.contains("url(\"assets/button-normal.png\")") && !text.contains("../assets/"));
            QVERIFY(QFileInfo::exists(ext + QLatin1String("/assets/button-normal.png")));
            QFile overrides(flatpak::overrideFile());
            QVERIFY(overrides.open(QIODevice::ReadOnly));
            const QByteArray o = overrides.readAll();
            QVERIFY2(o.contains("xdg-config/gtk-4.0:ro") && o.contains("xdg-data/themes:ro"), o.constData());
        }
        // Again: one line, not two.
        QVERIFY(gtk::apply(session, *luna, state, &log, &error));
        QCOMPARE(userText().count(QLatin1String("xplasma.css")), 1);
        gtk::revert(session, state, &log);
        QCOMPARE(session.config.value(QStringLiteral("gtk-3.0/settings.ini|Settings|gtk-theme-name")), QStringLiteral("Breeze"));
        QCOMPARE(userText(), QStringLiteral("@import 'colors.css';\n"));
        QVERIFY(!QFileInfo::exists(paths::localShare() + QLatin1String("/themes/") + gtk::themeName(*luna)));
        QVERIFY(!state.settings.contains(QStringLiteral("previousGtkTheme")));
        QVERIFY(!QFileInfo::exists(flatpak::gtkExtensionRoot(gtk::themeName(*luna))));
        QVERIFY(!QFileInfo::exists(flatpak::overrideFile()));
    }

    void startButtonExtrasApplied()
    {
        // Extras... applied alone: the button redrawn with the user's label
        // (State's settings) and handed to the running Start buttons.
        if (m_realThemes.isEmpty() || m_realIso.isEmpty())
            QSKIP("needs the asset library and the XP ISO");
        State state;
        state.media = m_realIso;
        state.settings.insert(QLatin1String(kStartButtonKey), StartButtonExtras{QStringLiteral("start me up"), {}, {}}.toJson());
        QVERIFY(state.save());
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        RecordingSession session;
        QString error;
        QVERIFY2(applyStartButton(session, *look, &error), qPrintable(error));
        QCOMPARE(session.scripts.size(), 1);
        const QString js = session.scripts.first();
        QVERIFY(js.contains(QLatin1String(kStartPlasmoidId)) && js.contains(QLatin1String("writeConfig('theme'")));
        const QRegularExpression width(QStringLiteral("\\\\?\"width\\\\?\":(\\d+)"));
        const auto m = width.match(js);
        QVERIFY2(m.hasMatch(), qPrintable(js.left(300)));
        QVERIFY(m.captured(1).toInt() > 99);   // wider than XP's "start"
    }

    // --- applying to the session --------------------------------------------------

    void scripts()
    {
        const QString js = scriptFrom(QStringLiteral("wallpaper"), {{QStringLiteral("IMAGE"), QStringLiteral("file:///a \"b\".jpg")},
                                                                     {QStringLiteral("FILL"), 2},
                                                                     {QStringLiteral("COLOUR"), QStringLiteral("#3a6ea5")}});
        QVERIFY(js.contains(QLatin1String("d.writeConfig('Image', \"file:///a \\\"b\\\".jpg\")")));
        QVERIFY(js.contains(QLatin1String("d.writeConfig('FillMode', 2)")));
        QVERIFY(!js.contains(QLatin1String("${")));
        const QString none = scriptFrom(QStringLiteral("wallpaper"), {{QStringLiteral("IMAGE"), QVariant::fromValue(nullptr)},
                                                                       {QStringLiteral("FILL"), QVariant::fromValue(nullptr)},
                                                                       {QStringLiteral("COLOUR"), QStringLiteral("#000000")}});
        QVERIFY(none.contains(QLatin1String("if (null === null)")));
    }

    void overridesPatchThisPlasma()
    {
        // The anchors hold against the Plasma on this machine.
        const QString tray = QStringLiteral("/usr/share/plasma/plasmoids/org.kde.plasma.systemtray/contents/ui/main.qml");
        if (!QFileInfo::exists(tray))
            QSKIP("no Plasma here");
        const QString plasmoids = m_home.filePath(QStringLiteral("plasmoids"));
        QString error;
        // A copy of the Plasma clock XPLasma once patched goes: the clock is
        // XPLasma's own now.
        writeFile(plasmoids + QLatin1String("/org.kde.plasma.digitalclock/metadata.json"), "{\"X-XPLasma-Panel-Override\": true}");
        QVERIFY2(overrides::installPanelWidgets(plasmoids, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.digitalclock")));
        // What the clock runs on a double-click: the editor, quoted, as its own unit.
        const QString run = QStringLiteral("'/opt/it'\\''s here/xplasma-edit' '--date-time'");
        QCOMPARE(overrides::launchCommand(QStringLiteral("/opt/it's here/xplasma-edit"), {QStringLiteral("--date-time")}),
                 QStringLiteral("systemd-run --user --collect --quiet -- %1 || %1").arg(run));
        QFile patchedTray(plasmoids + QLatin1String("/org.kde.plasma.systemtray/contents/ui/main.qml"));
        QVERIFY(patchedTray.open(QIODevice::ReadOnly));
        const QByteArray text = patchedTray.readAll();
        QVERIFY(text.contains("xpTrayAvailable") && text.contains("imagePath: \"widgets/xp-tray\"") && !text.contains("{{"));
        QVERIFY(QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.systemtray/contents/ui/PlasmaExpanderArrow.qml")));
        QFile metrics(plasmoids + QLatin1String("/org.kde.plasma.taskmanager/contents/ui/code/layoutmetrics.js"));
        QVERIFY(metrics.open(QIODevice::ReadOnly) && metrics.readAll().contains("tasks.xpTaskband"));
        // Balloons: the popups' delegate chosen by the art, the queue's icons in the tray.
        const QString notifications = plasmoids + QLatin1String("/org.kde.plasma.notifications/contents/ui/");
        QFile globals(notifications + QLatin1String("global/Globals.qml"));
        QVERIFY(globals.open(QIODevice::ReadOnly));
        const QByteArray globalsText = globals.readAll();
        QVERIFY(globalsText.contains("delegate: globals.xpBalloons ? xpDelegate : stockDelegate")
                && globalsText.contains("function xpAdvance()") && !globalsText.contains("{{"));
        for (const char *f : {"XpPopup.qml", "XpBalloon.qml", "XpCopyDialog.qml", "XpButton.qml", "main.qml"})
            QVERIFY2(QFileInfo(notifications + QLatin1String(f)).size() > 0, f);
        QVERIFY(text.contains("XpNotificationIcons {"));
        QVERIFY(QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.systemtray/contents/ui/XpNotificationIcons.qml")));
        // The desktop's menu: XP Properties, starting the editor outside the shell.
        QVERIFY2(overrides::installDesktopMenu(plasmoids, QStringLiteral("/opt/it's here/xplasma-edit"), &error), qPrintable(error));
        QFile layer(plasmoids + QLatin1String("/org.kde.desktopcontainment/contents/ui/FolderViewLayer.qml"));
        QVERIFY(layer.open(QIODevice::ReadOnly));
        const QByteArray desktop = layer.readAll();
        QVERIFY(desktop.contains("text: \"XP Properties\""));
        QVERIFY2(desktop.contains(R"(connectSource("systemd-run --user --collect --quiet -- '/opt/it'\\''s here/xplasma-edit' || '/opt/it'\\''s here/xplasma-edit'"))"),
                 desktop.mid(desktop.indexOf("connectSource"), 160).constData());
        QVERIFY(desktop.contains("Plasmoid.contextualActions.push(xpPropertiesAction)") && !desktop.contains("@LAUNCH@") && !desktop.contains("{{"));
        QFile folderView(plasmoids + QLatin1String("/org.kde.desktopcontainment/contents/ui/FolderView.qml"));
        QVERIFY(folderView.open(QIODevice::ReadOnly));
        const QByteArray drops = folderView.readAll();
        QVERIFY(drops.contains("dir.drop(target, xpShortcutDrop(event)") && drops.contains("function xpShortcutDrop(event)"));
        overrides::removeDesktopMenu(plasmoids);
        QVERIFY(!QFileInfo::exists(plasmoids + QLatin1String("/org.kde.desktopcontainment")));
        // Again (it's ours now), then gone.
        QVERIFY2(overrides::installPanelWidgets(plasmoids, &error), qPrintable(error));
        overrides::removePanelWidgets(plasmoids);
        QVERIFY(!QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.systemtray")));
        // Someone else's is left alone.
        writeFile(plasmoids + QLatin1String("/org.kde.plasma.taskmanager/metadata.json"), "{}");
        QVERIFY(!overrides::installPanelWidgets(plasmoids, &error));
        QVERIFY(error.contains(QLatin1String("someone else")));
        QVERIFY(!overrides::patched(QStringLiteral("systemtray/main.qml"), QStringLiteral("not KDE's QML")));
    }

    void overridesAllOrNothing()
    {
        // A Plasma that builds some widgets into their plugins (6.5 on): those
        // are the ones XPLasma's own builds stand in for; the rest are
        // patched, all or nothing, and a copy of ours of a built-in one goes.
        const QString tray = QStringLiteral("/usr/share/plasma/plasmoids/org.kde.plasma.systemtray");
        if (!QFileInfo::exists(tray))
            QSKIP("no Plasma here");
        const QString system = m_home.filePath(QStringLiteral("system-share"));
        QDir().mkpath(system + QLatin1String("/plasma/plasmoids"));
        QVERIFY(QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-r"), tray, system + QLatin1String("/plasma/plasmoids/")}) == 0);
        const QByteArray saved = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", system.toUtf8());
        const QString plasmoids = m_home.filePath(QStringLiteral("plasmoids-6.6"));
        writeFile(plasmoids + QLatin1String("/org.kde.plasma.taskmanager/metadata.json"), "{\"X-XPLasma-Panel-Override\": true}");
        const QStringList builtIn = overrides::builtInPanelWidgets();
        QString checked, installed;
        const bool can = overrides::checkPanelWidgets(plasmoids, &checked);
        const bool did = overrides::installPanelWidgets(plasmoids, &installed);
        qputenv("XDG_DATA_DIRS", saved);
        QCOMPARE(builtIn, (QStringList{QStringLiteral("org.kde.plasma.taskmanager"), QStringLiteral("org.kde.panel"),
                                       QStringLiteral("org.kde.plasma.notifications")}));
        QVERIFY2(can && did, qPrintable(checked + installed));
        QVERIFY(QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.systemtray/contents/ui/XpNotificationIcons.qml")));
        QVERIFY(!QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.taskmanager")));
    }

    void forkChoice()
    {
        // Down the shipped builds, the first that loads here: on this Plasma
        // a 6.6 build doesn't (older Qt than its floor, libPlasma.so.6 not
        // .7 -- whichever the loader meets first), and there is none for 6.4.
        if (paths::shippedForks().isEmpty() || !QFileInfo::exists(paths::shippedForks() + QLatin1String("/6.6")))
            QSKIP("no 6.6 builds here (./build-forks.sh 6.6, then ./build.sh)");
        if (!QFileInfo::exists(QStringLiteral("/usr/lib/libPlasma.so.6")))
            QSKIP("this test expects Plasma 6.4's libraries");
        const QStringList ids{QStringLiteral("org.kde.plasma.systemtray")};
        QString error;
        QVERIFY(!forks::choose(QStringLiteral("6.6.4"), ids, &error));
        QVERIFY2(error.contains(QLatin1String("the 6.6 build")) && error.contains(QLatin1String("org.kde.plasma.systemtray.so")), qPrintable(error));
        QVERIFY(!forks::choose(QStringLiteral("6.4.3"), ids, &error));
        QVERIFY2(error.contains(QLatin1String("no build")), qPrintable(error));
        QVERIFY(!forks::choose(QString(), ids, &error));
    }

    void applyRefusesPartialLook()
    {
        // Applying on a Plasma that can't take every override changes
        // nothing at all: no programs run, no settings written.
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(look);
        const QString system = m_home.filePath(QStringLiteral("empty-share"));
        QDir().mkpath(system);
        const QByteArray saved = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", system.toUtf8());
        RecordingSession session;
        ApplyOptions o;
        QString error;
        const bool applied = applyLook(session, *look, o, &error);
        qputenv("XDG_DATA_DIRS", saved);
        QVERIFY(!applied);
        QVERIFY2(error.contains(QLatin1String("nothing was changed")), qPrintable(error));
        // (The built-in widgets: no build of them for this Plasma, whichever it is.)
        QVERIFY2((error.contains(QLatin1String("which Plasma")) || error.contains(QLatin1String("no build"))
                  || error.contains(QLatin1String("builds of Plasma's widgets")))
                     && error.contains(QLatin1String("Aurorae")),
                 qPrintable(error));
        // (Only asking which Plasma it is.)
        for (const QStringList &run : std::as_const(session.runs))
            QVERIFY2(run.contains(QStringLiteral("plasmashell")) && run.contains(QStringLiteral("--version")), qPrintable(run.join(QLatin1Char(' '))));
        QVERIFY(session.config.isEmpty());
        QVERIFY(session.scripts.isEmpty());
    }

    void restartWithoutSystemd()
    {
        // A session without systemd (Slackware's): no user unit to stop and
        // start, so plasmashell is asked to quit and started again, in this
        // program's environment (XPLasma's plugin path in it).
        RecordingSession session;
        session.systemd = false;
        bool stopped = false;
        session.restartPlasmaShell([&] {
            stopped = std::any_of(session.runs.begin(), session.runs.end(), [](const QStringList &a) {
                return a == QStringList{QStringLiteral("kquitapp6"), QStringLiteral("plasmashell")};
            });
        });
        QVERIFY(stopped);
        QCOMPARE(session.detached, (QList<QStringList>{{QStringLiteral("plasmashell")}}));
        QVERIFY(std::none_of(session.runs.begin(), session.runs.end(), [](const QStringList &a) {
            return a.value(0) == QLatin1String("systemctl") && (a.contains(QStringLiteral("stop")) || a.contains(QStringLiteral("start")));
        }));
    }

    void optionalPatchRules()
    {
        // An optional rule is for what only some Plasmas have: skipped where
        // its text isn't, applied where it is -- but twice is still an error.
        const QJsonArray rules{
            QJsonObject{{QStringLiteral("find"), QStringLiteral("new line\n")}, {QStringLiteral("replace"), QString()},
                        {QStringLiteral("optional"), true}},
            QJsonObject{{QStringLiteral("find"), QStringLiteral("id: grid\n")}, {QStringLiteral("replace"), QStringLiteral("id: grid\nours\n")}},
        };
        QString error;
        QCOMPARE(overrides::patched(QStringLiteral("x.qml"), QStringLiteral("id: grid\n"), &error, &rules).value_or(QString()),
                 QStringLiteral("id: grid\nours\n"));
        QCOMPARE(overrides::patched(QStringLiteral("x.qml"), QStringLiteral("id: grid\nnew line\n"), &error, &rules).value_or(QString()),
                 QStringLiteral("id: grid\nours\n"));
        QVERIFY(!overrides::patched(QStringLiteral("x.qml"), QStringLiteral("id: grid\nnew line\nnew line\n"), &error, &rules));
    }

    void forkSources()
    {
        // A Plasma series' source, patched into a work tree: the patch, the
        // build file's changes, an added file; a second sync writes nothing;
        // one that can't patch writes nothing at all.
        QVERIFY(forks::seriesNames().contains(QStringLiteral("6.6")));
        const QString pristine = m_home.filePath(QStringLiteral("fork/pristine")), work = m_home.filePath(QStringLiteral("fork/work"));
        const QString js = QStringLiteral("function preferredMaxWidth() {\n    if (tasks.iconsOnly) {\n        return 1;\n    }\n}\n");
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/applets/tm/code/metrics.js"), js.toUtf8());
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/applets/tm/CMakeLists.txt"), "plasma_add_applet(org.example.tm)\n");
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/README"), "unchanged\n");
        forks::Series series;
        series.name = QStringLiteral("test");
        series.sources = {{QStringLiteral("plasma-desktop"), QStringLiteral("9.9"), QString()}};
        forks::Widget w;
        w.id = QStringLiteral("org.example.tm");
        w.project = QStringLiteral("plasma-desktop");
        w.dir = QStringLiteral("applets/tm");
        w.files.insert(QStringLiteral("taskmanager/code/layoutmetrics.js"), QStringLiteral("code/metrics.js"));
        w.extras.insert(QStringLiteral("XpButton.qml"), QStringLiteral("XpButton.qml"));
        w.registered.insert(QString(), {QStringLiteral("XpButton.qml")});
        series.widgets = {w};
        QString error;
        auto written = forks::syncSources(series, pristine, work, &error);
        QVERIFY2(written, qPrintable(error));
        QCOMPARE(*written, 4);
        const QString tree = work + QLatin1String("/plasma-desktop-9.9");
        QVERIFY(readFile(tree + QLatin1String("/applets/tm/code/metrics.js")).contains("xpTaskband"));
        const QByteArray cmake = readFile(tree + QLatin1String("/applets/tm/CMakeLists.txt"));
        QVERIFY(cmake.contains("QT_QML_SKIP_CACHEGEN TRUE") && cmake.indexOf("QT_QML_SKIP_CACHEGEN") < cmake.indexOf("plasma_add_applet"));
        QVERIFY(cmake.contains("ecm_target_qml_sources(org.example.tm SOURCES XpButton.qml)"));
        QVERIFY(QFileInfo::exists(tree + QLatin1String("/applets/tm/XpButton.qml")));
        written = forks::syncSources(series, pristine, work, &error);
        QVERIFY(written);
        QCOMPARE(*written, 0);
        // A source that has moved on: refused, nothing touched.
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/applets/tm/code/metrics.js"), "// something else\n");
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/README"), "changed\n");
        QVERIFY(!forks::syncSources(series, pristine, work, &error));
        QVERIFY(error.contains(QLatin1String("org.example.tm")));
        QCOMPARE(readFile(tree + QLatin1String("/README")), QByteArray("unchanged\n"));
    }

    void xpTextRules()
    {
        // XP's Tahoma 8 (11 px) is one-bit text: its gasp table says no
        // greyscale there, and the rules say so to fontconfig.
        if (m_realFonts.isEmpty())
            QSKIP("no XP fonts on this machine");
        const QString rules = QString::fromUtf8(xptext::rules(m_realFonts));
        QVERIFY(rules.contains(QLatin1String("<const>hintfull</const>")));
        QVERIFY(rules.contains(QLatin1String("<const>none</const>")));
        bool crisp11 = false, kern = false;
        for (const QString &block : rules.split(QLatin1String("<match target=\"font\">"))) {
            if (!block.contains(QLatin1String("<string>Tahoma</string>")))
                continue;
            kern |= block.contains(QLatin1String("kern off"));
            if (!block.contains(QLatin1String("name=\"antialias\"><bool>false")))
                continue;
            const QRegularExpression lo(QStringLiteral("more_eq\"><double>([0-9.]+)")), hi(QStringLiteral("less\"><double>([0-9.]+)"));
            const auto l = lo.match(block), h = hi.match(block);
            const double from = l.hasMatch() ? l.captured(1).toDouble() : 0, to = h.hasMatch() ? h.captured(1).toDouble() : 1e9;
            crisp11 |= from <= 10.67 && 10.67 < to && from <= 11 && 11 < to;
        }
        QVERIFY2(crisp11, qPrintable(rules));
        QVERIFY(kern);
    }

    void xpTextApplyAndRevert()
    {
        // Included at the end of KDE's fonts.conf (read after its settings),
        // KDE's Xft settings switched, and all of it undone.
        const QString conf = paths::config() + QLatin1String("/fontconfig/fonts.conf");
        const QByteArray kde = "<?xml version='1.0'?>\n<fontconfig>\n <match target=\"font\">\n  <edit mode=\"assign\" name=\"hintstyle\">\n"
                               "   <const>hintnone</const>\n  </edit>\n </match>\n</fontconfig>\n";
        writeFile(conf, kde);
        RecordingSession session;
        session.config.insert(QStringLiteral("kdeglobals|General|XftHintStyle"), QStringLiteral("hintnone"));
        State state;
        QString error;
        QVERIFY2(xptext::apply(session, state, &error), qPrintable(error));
        QFile f(conf);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        f.close();
        QVERIFY(text.indexOf("xplasma-xp-text.conf") > text.indexOf("hintnone"));
        QVERIFY(text.indexOf("xplasma-xp-text.conf") < text.indexOf("</fontconfig>"));
        QVERIFY(QFileInfo::exists(paths::config() + QLatin1String("/fontconfig/xplasma-xp-text.conf")));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|XftHintStyle")), QStringLiteral("hintfull"));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|XftSubPixel")), QStringLiteral("none"));
        QVERIFY(xptext::apply(session, state, &error));   // (twice: one include, the first previous kept)
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll().count("xplasma-xp-text.conf"), 1);
        f.close();

        xptext::revert(session, state);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), kde);
        f.close();
        QVERIFY(!QFileInfo::exists(paths::config() + QLatin1String("/fontconfig/xplasma-xp-text.conf")));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|XftHintStyle")), QStringLiteral("hintnone"));
        QVERIFY(!session.config.contains(QStringLiteral("kdeglobals|General|XftSubPixel")));
    }

    void taskbarsSettleWhileShellIsDown()
    {
        // A floating bottom panel is put back in the shell's saved state; a
        // top panel, and panels that aren't in the layout, are left alone.
        writeFile(paths::config() + QLatin1String("/plasma-org.kde.plasma.desktop-appletsrc"),
                  "[Containments][7]\nformfactor=2\nlocation=4\nplugin=org.kde.panel\n\n"
                  "[Containments][7][Applets][9]\nplugin=org.kde.plasma.digitalclock\n\n"
                  "[Containments][8]\nlocation=3\nplugin=org.kde.panel\n\n"
                  "[Containments][1]\nlocation=0\nplugin=org.kde.plasma.folder\n");
        writeFile(paths::config() + QLatin1String("/plasmashellrc"),
                  "[PlasmaViews][Panel 7]\nfloating=1\n\n[PlasmaViews][Panel 7][Defaults]\nthickness=44\n\n"
                  "[PlasmaViews][Panel 7][Horizontal1280]\nfloating=1\npanelOpacity=2\n\n"
                  "[PlasmaViews][Panel 8]\nfloating=1\n\n[PlasmaViews][Panel 5]\nfloating=1\n");
        RecordingSession session;
        settleTaskbars(session, 30);
        const QString rc = QStringLiteral("plasmashellrc|PlasmaViews/Panel 7");
        QCOMPARE(session.config.value(rc + QLatin1String("|floating")), QStringLiteral("0"));
        QCOMPARE(session.config.value(rc + QLatin1String("/Defaults|thickness")), QStringLiteral("30"));
        QCOMPARE(session.config.value(rc + QLatin1String("/Horizontal1280|floating")), QStringLiteral("0"));
        QCOMPARE(session.config.value(rc + QLatin1String("/Horizontal1280|panelOpacity")), QStringLiteral("1"));
        QCOMPARE(session.config.value(rc + QLatin1String("/Horizontal1280|panelLengthMode")), QStringLiteral("0"));
        for (auto it = session.config.cbegin(); it != session.config.cend(); ++it)
            QVERIFY2(it.key().startsWith(rc), qPrintable(it.key()));
    }

    void wallpaperAlone()
    {
        // The editor's Apply when only the desktop changed: one script, nothing rebuilt.
        RecordingSession session;
        QStringList said;
        applyWallpaper(session, Wallpaper{QStringLiteral("/w/stoneh.jpg"), 6, QColor(0, 78, 152)},
                       [&](const QString &line) { said << line; });
        QCOMPARE(session.scripts.size(), 1);
        QVERIFY(session.scripts.first().contains(QLatin1String("file:///w/stoneh.jpg")));
        QVERIFY(session.runs.isEmpty() && session.calls.isEmpty());
        QCOMPARE(said, QStringList{QStringLiteral("Wallpaper: stoneh.jpg")});
        QVERIFY(!QFileInfo::exists(paths::data() + QLatin1String("/state.json")));
    }

    void titleShadowsFromRc()
    {
        if (!QFileInfo::exists(QStringLiteral("/usr/share/kwin/aurorae/aurorae.qml")))
            QSKIP("no KWin Aurorae here");
        const QString themes = m_home.filePath(QStringLiteral("aurorae/themes"));
        const QString target = m_home.filePath(QStringLiteral("kwin/aurorae/aurorae.qml"));
        auto read = [&] {
            QFile f(target);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QString error;
        // None wanted: still an object (bare braces in QML are an empty block, i.e. undefined).
        QVERIFY2(overrides::installTitleShadows(themes, target, &error), qPrintable(error));
        QVERIFY(read().contains(QLatin1String("property var xpTitleShadows: ({})")));
        // As decoration.cpp writes it: the shadow keys are in [General].
        writeFile(themes + QLatin1String("/xp-blue/xp-bluerc"),
                  "[General]\nUseTextShadow=true\nActiveTextShadowColor=10,24,131,255\nInactiveTextShadowColor=10,24,131,255\n"
                  "TextShadowOffsetX=1\nTextShadowOffsetY=1\n\n[X-XPLasma-Generated]\nLook=Windows XP style (Default (blue))\n");
        writeFile(themes + QLatin1String("/plain/plainrc"),
                  "[General]\nUseTextShadow=false\n\n[X-XPLasma-Generated]\nLook=Windows Standard\n");
        QVERIFY2(overrides::installTitleShadows(themes, target, &error), qPrintable(error));
        QVERIFY2(read().contains(QLatin1String(
                     R"(property var xpTitleShadows: ({"xp-blue":{"active":"#ff0a1883","inactive":"#ff0a1883","x":1,"y":1}}))")),
                 qPrintable(read().section(QLatin1String("xpTitleShadows"), 1, 1).left(200)));
        // XP's title buttons, for XPLasma's themes (shadowed or not).
        QVERIFY(read().contains(QLatin1String(R"(property var xpThemes: ["plain","xp-blue"])")));
        QVERIFY(read().contains(QLatin1String("buttons: root.xpButtons(options.titleButtonsLeft)"))
                && read().contains(QLatin1String("buttons: root.xpButtons(options.titleButtonsRight)"))
                && read().contains(QLatin1String("function xpButtons(list)")));
    }

    void applyAndReset()
    {
        if (m_realThemes.isEmpty() || m_realIso.isEmpty())
            QSKIP("needs the asset library and the XP ISO");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const QString share = paths::localShare();
        // Another look's package, and one of the user's own.
        writeFile(share + QLatin1String("/aurorae/themes/old-look/metadata.json"), "{\"X-XPLasma-Generated\": true}");
        writeFile(share + QLatin1String("/aurorae/themes/Mine/metadata.json"), "{\"KPlugin\": {\"Name\": \"Mine\"}}");
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"), QStringLiteral("NORMALMETALLIC_INI"));
        QVERIFY(look);
        RecordingSession session;
        session.config.insert(QStringLiteral("kwinrulesrc|General|rules"), QStringLiteral("someone-elses"));
        session.config.insert(QStringLiteral("kdeglobals|KDE|widgetStyle"), QStringLiteral("Breeze"));
        session.config.insert(QStringLiteral("kdeglobals|Icons|Theme"), QStringLiteral("Papirus"));
        ApplyOptions o;
        o.wallpaper = Wallpaper{m_home.filePath(QStringLiteral("bliss.jpg")), 2, QColor(0, 0, 0)};
        QStringList said;
        o.progress = [&](const QString &line) { said << line; };
        QString error;
        QVERIFY2(applyLook(session, *look, o, &error), qPrintable(error + said.join(QLatin1Char('\n'))));

        // Installed where Plasma looks.
        QVERIFY(QFileInfo::exists(share + QLatin1String("/aurorae/themes/windows-xp-style-silver/decoration.svg")));
        QVERIFY(QFileInfo::exists(share + QLatin1String("/color-schemes/windows-xp-style-silver.colors")));
        QVERIFY(QFileInfo::exists(share + QLatin1String("/plasma/desktoptheme/windows-xp-style-silver-taskbar/widgets/tasks.svg")));
        {
            // KSvg's cache primed with the theme's size variants (the check
            // box's 16px box), stamped with the SVG's own time.
            const QString button = share + QLatin1String("/plasma/desktoptheme/windows-xp-style-silver-taskbar/widgets/button.svg");
            QFile cache(paths::home() + QLatin1String("/.cache/ksvg-elements"));
            QVERIFY(cache.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(cache.readAll());
            QVERIFY2(text.contains(QLatin1Char('[') + button + QStringLiteral("]\nLastModified=")
                                   + QString::number(QFileInfo(button).lastModified().toSecsSinceEpoch())),
                     qPrintable(text.left(300)));
            QVERIFY(text.contains(QLatin1String("shadow-center=4x4,16x16,")));
        }
        {
            // A Plasma 5+ theme, or Plasma gives its colour sets the Button colours.
            QFile meta(share + QLatin1String("/plasma/desktoptheme/windows-xp-style-silver-taskbar/metadata.json"));
            QVERIFY(meta.open(QIODevice::ReadOnly));
            QCOMPARE(QJsonDocument::fromJson(meta.readAll()).object().value(QStringLiteral("X-Plasma-API")).toString(), QStringLiteral("6.0"));
        }
        QVERIFY(QFileInfo::exists(share + QLatin1String("/plasma/plasmoids/fyi.hotsocket.xplasma.start/contents/ui/main.qml")));
        QVERIFY(QFileInfo::exists(share + QLatin1String("/icons/xplasma-icons/index.theme")));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/aurorae/themes/old-look")));
        QVERIFY(QFileInfo::exists(share + QLatin1String("/aurorae/themes/Mine")));
        // Switched to.
        QCOMPARE(session.config.value(QStringLiteral("kwinrc|org.kde.kdecoration2|theme")), QStringLiteral("__aurorae__svg__windows-xp-style-silver"));
        // The copy dialog's and the editor's rules, beside the user's own.
        QCOMPARE(session.config.value(QStringLiteral("kwinrulesrc|General|rules")),
                 QStringLiteral("someone-elses,xplasma-copy-dialog,xplasma-display-properties"));
        QCOMPARE(session.config.value(QStringLiteral("kwinrulesrc|General|count")), QStringLiteral("3"));
        QCOMPARE(session.config.value(QStringLiteral("kwinrulesrc|xplasma-display-properties|wmclass")),
                 QStringLiteral("^fyi\\.hotsocket\\.xplasma(\\.datetime)?$"));
        QCOMPARE(session.config.value(QStringLiteral("kwinrulesrc|xplasma-copy-dialog|minimizerule")), QStringLiteral("2"));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|KDE|widgetStyle")), QStringLiteral("XPLasma"));
        QVERIFY(session.config.value(QStringLiteral("kdeglobals|WM|activeFont")).startsWith(QLatin1String("Trebuchet MS,10")));
        QVERIFY(session.ran(QStringLiteral("plasma-apply-colorscheme")));
        // (The current scheme's name forgotten just before: plasma-apply-colorscheme
        // won't apply a scheme already current, a look applied again regenerated.)
        {
            const auto at = [&](auto match) { return int(std::find_if(session.runs.begin(), session.runs.end(), match) - session.runs.begin()); };
            const int forget = at([](const QStringList &a) { return a.contains(QStringLiteral("ColorScheme")) && a.last().isEmpty(); });
            const int apply = at([](const QStringList &a) { return a.value(0) == QLatin1String("plasma-apply-colorscheme"); });
            QVERIFY(forget < apply);
            QCOMPARE(apply - forget, 1);
        }
        QVERIFY(session.ran(QStringLiteral("plasma-apply-desktoptheme")));
        QVERIFY(session.calls.contains(QStringLiteral("/KDEPlatformTheme refreshFonts")));
        QVERIFY(session.calls.contains(QStringLiteral("/KWin reconfigure")));
        const QString layout = session.scripts.filter(QStringLiteral("LAUNCHER_KEYS")).value(0);
        QVERIFY(layout.contains(QLatin1String("var START = \"fyi.hotsocket.xplasma.start\"")));
        QVERIFY2(!layout.contains(QLatin1String("${")), "a value the layout script wants wasn't given");
        QVERIFY(layout.contains(QLatin1String("var DESKTOP_FILES = {")));
        QVERIFY(layout.contains(QLatin1String("\"button\":{")));
        QVERIFY(!session.scripts.filter(QStringLiteral("p.height = 30")).isEmpty());
        QVERIFY(!session.scripts.filter(QStringLiteral("bliss.jpg")).isEmpty());
        // The panel's order, written with the shell down.
        QCOMPARE(session.config.value(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc|Containments/1/General|AppletOrder")),
                 QStringLiteral("3;4;5"));
        // Themes swapped into place whole: nothing staged is left beside them.
        QDirIterator staged(share, {QStringLiteral("*.xplasma-new")}, QDir::Dirs, QDirIterator::Subdirectories);
        QVERIFY2(!staged.hasNext(), qPrintable(staged.hasNext() ? staged.next() : QString()));
        // Qt: the plugin, the frozen style, xplasmarc.
        QVERIFY(QFileInfo::exists(paths::stylePluginRoot(6) + QLatin1String("/styles/libxplasma.so")));
        QSettings rc(paths::config() + QLatin1String("/xplasmarc"), QSettings::IniFormat);
        QVERIFY(rc.value(QStringLiteral("Theme/Path")).toString().startsWith(paths::data() + QLatin1String("/active/")));
        QCOMPARE(rc.value(QStringLiteral("Theme/Variant")).toString(), QStringLiteral("NORMALMETALLIC_INI"));
        // (A copy beside the look, for Flatpak apps' sandboxes.)
        QCOMPARE(QSettings(paths::data() + QLatin1String("/active/xplasmarc"), QSettings::IniFormat).value(QStringLiteral("Theme/Path")),
                 rc.value(QStringLiteral("Theme/Path")));
        const State applied = State::load();
        QCOMPARE(applied.appliedKind, QStringLiteral("style"));
        QCOMPARE(applied.variant, QStringLiteral("NORMALMETALLIC_INI"));
        QCOMPARE(applied.settings.value(QStringLiteral("previousWidgetStyle")).toString(), QStringLiteral("Breeze"));
        QCOMPARE(applied.settings.value(QStringLiteral("previousIconTheme")).toString(), QStringLiteral("Papirus"));

        // Reset takes it all off.
        RecordingSession resetSession;
        resetSession.config = session.config;
        QVERIFY2(resetDesktop(resetSession, {}, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/aurorae/themes/windows-xp-style-silver")));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/color-schemes/windows-xp-style-silver.colors")));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/plasma/plasmoids/fyi.hotsocket.xplasma.start")));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/icons/xplasma-icons")));
        QVERIFY(!QFileInfo::exists(paths::config() + QLatin1String("/xplasmarc")));
        QVERIFY(QFileInfo::exists(share + QLatin1String("/aurorae/themes/Mine")));
        QCOMPARE(resetSession.config.value(QStringLiteral("kdeglobals|KDE|widgetStyle")), QStringLiteral("Breeze"));
        QCOMPARE(resetSession.config.value(QStringLiteral("kwinrulesrc|General|rules")), QStringLiteral("someone-elses"));
        QVERIFY(!resetSession.config.contains(QStringLiteral("kwinrulesrc|xplasma-copy-dialog|minimizerule")));
        QVERIFY(!resetSession.config.contains(QStringLiteral("kdeglobals|WM|activeFont")));
        QVERIFY(std::any_of(resetSession.runs.begin(), resetSession.runs.end(), [](const QStringList &a) {
            return a.value(0) == QLatin1String("plasma-apply-lookandfeel") && a.contains(QStringLiteral("--resetLayout"));
        }));
        QCOMPARE(State::load().appliedKind, QString());
    }

    void resetUnloadsForksFirst()
    {
        // XPLasma's builds of Plasma's widgets go with the shell down, and
        // the shell's back without them before Plasma's layout is -- given
        // the default panel, a shell that had them loaded would load
        // Plasma's own beside them, and die of the module registered twice.
        struct Watching : RecordingSession {
            QString plugin;
            QStringList seen;
            host::Result run(const QStringList &argv) override
            {
                if (argv.value(0) != QLatin1String("kwriteconfig6") && argv.value(0) != QLatin1String("kreadconfig6"))
                    seen << argv.join(QLatin1Char(' ')) + (QFileInfo::exists(plugin) ? QLatin1String(" [loaded]") : QLatin1String(" [gone]"));
                return RecordingSession::run(argv);
            }
        } session;
        session.plugin = paths::stylePluginRoot(6) + QLatin1String("/plasma/applets/org.kde.plasma.taskmanager.so");
        QVERIFY(QDir().mkpath(QFileInfo(session.plugin).path()));
        QFile plugin(session.plugin);
        QVERIFY(plugin.open(QIODevice::WriteOnly));
        plugin.close();
        State state = State::load();
        state.settings.insert(QStringLiteral("forkPlugins"), QJsonArray{session.plugin});
        state.save();

        QString error;
        QVERIFY2(resetDesktop(session, {}, &error), qPrintable(error));
        const auto at = [&](const QString &line) { return int(session.seen.indexOf(line)); };
        const int stop = at(QStringLiteral("systemctl --user stop plasma-plasmashell.service [loaded]"));
        const int start = at(QStringLiteral("systemctl --user start plasma-plasmashell.service [gone]"));
        const int layout = int(session.seen.indexOf(QRegularExpression(QStringLiteral("^plasma-apply-lookandfeel .*--resetLayout \\[gone\\]$"))));
        QVERIFY2(stop >= 0 && start > stop && layout > start, qPrintable(session.seen.join(QLatin1Char('\n'))));
        QVERIFY(!forks::installed(State::load()));
    }

    // --- setup ----------------------------------------------------------------------

    void xpTimeZones()
    {
        // XP's zones from its registry INF, in its order, each with its Linux
        // zone and its colours on XP's world map.
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        QString error;
        const auto zones = timezones::load(&error);
        QVERIFY2(zones, qPrintable(error));
        QCOMPARE(zones->size(), 75);
        QCOMPARE(zones->first().display, QStringLiteral("(GMT-12:00) International Date Line West"));
        const int central = timezones::find(*zones, "America/Chicago");
        QVERIFY(central >= 0);
        const timezones::Zone &z = zones->at(central);
        QCOMPARE(z.display, QStringLiteral("(GMT-06:00) Central Time (US & Canada)"));
        QCOMPARE(z.standard, QStringLiteral("Central Standard Time"));
        QCOMPARE(z.daylight, QStringLiteral("Central Daylight Time"));
        QCOMPARE(std::pair(z.mapSea, z.mapLand), std::pair(36, 37));
        // (Another Linux name for a zone XP has.)
        // Every zone stands for a Linux one (three by names Windows retired).
        for (const timezones::Zone &each : *zones)
            QVERIFY2(QTimeZone(each.iana).isValid(), qPrintable(each.key));
        QCOMPARE(timezones::find(*zones, "US/Central"), central);
        const auto map = timezones::map(&error);
        QVERIFY2(map, qPrintable(error));
        QCOMPARE(map->indices.size(), QSize(356, 184));
        int highest = 0;
        for (int y = 0; y < 184; ++y)
            for (int x = 0; x < 356; ++x)
                highest = qMax(highest, int(map->indices.constScanLine(y)[x]));
        QCOMPARE(highest, 89);
    }

    void dateTimeProperties()
    {
        // xplasma-edit --date-time draws its tabs offscreen at a fixed moment
        // in Central Time; the Time Zone tab's map is XP's, scrolled as XP
        // scrolls it for Central (249px, measured on XP).
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const QString editor = QCoreApplication::applicationDirPath() + QLatin1String("/../../src/app/xplasma-edit");
        if (!QFileInfo(editor).isExecutable())
            QSKIP("xplasma-edit isn't built here");
        const QString out = m_home.filePath(QStringLiteral("datetime"));
        QProcess run;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        run.setProcessEnvironment(env);
        run.start(editor, {QStringLiteral("--date-time"), QStringLiteral("--screenshots"), out});
        QVERIFY(run.waitForFinished(60000));
        QCOMPARE(run.exitCode(), 0);
        for (const char *tab : {"datetime", "timezone", "internettime"})
            QVERIFY2(QFileInfo::exists(out + QLatin1Char('/') + QLatin1String(tab) + QLatin1String(".png")), tab);
        const QImage zone(out + QLatin1String("/timezone.png"));
        // The map: its top-left pixel is the first navy-or-green one.
        QPoint origin(-1, -1);
        for (int y = 0; y < zone.height() && origin.x() < 0; ++y)
            for (int x = 0; x < zone.width(); ++x)
                if (const QRgb c = zone.pixel(x, y) & 0xffffff; c == 0x00007f || c == 0x007f00) {
                    origin = QPoint(x, y);
                    break;
                }
        QVERIFY(origin.x() >= 0);
        QString error;
        const auto map = timezones::map(&error);
        QVERIFY2(map, qPrintable(error));
        auto isSea = [](int i) { return (i < 48 && i % 2 == 0) || i == 78 || i == 88; };
        int wrong = 0;
        for (int y = 0; y < map->indices.height(); ++y)
            for (int x = 0; x < map->indices.width(); ++x) {
                const bool sea = isSea(map->indices.constScanLine(y)[(x + 249) % map->indices.width()]);
                wrong += (zone.pixel(origin.x() + x, origin.y() + y) & 0xffffff) != (sea ? 0x00007fu : 0x007f00u);
            }
        QCOMPARE(wrong, 0);
    }

    void importMedia()
    {
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        setup::Imported n;
        QString error;
        QVERIFY2(setup::importMedia(m_realIso, &n, {}, nullptr, &error), qPrintable(error));
        QVERIFY(n.fonts > 100);
        QCOMPARE(n.styles, 1);
        QVERIFY(n.themes >= 2);
        QCOMPARE(n.wallpapers, 31);   // XP's list: 20 in Web\\Wallpaper, 11 desktop wallpapers
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Luna/luna.msstyles")));
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Luna/Luna.theme")));
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Windows Classic.theme")));
        QVERIFY(QFileInfo::exists(paths::wallpapers() + QLatin1String("/Bliss.jpg")));
        QVERIFY(QFileInfo::exists(paths::localShare() + QLatin1String("/fonts/xp/tahoma.ttf")));
        QCOMPARE(State::load().media, m_realIso);
        // XP's names, its small tiles included, and nothing else from I386.
        for (const char *name : {"Red moon desert.jpg", "Windows XP.jpg", "Stonehenge.jpg", "Coffee Bean.bmp", "Blue Lace 16.bmp", "Zapotec.bmp"})
            QVERIFY2(QFileInfo::exists(paths::wallpapers() + QLatin1Char('/') + QLatin1String(name)), name);
        for (const char *name : {"setup_w.bmp", "table.bmp", "wpaback.jpg", "stoneh.jpg", "winnt.bmp", "bluehill.jpg"})
            QVERIFY2(!QFileInfo::exists(paths::wallpapers() + QLatin1Char('/') + QLatin1String(name)), name);
    }

    void importMediaTidiesOldWallpapers()
    {
        // An older import's 8.3 names become XP's (a saved theme naming one
        // follows); what XP didn't list goes; the user's own stays.
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        const QString dir = paths::wallpapers();
        writeFile(dir + QLatin1String("/stoneh.jpg"), "old");
        writeFile(dir + QLatin1String("/setup_w.bmp"), "old");
        writeFile(dir + QLatin1String("/mine.jpg"), "mine");
        const QString preset = paths::themes() + QLatin1String("/Saved/Old.theme");
        writeFile(preset, ("[Control Panel\\Desktop]\r\nWallpaper=" + dir + "/stoneh.jpg\r\n").toUtf8());
        QString error;
        QVERIFY2(setup::importMedia(m_realIso, nullptr, {}, nullptr, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir + QLatin1String("/stoneh.jpg")));
        QVERIFY(!QFileInfo::exists(dir + QLatin1String("/setup_w.bmp")));
        QVERIFY(QFileInfo::exists(dir + QLatin1String("/Stonehenge.jpg")));
        QVERIFY(QFileInfo::exists(dir + QLatin1String("/mine.jpg")));
        QFile f(preset);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains((dir + QLatin1String("/Stonehenge.jpg")).toUtf8()));
    }

    void infFields()
    {
        // XP's INF quoting: commas split fields outside quotes, "" is a quote
        // inside them, ; ends the line, %KEY% from [Strings].
        const Inf inf = Inf::parse("[Strings]\r\nName = \"Hands 1\"\r\nCursors=cursors\r\n");
        const QStringList f = inf.fields(QStringLiteral("HKLM,%NAME%,,\"\"\"%10%\\%Cursors%\\a.cur,,b.ani\"\"\" ; comment"));
        QCOMPARE(f.size(), 4);
        QCOMPARE(f.at(1), QStringLiteral("Hands 1"));
        QCOMPARE(f.at(3), QStringLiteral("\"%10%\\cursors\\a.cur,,b.ani\""));
    }

    void pointerSchemesFromTheMedia()
    {
        // XP's 19 schemes, each naming a file per role (or Windows Default's),
        // and a theme written from one with every name apps ask for.
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State s = State::load();
        s.media = m_realIso;
        QVERIFY(s.save());
        const QList<pointers::Scheme> all = pointers::schemes();
        QCOMPARE(all.size(), 19);
        const auto dino = std::find_if(all.begin(), all.end(), [](const pointers::Scheme &x) { return x.name == QLatin1String("Dinosaur"); });
        QVERIFY(dino != all.end());
        QCOMPARE(dino->files.value(pointers::AppStarting), QStringLiteral("dinosaur.ani"));
        QVERIFY(dino->files.value(pointers::Hand).isEmpty());   // (Windows Default's)
        QVERIFY(pointers::frames(*dino, pointers::AppStarting).size() > 1);   // animated
        const QList<CursorFrame> arrow = pointers::frames(pointers::defaultScheme(), pointers::Arrow);
        QCOMPARE(arrow.size(), 1);
        QCOMPARE(arrow.first().image.size(), QSize(32, 32));
        const QString dir = m_home.filePath(QStringLiteral("pointers"));
        int size = 0;
        QString error;
        QVERIFY2(pointers::writeTheme(*dino, true, dir, &size, &error), qPrintable(error));
        QCOMPARE(size, 32);
        for (int role = 0; role < pointers::RoleCount; ++role)
            for (const QString &name : pointers::xcursorNames(role))
                QVERIFY2(QFileInfo(dir + QLatin1String("/cursors/") + name).isFile(), qPrintable(name));
        QFile index(dir + QLatin1String("/index.theme"));
        QVERIFY(index.open(QIODevice::ReadOnly) && index.readAll().contains("Name=Dinosaur (XP)"));

        // Applied: a theme per scheme, at its size, the user's own kept for reset.
        RecordingSession session;
        session.config.insert(QStringLiteral("kcminputrc|Mouse|cursorTheme"), QStringLiteral("breeze_cursors"));
        State state;
        QVERIFY2(pointers::apply(session, state, *dino, false, &error), qPrintable(error));
        const QStringList run = session.runs.last();
        QCOMPARE(run.mid(0, 3), (QStringList{QStringLiteral("plasma-apply-cursortheme"), QStringLiteral("--size"), QStringLiteral("32")}));
        QVERIFY(run.last().startsWith(QLatin1String("xplasma-pointers-")));
        QCOMPARE(session.config.value(QStringLiteral("kcminputrc|Mouse|cursorSize")), QStringLiteral("32"));
        QVERIFY(QFileInfo(paths::localShare() + QLatin1String("/icons/") + run.last() + QLatin1String("/cursors/default")).exists());
        // (and in ~/.icons, where every Xcursor library looks)
        QVERIFY(QFileInfo(paths::home() + QLatin1String("/.icons/") + run.last() + QLatin1String("/cursors/default")).exists());
        QCOMPARE(pointers::applied(state)->first.name, QStringLiteral("Dinosaur"));
        pointers::revert(session, state);
        QCOMPARE(session.runs.last().last(), QStringLiteral("breeze_cursors"));
        QVERIFY(!session.config.contains(QStringLiteral("kcminputrc|Mouse|cursorSize")));   // (it had none)
        QVERIFY(!pointers::applied(state));
        QVERIFY(QDir(paths::localShare() + QLatin1String("/icons")).entryList({QStringLiteral("xplasma-pointers-*")}, QDir::Dirs).isEmpty());
        QVERIFY(!QFileInfo::exists(paths::home() + QLatin1String("/.icons")));   // (its links gone, and it with them)
    }

    void soundSchemes()
    {
        // Every event under Plasma's names; Windows Default as XP's registry
        // has it; a sound for each of WINDOWS\Media's names.
        QSet<QString> names;
        for (const sounds::Event &e : sounds::events()) {
            QVERIFY(!e.names.isEmpty());
            for (const QString &n : e.names) {
                QVERIFY2(!names.contains(n), qPrintable(n));   // (one event per name)
                names.insert(n);
            }
        }
        const sounds::Scheme xp = sounds::defaultScheme();
        QCOMPARE(xp.sounds.value(QStringLiteral("SystemStart")), QStringLiteral("Windows XP Startup.wav"));
        QVERIFY(!xp.sounds.contains(QStringLiteral("SystemQuestion")));   // (XP's was quiet)
        for (const QString &sound : std::as_const(xp.sounds))
            QVERIFY2(sounds::mediaSounds().contains(sound), qPrintable(sound));
        QCOMPARE(sounds::Scheme::fromJson(xp.toJson()), xp);
        State saved;
        sounds::saveScheme(saved, sounds::Scheme{QStringLiteral("Mine"), {{QStringLiteral(".Default"), QStringLiteral("chord.wav")}}});
        QCOMPARE(sounds::savedSchemes(saved).size(), 1);
        sounds::deleteScheme(saved, QStringLiteral("Mine"));
        QVERIFY(sounds::savedSchemes(saved).isEmpty());

        // No Sounds: silence under every name (no other theme's stands in).
        const QString quiet = m_home.filePath(QStringLiteral("quiet"));
        QString error;
        QVERIFY2(sounds::writeTheme(sounds::Scheme{QStringLiteral("No Sounds"), {}}, quiet, &error), qPrintable(error));
        for (const QString &n : std::as_const(names))
            QVERIFY2(QFileInfo(quiet + QLatin1String("/stereo/") + n + QLatin1String(".wav")).size() == 44 + 2204, qPrintable(n));

        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State s = State::load();
        s.media = m_realIso;
        QVERIFY(s.save());
        // Windows Default's sounds from the media, under each name; the
        // log-in and lock sounds switched on; the user's theme kept for reset.
        RecordingSession session;
        session.config.insert(QStringLiteral("kdeglobals|Sounds|Theme"), QStringLiteral("oxygen"));
        State state;
        QVERIFY2(sounds::apply(session, state, xp, &error), qPrintable(error));
        const QString id = session.config.value(QStringLiteral("kdeglobals|Sounds|Theme"));
        QVERIFY(id.startsWith(QLatin1String("xplasma-sounds-")));
        const QString stereo = paths::localShare() + QLatin1String("/sounds/") + id + QLatin1String("/stereo/");
        QFile login(stereo + QLatin1String("desktop-login.wav"));
        QVERIFY(login.open(QIODevice::ReadOnly));
        const QByteArray wav = login.readAll();
        QVERIFY(wav.startsWith("RIFF") && wav.size() > 400000);   // (XP's Startup: 4.8 s)
        QVERIFY(QFileInfo(stereo + QLatin1String("dialog-question.wav")).size() < 3000);   // (quiet)
        QCOMPARE(session.config.value(QStringLiteral("plasma_workspace.notifyrc|Event/startkde|Action")), QStringLiteral("Sound"));
        QCOMPARE(session.config.value(QStringLiteral("ksmserver.notifyrc|Event/locked|Sound")), QStringLiteral("service-logout"));
        QCOMPARE(sounds::applied(state)->name, QStringLiteral("Windows Default"));

        // Another scheme: its own theme, the last one gone.
        sounds::Scheme mine = xp;
        mine.name = QStringLiteral("Mine");
        mine.sounds.remove(QStringLiteral("SystemStart"));
        QVERIFY2(sounds::apply(session, state, mine, &error), qPrintable(error));
        QVERIFY(session.config.value(QStringLiteral("kdeglobals|Sounds|Theme")) != id);
        QVERIFY(!QFileInfo::exists(stereo));
        QVERIFY(!session.config.contains(QStringLiteral("plasma_workspace.notifyrc|Event/startkde|Action")));

        sounds::revert(session, state);
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|Sounds|Theme")), QStringLiteral("oxygen"));
        QVERIFY(!session.config.contains(QStringLiteral("ksmserver.notifyrc|Event/locked|Action")));
        QVERIFY(!sounds::applied(state));
        QVERIFY(QDir(paths::localShare() + QLatin1String("/sounds")).entryList({QStringLiteral("xplasma-sounds-*")}, QDir::Dirs).isEmpty());
    }

    void wallpaperNamesFromInfs()
    {
        // XP's setup INFs, as they're written: UTF-16 or ANSI, %strings%,
        // "dest, source", comments; only the wallpaper copy lists count.
        const QString shl = QStringLiteral(
            "[DestinationDirs]\r\nWallpaper.CopyFiles = 10,Web\\Wallpaper\r\n"
            "[Wallpaper.CopyFiles]\r\n\"%MOON_JPG_NAME%\",moon.jpg\r\ndefault.jpg,bliss.jpg\r\n\r\n"
            "[OldWinntBMP.CopyFiles]\r\nwinnt.bmp,win32pro.bmp\r\n"
            "[Strings]\r\nMOON_JPG_NAME = \"Moon flower.jpg\"\r\nBLISS_JPG_NAME = \"Bliss.jpg\"\r\n");
        QByteArray wide("\xff\xfe");
        wide += QByteArray(reinterpret_cast<const char *>(shl.utf16()), shl.size() * 2);
        const QByteArray accessor = "[DeskpaperCopyFilesSys]\r\n%Coffeebn%,             coffeebn.bmp ; a tile\r\n"
                                    "[DeskpaperOldCopyFilesSys]\r\narches.bmp\r\n[Strings]\r\nCoffeebn        = \"Coffee Bean.bmp\"\r\n";
        const QHash<QString, QString> names = setup::wallpaperNames({wide, accessor});
        QCOMPARE(names.value(QStringLiteral("moon.jpg")), QStringLiteral("Moon flower.jpg"));
        QCOMPARE(names.value(QStringLiteral("bliss.jpg")), QStringLiteral("Bliss.jpg"));
        QCOMPARE(names.value(QStringLiteral("coffeebn.bmp")), QStringLiteral("Coffee Bean.bmp"));
        QCOMPARE(names.size(), 3);
    }

    void importMediaRollsBack()
    {
        // Not XP's media: nothing stays, the recorded media stays as it was.
        State s;
        s.media = QStringLiteral("/before.iso");
        QVERIFY(s.save());
        writeFile(m_home.filePath(QStringLiteral("notxp/I386/README.TXT")), "hi");
        writeFile(paths::themes() + QLatin1String("/Mine/mine.theme"), "[Theme]\n");
        QString error;
        QVERIFY(!setup::importMedia(m_home.filePath(QStringLiteral("notxp")), nullptr, {}, nullptr, &error));
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Mine/mine.theme")));
        QCOMPARE(State::load().media, QStringLiteral("/before.iso"));
    }

    void fetchPacks()
    {
        // archive.org, played by a server here: the item's metadata, then
        // its file.
        const QString pack = m_home.filePath(QStringLiteral("XP_Themes.zip"));
        QVERIFY(writeArchive(pack, QStringLiteral("zip"), {{"Royale/Royale.msstyles", "MZ-royale"}}));
        QFile packFile(pack);
        QVERIFY(packFile.open(QIODevice::ReadOnly));
        const QByteArray zip = packFile.readAll();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QStringList requested;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const QByteArray request = socket->readAll();
                const QString path = QString::fromUtf8(request.split(' ').value(1));
                requested << path;
                QByteArray body = path.startsWith(QLatin1String("/metadata/"))
                                      ? QByteArray(R"({"files":[{"name":"readme.txt","size":"3"},{"name":"XP_Themes.zip","size":")")
                                            + QByteArray::number(zip.size()) + "\"}]}"
                                      : zip;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        qputenv("XPLASMA_ARCHIVE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
        const setup::Download packs = setup::downloads().at(1);
        QString error;
        qint64 last = 0;
        QVERIFY2(setup::fetch(packs, {}, [&](qint64 done, qint64) { last = done; }, nullptr, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Royale/Royale.msstyles")));
        QVERIFY(QFileInfo::exists(paths::downloads() + QLatin1String("/XP_Themes.zip")));
        QCOMPARE(last, zip.size());
        QCOMPARE(requested, (QStringList{QStringLiteral("/metadata/windows-xp-official-themes"),
                                         QStringLiteral("/download/windows-xp-official-themes/XP_Themes.zip")}));
        // Downloaded once: the second time never touches the network.
        server.close();
        QVERIFY2(setup::fetch(packs, {}, {}, nullptr, &error), qPrintable(error));
        qunsetenv("XPLASMA_ARCHIVE_URL");
    }

    void preflight()
    {
        const auto checks = setup::preflight();
        QVERIFY(std::any_of(checks.begin(), checks.end(), [](const setup::Check &c) { return c.tool == QLatin1String("kwriteconfig6"); }));
    }

    // --- host programs --------------------------------------------------------

    void hostRun()
    {
        QStringList lines;
        host::Options options;
        options.onLine = [&](const QString &line) { lines << line; };
        const auto r = host::run({QStringLiteral("sh"), QStringLiteral("-c"),
                                  QStringLiteral("printf 'one\\ntwo\\r'; echo three >&2; exit 3")},
                                 options);
        QCOMPARE(r.exitCode, 3);
        QVERIFY(!r.ok());
        QCOMPARE(r.out, QByteArray("one\ntwo\r"));
        QCOMPARE(r.err, QByteArray("three\n"));
        lines.sort();
        QCOMPARE(lines, (QStringList{QStringLiteral("one"), QStringLiteral("three"), QStringLiteral("two")}));
    }

    void hostInput()
    {
        host::Options options;
        options.input = "fed through stdin";
        const auto r = host::run({QStringLiteral("cat")}, options);
        QVERIFY(r.ok());
        QCOMPARE(r.out, options.input);
    }

    void hostMissing()
    {
        const auto r = host::run({QStringLiteral("kwriteconfig6-surely-not-installed")});
        QCOMPARE(r.exitCode, 127);
        QVERIFY(r.err.contains("not found"));
        QVERIFY(host::installHint(QStringLiteral("kwriteconfig6")).contains(QLatin1String("KDE")));
    }

    void hostCancel()
    {
        std::atomic_bool cancel{false};
        host::Options options;
        options.cancel = &cancel;
        std::thread stopper([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            cancel = true;
        });
        QElapsedTimer clock;
        clock.start();
        const auto r = host::run({QStringLiteral("sleep"), QStringLiteral("30")}, options);
        stopper.join();
        QVERIFY(r.cancelled);
        QVERIFY(clock.elapsed() < 5000);

        host::Options timed;
        timed.timeoutMs = 200;
        QVERIFY(host::run({QStringLiteral("sleep"), QStringLiteral("30")}, timed).cancelled);
    }

    // --- state and adoption ---------------------------------------------------

    void stateRoundTrip()
    {
        QCOMPARE(State::load().appliedKind, QString());   // no file yet
        State s;
        s.appliedKind = QStringLiteral("style");
        s.style = QStringLiteral("/x/luna.msstyles");
        s.variant = QStringLiteral("NORMALBLUE_INI");
        s.media = QStringLiteral("/x/xp.iso");
        s.settings[QStringLiteral("titleShadows")] = true;
        QString error;
        QVERIFY2(s.save(&error), qPrintable(error));
        const State t = State::load();
        QCOMPARE(t.appliedKind, s.appliedKind);
        QCOMPARE(t.style, s.style);
        QCOMPARE(t.variant, s.variant);
        QCOMPARE(t.media, s.media);
        QCOMPARE(t.settings, s.settings);
    }

    void stateKeepsUnknownKeys()
    {
        QJsonObject json{{QStringLiteral("version"), 7}, {QStringLiteral("fromTheFuture"), QStringLiteral("keep me")}};
        writeFile(paths::statePath(), QJsonDocument(json).toJson());
        State s = State::load();
        s.media = QStringLiteral("/x/xp.iso");
        QVERIFY(s.save());
        QFile f(paths::statePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(saved[QStringLiteral("fromTheFuture")].toString(), QStringLiteral("keep me"));
        QCOMPARE(saved[QStringLiteral("media")].toString(), QStringLiteral("/x/xp.iso"));
    }

    void stateCorrupt()
    {
        writeFile(paths::statePath(), "{ not json");
        QCOMPARE(State::load().appliedKind, QString());
    }

    void adoption()
    {
        writeFile(paths::legacyData() + QLatin1String("/assets/themes/Luna/luna.msstyles"), "MZ");
        writeFile(paths::legacyData() + QLatin1String("/schemes/user/mine.json"), "{}");
        writeFile(paths::legacyCache() + QLatin1String("/downloads/small.iso"), "x");
        writeFile(paths::legacyCache() + QLatin1String("/downloads/xp.iso"), QByteArray(4096, 'x'));
        // Something XPLasma already has isn't replaced.
        writeFile(paths::schemes() + QLatin1String("/keep.theme"), "[x]");

        const QStringList log = adoptLegacy();
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Luna/luna.msstyles")));
        // ...with a link left where it was, for what still points there.
        QVERIFY(QFileInfo(paths::legacyData() + QLatin1String("/assets")).isSymLink());
        QVERIFY(QFileInfo::exists(paths::legacyData() + QLatin1String("/assets/themes/Luna/luna.msstyles")));
        QVERIFY(QFileInfo::exists(paths::legacyData() + QLatin1String("/schemes/user/mine.json")));
        QVERIFY(QFileInfo::exists(paths::schemes() + QLatin1String("/keep.theme")));
        QVERIFY(QFileInfo::exists(paths::downloads() + QLatin1String("/xp.iso")));
        QCOMPARE(State::load().media, paths::downloads() + QLatin1String("/xp.iso"));   // the largest
        QVERIFY(!log.isEmpty());

        // Once is enough.
        QVERIFY(adoptLegacy().isEmpty());
    }
};

int main(int argc, char **argv)
{
    // (Fonts need a GUI application; an offscreen one shows nothing.)
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // (XPLasma's style draws the Plasma theme's controls)
    // (Loaded now, while HOME is the real one: the XP fonts live there.)
    QFontDatabase::families();
    TestApp test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_app.moc"
