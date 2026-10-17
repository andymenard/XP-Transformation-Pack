# SPDX-License-Identifier: GPL-3.0-or-later
# The app: a library holding everything (theme library, generators, apply,
# setup), and the thin programs over it. Qt 6 only.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network DBus)

# The project's settings (project.ini, at the top): its version and its
# addresses, into app/project.h.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/project.ini)
file(STRINGS ${PROJECT_SOURCE_DIR}/project.ini project_lines REGEX "^[A-Za-z]+=")
foreach(line IN LISTS project_lines)
  string(REGEX MATCH "^([A-Za-z]+)=(.*)$" _ "${line}")
  set(project_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()
set(XPLASMA_VERSION "${project_Version}")
set(XPLASMA_ISSUES_URL "${project_IssuesUrl}")
set(XPLASMA_UPDATES_URL "${project_UpdatesUrl}")
set(XPLASMA_ARCHIVE_URL "${project_ArchiveUrl}")
if(NOT XPLASMA_VERSION MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "project.ini's Version must be a whole number (a release's number), not '${XPLASMA_VERSION}'")
endif()
configure_file(project.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/app/project.h @ONLY)

add_library(xplasma-app STATIC
  paths.cpp
  state.cpp
  adopt.cpp
  host.cpp
  archive.cpp
  hivedef.cpp
  library.cpp
  themeimport.cpp
  look.cpp
  editor/customluna.cpp
  package.cpp
  colors.cpp
  darkmode.cpp
  decoration.cpp
  panel.cpp
  controls.cpp
  media.cpp
  startbutton.cpp
  gdifont.cpp
  icons.cpp
  startpanel.cpp
  startmenu.cpp
  session.cpp
  xptext.cpp
  crisptext.cpp
  inf.cpp
  pointers.cpp
  sounds.cpp
  updates.cpp
  timezones.cpp
  forks.cpp
  flatpak.cpp
  install.cpp
  gtk.cpp
  qttarget.cpp
  overrides.cpp
  apply.cpp
  setup.cpp
  desktop.cpp
)
target_include_directories(xplasma-app PUBLIC ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(xplasma-app PUBLIC XPLASMA_VERSION="${XPLASMA_VERSION}")
target_link_libraries(xplasma-app
  PUBLIC xplasma-core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::DBus
  PRIVATE xplasma::archive)

# XP's stock Windows Classic schemes (see hivedef.h), as :/schemes/*.theme.
file(GLOB stock_schemes RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS data/schemes/*.theme)
qt_add_resources(xplasma-app "schemes" PREFIX /schemes BASE data/schemes FILES ${stock_schemes})

# The Start plasmoid's package, as :/start-menu/...
file(GLOB_RECURSE start_menu RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS start-menu/*)
# The package must carry a helper built for the target machine, including when
# it is reinstalled by a theme change. Never embed a developer's binary.
list(FILTER start_menu EXCLUDE REGEX "^start-menu/popupguard/")
list(FILTER start_menu EXCLUDE REGEX "\\.so$")
qt_add_resources(xplasma-app "startmenu" PREFIX /start-menu BASE start-menu FILES ${start_menu})
add_subdirectory(start-menu/popupguard)
set(start_guard "${CMAKE_CURRENT_BINARY_DIR}/startmenu-generated/libxpmenuguard.so")
add_custom_command(OUTPUT "${start_guard}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/startmenu-generated"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:xpmenuguard>" "${start_guard}"
  DEPENDS xpmenuguard)
set_source_files_properties("${start_guard}" PROPERTIES GENERATED TRUE
  QT_RESOURCE_ALIAS "contents/ui/popupguard-v8/libxpmenuguard.so")
qt_add_resources(xplasma-app "startmenu_guard" PREFIX /start-menu FILES "${start_guard}")
# The clock plasmoid's package, as :/clock/...
file(GLOB_RECURSE clock_package RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS clock/*)
qt_add_resources(xplasma-app "clock" PREFIX /clock BASE clock FILES ${clock_package})

# KDE QML patched over (overrides/) and plasmashell scripts (scripts/), as
# :/overrides/... and :/scripts/...
file(GLOB override_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS overrides/*)
qt_add_resources(xplasma-app "overrides" PREFIX /overrides BASE overrides FILES ${override_files})
file(GLOB script_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS scripts/*.js)
qt_add_resources(xplasma-app "scripts" PREFIX /scripts BASE scripts FILES ${script_files})

# The CLI. (The target name `xplasma` is the style plugin's.)
add_executable(xplasma-cli cli/main.cpp)
target_link_libraries(xplasma-cli PRIVATE stdc++ xplasma-app)
set_target_properties(xplasma-cli PROPERTIES OUTPUT_NAME xplasma)

# Regenerates data/schemes from the XP ISO (a dev tool; not installed).
add_executable(xplasma-schemes ${PROJECT_SOURCE_DIR}/src/tools/schemes.cpp)
target_link_libraries(xplasma-schemes PRIVATE stdc++ xplasma-app)

# The editor: Qt Widgets, in the system's own widget style.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(xplasma-edit
  editor/main.cpp
  editor/displayproperties.cpp
  editor/datetime.cpp
  editor/soundstab.cpp
  editor/abouttab.cpp
  wizard/art.cpp
  editor/updatenotifier.cpp
  editor/preview.cpp
  editor/advanced.cpp
  editor/extras.cpp
  editor/setupdialog.cpp
  editor/jobs.cpp
)
target_link_libraries(xplasma-edit PRIVATE stdc++ xplasma-app Qt6::Widgets)
# (the About tab's licence and notices, as :/licenses/...)
qt_add_resources(xplasma-edit "licenses" PREFIX /licenses BASE ${PROJECT_SOURCE_DIR}
                 FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/THIRD-PARTY-NOTICES)
# Appearance tab: embedded Tux Start logo.
qt_add_resources(xplasma-edit "logos" PREFIX /logos BASE editor/logos FILES editor/logos/tux.png editor/logos/kde-crystal.png)
add_test(NAME editor-screenshots COMMAND xplasma-edit --screenshots ${CMAKE_BINARY_DIR}/editor-screenshots)
set_tests_properties(editor-screenshots PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

# The Setup wizard: a program of its own, which carries XPlasma (package.sh
# appends the release tree to it) and isn't installed with it.
add_executable(xplasma-setup wizard/main.cpp wizard/art.cpp)
target_link_libraries(xplasma-setup PRIVATE stdc++ xplasma-app Qt6::Widgets)
add_test(NAME setup-screenshots COMMAND xplasma-setup --screenshots ${CMAKE_BINARY_DIR}/setup-screenshots)
set_tests_properties(setup-screenshots PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

# Installed as <prefix>/bin/xplasma{,-edit}, beside lib/xplasma (the style
# plugins) and the launcher; install.sh points the launcher at the real path.
install(TARGETS xplasma-cli xplasma-edit xplasma-setup RUNTIME DESTINATION bin)
install(FILES data/fyi.hotsocket.xplasma.desktop data/fyi.hotsocket.xplasma.datetime.desktop DESTINATION share/applications)
# (run at login: installed into ~/.config/autostart by Setup and install.sh)
install(FILES data/fyi.hotsocket.xplasma.updates.desktop DESTINATION share/xplasma/autostart)
# (the licence and the third-party notices, with the programs)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/THIRD-PARTY-NOTICES DESTINATION share/licenses/xplasma)

qt_add_resources(xplasma-app "darkmode" PREFIX /dark-mode BASE data/dark-mode FILES data/dark-mode/build.py data/dark-mode/palette.theme)
