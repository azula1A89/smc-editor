
cmake_minimum_required(VERSION 3.20)
project(smc_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(smc_core
    src/generator.cpp
    src/lexer.cpp
    src/parser.cpp
    src/syntax_checker.cpp
)
target_include_directories(smc_core PUBLIC include)
target_compile_features(smc_core PUBLIC cxx_std_20)

add_executable(smc_cpp src/main.cpp)
target_link_libraries(smc_cpp PRIVATE smc_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(smc_tests tests/test_smc.cpp)
    target_link_libraries(smc_tests PRIVATE smc_core)
    add_test(NAME smc_unit_tests COMMAND smc_tests)
    add_test(NAME smc_cli_valid
             COMMAND smc_cpp -c++ --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid.sm)
    add_test(NAME smc_cli_invalid
             COMMAND smc_cpp -c++ --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid.sm)
    add_test(NAME smc_cli_lexer_grammar
             COMMAND smc_cpp -c++ --check ${CMAKE_CURRENT_SOURCE_DIR}/SmcLexer.sm)
    add_test(NAME smc_cli_parser_grammar
             COMMAND smc_cpp -c++ --check ${CMAKE_CURRENT_SOURCE_DIR}/SmcParser.sm)
    set_tests_properties(smc_cli_invalid PROPERTIES WILL_FAIL TRUE)
endif()
