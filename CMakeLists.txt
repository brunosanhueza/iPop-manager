cmake_minimum_required(VERSION 3.16)

project(iPop
    VERSION 1.0.0
    DESCRIPTION "Gestor moderno de iPod para Linux con Qt 6 y C++"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
)

set(IPOP_SOURCES
    src/main.cpp
    src/core/Track.h
    src/core/Playlist.h
    src/core/ITunesDBReader.h
    src/core/ITunesDBReader.cpp
    src/core/ITunesHash58.h
    src/core/ITunesHash58.cpp
    src/core/IPodDevice.h
    src/core/IPodDevice.cpp
    src/core/DeviceDetector.h
    src/core/DeviceDetector.cpp
    src/ui/CapacityBar.h
    src/ui/CapacityBar.cpp
    src/ui/TrackTableModel.h
    src/ui/TrackTableModel.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

add_executable(ipop ${IPOP_SOURCES})

target_include_directories(ipop PRIVATE
    src
    src/core
    src/ui
)

target_link_libraries(ipop PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Opciones de compilación
if(MSVC)
    target_compile_options(ipop PRIVATE /W4)
else()
    target_compile_options(ipop PRIVATE -Wall -Wextra -pedantic)
endif()

install(TARGETS ipop
    RUNTIME DESTINATION bin
)
