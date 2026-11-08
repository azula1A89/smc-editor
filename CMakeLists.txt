cmake_minimum_required(VERSION 3.24)

project(graphviz_dot_layout_demo LANGUAGES C CXX)

include(CTest)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
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
target_link_libraries(dot_layout_demo PRIVATE dot_layout imgui glew_s smc_core Threads::Threads)

if(BUILD_TESTING)
  add_test(NAME dot_layout_demo_runs COMMAND dot_layout_demo)
  add_executable(dot_layout_tests tests/dot_layout_tests.cpp)
  target_link_libraries(dot_layout_tests PRIVATE dot_layout)
  add_test(NAME dot_layout_tests COMMAND dot_layout_tests)
  add_executable(render_graph_tests tests/render_graph_tests.cpp)
  target_link_libraries(render_graph_tests PRIVATE dot_layout imgui glew_s smc_core Threads::Threads)
  target_compile_definitions(render_graph_tests PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME render_graph_tests COMMAND render_graph_tests)
  add_executable(make_graph_tests tests/make_graph_tests.cpp)
  target_link_libraries(make_graph_tests PRIVATE dot_layout imgui glew_s smc_core Threads::Threads)
  target_compile_definitions(make_graph_tests PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME make_graph_tests COMMAND make_graph_tests)
  add_executable(performance_pipeline_tests tests/performance_pipeline_tests.cpp)
  target_link_libraries(performance_pipeline_tests PRIVATE dot_layout imgui glew_s smc_core Threads::Threads)
  target_compile_definitions(performance_pipeline_tests PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME performance_pipeline_tests COMMAND performance_pipeline_tests)
  add_executable(smc_editor_language_tests tests/smc_editor_language_tests.cpp)
  target_link_libraries(smc_editor_language_tests PRIVATE imgui)
  target_include_directories(smc_editor_language_tests PRIVATE include)
  add_test(NAME smc_editor_language_tests COMMAND smc_editor_language_tests)
endif()
