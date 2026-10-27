#include "remoteSplitTunnelingUpdater.h"

#include "core/controllers/ipSplitTunnelingController.h"
#include "ui/controllers/connectionUiController.h"
#include "ui/models/ipSplitTunnelingModel.h"

#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
constexpr auto kDomainList = "https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia.json";
constexpr auto kLiteList = "https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia-ip-lite.json";
constexpr auto kFullList = "https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia-ip.json";
constexpr int kMaxBytes = 8 * 1024 * 1024;
constexpr int kMaxEntries = 100000;
constexpr int kRefreshIntervalMs = 6 * 60 * 60 * 1000;

bool isIPv4(const QString &value)
{
    QHostAddress address;
    return address.setAddress(value) && address.protocol() == QAbstractSocket::IPv4Protocol;
}

bool isIpOrCidr(const QString &value)
{
    const auto parts = value.split('/');
    if (parts.size() < 1 || parts.size() > 2 || !isIPv4(parts[0]))
        return false;
    if (parts.size() == 2) {
        bool ok = false;
        const int prefix = parts[1].toInt(&ok);
        return ok && prefix >= 0 && prefix <= 32;
    }
    return true;
}

QString normalizedHost(QString hostname)
{
    hostname = hostname.trimmed();
    for (const QString &prefix : {QStringLiteral("https://"), QStringLiteral("http://"),
                                  QStringLiteral("ftp://")}) {
        if (hostname.startsWith(prefix, Qt::CaseInsensitive)) {
            hostname.remove(0, prefix.size());
            break;
        }
    }
    if (!isIpOrCidr(hostname))
        hostname = hostname.section('/', 0, 0);
    return hostname.toLower();
}

bool isValidHost(const QString &host)
{
    if (isIpOrCidr(host))
        return true;
    static const QRegularExpression domain(
        QStringLiteral(R"(^(?:[a-z0-9_-]+\.)+[a-z0-9_-]+$)"));
    // Do not interpret invalid dotted-decimal IPv4 addresses as domain names.
    static const QRegularExpression numeric(QStringLiteral(R"(^[0-9.]+$)"));
    return host.size() <= 255 && !numeric.match(host).hasMatch()
        && domain.match(host).hasMatch();
}

}

RemoteSplitTunnelingUpdater::RemoteSplitTunnelingUpdater(
    IpSplitTunnelingController *sites, IpSplitTunnelingModel *model,
    ConnectionUiController *connection, QObject *parent)
    : QObject(parent), m_sites(sites), m_model(model), m_connection(connection),
      m_settings(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                     .filePath(QStringLiteral("remote-split-tunneling.ini")), QSettings::IniFormat)
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    m_timer.setInterval(kRefreshIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &RemoteSplitTunnelingUpdater::checkNow);
    if (autoUpdate()) {
        m_timer.start();
        QTimer::singleShot(0, this, &RemoteSplitTunnelingUpdater::checkNow);
    }
}

int RemoteSplitTunnelingUpdater::sourceIndex() const
{
    return m_settings.value(QStringLiteral("sourceIndex"), 0).toInt();
}

QString RemoteSplitTunnelingUpdater::customUrl() const
{
    return m_settings.value(QStringLiteral("customUrl")).toString();
}

bool RemoteSplitTunnelingUpdater::autoUpdate() const
{
    return m_settings.value(QStringLiteral("autoUpdate"), false).toBool();
}

void RemoteSplitTunnelingUpdater::setSourceIndex(int index)
{
    if (index < 0 || index > 3 || index == sourceIndex()) return;
    m_settings.setValue(QStringLiteral("sourceIndex"), index);
    emit settingsChanged();
}

void RemoteSplitTunnelingUpdater::setCustomUrl(const QString &url)
{
    if (customUrl() == url.trimmed()) return;
    m_settings.setValue(QStringLiteral("customUrl"), url.trimmed());
    emit settingsChanged();
}

void RemoteSplitTunnelingUpdater::setAutoUpdate(bool enabled)
{
    if (autoUpdate() == enabled) return;
    m_settings.setValue(QStringLiteral("autoUpdate"), enabled);
    if (enabled) {
        m_timer.start();
        QTimer::singleShot(0, this, &RemoteSplitTunnelingUpdater::checkNow);
    } else {
        m_timer.stop();
    }
    emit settingsChanged();
}

QString RemoteSplitTunnelingUpdater::sourceUrl() const
{
    switch (sourceIndex()) {
    case 0: return QString::fromLatin1(kDomainList);
    case 1: return QString::fromLatin1(kLiteList);
    case 2: return QString::fromLatin1(kFullList);
    case 3: return customUrl();
    default: return {};
    }
}

void RemoteSplitTunnelingUpdater::setStatus(const QString &value)
{
    if (m_status == value) return;
    m_status = value;
    emit statusChanged();
}

