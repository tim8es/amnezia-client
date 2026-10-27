#include "serverPingController.h"

#include "ui/models/serversModel.h"
#include "ui/models/api/apiCountryModel.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QTimer>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <string>
#endif

namespace {
constexpr int kMaxConcurrent = 3;
constexpr int kTimeoutMs = 3500;

QStringList pingArguments(const QString &host)
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("-4"), QStringLiteral("-n"), QStringLiteral("1"),
            QStringLiteral("-w"), QStringLiteral("1500"), host};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("-n"), QStringLiteral("-c"), QStringLiteral("1"),
            QStringLiteral("-W"), QStringLiteral("1500"), host};
#else
    return {QStringLiteral("-4"), QStringLiteral("-n"), QStringLiteral("-c"),
            QStringLiteral("1"), QStringLiteral("-W"), QStringLiteral("2"), host};
#endif
}
} // namespace

ServerPingController::ServerPingController(ServersModel *servers, ApiCountryModel *countries, QObject *parent)
    : QObject(parent), m_servers(servers), m_countries(countries)
{
    connect(servers, &QAbstractItemModel::modelReset, this, &ServerPingController::cancel);
    connect(countries, &QAbstractItemModel::modelReset, this, &ServerPingController::cancel);
}

bool ServerPingController::busy() const
{
    return !m_queue.isEmpty() || !m_running.isEmpty();
}

QString ServerPingController::resultText(const QString &serverId) const
{
    return m_results.value(serverId);
}

bool ServerPingController::isValidTarget(const QString &host)
{
    if (host.isEmpty() || host.size() > 253)
        return false;

    QHostAddress address;
    if (address.setAddress(host))
        return address.protocol() == QAbstractSocket::IPv4Protocol;

    // No command shell is involved, but still reject flags, URLs, ports and paths.
    static const QRegularExpression label(QStringLiteral(
        R"(^(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.)*[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$)"));
    static const QRegularExpression numeric(QStringLiteral(R"(^[0-9.]+$)"));
    return label.match(host).hasMatch() && !numeric.match(host).hasMatch();
}

int ServerPingController::parseRttMs(const QByteArray &output)
{
    // ping output varies by OS and locale; no RTT is better than a fabricated one.
    static const QRegularExpression rtt(QStringLiteral(
        R"((?:time|время)\s*[=<]\s*(\d+(?:[.,]\d+)?)\s*(?:ms|мс))"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = rtt.match(QString::fromUtf8(output));
#if defined(Q_OS_WIN)
    if (!match.hasMatch()) {
        // Windows console tools commonly emit OEM code-page text, not UTF-8.
        const int count = MultiByteToWideChar(CP_OEMCP, 0, output.constData(),
                                              static_cast<int>(output.size()), nullptr, 0);
        if (count > 0) {
            std::wstring wide(static_cast<size_t>(count), L'\0');
            MultiByteToWideChar(CP_OEMCP, 0, output.constData(),
                                static_cast<int>(output.size()), wide.data(), count);
            match = rtt.match(QString::fromWCharArray(wide.data(), count));
        }
    }
#endif
    if (!match.hasMatch())
        return -1;
    bool ok = false;
    const double ms = match.captured(1).replace(',', '.').toDouble(&ok);
    return ok && ms >= 0 && ms < 60000 ? qRound(ms) : -1;
}

void ServerPingController::setResult(const QString &id, const QString &value)
{
    if (m_results.value(id) == value)
        return;
    m_results.insert(id, value);
    scheduleResultsChanged();
}

void ServerPingController::scheduleResultsChanged()
{
    if (m_resultsNotifyPending)
        return;
    m_resultsNotifyPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_resultsNotifyPending = false;
        ++m_revision;
        emit resultsChanged();
    });
}

void ServerPingController::enqueue(const QString &id, const QString &host)
{
    if (id.isEmpty())
        return;
    if (!isValidTarget(host)) {
        setResult(id, tr("ICMP: unsupported address"));
        return;
    }
    for (const auto &probe : m_queue) {
        if (probe.id == id)
            return;
    }
    if (m_running.values().contains(id))
        return;
    m_queue.enqueue({id, host});
    setResult(id, tr("Queued"));
}

void ServerPingController::pingServer(const QString &serverId)
{
    for (int row = 0; row < m_servers->rowCount(); ++row) {
        const QModelIndex index = m_servers->index(row);
        if (m_servers->data(index, ServersModel::ServerIdRole).toString() == serverId) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
            setResult(serverId, tr("ICMP check is available on desktop only"));
#else
            enqueue(serverId, m_servers->data(index, ServersModel::HostNameRole).toString());
            startQueued();
            emit busyChanged();
#endif
            return;
        }
    }
}


