import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1180
    height: 760
    minimumWidth: 920
    minimumHeight: 620
    visible: true
    title: qsTr("SGS Client")
    color: "#101722"

    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#25394a" }
            GradientStop { position: 0.45; color: "#152536" }
            GradientStop { position: 1.0; color: "#0b1119" }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: roomModel.active ? 2 : (lobbyModel.inLobby ? 1 : 0)

        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(680, root.width - 80)
                spacing: 18
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("SGS Client"); color: "#f5ead2"; font.pixelSize: 46; font.bold: true }
                    Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("SGS/1 · 大厅与等待房间 · %1").arg(Qt.application.version); color: "#a7b8c7"; font.pixelSize: 15 }
                }
                Frame {
                    Layout.fillWidth: true
                    padding: 24
                    background: Rectangle { color: "#dd172534"; radius: 14; border.color: "#5d7487" }
                    GridLayout {
                        anchors.fill: parent
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 14
                        Label { text: qsTr("服务器"); color: "#dce7ef" }
                        TextField { id: hostField; Layout.fillWidth: true; text: "127.0.0.1"; placeholderText: qsTr("IP 或域名"); enabled: gameClient.canConnect }
                        Label { text: qsTr("端口"); color: "#dce7ef" }
                        TextField {
                            id: portField
                            Layout.fillWidth: true
                            text: "18765"
                            validator: IntValidator { bottom: 1; top: 65535 }
                            enabled: gameClient.canConnect
                        }
                        Label { text: qsTr("临时昵称"); color: "#dce7ef" }
                        TextField { id: nicknameField; Layout.fillWidth: true; text: qsTr("玩家"); maximumLength: 20; placeholderText: qsTr("1～20 个字符"); enabled: gameClient.canConnect; onAccepted: connectButton.clicked() }
                        Button {
                            id: connectButton
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            text: lobbyModel.busy ? qsTr("正在连接并进入大厅…") : qsTr("进入大厅")
                            enabled: gameClient.canConnect && !lobbyModel.busy && hostField.text.trim().length > 0 && portField.acceptableInput && nicknameField.text.trim().length > 0
                            onClicked: lobbyModel.connectAndEnter(hostField.text, Number(portField.text), nicknameField.text)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: lobbyModel.phaseText; color: lobbyModel.phase === 0 ? "#e5b86b" : "#73d99a"; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("取消连接"); visible: gameClient.canDisconnect; onClicked: lobbyModel.disconnectFromServer() }
                }
                PackageSyncPanel { }
                Label { Layout.fillWidth: true; visible: lobbyModel.errorText.length > 0 || gameClient.lastError.length > 0; text: lobbyModel.errorText.length > 0 ? lobbyModel.errorText : gameClient.lastError; color: "#ff9a92"; wrapMode: Text.Wrap }
                Frame {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    background: Rectangle { color: "#8a0a1119"; radius: 8; border.color: "#354b5d" }
                    ScrollView { anchors.fill: parent; TextArea { text: gameClient.logText; readOnly: true; color: "#9eb0bf"; background: null; font.family: "Consolas"; wrapMode: TextEdit.Wrap } }
                }
            }
        }
        Lobby { }
        WaitingRoom { }
    }
}
