// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/cachepreferences.h"
#include "app/state.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class TestCachePreferences : public QObject {
    Q_OBJECT
    QTemporaryDir home;
    std::optional<bool> choose(bool checked, bool explicitChoice, QMessageBox::StandardButton reply, bool *shown = nullptr) {
        QTimer responder;
        responder.setSingleShot(true);
        connect(&responder, &QTimer::timeout, this, [=] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                    if (shown) *shown = true;
                    QCOMPARE(box->defaultButton(), box->button(QMessageBox::No));
                    QVERIFY(box->text().contains(QStringLiteral("Appearance")));
                    box->button(reply)->click();
                }
            }
        });
        responder.start(0);
        QString error;
        const auto result = xpl::editor::cachepreferences::choose(nullptr, checked, explicitChoice, &error);
        responder.stop();
        if (!error.isEmpty()) QTest::qFail(qPrintable(error), __FILE__, __LINE__);
        return result;
    }
private Q_SLOTS:
    void initTestCase() { QVERIFY(home.isValid()); qputenv("HOME", QFile::encodeName(home.path())); }
    void init() { QDir(home.path()).removeRecursively(); QDir().mkpath(home.path()); }
    void asksOnceAndRemembersYes() {
        bool shown = false;
        const auto first = choose(false, false, QMessageBox::Yes, &shown);
        QVERIFY(shown); QVERIFY(first.has_value()); QVERIFY(*first);
        QVERIFY(xpl::State::load().settings.value(QStringLiteral("themeCacheEnabled")).toBool());
        shown = false;
        QVERIFY(choose(true, false, QMessageBox::No, &shown).value_or(false));
        QVERIFY(!shown);
    }
    void noIsRememberedAndCanBeEnabledLater() {
        const auto first = choose(false, false, QMessageBox::No);
        QVERIFY(first.has_value()); QVERIFY(!*first);
        bool shown = false;
        const auto later = choose(true, true, QMessageBox::No, &shown);
        QVERIFY(later.has_value()); QVERIFY(*later); QVERIFY(!shown);
        QVERIFY(xpl::State::load().settings.value(QStringLiteral("themeCacheEnabled")).toBool());
    }
    void cancelDoesNotRecordAChoice() {
        QVERIFY(!choose(false, false, QMessageBox::Cancel).has_value());
        QVERIFY(!xpl::State::load().settings.contains(QStringLiteral("themeCacheEnabled")));
    }
    void explicitCheckboxChoiceDoesNotPrompt() {
        bool shown = false;
        const auto result = choose(false, true, QMessageBox::Yes, &shown);
        QVERIFY(result.has_value()); QVERIFY(!*result); QVERIFY(!shown);
        QVERIFY(xpl::State::load().settings.contains(QStringLiteral("themeCacheEnabled")));
    }
};
QTEST_MAIN(TestCachePreferences)
#include "test_cachepreferences.moc"