bool RemoteSplitTunnelingUpdater::parseList(
    const QByteArray &bytes, QMap<QString, QStringList> &sites, QString &error)
{
    sites.clear();
    if (bytes.isEmpty() || bytes.size() > kMaxBytes) {
        error = tr("The list is empty or exceeds the size limit.");
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        error = tr("Invalid JSON list: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonArray array = document.array();
    if (array.isEmpty() || array.size() > kMaxEntries) {
        error = tr("The list is empty or unexpectedly large.");
        return false;
    }
    for (const auto &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        const QString host = normalizedHost(item.value(QStringLiteral("hostname")).toString());
        if (!isValidHost(host))
            continue;
        QStringList ips;
        if (item.value(QStringLiteral("ips")).isArray()) {
            for (const auto &ip : item.value(QStringLiteral("ips")).toArray()) {
                const QString candidate = ip.toString().trimmed();
                if (isIpOrCidr(candidate)) ips.append(candidate);
            }
        }
        const QString singleIp = item.value(QStringLiteral("ip")).toString().trimmed();
        if (isIpOrCidr(singleIp)) ips.append(singleIp);
        ips.removeDuplicates();
        if (sites.contains(host)) {
            QStringList combined = sites.value(host);
            combined.append(ips);
            combined.removeDuplicates();
            sites.insert(host, combined);
        } else {
            sites.insert(host, ips);
        }
    }
    if (sites.isEmpty()) {
        error = tr("No valid entries found in the list.");
        return false;
    }
    return true;
}



QMap<QString, QStringList> RemoteSplitTunnelingUpdater::mergeSites(
    const QMap<QString, QStringList> &current,
    const QMap<QString, QStringList> &previous,
    const QMap<QString, QStringList> &incoming)
{
    QMap<QString, QStringList> result = current;
    for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
        // A manually edited or deleted entry is no longer owned by the remote list.
        if (current.contains(it.key()) && current.value(it.key()) == it.value())
            result.remove(it.key());
    }
    for (auto it = incoming.cbegin(); it != incoming.cend(); ++it) {
        const bool untouched = previous.contains(it.key()) &&
            current.contains(it.key()) && current.value(it.key()) == previous.value(it.key());
        const bool newEntry = !previous.contains(it.key()) && !current.contains(it.key());
        if (untouched || newEntry) {
            const QStringList ips = it.value().isEmpty() && untouched
                ? previous.value(it.key()) : it.value();
            result.insert(it.key(), ips);
        }
    }
    return result;
}

void RemoteSplitTunnelingUpdater::checkNow()
{
    if (m_busy) return;
    if (m_connection->isConnected() || m_connection->isConnectionInProgress()) {
        setStatus(tr("Disconnect the VPN before updating the list."));
        return;
    }
    const QString url = sourceUrl();
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme() != QStringLiteral("https")
        || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()) {
        setStatus(tr("Provide a valid HTTPS source URL."));
        return;
    }

    const int mode = static_cast<int>(m_sites->getRouteMode());
    m_busy = true;
    emit busyChanged();
    setStatus(tr("Checking list updates..."));
    QNetworkRequest request(parsed);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 bytes, qint64) {
        if (bytes > kMaxBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, mode]() {
        const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool networkOk = reply->error() == QNetworkReply::NoError;
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        m_busy = false;
        emit busyChanged();

        if (sourceUrl() != url || static_cast<int>(m_sites->getRouteMode()) != mode) {
            setStatus(tr("Source or route mode changed. Check again."));
            return;
        }
        if (!networkOk || httpCode != 200 || body.size() > kMaxBytes) {
            setStatus(tr("Unable to download list (HTTP %1).").arg(httpCode));
            return;
        }

        QMap<QString, QStringList> incoming;
        QString error;
        if (!parseList(body, incoming, error)) { setStatus(error); return; }

        const QString key = QStringLiteral("routeMode-%1/").arg(mode);
        const QByteArray oldBytes = m_settings.value(key + QStringLiteral("managedJson")).toByteArray();
        QMap<QString, QStringList> previous;
        if (!oldBytes.isEmpty() && !parseList(oldBytes, previous, error)) {
            setStatus(tr("Previous subscription state is invalid; rules were not changed."));
            return;
        }
        if (m_settings.value(key + QStringLiteral("url")).toString() == url
            && oldBytes == body) {
            setStatus(tr("The list is already up to date."));
            return;
        }
        if (m_connection->isConnected() || m_connection->isConnectionInProgress()) {
            setStatus(tr("Disconnect the VPN before applying the list."));
            return;
        }

        QMap<QString, QStringList> current;
        for (const auto &site : m_sites->getCurrentSites())
            current.insert(site.first, site.second);
        const auto merged = mergeSites(current, previous, incoming);
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QSaveFile backup(QDir(directory).filePath(QStringLiteral("remote-split-backup-mode-%1.json").arg(mode)));
        const QByteArray backupData = m_sites->exportSitesToJson();
        if (!backup.open(QIODevice::WriteOnly) ||
            backup.write(backupData) != backupData.size() || !backup.commit()) {
            setStatus(tr("Cannot back up the current list; rules were not changed."));
            return;
        }

        m_sites->addSites(merged, true);
        m_model->updateModel(m_sites->getCurrentSites());
        m_settings.setValue(key + QStringLiteral("url"), url);
        m_settings.setValue(key + QStringLiteral("managedJson"), body);
        m_settings.sync();
        if (m_settings.status() != QSettings::NoError) {
            m_sites->addSites(current, true);
            m_model->updateModel(m_sites->getCurrentSites());
            setStatus(tr("Cannot save update state; previous rules restored."));
            return;
        }
        setStatus(tr("Remote list updated (%1 entries).").arg(incoming.size()));
    });
}
