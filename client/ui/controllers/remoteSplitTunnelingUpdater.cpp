#include "remoteSplitTunnelingUpdater.h"

#include "core/controllers/ipSplitTunnelingController.h"
#include "ui/controllers/connectionUiController.h"
#include "ui/models/ipSplitTunnelingModel.h"

#include <QCryptographicHash>
#include <QDateTime>
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

QByteArray backupJson(const QVariantMap &sites, int mode)
{
    QJsonObject map;
    for (auto it = sites.cbegin(); it != sites.cend(); ++it)
        map.insert(it.key(), QJsonArray::fromStringList(it.value().toStringList()));
    return QJsonDocument(QJsonObject{
        {QStringLiteral("routeMode"), mode},
        {QStringLiteral("createdUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("sites"), map}
    }).toJson(QJsonDocument::Indented);
}
}

RemoteSplitTunnelingUpdater::RemoteSplitTunnelingUpdater(
    IpSplitTunnelingController *sites, IpSplitTunnelingModel *model,
    ConnectionUiController *connection, QObject *parent)
    : QObject(parent), m_sites(sites), m_model(model), m_connection(connection),
      m_settings(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                     .filePath(QStringLiteral("remote-split-tunneling.ini")),
                 QSettings::IniFormat)
{
    // Create the parent directory before the first subscription write.
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
    if (index < 0 || index > 3 || index == sourceIndex())
        return;
    m_settings.setValue(QStringLiteral("sourceIndex"), index);
    clearPending();
    emit settingsChanged();
}

void RemoteSplitTunnelingUpdater::setCustomUrl(const QString &url)
{
    if (customUrl() == url.trimmed())
        return;
    m_settings.setValue(QStringLiteral("customUrl"), url.trimmed());
    clearPending();
    emit settingsChanged();
}

void RemoteSplitTunnelingUpdater::setAutoUpdate(bool enabled)
{
    if (autoUpdate() == enabled)
        return;
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

QString RemoteSplitTunnelingUpdater::modeGroup(int mode) const
{
    return QStringLiteral("routeMode-%1").arg(mode);
}

void RemoteSplitTunnelingUpdater::setStatus(const QString &text)
{
    if (m_status == text)
        return;
    m_status = text;
    emit statusChanged();
}

void RemoteSplitTunnelingUpdater::clearPending()
{
    const bool wasPending = pending();
    m_pendingSites.clear();
    m_pendingSha.clear();
    m_pendingEtag.clear();
    m_pendingUrl.clear();
    m_pendingMode = -1;
    if (wasPending)
        emit pendingChanged();
}

bool RemoteSplitTunnelingUpdater::parseList(
    const QByteArray &bytes, QMap<QString, QStringList> &sites, QString &error)
{
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
    const QStringList &previousManaged,
    const QMap<QString, QStringList> &incoming)
{
    QMap<QString, QStringList> result = current;
    for (const QString &host : previousManaged)
        result.remove(host);
    for (auto it = incoming.cbegin(); it != incoming.cend(); ++it) {
        QStringList ips = it.value();
        if (ips.isEmpty() && previousManaged.contains(it.key()))
            ips = current.value(it.key());
        result.insert(it.key(), ips);
    }
    return result;
}

void RemoteSplitTunnelingUpdater::checkNow()
{
    if (m_busy)
        return;
    const QString url = sourceUrl();
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme() != QStringLiteral("https") ||
        parsed.host().isEmpty() || !parsed.userInfo().isEmpty()) {
        setStatus(tr("Provide a valid HTTPS source URL."));
        return;
    }
    const int mode = static_cast<int>(m_sites->getRouteMode());
    if (pending() && m_pendingUrl == url && m_pendingMode == mode) {
        applyPending();
        return;
    }
    clearPending();

    const QString group = modeGroup(mode);
    const QString appliedUrl = m_settings.value(group + QStringLiteral("/url")).toString();
    QNetworkRequest request(parsed);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("User-Agent", "AmneziaVPN/remote-split-tunneling");
    if (appliedUrl == url) {
        const QByteArray etag = m_settings.value(group + QStringLiteral("/etag")).toString().toUtf8();
        if (!etag.isEmpty()) request.setRawHeader("If-None-Match", etag);
    }
    m_busy = true;
    emit busyChanged();
    setStatus(tr("Checking list updates..."));
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, mode]() {
        const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString responseEtag = QString::fromUtf8(reply->rawHeader("ETag"));
        const auto error = reply->error();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (url != sourceUrl() || mode != static_cast<int>(m_sites->getRouteMode())) {
            finishDownload(false, tr("Source or route mode changed. Check again."));
            return;
        }
        if (httpCode == 304 && error == QNetworkReply::NoError) {
            finishDownload(true, tr("The list is already up to date."));
            return;
        }
        if (error != QNetworkReply::NoError || httpCode != 200 || body.size() > kMaxBytes) {
            finishDownload(false, tr("Unable to download the list (HTTP %1).").arg(httpCode));
            return;
        }
        QMap<QString, QStringList> sites;
        QString parseError;
        if (!parseList(body, sites, parseError)) {
            finishDownload(false, parseError);
            return;
        }
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex());
        const QString prefix = modeGroup(mode);
        if (m_settings.value(prefix + QStringLiteral("/url")).toString() == url &&
            m_settings.value(prefix + QStringLiteral("/sha")).toString() == sha) {
            finishDownload(true, tr("The list is already up to date."));
            return;
        }
        if (m_settings.value(prefix + QStringLiteral("/skippedUrl")).toString() == url &&
            m_settings.value(prefix + QStringLiteral("/skippedSha")).toString() == sha) {
            finishDownload(true, tr("This list version was skipped. Waiting for a newer version."));
            return;
        }
        const int priorCount = m_settings.value(prefix + QStringLiteral("/managed")).toStringList().size();
        if (priorCount >= 20 &&
            m_settings.value(prefix + QStringLiteral("/url")).toString() == url &&
            sites.size() * 2 < priorCount) {
            finishDownload(false, tr("List shrank by more than 50%; automatic replacement blocked."));
            return;
        }
        m_pendingSites = sites;
        m_pendingSha = sha;
        m_pendingEtag = responseEtag;
        m_pendingUrl = url;
        m_pendingMode = mode;
        emit pendingChanged();
        finishDownload(true);
        applyPending();
    });
}

