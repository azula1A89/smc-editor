

set(generated_header "${CMAKE_CURRENT_SOURCE_DIR}/GraphEditorContext.hpp")
set(generated_source "${CMAKE_CURRENT_SOURCE_DIR}/GraphEditorContext.cpp")
add_custom_command(
    OUTPUT "${generated_header}" "${generated_source}"
    COMMAND "$<TARGET_FILE:smc_cpp>" -c++ -d "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/GraphEditor.sm"
    DEPENDS smc_cpp "${CMAKE_CURRENT_SOURCE_DIR}/GraphEditor.sm"
    VERBATIM
)

add_library(graph_editor_context graph_editor.cpp "${generated_header}" "${generated_source}")
target_include_directories(graph_editor_context PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${smc_cpp_SOURCE_DIR}/include/headers/c++"
)

