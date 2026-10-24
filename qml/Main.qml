import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 980
    height: 680
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    title: qsTr("SGS Client")
    color: "#10161e"

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1d2b3a" }
            GradientStop { position: 1.0; color: "#0d131a" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Label {
                    text: qsTr("SGS Client")
                    color: "#f2f5f7"
                    font.pixelSize: 34
                    font.bold: true
                }
                Label {
                    text: qsTr("SGS/1 网络调试客户端 · %1").arg(Qt.application.version)
                    color: "#91a5b7"
                    font.pixelSize: 14
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: gameClient.stateText
                color: gameClient.ready ? "#65d68b" : "#f1c26b"
                font.pixelSize: 18
                font.bold: true
            }
        }

        Frame {
            Layout.fillWidth: true
            background: Rectangle { color: "#172330"; radius: 8; border.color: "#31475c" }
            RowLayout {
                anchors.fill: parent
                spacing: 10
                TextField {
                    id: hostField
                    Layout.fillWidth: true
                    placeholderText: qsTr("服务器 IP 或域名")
                    text: "127.0.0.1"
                    enabled: gameClient.canConnect
                }
                TextField {
                    id: portField
                    Layout.preferredWidth: 120
                    placeholderText: qsTr("端口")
                    text: "18765"
                    enabled: gameClient.canConnect
                    validator: IntValidator { bottom: 1; top: 65535 }
                }
                Button {
                    text: qsTr("连接")
                    enabled: gameClient.canConnect && hostField.text.trim().length > 0
                             && portField.acceptableInput
                    onClicked: gameClient.connectToServer(hostField.text, Number(portField.text))
                }
                Button {
                    text: qsTr("断开")
                    enabled: gameClient.canDisconnect
                    onClicked: gameClient.disconnectFromServer()
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            background: Rectangle { color: "#172330"; radius: 8; border.color: "#31475c" }
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("Ping")
                        enabled: gameClient.ready
                        onClicked: gameClient.sendPing()
                    }
                    TextField {
                        id: echoField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Echo 文本（支持 UTF-8 中文）")
                        text: qsTr("你好，SgsServer！")
                        enabled: gameClient.ready
                        onAccepted: if (gameClient.ready) gameClient.sendEcho(text)
                    }
                    Button {
                        text: qsTr("发送 Echo")
                        enabled: gameClient.ready
                        onClicked: gameClient.sendEcho(echoField.text)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: gameClient.lastError.length > 0
                    text: gameClient.lastError
                    color: "#ff8d8d"
                    wrapMode: Text.Wrap
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { color: "#0b1118"; radius: 8; border.color: "#31475c" }
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("通信日志"); color: "#dce6ee"; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("清空"); onClicked: gameClient.clearLog() }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        text: gameClient.logText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        color: "#b9c8d4"
                        background: null
                        font.family: "Consolas"
                    }
                }
            }
        }
    }
}
