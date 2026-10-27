import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import SortFilterProxyModel 0.2

import PageEnum 1.0
import ContainerProps 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    ListViewType {
        id: servers

        anchors.fill: parent

        header: NavigationBarType {
            width: servers.width

            title: qsTr("Servers")
            description: ConnectionController.isConnected || ConnectionController.isConnectionInProgress
                         ? qsTr("Disconnect VPN before checking latency.")
                         : qsTr("ICMP tests host reachability, not VPN availability.")

            ButtonType {
                text: ServerPingController.busy ? qsTr("Checking...") : qsTr("Ping all")
                size: ButtonType.Small
                variant: ButtonType.Secondary
                enabled: !ServerPingController.busy && !ConnectionController.isConnected
                         && !ConnectionController.isConnectionInProgress
                visible: !GC.isMobile()
                onClicked: ServerPingController.pingAll()
            }
        }

        model: ServersModel

        delegate: Item {
            implicitWidth: servers.width
            implicitHeight: server.implicitHeight

            ListItemType {
                id: server

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 16
                anchors.rightMargin: 16

                text: name

                icon.source: ServersUiController.serverHasOutdatedAwgContainer(serverId) ? "qrc:/images/controls/alert-circle.svg" : ""
                icon.color: AmneziaStyle.color.statusWarning

                subtitle: {
                    var servicesNameString = ""
                    var servicesName = ServersUiController.getAllInstalledServicesName(index)
                    for (var i = 0; i < servicesName.length; i++) {
                        servicesNameString += servicesName[i] + " · "
                    }

                    // The revision property makes the status update reactive.
                    ServerPingController.revision
                    var result = ServerPingController.resultText(serverId)
                    var base = ServersUiController.isServerFromApi(serverId)
                               ? servicesNameString + serverDescription
                               : servicesNameString + hostName
                    return base + (result !== "" ? " · " + result : "")
                }
                ButtonType {
                    text: qsTr("Ping")
                    size: ButtonType.Small
                    variant: ButtonType.Secondary
                    visible: !GC.isMobile()
                    enabled: !ConnectionController.isConnected && !ConnectionController.isConnectionInProgress
                    Accessible.name: qsTr("Ping %1").arg(name)
                    onClicked: ServerPingController.pingServer(serverId)
                }

                onClicked: {
                    ServersUiController.setProcessedServerId(serverId)

                    if (ServersUiController.isServerFromApi(ServersUiController.processedServerId)) {
                        PageController.showBusyIndicator(true)
                        let result = SubscriptionUiController.getAccountInfo(ServersUiController.processedServerId, false)
                        PageController.showBusyIndicator(false)
                        if (!result) {
                            return
                        }

                        PageController.goToPage(PageEnum.PageSettingsApiServerInfo)
                    } else {
                        PageController.goToPage(PageEnum.PageSettingsServerInfo)
                    }
                }
            }
        }

        footer: Item {
            width: servers.width
            height: 16 + PageController.safeAreaBottomMargin
        }
    }
}
