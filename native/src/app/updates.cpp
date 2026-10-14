// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/updates.h"

#include "app/project.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

namespace xpl::updates {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

} // namespace

QString url()
{
    const QString env = qEnvironmentVariable("XPLASMA_UPDATES_URL");
    return env.isEmpty() ? QString::fromUtf8(project::kUpdatesUrl) : env;
}

std::optional<Release> parse(const QByteArray &json, QString *error)
{
    QJsonParseError why;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &why);
    if (!doc.isObject()) {
        fail(error, QStringLiteral("not an XPlasma release file (%1)").arg(why.errorString()));
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    // (the release's number: 2, or "2")
    const QJsonValue v = o.value(QLatin1String("version"));
    bool whole = false;
    const int version = v.isDouble() && v.toDouble() == double(v.toInt()) ? (whole = true, v.toInt()) : v.toString().trimmed().toInt(&whole);
    Release r{version, o.value(QLatin1String("page")).toString().trimmed(), o.value(QLatin1String("notes")).toString().trimmed()};
    if (!whole || r.version < 1) {
        fail(error, QStringLiteral("the release file has no release number"));
        return std::nullopt;
    }
    const QUrl page(r.page);
    if (!page.isValid() || (page.scheme() != QLatin1String("https") && page.scheme() != QLatin1String("http"))) {
        fail(error, QStringLiteral("the release file's page isn't a web address"));
        return std::nullopt;
    }
    return r;
}

bool due(const State &state, const QDateTime &now)
{
    const QDateTime last = QDateTime::fromString(state.settings.value(QStringLiteral("updatesChecked")).toString(), Qt::ISODate);
    return !last.isValid() || last.secsTo(now) >= 24 * 60 * 60 || last > now;
}

void checked(State &state, const QDateTime &now)
{
    state.settings.insert(QStringLiteral("updatesChecked"), now.toString(Qt::ISODate));
}

bool worthTelling(const State &state, const Release &release)
{
    return release.version > project::kVersion && state.settings.value(QStringLiteral("updatesDeclined")).toInt() != release.version;
}

void decline(State &state, const Release &release)
{
    state.settings.insert(QStringLiteral("updatesDeclined"), release.version);
}

std::optional<Release> fetch(const QString &url, QString *error)
{
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("XPlasma/%1").arg(project::kVersion));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network.get(request);
    QEventLoop loop;
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        fail(error, QStringLiteral("no answer from %1").arg(url));
        return std::nullopt;
    }
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(error, reply->errorString());
        return std::nullopt;
    }
    return parse(reply->readAll(), error);
}

} // namespace xpl::updates
