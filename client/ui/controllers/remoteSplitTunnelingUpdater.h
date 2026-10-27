#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QTimer>

class IpSplitTunnelingController;
class IpSplitTunnelingModel;
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
    RemoteSplitTunnelingUpdater(IpSplitTunnelingController *, IpSplitTunnelingModel *,
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
        const QMap<QString, QStringList> &incoming);

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

    IpSplitTunnelingController *m_sites;
    IpSplitTunnelingModel *m_model;
    ConnectionUiController *m_connection;
    QSettings m_settings;
    QNetworkAccessManager m_network;
    QTimer m_timer;
    bool m_busy = false;
    QString m_status;
};
