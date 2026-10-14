# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 REQUIRED COMPONENTS Test Network)

add_executable(test-app test_app.cpp)
target_link_libraries(test-app PRIVATE stdc++ xplasma-app xplasma::archive Qt6::Test Qt6::Network)
add_test(NAME test-app COMMAND test-app)

# The Start menu's QML, drawn offscreen from generated look data.
find_package(Qt6 COMPONENTS Quick)
if(Qt6Quick_FOUND)
  add_executable(test-startmenu test_startmenu.cpp)
  target_link_libraries(test-startmenu PRIVATE stdc++ xplasma-app Qt6::Quick Qt6::Widgets Qt6::Test)
  add_test(NAME test-startmenu COMMAND test-startmenu)
endif()

add_executable(test-preview test_preview.cpp ${PROJECT_SOURCE_DIR}/src/app/editor/preview.cpp)
target_link_libraries(test-preview PRIVATE stdc++ xplasma-app Qt6::Widgets Qt6::Test)
add_test(NAME test-preview COMMAND test-preview)
set_tests_properties(test-preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion")
