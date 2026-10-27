#ifndef SERVERPINGCONTROLLER_H
#define SERVERPINGCONTROLLER_H

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QQueue>

class ServersModel;
class ApiCountryModel;

// On-demand ICMP echo checks. A reply proves host reachability, not VPN health.
class ServerPingController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int revision READ revision NOTIFY resultsChanged)

public:
    explicit ServerPingController(ServersModel *servers, ApiCountryModel *countries, QObject *parent = nullptr);

    bool busy() const;
    int revision() const { return m_revision; }

    Q_INVOKABLE void pingAll();
    Q_INVOKABLE void pingServer(const QString &serverId);
    Q_INVOKABLE void pingCountry(const QString &serverId, const QString &countryCode);
    Q_INVOKABLE void pingCountries(const QString &serverId);
    Q_INVOKABLE QString resultText(const QString &serverId) const;

    static bool isValidTarget(const QString &host);
    static int parseRttMs(const QByteArray &output);
    static QString countryTarget(const QString &code, const QString &currentCode, const QString &currentHost);

signals:
    void busyChanged();
    void resultsChanged();

private:
    struct Probe {
        QString id;
        QString host;
    };

    void enqueue(const QString &id, const QString &host);
    void startQueued();
    void finish(QProcess *process, bool replied);
    void cancel();
    void setResult(const QString &id, const QString &text);
    void scheduleResultsChanged();

    ServersModel *m_servers;
    ApiCountryModel *m_countries;
    QString hostForCountry(const QString &serverId, const QString &countryCode) const;
    QQueue<Probe> m_queue;
    QHash<QProcess *, QString> m_running;
    QHash<QString, QString> m_results;
    int m_revision = 0;
    bool m_resultsNotifyPending = false;
};

#endif // SERVERPINGCONTROLLER_H
