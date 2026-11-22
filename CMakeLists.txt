cmake_minimum_required(VERSION 3.16)
project(solar_module_c LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(solar_c STATIC
    src/geometry.cpp
    src/texture.cpp
    src/shading.cpp
)
target_include_directories(solar_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(test_module_c tests/test_module_c.cpp)
target_link_libraries(test_module_c PRIVATE solar_c)

add_executable(demo_render demo/demo_render.cpp)
target_link_libraries(demo_render PRIVATE solar_c)

enable_testing()
add_test(NAME module_c_selfcheck COMMAND test_module_c)
