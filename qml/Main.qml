import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 960
    height: 600
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: qsTr("SGS Client")
    color: "#121820"

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1c2938" }
            GradientStop { position: 1.0; color: "#0e141b" }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 18

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("SGS Client")
            color: "#f2f5f7"
            font.pixelSize: 42
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("多人在线回合制卡牌游戏客户端")
            color: "#aebdca"
            font.pixelSize: 17
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 18
            Layout.preferredWidth: 180
            Layout.preferredHeight: 48
            text: qsTr("进入游戏")
            onClicked: statusLabel.text = qsTr("功能开发中，当前版本尚未连接服务器。")
        }

        Label {
            id: statusLabel
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            text: qsTr("M0 基础工程")
            color: "#91a5b7"
            font.pixelSize: 14
        }
    }

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        text: qsTr("版本 %1").arg(Qt.application.version)
        color: "#7890a4"
        font.pixelSize: 13
    }
}
