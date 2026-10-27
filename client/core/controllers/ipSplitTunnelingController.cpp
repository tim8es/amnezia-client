#include "ipSplitTunnelingController.h"
#include "core/utils/networkUtilities.h"
#include <QJsonObject>
#include <QDebug>
#include <QHostAddress>
#include <QRegularExpression>

IpSplitTunnelingController::IpSplitTunnelingController(SecureAppSettingsRepository* appSettingsRepository, QObject* parent)
    : QObject(parent),
      m_appSettingsRepository(appSettingsRepository)
{
    m_currentRouteMode = m_appSettingsRepository->routeMode();
    if (m_currentRouteMode == RouteMode::VpnAllSites) { // for old split tunneling configs
        m_appSettingsRepository->setRouteMode(RouteMode::VpnOnlyForwardSites);
        m_currentRouteMode = RouteMode::VpnOnlyForwardSites;
    }
    fillSites();
    m_dnsFlushTimer.setSingleShot(true);
    m_dnsFlushTimer.setInterval(200);
    connect(&m_dnsFlushTimer, &QTimer::timeout, this, &IpSplitTunnelingController::flushResolvedSites);
}

bool IpSplitTunnelingController::addSiteInternal(const QString &hostname, const QStringList &ips)
{
    QVariantMap existing = m_appSettingsRepository->vpnSites(m_currentRouteMode);
    if (existing.contains(hostname) && ips.isEmpty()) {
        return false;
    }

    for (int i = 0; i < m_sites.size(); i++) {
        if (m_sites[i].first == hostname) {
            bool changed = false;
            for (const QString &ip : ips) {
                if (!ip.isEmpty() && !m_sites[i].second.contains(ip)) {
                    m_sites[i].second.append(ip);
                    changed = true;
                }
            }
            if (!changed) {
                return false;
            }
            m_appSettingsRepository->addVpnSite(m_currentRouteMode, hostname, ips);
            return true;
        }
    }
    m_sites.append(qMakePair(hostname, ips));
    m_appSettingsRepository->addVpnSite(m_currentRouteMode, hostname, ips);
    return true;
}

void IpSplitTunnelingController::addSites(const QMap<QString, QStringList> &sites, bool replaceExisting)
{
    if (replaceExisting) {
        ++m_dnsRevision;
        m_dnsPending.clear();
        m_dnsReady.clear();
        m_dnsFlushTimer.stop();

        m_sites.clear();
        m_sites.reserve(sites.size());
        for (auto it = sites.cbegin(); it != sites.cend(); ++it)
            m_sites.append(qMakePair(it.key(), it.value()));

        m_appSettingsRepository->removeAllVpnSites(m_currentRouteMode);
        m_appSettingsRepository->addVpnSites(m_currentRouteMode, sites);
        queueUnresolvedSites();
        return;
    }

    for (auto it = sites.cbegin(); it != sites.cend(); ++it) {
        bool found = false;
        for (auto &site : m_sites) {
            if (site.first != it.key())
                continue;
            for (const QString &ip : it.value()) {
                if (!ip.isEmpty() && !site.second.contains(ip))
                    site.second.append(ip);
            }
            found = true;
            break;
        }
        if (!found)
            m_sites.append(qMakePair(it.key(), it.value()));
    }
    m_appSettingsRepository->addVpnSites(m_currentRouteMode, sites);
}

bool IpSplitTunnelingController::addSite(const QString &hostname)
{
    QString normalizedHostname = normalizeHostname(hostname);
    
    if (!validateHostname(normalizedHostname)) {
        return false;
    }
    
    if (NetworkUtilities::ipAddressWithSubnetRegExp().exactMatch(normalizedHostname)) {
        processSite(normalizedHostname, {});
        return true;
    }
    
    if (addSiteInternal(normalizedHostname, {})) {
        m_dnsPending.enqueue(normalizedHostname);
        startDnsLookups();
        return true;
    }
    
    return false;
}

