cmake_minimum_required(VERSION 3.24)
project(ImageTool VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IMAGETOOL_FETCH_DEPS "Fetch third-party dependencies at configure time" ON)

find_package(OpenGL REQUIRED)

if(IMAGETOOL_FETCH_DEPS)
  include(FetchContent)

  set(FETCHCONTENT_QUIET OFF)

  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
  )

  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
  )

  FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
  )

  FetchContent_MakeAvailable(glfw imgui stb)
else()
  message(FATAL_ERROR "IMAGETOOL_FETCH_DEPS=OFF is not supported in this template yet.")
endif()

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib
  PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

if(APPLE)
  target_compile_definitions(imgui_lib PUBLIC GL_SILENCE_DEPRECATION)
endif()

add_library(image_core STATIC
  core/image_core.c
)

target_include_directories(image_core
  PUBLIC
    core
    ${stb_SOURCE_DIR}
)

add_executable(ImageTool
  app/main.cpp
)

target_link_libraries(ImageTool
  PRIVATE
    image_core
    imgui_lib
    glfw
    OpenGL::GL
)

target_include_directories(ImageTool
  PRIVATE
    app
    core
)

if(MSVC)
  target_compile_options(ImageTool PRIVATE /W4 /permissive-)
  target_compile_options(image_core PRIVATE /W4)
else()
  target_compile_options(ImageTool PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(image_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
  target_compile_definitions(ImageTool PRIVATE GL_SILENCE_DEPRECATION)
endif()
