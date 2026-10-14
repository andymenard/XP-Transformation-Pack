# SPDX-License-Identifier: GPL-3.0-or-later
find_package(${QT} REQUIRED COMPONENTS Test)
add_executable(test-style-geometry test_geometry.cpp ../../src/style/xplasmastyle.cpp)
target_include_directories(test-style-geometry PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test-style-geometry PRIVATE stdc++ xplasma-core ${QT}::Widgets ${QT}::Test)
add_test(NAME test-style-geometry COMMAND test-style-geometry)
