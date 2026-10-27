#include "remoteSplitTunnelingUpdater.h"

#include "core/controllers/ipSplitTunnelingController.h"
#include "core/utils/networkUtilities.h"
#include "ui/controllers/connectionUiController.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
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

QString managedFilePath(int mode)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("remote-split-managed-mode-%1.json").arg(mode));
}

}

RemoteSplitTunnelingUpdater::RemoteSplitTunnelingUpdater(
    IpSplitTunnelingController *sites, ConnectionUiController *connection,
    QObject *parent)
    : QObject(parent), m_sites(sites), m_connection(connection),
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
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        error = tr("Invalid JSON list: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonArray items = document.array();
    if (items.isEmpty() || items.size() > kMaxEntries) {
        error = tr("The list is empty or unexpectedly large.");
        return false;
    }

    static const QRegularExpression domain(
        QStringLiteral(R"(^(?:[a-z0-9_-]+\.)+[a-z0-9_-]+$)"));
    static const QRegularExpression numeric(QStringLiteral(R"(^[0-9.]+$)"));
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        QString host = item.value(QStringLiteral("hostname")).toString().trimmed().toLower();
        for (const QString &prefix : {QStringLiteral("https://"), QStringLiteral("http://"),
                                      QStringLiteral("ftp://")}) {
            if (host.startsWith(prefix)) {
                host.remove(0, prefix.size());
                break;
            }
        }
        if (!NetworkUtilities::checkIpSubnetFormat(host))
            host = host.section('/', 0, 0);

        if (!NetworkUtilities::checkIpSubnetFormat(host) &&
            (host.size() > 253 || numeric.match(host).hasMatch() ||
             !domain.match(host).hasMatch()))
            continue;

        QStringList ips;
        if (item.value(QStringLiteral("ips")).isArray()) {
            for (const QJsonValue &ip : item.value(QStringLiteral("ips")).toArray()) {
                const QString address = ip.toString().trimmed();
                if (NetworkUtilities::checkIpSubnetFormat(address))
                    ips.append(address);
            }
        }
        const QString oneIp = item.value(QStringLiteral("ip")).toString().trimmed();
        if (NetworkUtilities::checkIpSubnetFormat(oneIp))
            ips.append(oneIp);
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
    const QMap<QString, QStringList> &incoming,
    QMap<QString, QStringList> *managed)
{
    if (managed)
        managed->clear();

    QMap<QString, QStringList> result = current;
    for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
        // Only remove entries still identical to the last remote-owned value.
        if (current.contains(it.key()) && current.value(it.key()) == it.value())
            result.remove(it.key());
    }
    for (auto it = incoming.cbegin(); it != incoming.cend(); ++it) {
        const bool untouched = previous.contains(it.key()) &&
            current.contains(it.key()) && current.value(it.key()) == previous.value(it.key());
        const bool newEntry = !previous.contains(it.key()) && !current.contains(it.key());
        if (untouched || newEntry) {
            // If DNS temporarily fails, keep the last known good address.
            const QStringList ips = it.value().isEmpty() && untouched
                ? current.value(it.key()) : it.value();
            result.insert(it.key(), ips);
            if (managed)
                managed->insert(it.key(), ips);
        }
    }
    return result;
}


void RemoteSplitTunnelingUpdater::complete(const QString &message)
{
    m_dnsPending.clear();
    m_remoteSites.clear();
    m_downloadBody.clear();
    m_downloadUrl.clear();
    m_busy = false;
    emit busyChanged();
    setStatus(message);
}

void RemoteSplitTunnelingUpdater::checkNow()
{
    if (m_busy)
        return;
    if (m_connection->isConnected() || m_connection->isConnectionInProgress()) {
        setStatus(tr("Disconnect the VPN before updating the list."));
        return;
    }

    const QString url = sourceUrl();
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme() != QStringLiteral("https") ||
        parsed.host().isEmpty() || !parsed.userInfo().isEmpty()) {
        setStatus(tr("Provide a valid HTTPS source URL."));
        return;
    }

    m_busy = true;
    emit busyChanged();
    setStatus(tr("Checking list updates..."));
    QNetworkRequest request(parsed);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 bytes, qint64) {
        if (bytes > kMaxBytes)
            reply->abort();
    });
    const int mode = static_cast<int>(m_sites->getRouteMode());
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, mode]() {
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!ok || http != 200 || body.size() > kMaxBytes) {
            complete(tr("Unable to download list (HTTP %1).").arg(http));
            return;
        }
        QMap<QString, QStringList> sites;
        QString error;
        if (!parseList(body, sites, error)) {
            complete(error);
            return;
        }
        if (url != sourceUrl() || mode != static_cast<int>(m_sites->getRouteMode())) {
            complete(tr("Source or route mode changed. Check again."));
            return;
        }

        m_downloadBody = body;
        m_downloadUrl = url;
        m_downloadMode = mode;
        m_remoteSites = sites;
        for (auto it = sites.cbegin(); it != sites.cend(); ++it) {
            if (it.value().isEmpty() && !NetworkUtilities::checkIpSubnetFormat(it.key()))
                m_dnsPending.enqueue(it.key());
        }
        if (m_dnsPending.size() > 5000) {
            complete(tr("Too many hostnames to resolve."));
            return;
        }
        if (m_dnsPending.isEmpty())
            applyRemoteList();
        else {
            setStatus(tr("Resolving list domains..."));
            resolveNext();
        }
    });
}

