
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b-docking.zip
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    color_te
    URL https://github.com/goossens/ImGuiColorTextEdit/archive/refs/tags/v1.93.0.zip
)
FetchContent_GetProperties(color_te)
if(NOT color_te_POPULATED)
    FetchContent_Populate(color_te)
endif()

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

    target_link_libraries(imgui PUBLIC freetype glew_s glfw ${OPENGL_LIBRARIES})
endif()





