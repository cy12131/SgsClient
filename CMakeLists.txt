cmake_minimum_required(VERSION 3.21)

project(SgsClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Qml QuickControls2 Test)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

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
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
)

target_compile_definitions(sgs-client PRIVATE
    SGS_CLIENT_VERSION="0.1.0-dev"
)

if(MSVC)
    target_compile_options(sgs-client PRIVATE /W4 /permissive-)
else()
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
endif()

include(GNUInstallDirs)
install(TARGETS sgs-client
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