bool IpSplitTunnelingController::removeSite(const QString &hostname)
{
    for (int i = 0; i < m_sites.size(); i++) {
        if (m_sites[i].first == hostname) {
            m_sites.removeAt(i);
            m_appSettingsRepository->removeVpnSite(m_currentRouteMode, hostname);
            return true;
        }
    }
    return false;
}

void IpSplitTunnelingController::removeSites()
{
    ++m_dnsRevision;
    m_dnsPending.clear();
    m_dnsReady.clear();
    m_dnsFlushTimer.stop();
    m_sites.clear();
    m_appSettingsRepository->removeAllVpnSites(m_currentRouteMode);
}

void IpSplitTunnelingController::setRouteMode(RouteMode routeMode)
{
    flushResolvedSites();
    ++m_dnsRevision;
    m_dnsPending.clear();
    m_currentRouteMode = routeMode;
    fillSites();
    m_appSettingsRepository->setRouteMode(routeMode);
}

void IpSplitTunnelingController::toggleSplitTunneling(bool enabled)
{
    m_appSettingsRepository->setSitesSplitTunnelingEnabled(enabled);
}

RouteMode IpSplitTunnelingController::getRouteMode() const
{
    return m_currentRouteMode;
}

bool IpSplitTunnelingController::isSplitTunnelingEnabled() const
{
    return m_appSettingsRepository->isSitesSplitTunnelingEnabled();
}

QVector<QPair<QString, QStringList>> IpSplitTunnelingController::getCurrentSites() const
{
    return m_sites;
}

void IpSplitTunnelingController::fillSites()
{
    QVariantMap sitesMap = m_appSettingsRepository->vpnSites(m_currentRouteMode);
    m_sites.clear();
    for (auto it = sitesMap.begin(); it != sitesMap.end(); ++it) {
        m_sites.append(qMakePair(it.key(), SecureAppSettingsRepository::siteIpList(it.value())));
    }
}

