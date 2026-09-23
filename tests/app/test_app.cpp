// QtTest for the app library: archives, host programs, state, adoption.
//
// Every test runs with HOME moved to a scratch directory. The XP install ISO,
// when xp2plasma or XPLasma downloaded one on this machine, is used for the
// real-media tests (skipped otherwise).
#include "app/adopt.h"
#include "app/archive.h"
#include "app/host.h"
#include "app/paths.h"
#include "app/state.h"

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
