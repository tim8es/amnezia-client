#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>

class IpSplitTunnelingController;
class IpSplitTunnelingModel;
class ConnectionUiController;

// An opt-in, in-process updater. No driver access, external process or VPN restart.
// A fetched list is never applied while a connection is active or being prepared.
class RemoteSplitTunnelingUpdater final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int sourceIndex READ sourceIndex WRITE setSourceIndex NOTIFY settingsChanged)
    Q_PROPERTY(QString customUrl READ customUrl WRITE setCustomUrl NOTIFY settingsChanged)
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY settingsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    RemoteSplitTunnelingUpdater(IpSplitTunnelingController *sites,
                                IpSplitTunnelingModel *model,
                                ConnectionUiController *connection,
                                QObject *parent = nullptr);

    int sourceIndex() const;
    QString customUrl() const;
    bool autoUpdate() const;
    bool busy() const { return m_busy; }
    bool pending() const { return !m_pendingSha.isEmpty(); }
    QString status() const { return m_status; }

    // Pure operations used by both the updater and offline unit tests.
    static bool parseList(const QByteArray &bytes, QMap<QString, QStringList> &sites,
                          QString &error);
    static QMap<QString, QStringList> mergeSites(
        const QMap<QString, QStringList> &current,
        const QStringList &previousManaged,
        const QMap<QString, QStringList> &incoming);

    Q_INVOKABLE void setSourceIndex(int index);
    Q_INVOKABLE void setCustomUrl(const QString &url);
    Q_INVOKABLE void setAutoUpdate(bool enabled);
    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void skipPending();

signals:
    void settingsChanged();
    void busyChanged();
    void pendingChanged();
    void statusChanged();

private:
    QString sourceUrl() const;
    QString modeGroup(int mode) const;
    void applyPending();
    void clearPending();
    void setStatus(const QString &text);
    void finishDownload(bool ok, const QString &message = {});

    IpSplitTunnelingController *m_sites;
    IpSplitTunnelingModel *m_model;
    ConnectionUiController *m_connection;
    QSettings m_settings;
    QNetworkAccessManager m_network;
    QTimer m_timer;
    bool m_busy = false;
    QString m_status;

    QMap<QString, QStringList> m_pendingSites;
    QString m_pendingSha;
    QString m_pendingEtag;
    QString m_pendingUrl;
    int m_pendingMode = -1;
};