QString IpSplitTunnelingController::normalizeHostname(const QString &hostname)
{
    QString normalized = hostname;
    normalized.replace("https://", "");
    normalized.replace("http://", "");
    normalized.replace("ftp://", "");

    if (NetworkUtilities::ipAddressWithSubnetRegExp().exactMatch(normalized)) {
        return normalized;
    }

    const QStringList parts = normalized.split("/", Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString() : parts.first();
}

bool IpSplitTunnelingController::validateHostname(const QString &hostname)
{
    if (hostname.isEmpty()) {
        return false;
    }
    if (!hostname.contains(".") && !NetworkUtilities::ipAddressWithSubnetRegExp().exactMatch(hostname)) {
        return false;
    }
    return true;
}


void IpSplitTunnelingController::queueUnresolvedSites()
{
    for (const auto &site : m_sites) {
        if (site.second.isEmpty() &&
            !NetworkUtilities::ipAddressWithSubnetRegExp().exactMatch(site.first))
            m_dnsPending.enqueue(site.first);
    }
    startDnsLookups();
}

void IpSplitTunnelingController::startDnsLookups()
{
    constexpr int maxParallelLookups = 6;
    while (m_dnsActive < maxParallelLookups && !m_dnsPending.isEmpty()) {
        const QString hostname = m_dnsPending.dequeue();
        const quint64 revision = m_dnsRevision;
        const RouteMode mode = m_currentRouteMode;
        ++m_dnsActive;
        QHostInfo::lookupHost(hostname, this, [this, hostname, revision, mode](const QHostInfo &info) {
            --m_dnsActive;
            if (revision == m_dnsRevision && mode == m_currentRouteMode) {
                QStringList ips;
                for (const QHostAddress &address : info.addresses()) {
                    if (address.protocol() == QAbstractSocket::IPv4Protocol)
                        ips.append(address.toString());
                }
                ips.removeDuplicates();
                if (!ips.isEmpty()) {
                    for (auto &site : m_sites) {
                        if (site.first == hostname && site.second.isEmpty()) {
                            site.second = ips;
                            m_dnsReady.insert(hostname, ips);
                            if (!m_dnsFlushTimer.isActive())
                                m_dnsFlushTimer.start();
                            break;
                        }
                    }
                }
            }
            startDnsLookups();
        });
    }
}

void IpSplitTunnelingController::flushResolvedSites()
{
    if (m_dnsReady.isEmpty())
        return;
    m_appSettingsRepository->addVpnSites(m_currentRouteMode, m_dnsReady);
    m_dnsReady.clear();
}

void IpSplitTunnelingController::processSite(const QString &hostname, const QStringList &ips)
{
    addSiteInternal(hostname, ips);
}

bool IpSplitTunnelingController::parseSitesFromJson(
    const QByteArray &jsonData, QMap<QString, QStringList> &sites,
    QString &errorMessage, bool strict)
{
    sites.clear();
    if (strict && (jsonData.isEmpty() || jsonData.size() > 8 * 1024 * 1024)) {
        errorMessage = tr("The list is empty or exceeds the size limit.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        errorMessage = tr("Invalid JSON list: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonArray array = document.array();
    if (strict && (array.isEmpty() || array.size() > 100000)) {
        errorMessage = tr("The list is empty or unexpectedly large.");
        return false;
    }

    static const QRegularExpression domain(
        QStringLiteral(R"(^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$)"));
    static const QRegularExpression numeric(QStringLiteral(R"(^[0-9.]+$)"));

    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        QString hostname = normalizeHostname(item.value(QStringLiteral("hostname")).toString());
        if (strict) hostname = hostname.toLower();
        if (!validateHostname(hostname))
            continue;
        if (strict && !NetworkUtilities::checkIpSubnetFormat(hostname) &&
            (hostname.size() > 253 || numeric.match(hostname).hasMatch() ||
             !domain.match(hostname).hasMatch()))
            continue;

        QStringList ips;
        if (item.value(QStringLiteral("ips")).isArray()) {
            for (const QJsonValue &ip : item.value(QStringLiteral("ips")).toArray()) {
                const QString address = ip.toString().trimmed();
                if (!address.isEmpty() && (!strict || NetworkUtilities::checkIpSubnetFormat(address)))
                    ips.append(address);
            }
        }
        const QString address = item.value(QStringLiteral("ip")).toString().trimmed();
        if (!address.isEmpty() && (!strict || NetworkUtilities::checkIpSubnetFormat(address)))
            ips.append(address);
        ips.removeDuplicates();

        if (strict && sites.contains(hostname)) {
            QStringList combined = sites.value(hostname);
            combined.append(ips);
            combined.removeDuplicates();
            sites.insert(hostname, combined);
        } else {
            sites.insert(hostname, ips);
        }
    }
    if (strict && sites.isEmpty()) {
        errorMessage = tr("No valid entries found in the list.");
        return false;
    }
    return true;
}

bool IpSplitTunnelingController::importSitesFromJson(
    const QByteArray &jsonData, bool replaceExisting, QString &errorMessage)
{
    QMap<QString, QStringList> sites;
    if (!parseSitesFromJson(jsonData, sites, errorMessage))
        return false;
    addSites(sites, replaceExisting);
    return true;
}

QByteArray IpSplitTunnelingController::exportSitesToJson() const
{
    QVector<QPair<QString, QStringList>> sites = getCurrentSites();
    QJsonArray jsonArray;
    
    for (const auto &site : sites) {
        QJsonObject jsonObject;
        jsonObject["hostname"] = site.first;

        QJsonArray ipsArray;
        for (const QString &ip : site.second) {
            ipsArray.append(ip);
        }
        jsonObject["ips"] = ipsArray;
        // Keep the legacy "ip" field (first address) for backward compatibility.
        jsonObject["ip"] = site.second.isEmpty() ? QString() : site.second.first();

        jsonArray.append(jsonObject);
    }
    
    QJsonDocument jsonDocument(jsonArray);
    return jsonDocument.toJson();
}

