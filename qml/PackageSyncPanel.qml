import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    Layout.fillWidth: true
    padding: 14
    background: Rectangle {
        color: "#b0121d29"
        radius: 10
        border.color: packageSyncManager.state === packageSyncManager.Failed ? "#b85f5a" : "#456074"
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("资源包同步"); color: "#e7edf2"; font.bold: true }
            Item { Layout.fillWidth: true }
            Label {
                text: packageSyncManager.stateText
                color: packageSyncManager.state === packageSyncManager.Ready ? "#73d99a"
                      : packageSyncManager.state === packageSyncManager.Failed ? "#ff9a92" : "#e5b86b"
            }
        }
        ProgressBar {
            Layout.fillWidth: true
            indeterminate: packageSyncManager.state >= packageSyncManager.FetchingManifest
                           && packageSyncManager.state <= packageSyncManager.Verifying
        }
        Label {
            Layout.fillWidth: true
            text: (packageSyncManager.currentPackage.length > 0
                   ? packageSyncManager.currentPackage + " · " : "") + packageSyncManager.statusText
            color: "#b8c7d2"; wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            visible: packageSyncManager.errorText.length > 0
            text: packageSyncManager.errorText
            color: "#ff9a92"; wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: packageSyncManager.packagesPath; color: "#778b9b"; elide: Text.ElideMiddle }
            Button {
                visible: packageSyncManager.state === packageSyncManager.Failed
                enabled: packageSyncManager.retryable && gameClient.ready
                text: qsTr("重试同步")
                onClicked: packageSyncManager.retry()
            }
        }
    }
}
