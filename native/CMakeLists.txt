# XPLasma native: the msstyles core, renderer and Qt style plugin.
#
# One source tree, built once per Qt major:
#   cmake -S native -B native/build-qt6 -G Ninja -DQT_MAJOR=6
#   cmake -S native -B native/build-qt5 -G Ninja -DQT_MAJOR=5
#
# Only public Qt API is allowed (no *Private targets, no _p.h headers). That is
# what lets a plugin built against the oldest supported Qt of a major load into
# every newer one -- see PLAN.md, D1.
cmake_minimum_required(VERSION 3.16)
project(xplasma LANGUAGES CXX)

set(QT_MAJOR 6 CACHE STRING "Qt major version to build against (5 or 6)")
set_property(CACHE QT_MAJOR PROPERTY STRINGS 5 6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# Hidden by default: the plugin is loaded into every Qt process on the desktop
# and must never interpose on (or be interposed by) another library's symbols.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt${QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
set(QT Qt${QT_MAJOR})

add_compile_options(-Wall -Wextra)
add_compile_definitions(QT_NO_KEYWORDS QT_USE_QSTRINGBUILDER)

# --- the core: .msstyles in, typed theme data out (QtCore + QtGui only) --------
add_library(xplasma-core STATIC
  core/pe.cpp
  core/dib.cpp
  core/ini.cpp
  core/theme.cpp
)
target_include_directories(xplasma-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xplasma-core PUBLIC ${QT}::Gui)
set_target_properties(xplasma-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- the style plugin ---------------------------------------------------------
add_library(xplasma MODULE
  style/plugin.cpp
  style/xplasmastyle.cpp
)
# libstdc++ first. Link order decides which library a symbol is bound to, and
# the Qt Company's official Qt 5.15 binaries export sized operator delete
# (_ZdlPvm@Qt_5): linked after Qt, the plugin binds it there and then fails
# to resolve it on every distribution's Qt 5, which doesn't. check-abi.sh
# guards against this whole class.
target_link_libraries(xplasma PRIVATE stdc++ xplasma-core ${QT}::Widgets)
set_target_properties(xplasma PROPERTIES PREFIX "lib" OUTPUT_NAME xplasma)
# Installs where app/widget_style.py looks for the prebuilt plugin:
#   <prefix>/qt<N>/styles/libxplasma.so   (prefix: app/plugins in a checkout)
install(TARGETS xplasma LIBRARY DESTINATION qt${QT_MAJOR}/styles)

# --- tools --------------------------------------------------------------------
add_executable(xplasma-probe tools/probe.cpp)
target_link_libraries(xplasma-probe PRIVATE ${QT}::Widgets)

add_executable(xplasma-dump tools/dump.cpp)
target_link_libraries(xplasma-dump PRIVATE stdc++ xplasma-core)

# --- tests ------------------------------------------------------------------------
add_executable(test-core tests/test_core.cpp)
target_link_libraries(test-core PRIVATE stdc++ xplasma-core)

# --- fuzzing (native/fuzz.sh; clang only) --------------------------------------------
option(XPLASMA_FUZZ "Build the libFuzzer targets (needs clang)" OFF)
if(XPLASMA_FUZZ)
  foreach(target theme dib ini)
    string(TOUPPER ${target} TARGET_UPPER)
    add_executable(fuzz-${target} tests/fuzz.cpp)
    target_compile_definitions(fuzz-${target} PRIVATE FUZZ_TARGET_${TARGET_UPPER}=1)
    target_link_libraries(fuzz-${target} PRIVATE xplasma-core)
    target_link_options(fuzz-${target} PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()
