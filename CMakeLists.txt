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

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
