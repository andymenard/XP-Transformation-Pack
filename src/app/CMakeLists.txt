# The app: a library holding everything (theme library, generators, apply,
# setup), and the thin programs over it. Qt 6 only.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network DBus)

set(XPLASMA_VERSION 0.1.0)

add_library(xplasma-app STATIC
  paths.cpp
  state.cpp
  adopt.cpp
  host.cpp
  archive.cpp
)
target_include_directories(xplasma-app PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(xplasma-app PUBLIC XPLASMA_VERSION="${XPLASMA_VERSION}")
target_link_libraries(xplasma-app
  PUBLIC xplasma-core Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus
  PRIVATE xplasma::archive)

# The CLI. (The target name `xplasma` is the style plugin's.)
add_executable(xplasma-cli cli/main.cpp)
target_link_libraries(xplasma-cli PRIVATE stdc++ xplasma-app)
set_target_properties(xplasma-cli PROPERTIES OUTPUT_NAME xplasma)
