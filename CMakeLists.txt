cmake_minimum_required(VERSION 3.21)

project(SgsClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Network Quick Qml QuickControls2 Test)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_library(sgs-client-core STATIC
    src/network/game_client.cpp
    src/network/game_client.h
    src/protocol/message_codec.cpp
    src/protocol/message_codec.h
)

target_include_directories(sgs-client-core PUBLIC src)
target_link_libraries(sgs-client-core PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(sgs-client
    src/main.cpp
    src/application.cpp
    src/application.h
)

qt_add_qml_module(sgs-client
    URI SgsClient
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(sgs-client PRIVATE
    sgs-client-core
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
)

target_compile_definitions(sgs-client PRIVATE
    SGS_CLIENT_VERSION="0.1.0-dev"
)

if(MSVC)
    target_compile_options(sgs-client-core PRIVATE /W4 /permissive-)
    target_compile_options(sgs-client PRIVATE /W4 /permissive-)
else()
    target_compile_options(sgs-client-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(sgs-client PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(sgs-client PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(sgs-client-smoke
        tests/smoke_test.cpp
    )
    target_link_libraries(sgs-client-smoke PRIVATE Qt6::Test)
    target_compile_definitions(sgs-client-smoke PRIVATE
        SGS_CLIENT_EXECUTABLE="$<TARGET_FILE:sgs-client>"
    )
    add_dependencies(sgs-client-smoke sgs-client)

    if(MSVC)
        target_compile_options(sgs-client-smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sgs-client-smoke PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME sgs-client-smoke COMMAND sgs-client-smoke)
    set_tests_properties(sgs-client-smoke PROPERTIES TIMEOUT 20)

    qt_add_executable(sgs-client-network-test tests/network_test.cpp)
    target_link_libraries(sgs-client-network-test PRIVATE
        sgs-client-core Qt6::Network Qt6::Test)
    if(MSVC)
        target_compile_options(sgs-client-network-test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sgs-client-network-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME sgs-client-network COMMAND sgs-client-network-test)
    set_tests_properties(sgs-client-network PROPERTIES TIMEOUT 30)
endif()

include(GNUInstallDirs)
install(TARGETS sgs-client
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
