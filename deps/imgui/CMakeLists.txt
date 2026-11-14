
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b-docking.zip
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    imgui_file_selector
    URL https://github.com/goossens/ImGuiFileSelector/archive/refs/tags/v1.93.0.zip
)
FetchContent_MakeAvailable(imgui_file_selector)

FetchContent_Declare(
    color_te
    URL https://github.com/goossens/ImGuiColorTextEdit/archive/refs/tags/v1.93.0.zip
)
FetchContent_GetProperties(color_te)
if(NOT color_te_POPULATED)
    FetchContent_Populate(color_te)
endif()

set(color_te_original_source_dir "${color_te_SOURCE_DIR}")
set(color_te_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/color_te_patched")
file(COPY "${color_te_original_source_dir}/" DESTINATION "${color_te_SOURCE_DIR}"
    PATTERN "TextEditor.h" EXCLUDE PATTERN "TextEditor.cpp" EXCLUDE)
include("${CMAKE_CURRENT_SOURCE_DIR}/semantic_editor_patch.cmake")

# Some platforms cannot convert all filesystem names to wide strings. Keep the
# directory usable and fall back to byte ordering for those names.
file(READ "${imgui_file_selector_SOURCE_DIR}/FileSelector.cpp" selector_source)
replace_once(selector_source
    "auto sortString = entry.path.filename().generic_wstring();"
    "std::wstring sortString;\n\t\t\t\ttry {\n\t\t\t\t\tsortString = entry.path.filename().generic_wstring();\n\t\t\t\t} catch (const std::filesystem::filesystem_error&) {\n\t\t\t\t\tfor (unsigned char byte : pathToString(entry.path.filename()))\n\t\t\t\t\t\tsortString.push_back(byte);\n\t\t\t\t}")
set(selector_patched_source "${CMAKE_CURRENT_BINARY_DIR}/FileSelector.cpp")
write_if_changed("${selector_patched_source}" "${selector_source}")

add_library(imgui_file_selector STATIC ${selector_patched_source})
target_include_directories(imgui_file_selector PUBLIC ${imgui_file_selector_SOURCE_DIR})
target_link_libraries(imgui_file_selector PUBLIC imgui)

if(NOT TARGET imgui)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
	    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
        ${color_te_SOURCE_DIR}/TextEditor.cpp
        ${color_te_SOURCE_DIR}/TextDiff.cpp
        ${color_te_SOURCE_DIR}/example/dejavu.cpp
        ${color_te_SOURCE_DIR}/example/notosans.cpp
        ${color_te_SOURCE_DIR}/extras/TrieAutoComplete.cpp
    )
    target_compile_definitions(imgui PUBLIC "IMGUI_USE_WCHAR32")
    target_compile_definitions(imgui PUBLIC "IMGUI_DEFINE_MATH_OPERATORS")
    target_compile_definitions(imgui PUBLIC "IMGUI_ENABLE_FREETYPE")
    target_compile_definitions(imgui PUBLIC "IMGUI_DISABLE_DEFAULT_FONT")
    target_compile_definitions(imgui PUBLIC "IMGUI_DISABLE_OBSOLETE_FUNCTIONS")
    target_compile_definitions(imgui PUBLIC "IMGUI_DISABLE_OBSOLETE_KEYIO")
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}/misc)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}/misc/cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}/misc/freetype)
    target_include_directories(imgui PUBLIC ${glfw_SOURCE_DIR}/include)
    target_include_directories(imgui PUBLIC ${color_te_SOURCE_DIR})
    target_include_directories(imgui PUBLIC ${color_te_SOURCE_DIR}/example)
    target_include_directories(imgui PUBLIC ${color_te_SOURCE_DIR}/extras)

    target_link_libraries(imgui PUBLIC freetype glew_s glfw ${OPENGL_LIBRARIES})
endif()





