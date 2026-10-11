cmake_minimum_required(VERSION 3.20)
project(ampsim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC PkgConfig::GLFW OpenGL::GL)

add_executable(ampsim src/main.cpp src/audio.cpp src/amp.cpp src/measured_eq.cpp)
target_link_libraries(ampsim PRIVATE imgui_lib PkgConfig::PORTAUDIO OpenGL::GL)
target_compile_options(ampsim PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

add_executable(test_amp tests/test_amp.cpp src/amp.cpp src/measured_eq.cpp)
target_compile_options(test_amp PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME test_amp COMMAND test_amp)

add_executable(test_audio tests/test_audio.cpp src/audio.cpp src/amp.cpp src/measured_eq.cpp)
target_include_directories(test_audio PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
target_compile_options(test_audio PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME test_audio COMMAND test_audio)

add_executable(analyze_amp tests/analyze_amp.cpp src/amp.cpp src/measured_eq.cpp)
target_compile_options(analyze_amp PRIVATE -Wall -Wextra -Wpedantic)
