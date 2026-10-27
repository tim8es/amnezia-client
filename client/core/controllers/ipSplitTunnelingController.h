#ifndef IPSPLITTUNNELINGCONTROLLER_H
#define IPSPLITTUNNELINGCONTROLLER_H

#include <QObject>
#include <QVector>
#include <QMap>
#include <QPair>
#include <QQueue>
#include <QTimer>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonArray>
#include <QHostInfo>

#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"
#include "core/repositories/secureAppSettingsRepository.h"

using namespace amnezia;

class IpSplitTunnelingController : public QObject
{
    Q_OBJECT

public:
    explicit IpSplitTunnelingController(SecureAppSettingsRepository* appSettingsRepository, QObject* parent = nullptr);

    bool addSite(const QString &hostname);
    void addSites(const QMap<QString, QStringList> &sites, bool replaceExisting);
    bool removeSite(const QString &hostname);
    void removeSites();
    void setRouteMode(RouteMode routeMode);
    void toggleSplitTunneling(bool enabled);

    RouteMode getRouteMode() const;
    bool isSplitTunnelingEnabled() const;
    QVector<QPair<QString, QStringList>> getCurrentSites() const;

    static bool parseSitesFromJson(const QByteArray &jsonData,
                                   QMap<QString, QStringList> &sites,
                                   QString &errorMessage, bool strict = false);
    bool importSitesFromJson(const QByteArray& jsonData, bool replaceExisting, QString &errorMessage);
    QByteArray exportSitesToJson() const;

private:
    void fillSites();
    bool addSiteInternal(const QString &hostname, const QStringList &ips);
    static QString normalizeHostname(const QString &hostname);
    static bool validateHostname(const QString &hostname);
    void queueUnresolvedSites();
    void startDnsLookups();
    void flushResolvedSites();
    void processSite(const QString &hostname, const QStringList &ips);

    SecureAppSettingsRepository* m_appSettingsRepository;
    RouteMode m_currentRouteMode;
    QVector<QPair<QString, QStringList>> m_sites;
    QQueue<QString> m_dnsPending;
    QMap<QString, QStringList> m_dnsReady;
    QTimer m_dnsFlushTimer;
    quint64 m_dnsRevision = 0;
    int m_dnsActive = 0;
};

#endif // IPSPLITTUNNELINGCONTROLLER_H