void RemoteSplitTunnelingUpdater::skipPending()
{
    if (!pending()) return;
    const QString prefix = modeGroup(m_pendingMode);
    m_settings.setValue(prefix + QStringLiteral("/skippedSha"), m_pendingSha);
    m_settings.setValue(prefix + QStringLiteral("/skippedUrl"), m_pendingUrl);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        setStatus(tr("Could not record the skipped version."));
        return;
    }
    clearPending();
    setStatus(tr("Version skipped. Newer updates will still be offered."));
}

void RemoteSplitTunnelingUpdater::finishDownload(bool /*ok*/, const QString &message)
{
    m_busy = false;
    emit busyChanged();
    if (!message.isEmpty()) setStatus(message);
}

void RemoteSplitTunnelingUpdater::applyPending()
{
    if (!pending()) return;
    if (m_pendingUrl != sourceUrl() ||
        m_pendingMode != static_cast<int>(m_sites->getRouteMode())) {
        clearPending();
        setStatus(tr("Source or routing mode changed; check for updates again."));
        return;
    }
    if (m_connection->isConnected() || m_connection->isConnectionInProgress()) {
        setStatus(tr("Disconnect the VPN, then press Update now to apply this list."));
        return;
    }

    const QString prefix = modeGroup(m_pendingMode);
    const QStringList oldManaged = m_settings.value(prefix + QStringLiteral("/managed")).toStringList();
    QMap<QString, QStringList> current;
    for (const auto &site : m_sites->getCurrentSites())
        current.insert(site.first, site.second);
    const QMap<QString, QStringList> merged =
        mergeSites(current, oldManaged, m_pendingSites);

    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(directory)) {
        setStatus(tr("Cannot create the local backup directory."));
        return;
    }
    QSaveFile backup(QDir(directory).filePath(QStringLiteral("remote-split-backup-mode-%1.json").arg(m_pendingMode)));
    const QByteArray backupData = backupJson(current, m_pendingMode);
    if (!backup.open(QIODevice::WriteOnly) ||
        backup.write(backupData) != backupData.size() || !backup.commit()) {
        setStatus(tr("Could not save a backup; no rules were changed."));
        return;
    }

    m_sites->addSites(merged, true);
    m_model->updateModel(m_sites->getCurrentSites());
    m_settings.setValue(prefix + QStringLiteral("/url"), m_pendingUrl);
    m_settings.setValue(prefix + QStringLiteral("/sha"), m_pendingSha);
    m_settings.setValue(prefix + QStringLiteral("/etag"), m_pendingEtag);
    m_settings.setValue(prefix + QStringLiteral("/managed"), m_pendingSites.keys());
    m_settings.remove(prefix + QStringLiteral("/skippedSha"));
    m_settings.remove(prefix + QStringLiteral("/skippedUrl"));
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        QMap<QString, QStringList> previous;
        for (auto it = current.cbegin(); it != current.cend(); ++it)
            previous.insert(it.key(), it.value().toStringList());
        m_sites->addSites(previous, true);
        m_model->updateModel(m_sites->getCurrentSites());
        setStatus(tr("Saving subscription state failed; previous rules restored."));
        return;
    }
    const int count = m_pendingSites.size();
    clearPending();
    setStatus(tr("Updated %1 subscription entries. User entries were preserved.").arg(count));
}