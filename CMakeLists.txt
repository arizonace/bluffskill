cmake_minimum_required(VERSION 3.25)

project(BluffSkill VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Network HttpServer)
qt_standard_project_setup()

add_subdirectory(src/cards)
add_subdirectory(src/poker)
add_subdirectory(apps/server)
add_subdirectory(apps/client)

if(APPLE)
    add_custom_target(build-mac-bundles
        COMMAND ${CMAKE_COMMAND}
            "-DBUNDLE_PATH=$<TARGET_FILE_DIR:bluffskill-server>/BluffSkill Server.app"
            "-DBUNDLE_NAME=BluffSkill Server"
            "-DBUNDLE_IDENTIFIER=com.bluffskill.server"
            "-DBUNDLE_EXECUTABLE=$<TARGET_FILE:bluffskill-server>"
            "-DBUNDLE_ICON=${CMAKE_SOURCE_DIR}/apps/server/resources/BluffSkillServer.icns"
            -P "${CMAKE_SOURCE_DIR}/cmake/create_macos_bundle.cmake"
        COMMAND ${CMAKE_COMMAND}
            "-DBUNDLE_PATH=$<TARGET_FILE_DIR:bluffskill-client>/BluffSkill.app"
            "-DBUNDLE_NAME=BluffSkill"
            "-DBUNDLE_IDENTIFIER=com.bluffskill.client"
            "-DBUNDLE_EXECUTABLE=$<TARGET_FILE:bluffskill-client>"
            "-DBUNDLE_ICON=${CMAKE_SOURCE_DIR}/apps/client/resources/BluffSkillClient.icns"
            -P "${CMAKE_SOURCE_DIR}/cmake/create_macos_bundle.cmake"
        DEPENDS bluffskill-server bluffskill-client
        COMMENT "Creating local macOS app bundles"
        VERBATIM
        USES_TERMINAL
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
