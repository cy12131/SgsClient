# M0 里程碑状态

完成日期：2026-10-24（Asia/Shanghai）

## 1. 实际项目路径

`D:\Game\NewKill`

参考文件 `FreeKill-0.5.25.zip` 仅用于了解 Qt/QML 分层，已加入 `.gitignore`；本客户端没有复制其复杂实现，也没有构建或运行依赖。

## 2. 开发环境（已实际检查）

| 项目 | 实测结果 |
| --- | --- |
| Windows | 注册表报告 Windows 10 Home 26H2，OS Build `26300.9711`，64 位 |
| Qt | Qt `6.11.2`，使用 `C:\Qt\6.11.2\mingw_64` |
| Qt 模块 | Quick、QML、Quick Controls 2、Test 的 CMake 包均已找到并参与构建 |
| 编译器 | Qt 自带 MinGW-w64 GCC/G++ `13.1.0`，`C:\Qt\Tools\mingw1310_64` |
| CMake | `3.30.5`，`C:\Qt\Tools\CMake_64` |
| Ninja | `1.12.1`，`C:\Qt\Tools\Ninja` |
| Git | `2.53.0.windows.3` |

工具链匹配结论：本次使用 MinGW 版 Qt 与 Qt 自带 MinGW 13.1，不与已安装的 MSVC 套件混用。机器同时存在 `C:\Qt\6.11.2\msvc2022_64`，但本次未使用。

环境注意：系统默认 `PATH` 中的 `qmake` 来自 Anaconda Qt 5.15.2，且默认找不到 `cmake`/`g++`。因此实际构建显式选择了上述 Qt 6 MinGW Kit。项目的 `CMakeLists.txt` 未写死 Qt 安装路径。

## 3. 最终目录结构

```text
NewKill/
├── .gitignore
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── architecture.md
│   └── milestone_status.md
├── qml/
│   └── Main.qml
├── src/
│   ├── application.cpp
│   ├── application.h
│   └── main.cpp
└── tests/
    └── smoke_test.cpp
```

`build/` 和 `build-release/` 是本地验证产物并已忽略，不属于交付源码。参考 ZIP 同样已忽略。

## 4. 新增或修改文件

- `CMakeLists.txt`：C++20、Qt 模块、QML Module、警告级别、测试和 Windows 产物配置。
- `src/main.cpp`：创建 `QGuiApplication`、初始化 `Application`、进入事件循环。
- `src/application.h/.cpp`：应用元数据、QML 引擎生命周期、主界面加载和失败处理。
- `qml/Main.qml`：最小桌面主窗口、版本显示及“功能开发中”入口。
- `tests/smoke_test.cpp`：使用子进程和离屏平台加载真实客户端/QML 后正常退出。
- `README.md`：依赖、构建、运行、测试和范围说明。
- `docs/architecture.md`：当前分层和未来模块职责边界。
- `.gitignore`：Windows、Qt、CMake、IDE 和参考归档忽略项。

## 5. Application 启动执行链

`main()` 创建 `QGuiApplication` → 创建借用它的 `Application` → `Application::initialize()` 设置名称/版本并加载 QML → 初始化成功后进入 `QGuiApplication::exec()`。

局部变量的逆序析构保证 `Application`（含 `QQmlApplicationEngine`）先销毁，`QGuiApplication` 后销毁。QML 根对象创建失败时，初始化返回 `false`；异步 `objectCreationFailed` 也会请求以失败码退出。

## 6. QML 窗口加载机制

`qt_add_qml_module` 将 `qml/Main.qml` 作为 URI `SgsClient` 的 QML 模块纳入构建和资源缓存。C++ 使用 `QQmlApplicationEngine::loadFromModule("SgsClient", "Main")` 加载，无需依赖源目录下的松散 QML 文件。版本文本读取 `Qt.application.version`，其值由 C++ 设置为 `0.1.0-dev`。

## 7. CMake 配置及编译结果（已实际测试）

- Debug：干净目录配置成功；C++20 编译、QML 缓存生成和 `build\bin\sgs-client.exe` 链接成功。
- Release：独立的 `build-release` 目录配置成功；完整编译和链接成功。
- 编译启用了 GCC 的 `-Wall -Wextra -Wpedantic`（MSVC 分支配置为 `/W4 /permissive-`）。
- Qt 报告未找到可选的 Vulkan headers；本项目未使用 Vulkan，该提示不影响配置、编译或运行。

## 8. CTest 结果（已实际测试）

| 构建类型 | 结果 | 用时 |
| --- | --- | --- |
| Debug | 1/1 通过，0 失败 | 干净构建后 0.86 秒；最终增量复核 0.21 秒 |
| Release | 1/1 通过，0 失败 | 干净构建后 0.47 秒；最终增量复核 0.17 秒 |

冒烟测试以 `QT_QPA_PLATFORM=offscreen` 启动最终客户端程序，在实际创建 QML 根窗口后由专用 `--smoke-test` 参数退出，并检查正常退出码及关键 QML 模块加载错误。该结果只证明自动化加载，不冒充可见窗口验收。

## 9. 实际 GUI 运行结果（已实际测试）

Debug 客户端以正常桌面平台启动。进程保持运行，检测到非零顶层窗口句柄 `25298420`，窗口标题为 `SGS Client`。随后通过正常的窗口关闭请求退出，`CloseMainWindow()` 返回成功，进程在 5 秒内结束且退出码为 `0`。

因此“窗口可以实际显示并正常关闭”已与离屏 CTest 分开验证。主界面内容、版本文本和按钮占位行为同时由已编译的 `Main.qml` 定义；本阶段没有任何网络连接假象。

## 10. Git 状态

当前目录原先不是 Git 仓库。M0 完成时已初始化本地仓库并使用 `main` 分支；没有配置或推送远程。源码文件处于待首次提交状态，构建目录、二进制、Qt Creator 用户配置和参考 ZIP 均被忽略。

## 11. 已知问题与限制

- Qt/CMake/MinGW 未统一加入系统 `PATH`，命令行构建需使用完整路径、临时调整 `PATH`，或在 Qt Creator 中选择匹配 Kit。
- 尚未运行 `windeployqt`，所以可执行文件依赖开发机上的 Qt/MinGW 运行库；M0 不要求安装包。
- 自动化测试验证 QML 创建和退出，不做像素级界面断言；已另行完成可见窗口验收。
- 网络、协议、账号、大厅、房间、牌桌、规则、Lua 和正式资源系统均未实现，符合 M0 范围。

## 12. M1 建议（仅建议，未实现）

等待架构负责人统一跨端协议后，再确定 Network 的连接状态机、消息边界、错误模型和可测试接口。建议优先建立不暴露传输细节给 QML 的 C++ 网络抽象与协议契约测试，再由只读 Model 向 QML 提供连接状态；不要在协议确定前创建大厅、房间或游戏对象。
