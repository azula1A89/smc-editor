cmake_minimum_required(VERSION 3.24)

project(graphviz_dot_layout_demo LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(deps)

add_library(dot_layout STATIC
  src/dot_layout.cpp
  src/static_plugins.cpp
)
target_include_directories(dot_layout PUBLIC include)
target_link_libraries(dot_layout PRIVATE graphviz_dot_static)
add_dependencies(dot_layout graphviz_static_build)

add_executable(dot_layout_demo src/main.cpp)
target_link_libraries(dot_layout_demo PRIVATE dot_layout imgui glew_s)

if(BUILD_TESTING)
  add_test(NAME dot_layout_demo_runs COMMAND dot_layout_demo)
  add_executable(dot_layout_tests tests/dot_layout_tests.cpp)
  target_link_libraries(dot_layout_tests PRIVATE dot_layout)
  add_test(NAME dot_layout_tests COMMAND dot_layout_tests)
endif()
