import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14
        Frame {
            Layout.fillWidth: true
            padding: 16
            background: Rectangle { color: "#dc192938"; radius: 12; border.color: "#5a7182" }
            RowLayout {
                anchors.fill: parent
                ColumnLayout {
                    Label { text: roomModel.name; color: "#f6ead1"; font.pixelSize: 27; font.bold: true }
                    Label { text: qsTr("房间 #%1 · %2 / %3 人 · revision %4").arg(roomModel.roomId).arg(roomModel.players.length).arg(roomModel.maxPlayers).arg(roomModel.revision); color: "#a0b2c0" }
                }
                Item { Layout.fillWidth: true }
                Label { text: roomModel.currentUserOwner ? qsTr("房主") : (roomModel.currentUserReady ? qsTr("已准备") : qsTr("未准备")); color: roomModel.currentUserOwner ? "#e7bf72" : (roomModel.currentUserReady ? "#6fe09b" : "#b7c4ce"); font.pixelSize: 18; font.bold: true }
                Button { visible: !roomModel.currentUserOwner; text: roomModel.currentUserReady ? qsTr("取消准备") : qsTr("准备"); enabled: !roomModel.busy; onClicked: roomModel.setReady(!roomModel.currentUserReady) }
                Button { visible: roomModel.currentUserOwner; text: qsTr("开始游戏（未实现）"); enabled: false }
                Button { text: qsTr("退出房间"); enabled: !roomModel.busy; onClicked: roomModel.leaveRoom() }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 16
                background: Rectangle { color: "#c90b151e"; radius: 12; border.color: "#40596b" }
                GridLayout {
                    anchors.fill: parent
                    columns: 4
                    columnSpacing: 12
                    rowSpacing: 12
                    Repeater {
                        model: roomModel.maxPlayers
                        delegate: Frame {
                            id: seatFrame
                            required property int index
                            property var player: { const dependency = roomModel.players; return roomModel.playerAtSeat(index + 1) }
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 150
                            padding: 12
                            background: Rectangle { radius: 12; color: seatFrame.player.nickname ? "#203645" : "#121e28"; border.width: seatFrame.player.is_owner ? 2 : 1; border.color: seatFrame.player.is_owner ? "#d5a650" : "#3c5669" }
                            ColumnLayout {
                                anchors.fill: parent
                                Label { text: qsTr("座位 %1").arg(seatFrame.index + 1); color: "#8499a8"; font.bold: true }
                                Item { Layout.fillHeight: true }
                                Rectangle { Layout.alignment: Qt.AlignHCenter; width: 54; height: 54; radius: 27; color: seatFrame.player.nickname ? "#9f763c" : "#2b3b47"; Label { anchors.centerIn: parent; text: seatFrame.player.nickname ? seatFrame.player.nickname.slice(0, 1) : "＋"; color: "#fff0d1"; font.pixelSize: 23; font.bold: true } }
                                Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: seatFrame.player.nickname ? seatFrame.player.nickname : qsTr("空座位"); color: seatFrame.player.nickname ? "#edf3f6" : "#607687"; font.pixelSize: 17; font.bold: true; elide: Text.ElideRight }
                                Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: seatFrame.player.is_owner ? qsTr("房主") : (seatFrame.player.ready ? qsTr("已准备") : (seatFrame.player.nickname ? qsTr("未准备") : "")); color: seatFrame.player.is_owner ? "#e6bb69" : (seatFrame.player.ready ? "#6fdb98" : "#9babb7") }
                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                }
            }
            Frame {
                Layout.preferredWidth: 270
                Layout.fillHeight: true
                padding: 18
                background: Rectangle { color: "#b9162532"; radius: 12; border.color: "#40586b" }
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: qsTr("等待房间"); color: "#f4e9d3"; font.pixelSize: 20; font.bold: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#334a5c" }
                    Label { Layout.fillWidth: true; text: qsTr("座位严格使用服务端 seat 编号；成员离开不会让其他玩家自动移位。"); color: "#9fb1bf"; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                    Label { text: qsTr("游戏开始功能尚未实现"); color: "#607688"; font.italic: true }
                }
            }
        }
        Label { Layout.fillWidth: true; visible: roomModel.errorText.length > 0; text: roomModel.errorText; color: "#ff9b92"; wrapMode: Text.Wrap }
    }
}
