cmake_minimum_required(VERSION 3.16)

project(iPop
    VERSION 1.0.0
    DESCRIPTION "Gestor moderno de iPod para Linux con Qt 6 y C++"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
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
    src/core/IpodTouchBridge.h
    src/core/IpodTouchBridge.cpp
    src/ui/CapacityBar.h
    src/ui/CapacityBar.cpp
    src/ui/TrackTableModel.h
    src/ui/TrackTableModel.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/DeleteProgressDialog.h
    src/ui/DeleteProgressDialog.cpp
    src/ui/MetadataEditDialog.h
    src/ui/MetadataEditDialog.cpp
    src/core/AudioConverter.h
    src/core/AudioConverter.cpp
    src/core/TrackImporter.h
    src/core/TrackImporter.cpp
    src/ui/ITunesPlayerWidget.h
    src/ui/ITunesPlayerWidget.cpp
)

add_executable(ipop ${IPOP_SOURCES})

# --- Herramientas para iPod Touch (iOS 4): gestor (libgpod) + nochmod.so + openssl-legacy.cnf ---
# Requieren libgpod-dev y glib (xbps-install libgpod-devel glib-devel). En Linux solamente.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND UNIX AND NOT APPLE)
    pkg_check_modules(GPOD QUIET IMPORTED_TARGET libgpod-1.0 glib-2.0)
endif()

if(GPOD_FOUND)
    set(IPOD_TOOLS_OUT ${CMAKE_BINARY_DIR}/tools/ipod)

    add_executable(gestor tools/ipod/gestor.c)
    target_link_libraries(gestor PRIVATE PkgConfig::GPOD)
    set_target_properties(gestor PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${IPOD_TOOLS_OUT})

    add_library(nochmod SHARED tools/ipod/nochmod.c)
    set_target_properties(nochmod PROPERTIES
        PREFIX ""
        OUTPUT_NAME nochmod
        LIBRARY_OUTPUT_DIRECTORY ${IPOD_TOOLS_OUT})

    add_custom_command(TARGET gestor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/tools/ipod/openssl-legacy.cnf ${IPOD_TOOLS_OUT}/openssl-legacy.cnf)

    add_dependencies(ipop gestor nochmod)
    target_compile_definitions(ipop PRIVATE IPOP_SOURCE_TOOLS_DIR="${IPOD_TOOLS_OUT}")
else()
    message(WARNING "libgpod-1.0/glib-2.0 no encontrados: no se compilaran las herramientas del iPod Touch (gestor). "
                    "Instala libgpod-devel y glib-devel para poder anadir/borrar canciones en iPod Touch.")
endif()

target_include_directories(ipop PRIVATE
    src
    src/core
    src/ui
)

target_link_libraries(ipop PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    SQLite::SQLite3
    ZLIB::ZLIB
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
