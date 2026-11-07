FetchContent_Declare(
    glew
    URL https://github.com/nigels-com/glew/releases/download/glew-2.3.1/glew-2.3.1.tgz
)
FetchContent_GetProperties(glew)
if(NOT glew_POPULATED)
    FetchContent_Populate(glew)
endif()

set(GLEW_SOURCE_ROOT "${glew_SOURCE_DIR}")
add_subdirectory("${GLEW_SOURCE_ROOT}/build/cmake" "${glew_BINARY_DIR}")

foreach(target glew glew_s)
    target_include_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${GLEW_SOURCE_ROOT}/include>"
    )
endforeach()
