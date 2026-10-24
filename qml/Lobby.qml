import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    Dialog {
        id: createDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("创建房间")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: lobbyModel.createRoom(roomName.text, playerCount.value)
        ColumnLayout {
            width: 360
            Label { text: qsTr("房间名称") }
            TextField { id: roomName; Layout.fillWidth: true; text: qsTr("%1的房间").arg(lobbyModel.nickname); maximumLength: 40 }
            Label { text: qsTr("最大人数") }
            SpinBox { id: playerCount; Layout.fillWidth: true; from: 2; to: 8; value: 4 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14
        Frame {
            Layout.fillWidth: true
            padding: 16
            background: Rectangle { color: "#d91a2a39"; radius: 12; border.color: "#536b7d" }
            RowLayout {
                anchors.fill: parent
                Rectangle { width: 48; height: 48; radius: 24; color: "#c09145"; Label { anchors.centerIn: parent; text: "♙"; font.pixelSize: 26; color: "#fff5df" } }
                ColumnLayout {
                    Label { text: lobbyModel.nickname; color: "#f5ead2"; font.pixelSize: 20; font.bold: true }
                    Label { text: qsTr("临时用户 #%1").arg(lobbyModel.userId); color: "#9fb2c1" }
                }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("● 已连接"); color: "#70dc99"; font.bold: true }
                Button { text: qsTr("刷新房间"); enabled: !lobbyModel.busy; onClicked: lobbyModel.refreshRooms() }
                Button { text: qsTr("＋ 创建房间"); enabled: !lobbyModel.busy; onClicked: createDialog.open() }
                Button { text: qsTr("断开"); onClicked: lobbyModel.disconnectFromServer() }
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
                background: Rectangle { color: "#c90e1822"; radius: 12; border.color: "#40586b" }
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("房间列表"); color: "#f4e9d3"; font.pixelSize: 23; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Label { text: qsTr("%1 个房间").arg(lobbyModel.rooms.length); color: "#91a7b8" }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#334a5c" }
                    Label { Layout.alignment: Qt.AlignCenter; Layout.fillHeight: true; visible: lobbyModel.rooms.length === 0; text: qsTr("当前没有等待中的房间\n可以刷新列表或创建一个新房间"); horizontalAlignment: Text.AlignHCenter; color: "#8196a7"; font.pixelSize: 17 }
                    ListView {
                        id: roomList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: lobbyModel.rooms.length > 0
                        spacing: 10
                        clip: true
                        model: lobbyModel.rooms
                        delegate: Rectangle {
                            required property var modelData
                            width: roomList.width
                            height: 92
                            radius: 9
                            color: hoverHandler.hovered ? "#293f50" : "#1c2d3b"
                            border.color: "#496174"
                            HoverHandler { id: hoverHandler }
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                Rectangle { width: 54; height: 54; radius: 8; color: "#9a7138"; Label { anchors.centerIn: parent; text: "房"; color: "#fff1d3"; font.pixelSize: 22; font.bold: true } }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: modelData.name; color: "#eef4f7"; font.pixelSize: 18; font.bold: true }
                                    Label { text: qsTr("房主 %1 · 房间 #%2").arg(modelData.owner_nickname).arg(modelData.room_id); color: "#9eb0bd" }
                                }
                                Label { text: qsTr("%1 / %2").arg(modelData.player_count).arg(modelData.max_players); color: "#e7c27d"; font.pixelSize: 18; font.bold: true }
                                Button { text: qsTr("加入"); enabled: !lobbyModel.busy && modelData.player_count < modelData.max_players; onClicked: lobbyModel.joinRoom(modelData.room_id) }
                            }
                        }
                    }
                }
            }
            Frame {
                Layout.preferredWidth: 285
                Layout.fillHeight: true
                padding: 18
                background: Rectangle { color: "#b9162532"; radius: 12; border.color: "#40586b" }
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: qsTr("大厅信息"); color: "#f4e9d3"; font.pixelSize: 20; font.bold: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#334a5c" }
                    Label { Layout.fillWidth: true; text: qsTr("房间列表由服务端维护。收到列表变化通知时会自动刷新，也可以手动刷新。"); color: "#9fb1bf"; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                    Label { text: qsTr("聊天功能将在后续阶段实现"); color: "#607688"; font.italic: true }
                }
            }
        }
        Label { Layout.fillWidth: true; visible: lobbyModel.errorText.length > 0; text: lobbyModel.errorText; color: "#ff9b92"; wrapMode: Text.Wrap }
    }
}
