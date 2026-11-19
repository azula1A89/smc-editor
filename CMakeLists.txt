cmake_minimum_required(VERSION 3.24)

project(smc-editor LANGUAGES C CXX)

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

add_library(app_log STATIC src/log.cpp)
target_include_directories(app_log PUBLIC include)
target_link_libraries(app_log PUBLIC Threads::Threads)

add_library(app_ui STATIC
  src/app_context.cpp
  src/user_settings.cpp
  src/analysis.cpp
  src/cpp_generation.cpp
  src/dockspace.cpp
  src/editor_window.cpp
  src/graph_window.cpp
  src/log_window.cpp
)
target_include_directories(app_ui PUBLIC include)
target_link_libraries(app_ui PUBLIC app_log dot_layout imgui imgui_file_selector glew_s smc_core Threads::Threads)

add_executable(smc-editor src/main.cpp src/application.cpp)
target_link_libraries(smc-editor PRIVATE app_ui)

if(BUILD_TESTING)
  add_test(NAME smc_editor_runs COMMAND smc-editor)
  add_executable(log_tests tests/log_tests.cpp)
  target_link_libraries(log_tests PRIVATE app_log)
  add_test(NAME log_tests COMMAND log_tests)
  add_executable(dot_layout_tests tests/dot_layout_tests.cpp)
  target_link_libraries(dot_layout_tests PRIVATE dot_layout)
  add_test(NAME dot_layout_tests COMMAND dot_layout_tests)
  add_executable(render_graph_tests tests/render_graph_tests.cpp)
  target_link_libraries(render_graph_tests PRIVATE app_ui)
  target_compile_definitions(render_graph_tests PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME render_graph_tests COMMAND render_graph_tests)
  add_executable(make_graph_tests tests/make_graph_tests.cpp)
  target_link_libraries(make_graph_tests PRIVATE app_ui)
  target_compile_definitions(make_graph_tests PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME make_graph_tests COMMAND make_graph_tests)
  add_executable(performance_pipeline_tests tests/performance_pipeline_tests.cpp)
  target_link_libraries(performance_pipeline_tests PRIVATE app_ui)
  target_compile_definitions(performance_pipeline_tests PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME performance_pipeline_tests COMMAND performance_pipeline_tests)
  add_test(NAME smc_error_markers_tests COMMAND performance_pipeline_tests --error-markers)
  add_test(NAME smc_semantic_highlighting_tests COMMAND performance_pipeline_tests --semantic)
  add_executable(cpp_generation_tests tests/cpp_generation_tests.cpp)
  target_link_libraries(cpp_generation_tests PRIVATE app_ui)
  add_test(NAME cpp_generation_tests COMMAND cpp_generation_tests)
  add_executable(smc_editor_language_tests tests/smc_editor_language_tests.cpp)
  target_link_libraries(smc_editor_language_tests PRIVATE imgui)
  target_include_directories(smc_editor_language_tests PRIVATE include)
  add_test(NAME smc_editor_language_tests COMMAND smc_editor_language_tests)
  add_executable(user_settings_tests tests/user_settings_tests.cpp src/user_settings.cpp)
  target_link_libraries(user_settings_tests PRIVATE imgui app_log)
  target_include_directories(user_settings_tests PRIVATE include)
  add_test(NAME user_settings_tests COMMAND user_settings_tests)
  add_executable(dockspace_tests tests/dockspace_tests.cpp)
  target_link_libraries(dockspace_tests PRIVATE app_ui)
  add_test(NAME dockspace_tests COMMAND dockspace_tests)
endif()
