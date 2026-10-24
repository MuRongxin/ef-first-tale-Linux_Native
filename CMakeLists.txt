cmake_minimum_required(VERSION 3.24)
project(ef_native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(EF_BUILD_APP "Build the SDL3/OpenGL preview application" ON)
option(EF_FETCH_DEPENDENCIES "Fetch pinned dependencies when not installed" ON)
option(EF_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
include(CTest)
include(cmake/Dependencies.cmake)

add_library(ef_core
    src/core/display.cpp
    src/core/utf8.cpp
    src/data/content.cpp
    src/data/image.cpp
    src/scene/scene.cpp
    src/script/script.cpp)
target_include_directories(ef_core PUBLIC src)
target_link_libraries(ef_core PUBLIC nlohmann_json::nlohmann_json PNG::PNG ${EF_WEBP_TARGET})

function(ef_compile_options target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(EF_SANITIZERS)
            target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PUBLIC -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
ef_compile_options(ef_core)

if(EF_BUILD_APP)
    add_executable(ef_native src/main.cpp src/app/application.cpp src/app/script_view.cpp src/render/renderer.cpp src/text/font.cpp)
    target_link_libraries(ef_native PRIVATE ef_core SDL3::SDL3 OpenGL::GL GLEW::GLEW Freetype::Freetype)
    ef_compile_options(ef_native)
    install(TARGETS ef_native RUNTIME DESTINATION bin)
endif()

if(BUILD_TESTING)
    add_executable(ef_core_tests tests/core_tests.cpp)
    target_link_libraries(ef_core_tests PRIVATE ef_core)
    ef_compile_options(ef_core_tests)
    add_test(NAME core COMMAND ef_core_tests)
    add_executable(ef_script_tests tests/script_tests.cpp)
    target_link_libraries(ef_script_tests PRIVATE ef_core)
    ef_compile_options(ef_script_tests)
    add_test(NAME script COMMAND ef_script_tests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME content_import COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prepare_content.py)
    endif()
endif()
