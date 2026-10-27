#include <QTest>

#include "ui/controllers/serverPingController.h"

class TestServerPingController : public QObject
{
    Q_OBJECT

private slots:
    void targetValidation_data()
    {
        QTest::addColumn<QString>("host");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("domain") << QStringLiteral("vpn.example.org") << true;
        QTest::newRow("ipv4") << QStringLiteral("192.0.2.1") << true;
        QTest::newRow("single-name") << QStringLiteral("localhost") << true;
        QTest::newRow("option-injection") << QStringLiteral("-c 100") << false;
        QTest::newRow("url") << QStringLiteral("https://example.org") << false;
        QTest::newRow("host-port") << QStringLiteral("example.org:443") << false;
        QTest::newRow("invalid-ip") << QStringLiteral("999.999.999.999") << false;
        QTest::newRow("ipv6-unsupported") << QStringLiteral("2001:db8::1") << false;
    }

    void targetValidation()
    {
        QFETCH(QString, host);
        QFETCH(bool, accepted);
        QCOMPARE(ServerPingController::isValidTarget(host), accepted);
    }

    void parsesRtt_data()
    {
        QTest::addColumn<QByteArray>("output");
        QTest::addColumn<int>("ms");
        QTest::newRow("linux") << QByteArray("64 bytes: time=12.7 ms") << 13;
        QTest::newRow("windows") << QByteArray("Reply: time=34ms TTL=55") << 34;
        QTest::newRow("under-one") << QByteArray("Reply: time<1ms") << 1;
        QTest::newRow("localized") << QByteArray("Ответ: время=2,4мс") << 2;
        QTest::newRow("timeout") << QByteArray("Request timed out.") << -1;
    }

    void parsesRtt()
    {
        QFETCH(QByteArray, output);
        QFETCH(int, ms);
        QCOMPARE(ServerPingController::parseRttMs(output), ms);
    }

#if defined(Q_OS_WIN)
    void parsesWindowsOemRussianOutput()
    {
        // Actual CP866 encoded ping.exe output from a Russian Windows locale.
        const QByteArray data = QByteArray::fromHex(
            "8ee2a2a5e220aee220312e312e312e313a20e7a8e1abae20a1a0a9e23d"
            "333220a2e0a5acef3d3231ace12054544c3d3534");
        QCOMPARE(ServerPingController::parseRttMs(data), 21);
    }
#endif
};

QTEST_MAIN(TestServerPingController)
#include "testServerPingController.moc"
