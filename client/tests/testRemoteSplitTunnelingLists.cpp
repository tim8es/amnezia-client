#include <QTest>

#include "ui/controllers/remoteSplitTunnelingUpdater.h"

class TestRemoteSplitTunnelingLists : public QObject
{
    Q_OBJECT

private slots:
    void parsesHostnamesAndCidrs()
    {
        const QByteArray input = R"([
          {"hostname":"Example.COM","ips":["1.2.3.4","1.2.3.4","invalid"]},
          {"hostname":"192.0.2.0/24","ip":""},
          {"hostname":"example.com","ip":"2.3.4.5"},
          {"hostname":"invalid hostname","ips":[]}
        ])";
        QMap<QString, QStringList> sites;
        QString error;
        QVERIFY(RemoteSplitTunnelingUpdater::parseList(input, sites, error));
        QCOMPARE(sites.size(), 2);
        QCOMPARE(sites.value("example.com"), (QStringList{"1.2.3.4", "2.3.4.5"}));
        QVERIFY(sites.contains("192.0.2.0/24"));
    }

    void rejectsBrokenListsWithoutPartialResults()
    {
        QMap<QString, QStringList> sites;
        QString error;
        QVERIFY(!RemoteSplitTunnelingUpdater::parseList("{}", sites, error));
        QVERIFY(!RemoteSplitTunnelingUpdater::parseList("[]", sites, error));
        QVERIFY(!RemoteSplitTunnelingUpdater::parseList(
            R"([{"hostname":"999.999.999.999"}])", sites, error));
        QVERIFY(!RemoteSplitTunnelingUpdater::parseList(
            R"([{"hostname":"not a domain"}])", sites, error));
        QVERIFY(sites.isEmpty());
    }

    void mergesWithoutDeletingLocalSites()
    {
        const QMap<QString, QStringList> current{
            {"local.test", {"192.0.2.10"}},
            {"overlap.test", {"192.0.2.11"}},
            {"old-managed.test", {"192.0.2.1"}},
            {"retained.test", {"192.0.2.2"}}
        };
        const QMap<QString, QStringList> incoming{
            {"overlap.test", {"203.0.113.11"}},
            {"retained.test", {}},
            {"new-managed.test", {"203.0.113.0/24"}}
        };
        const auto merged = RemoteSplitTunnelingUpdater::mergeSites(
            current, {{"old-managed.test", {"192.0.2.1"}}, {"retained.test", {"192.0.2.2"}}}, incoming);
        QCOMPARE(merged.size(), 4);
        QVERIFY(!merged.contains("old-managed.test"));
        QCOMPARE(merged.value("local.test"), (QStringList{"192.0.2.10"}));
        QCOMPARE(merged.value("overlap.test"), (QStringList{"192.0.2.11"}));
        QCOMPARE(merged.value("retained.test"), (QStringList{"192.0.2.2"}));
        QCOMPARE(merged.value("new-managed.test"), (QStringList{"203.0.113.0/24"}));
    }

    void preservesManualEditsToManagedEntries()
    {
        const QMap<QString, QStringList> current{{"edited.test", {"192.0.2.90"}}};
        const QMap<QString, QStringList> previous{{"edited.test", {"192.0.2.1"}}};
        const QMap<QString, QStringList> incoming{{"edited.test", {"192.0.2.2"}}};
        const auto result = RemoteSplitTunnelingUpdater::mergeSites(current, previous, incoming);
        QCOMPARE(result.value("edited.test"), (QStringList{"192.0.2.90"}));
        QCOMPARE(RemoteSplitTunnelingUpdater::mergeSites({}, previous, incoming).size(), 0);
    }
};

QTEST_MAIN(TestRemoteSplitTunnelingLists)
#include "testRemoteSplitTunnelingLists.moc"