void RemoteSplitTunnelingUpdater::resolveNext()
{
    constexpr int maxConcurrent = 8;
    while (m_dnsActive < maxConcurrent && !m_dnsPending.isEmpty()) {
        const QString hostname = m_dnsPending.dequeue();
        ++m_dnsActive;
        QHostInfo::lookupHost(hostname, this, [this, hostname](const QHostInfo &info) {
            --m_dnsActive;
            QStringList ips;
            for (const QHostAddress &address : info.addresses()) {
                if (address.protocol() == QAbstractSocket::IPv4Protocol)
                    ips.append(address.toString());
            }
            ips.removeDuplicates();
            if (!ips.isEmpty())
                m_remoteSites[hostname] = ips;

            if (m_dnsPending.isEmpty() && m_dnsActive == 0)
                applyRemoteList();
            else
                resolveNext();
        });
    }
}

void RemoteSplitTunnelingUpdater::applyRemoteList()
{
    if (sourceUrl() != m_downloadUrl ||
        static_cast<int>(m_sites->getRouteMode()) != m_downloadMode ||
        m_connection->isConnected() || m_connection->isConnectionInProgress()) {
        complete(tr("VPN state or source changed. Check again."));
        return;
    }

    const QString key = QStringLiteral("routeMode-%1/").arg(m_downloadMode);
    const QString path = managedFilePath(m_downloadMode);
    QByteArray previousBytes;
    QString previousUrl;
    QFile old(path);
    if (old.exists()) {
        if (!old.open(QIODevice::ReadOnly)) {
            complete(tr("Cannot read previous list state."));
            return;
        }
        const QJsonDocument state = QJsonDocument::fromJson(old.readAll());
        if (!state.isObject() || !state.object().value(QStringLiteral("list")).isArray()) {
            complete(tr("Previous list state is invalid; rules were not changed."));
            return;
        }
        previousUrl = state.object().value(QStringLiteral("source")).toString();
        const QJsonArray list = state.object().value(QStringLiteral("list")).toArray();
        if (!list.isEmpty())
            previousBytes = QJsonDocument(list).toJson(QJsonDocument::Compact);
    } else {
        // Old builds kept the last imported list in preferences.
        previousBytes = m_settings.value(key + QStringLiteral("managedJson")).toByteArray();
        previousUrl = m_settings.value(key + QStringLiteral("url")).toString();
    }

    QMap<QString, QStringList> previous;
    QString error;
    if (!previousBytes.isEmpty() && !parseList(previousBytes, previous, error)) {
        complete(tr("Previous list state is invalid; rules were not changed."));
        return;
    }

    QMap<QString, QStringList> current;
    for (const auto &site : m_sites->getCurrentSites())
        current.insert(site.first, site.second);
    QMap<QString, QStringList> managed;
    const auto merged = mergeSites(current, previous, m_remoteSites, &managed);
    if (merged == current && managed == previous && previousUrl == m_downloadUrl) {
        complete(tr("The list is already up to date."));
        return;
    }

    const bool changed = merged != current;
    if (changed) {
        QSaveFile backup(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                             .filePath(QStringLiteral("remote-split-backup-mode-%1.json").arg(m_downloadMode)));
        const QByteArray original = m_sites->exportSitesToJson();
        if (!backup.open(QIODevice::WriteOnly) ||
            backup.write(original) != original.size() || !backup.commit()) {
            complete(tr("Cannot back up the current list; rules were not changed."));
            return;
        }
        m_sites->addSites(merged, true);
    }

    QJsonArray owned;
    for (auto it = managed.cbegin(); it != managed.cend(); ++it) {
        owned.append(QJsonObject{
            {QStringLiteral("hostname"), it.key()},
            {QStringLiteral("ips"), QJsonArray::fromStringList(it.value())}
        });
    }
    const QJsonObject snapshot{
        {QStringLiteral("source"), m_downloadUrl},
        {QStringLiteral("list"), owned}
    };
    const QByteArray data = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
    QSaveFile state(path);
    if (!state.open(QIODevice::WriteOnly) ||
        state.write(data) != data.size() || !state.commit()) {
        if (changed)
            m_sites->addSites(current, true);
        complete(tr("Cannot save update state; previous rules restored."));
        return;
    }
    m_settings.remove(key + QStringLiteral("managedJson"));
    m_settings.remove(key + QStringLiteral("url"));
    const int count = m_remoteSites.size();
    complete(tr("Remote list updated (%1 entries).").arg(count));
}

