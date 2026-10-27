#ifndef REMOTESPLITTUNNELINGUPDATER_H
#define REMOTESPLITTUNNELINGUPDATER_H

#include <QMap>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>

class IpSplitTunnelingController;
class ConnectionUiController;

class RemoteSplitTunnelingUpdater final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int sourceIndex READ sourceIndex WRITE setSourceIndex NOTIFY settingsChanged)
    Q_PROPERTY(QString customUrl READ customUrl WRITE setCustomUrl NOTIFY settingsChanged)
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY settingsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    RemoteSplitTunnelingUpdater(IpSplitTunnelingController *,
                                ConnectionUiController *, QObject *parent = nullptr);

    int sourceIndex() const;
    QString customUrl() const;
    bool autoUpdate() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    static bool parseList(const QByteArray &, QMap<QString, QStringList> &, QString &);
    static QMap<QString, QStringList> mergeSites(
        const QMap<QString, QStringList> &current,
        const QMap<QString, QStringList> &previous,
        const QMap<QString, QStringList> &incoming,
        QMap<QString, QStringList> *managed = nullptr);

    Q_INVOKABLE void setSourceIndex(int);
    Q_INVOKABLE void setCustomUrl(const QString &);
    Q_INVOKABLE void setAutoUpdate(bool);
    Q_INVOKABLE void checkNow();

signals:
    void settingsChanged();
    void busyChanged();
    void statusChanged();

private:
    QString sourceUrl() const;
    void setStatus(const QString &);
    void complete(const QString &);
    void resolveNext();
    void applyRemoteList();

    IpSplitTunnelingController *m_sites;
    ConnectionUiController *m_connection;
    QSettings m_settings;
    QNetworkAccessManager m_network;
    QTimer m_timer;
    bool m_busy = false;
    QString m_status;
    QString m_downloadUrl;
    QByteArray m_downloadBody;
    QMap<QString, QStringList> m_remoteSites;
    QQueue<QString> m_dnsPending;
    int m_downloadMode = 0;
    int m_dnsActive = 0;
};

#endif // REMOTESPLITTUNNELINGUPDATER_H
