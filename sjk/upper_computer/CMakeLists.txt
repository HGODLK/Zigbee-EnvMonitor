cmake_minimum_required(VERSION 3.16)
project(EnvMonitor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt 6
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Network)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/tcpclient.cpp
    src/dataparser.cpp
    src/dashboard.cpp
    src/chartview.cpp
    src/datastore.cpp
    src/devicepanel.cpp
    src/databridgeserver.cpp
)

set(HEADERS
    src/mainwindow.h
    src/tcpclient.h
    src/dataparser.h
    src/dashboard.h
    src/chartview.h
    src/datastore.h
    src/devicepanel.h
    src/databridgeserver.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::Network
)

# Windows: hide console in release
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE $<CONFIG:Release>
)