QString ServerPingController::countryTarget(const QString &code, const QString &currentCode,
                                            const QString &currentHost)
{
    // The subscription country catalog does not expose known endpoint addresses.
    return code == currentCode ? currentHost : QString();
}

QString ServerPingController::hostForCountry(const QString &serverId, const QString &countryCode) const
{
    if (m_countries->serverId() != serverId ||
        countryCode != m_countries->getCurrentCountryCode())
        return {};

    for (int row = 0; row < m_servers->rowCount(); ++row) {
        const QModelIndex index = m_servers->index(row);
        if (m_servers->data(index, ServersModel::ServerIdRole).toString() == serverId)
            return m_servers->data(index, ServersModel::HostNameRole).toString();
    }
    return {};
}

void ServerPingController::pingCountry(const QString &serverId, const QString &countryCode)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    Q_UNUSED(serverId)
    Q_UNUSED(countryCode)
#else
    if (m_countries->serverId() != serverId || countryCode.isEmpty())
        return;
    const QString id = serverId + QStringLiteral("/") + countryCode;
    const QString host = hostForCountry(serverId, countryCode);
    if (host.isEmpty()) {
        setResult(id, tr("Address unavailable"));
        return;
    }
    enqueue(id, host);
    startQueued();
    emit busyChanged();
#endif
}

void ServerPingController::pingCountries(const QString &serverId)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    Q_UNUSED(serverId)
#else
    if (m_countries->serverId() != serverId)
        return;
    cancel();
    const QString currentCode = m_countries->getCurrentCountryCode();
    const QString currentHost = hostForCountry(serverId, currentCode);
    for (const auto &country : m_countries->countries()) {
        const QString id = serverId + QStringLiteral("/") + country.countryCode;
        const QString target = countryTarget(country.countryCode, currentCode, currentHost);
        if (target.isEmpty())
            setResult(id, tr("Address unavailable"));
        else
            enqueue(id, target);
    }
    startQueued();
    emit busyChanged();
#endif
}

void ServerPingController::pingAll()
{
    cancel();
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return;
#else
    for (int row = 0; row < m_servers->rowCount(); ++row) {
        const QModelIndex index = m_servers->index(row);
        enqueue(m_servers->data(index, ServersModel::ServerIdRole).toString(),
                m_servers->data(index, ServersModel::HostNameRole).toString());
    }
    startQueued();
    emit busyChanged();
#endif
}

void ServerPingController::startQueued()
{
    const bool wasBusy = busy();
    while (m_running.size() < kMaxConcurrent && !m_queue.isEmpty()) {
        const Probe probe = m_queue.dequeue();
        auto *process = new QProcess(this);
        m_running.insert(process, probe.id);
        setResult(probe.id, tr("Checking..."));

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process](int code, QProcess::ExitStatus status) {
                    finish(process, code == 0 && status == QProcess::NormalExit);
                });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                const QString id = m_running.value(process);
                finish(process, false);
                setResult(id, tr("ICMP tool unavailable"));
            }
        });
        QTimer::singleShot(kTimeoutMs, process, [process]() {
            if (process->state() != QProcess::NotRunning)
                process->kill();
        });
        process->start(QStringLiteral("ping"), pingArguments(probe.host));
    }
    if (wasBusy != busy())
        emit busyChanged();
}

void ServerPingController::finish(QProcess *process, bool replied)
{
    if (!m_running.contains(process))
        return;
    const bool wasBusy = busy();
    const QString id = m_running.take(process);
    const int ms = replied ? parseRttMs(process->readAllStandardOutput()) : -1;
    process->deleteLater();
    if (!replied)
        setResult(id, tr("No ICMP reply"));
    else if (ms >= 0)
        setResult(id, tr("ICMP: %1 ms").arg(ms));
    else
        setResult(id, tr("ICMP: reachable"));
    startQueued();
    if (wasBusy != busy())
        emit busyChanged();
}

void ServerPingController::cancel()
{
    const bool wasBusy = busy();
    m_queue.clear();
    for (auto *process : m_running.keys()) {
        process->disconnect(this);
        process->kill();
        process->deleteLater();
    }
    m_running.clear();
    m_results.clear();
    scheduleResultsChanged();
    if (wasBusy)
        emit busyChanged();
}
