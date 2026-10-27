cmake_minimum_required(VERSION 3.16)

project(bkiso-qt VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(BKISO_QT_BUILD_GUI "Build the Qt application" ON)
option(BKISO_QT_BUILD_TESTS "Build the Qt-free IsoSession test" ON)

# ---------------------------------------------------------------- Qt (6 or 5)
if(BKISO_QT_BUILD_GUI)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Concurrent)
    find_package(Qt${QT_VERSION_MAJOR} 5.12 REQUIRED COMPONENTS Widgets Concurrent)
endif()

# ----------------------------------------------------------------- bkisofs
# Default: compile the bkisofs sources into a static library.
# Alternative: point BKISOFS_LIBRARY at a prebuilt libbk.so / bk.a; the
# headers are still taken from BKISOFS_SOURCE_DIR.
set(BKISOFS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bkisofs"
    CACHE PATH "Directory containing bk.h and the bkisofs sources")
set(BKISOFS_LIBRARY "" CACHE FILEPATH
    "Prebuilt bkisofs library (libbk.so or bk.a). Empty = build from source")

if(NOT EXISTS "${BKISOFS_SOURCE_DIR}/bk.h")
    message(FATAL_ERROR
        "bk.h not found in BKISOFS_SOURCE_DIR (${BKISOFS_SOURCE_DIR}). "
        "Configure with -DBKISOFS_SOURCE_DIR=/path/to/bkisofs")
endif()

if(BKISOFS_LIBRARY)
    add_library(bkisofs UNKNOWN IMPORTED)
    set_target_properties(bkisofs PROPERTIES
        IMPORTED_LOCATION "${BKISOFS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${BKISOFS_SOURCE_DIR}")
    message(STATUS "bkisofs: using prebuilt ${BKISOFS_LIBRARY}")
else()
    set(BK_SOURCES
        bkAdd.c bkCache.c bkDelete.c bkError.c bkExtract.c bkGet.c
        bkIoWrappers.c bkLink.c bkMangle.c bkMisc.c bkPath.c bkRead.c
        bkRead7x.c bkSet.c bkSort.c bkTime.c bkWrite.c bkWrite7x.c)
    list(TRANSFORM BK_SOURCES PREPEND "${BKISOFS_SOURCE_DIR}/")
    add_library(bkisofs STATIC ${BK_SOURCES})
    target_include_directories(bkisofs PUBLIC "${BKISOFS_SOURCE_DIR}")
    # gnu99 like the upstream Makefile (lstat, readlink, symlink)
    set_target_properties(bkisofs PROPERTIES
        C_STANDARD 99 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
    target_compile_options(bkisofs PRIVATE -Wno-unused-parameter)
    message(STATUS "bkisofs: building from ${BKISOFS_SOURCE_DIR}")
endif()

# VolInfo embeds off_t (bk_off_t). The library is built with
# _FILE_OFFSET_BITS=64, so every user of bk.h must be too or the struct
# layouts differ on 32-bit systems.
set_property(TARGET bkisofs APPEND PROPERTY
    INTERFACE_COMPILE_DEFINITIONS _FILE_OFFSET_BITS=64)
if(NOT BKISOFS_LIBRARY)
    target_compile_definitions(bkisofs PRIVATE _FILE_OFFSET_BITS=64)
endif()

# ------------------------------------------------------------- application
if(BKISO_QT_BUILD_GUI)
add_executable(bkiso-qt
    src/main.cpp
    src/mainwindow.cpp src/mainwindow.h
    src/isosession.cpp src/isosession.h
    src/isotreewidget.cpp src/isotreewidget.h
    src/operationrunner.cpp src/operationrunner.h
    src/dialogs.cpp src/dialogs.h)

set_target_properties(bkiso-qt PROPERTIES AUTOMOC ON)
target_compile_definitions(bkiso-qt PRIVATE
    BKISO_QT_VERSION="${PROJECT_VERSION}")
target_compile_options(bkiso-qt PRIVATE -Wall -Wextra)
target_link_libraries(bkiso-qt PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    bkisofs)

include(GNUInstallDirs)
install(TARGETS bkiso-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ------------------------------------------------------------------- tests
if(BKISO_QT_BUILD_TESTS)
    enable_testing()
    add_executable(session_test tests/session_test.cpp src/isosession.cpp)
    target_include_directories(session_test PRIVATE src)
    target_link_libraries(session_test PRIVATE bkisofs)
    add_test(NAME session_test
             COMMAND session_test "${CMAKE_CURRENT_BINARY_DIR}/session_test_scratch")
endif()
