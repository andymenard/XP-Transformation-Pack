# The app: a library holding everything (theme library, generators, apply,
# setup), and the thin programs over it. Qt 6 only.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network DBus)

set(XPLASMA_VERSION 0.1.0)

add_library(xplasma-app STATIC
  paths.cpp
  state.cpp
  adopt.cpp
  host.cpp
  archive.cpp
  hivedef.cpp
  library.cpp
  look.cpp
  package.cpp
  colors.cpp
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
  inf.cpp
  pointers.cpp
  timezones.cpp
  forks.cpp
  flatpak.cpp
  qttarget.cpp
  overrides.cpp
  apply.cpp
  setup.cpp
)
target_include_directories(xplasma-app PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(xplasma-app PUBLIC XPLASMA_VERSION="${XPLASMA_VERSION}")
target_link_libraries(xplasma-app
  PUBLIC xplasma-core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::DBus
  PRIVATE xplasma::archive)

# XP's stock Windows Classic schemes (see hivedef.h), as :/schemes/*.theme.
file(GLOB stock_schemes RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS data/schemes/*.theme)
qt_add_resources(xplasma-app "schemes" PREFIX /schemes BASE data/schemes FILES ${stock_schemes})

# The Start plasmoid's package, as :/start-menu/...
file(GLOB_RECURSE start_menu RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS start-menu/*)
qt_add_resources(xplasma-app "startmenu" PREFIX /start-menu BASE start-menu FILES ${start_menu})
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
  editor/preview.cpp
  editor/advanced.cpp
  editor/extras.cpp
  editor/setupdialog.cpp
  editor/jobs.cpp
)
target_link_libraries(xplasma-edit PRIVATE stdc++ xplasma-app Qt6::Widgets)
add_test(NAME editor-screenshots COMMAND xplasma-edit --screenshots ${CMAKE_BINARY_DIR}/editor-screenshots)
set_tests_properties(editor-screenshots PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

# Installed as <prefix>/bin/xplasma{,-edit}, beside lib/xplasma (the style
# plugins) and the launcher; install.sh points the launcher at the real path.
install(TARGETS xplasma-cli xplasma-edit RUNTIME DESTINATION bin)
install(FILES data/fyi.hotsocket.xplasma.desktop data/fyi.hotsocket.xplasma.datetime.desktop DESTINATION share/applications)
