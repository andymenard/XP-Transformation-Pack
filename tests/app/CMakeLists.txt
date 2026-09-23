find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test-app test_app.cpp)
target_link_libraries(test-app PRIVATE stdc++ xplasma-app xplasma::archive Qt6::Test)
add_test(NAME test-app COMMAND test-app)
