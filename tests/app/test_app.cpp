// QtTest for the app library: archives, host programs, state, adoption.
//
// Every test runs with HOME moved to a scratch directory. The XP install ISO,
// when xp2plasma or XPLasma downloaded one on this machine, is used for the
// real-media tests (skipped otherwise).
#include "app/adopt.h"
#include "app/archive.h"
#include "app/hivedef.h"
#include "app/host.h"
#include "app/library.h"
#include "app/paths.h"
#include "app/state.h"
#include "core/themefile.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
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

} // namespace

class TestApp : public QObject
{
    Q_OBJECT

    QTemporaryDir m_home;
    QString m_realIso;
    QString m_realThemes;   // the asset library's themes, for the real-style tests

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

        const StyleInfo noir = Library::style(m_realThemes + QLatin1String("/Royale Noir/Royale Noir.msstyles"));
        QCOMPARE(noir.resolve(QStringLiteral("Metallic"), QStringLiteral("NormalSize")), QStringLiteral("NORMALDARKROYALE_INI"));

        const ThemeBundle xp = Library::bundle(m_realThemes + QLatin1String("/Luna/Luna.theme"));
        QCOMPARE(xp.label, QStringLiteral("Windows XP"));
        QCOMPARE(QFileInfo(xp.style).fileName(), QStringLiteral("luna.msstyles"));
        QCOMPARE(xp.variant, QStringLiteral("NORMALBLUE_INI"));

        const Library lib = Library::scan({m_realThemes});
        QVERIFY(lib.styles.size() >= 5);
        QVERIFY(lib.styles.first().official);
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

QTEST_GUILESS_MAIN(TestApp)
#include "test_app.moc"
