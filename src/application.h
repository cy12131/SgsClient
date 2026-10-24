#pragma once

#include "network/game_client.h"
#include "models/lobby_model.h"
#include "models/room_model.h"

#include <QQmlApplicationEngine>

class QGuiApplication;

// Application 是客户端外壳的生命周期管理者。
// 它借用 main.cpp 中 QGuiApplication 的生命周期，并独占 QML 引擎；
// 因此销毁顺序始终是先释放界面对象，再释放 Qt 应用对象。
class Application final
{
public:
    explicit Application(QGuiApplication &qtApplication);

    // 初始化应用元数据并加载主界面。加载失败时返回 false，调用方应停止启动。
    [[nodiscard]] bool initialize();

private:
    QGuiApplication &m_qtApplication;
    GameClient m_gameClient;
    RoomModel m_roomModel;
    LobbyModel m_lobbyModel;
    QQmlApplicationEngine m_engine;
};
