include(FetchContent)
find_path(EF_STB_VORBIS_INCLUDE_DIR NAMES stb_vorbis.c stb_vorbis.h PATH_SUFFIXES stb)
if(NOT EF_STB_VORBIS_INCLUDE_DIR)
    if(NOT EF_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "Install libstb-dev, provide EF_STB_VORBIS_INCLUDE_DIR, or enable EF_FETCH_DEPENDENCIES")
    endif()
    FetchContent_Declare(ef_stb
        URL https://codeload.github.com/nothings/stb/tar.gz/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5
        URL_HASH SHA256=a6c31950b3bdf05eb92258768ba2a11bd44449490574b1aac67028452288c40d)
    FetchContent_MakeAvailable(ef_stb)
    set(EF_STB_VORBIS_INCLUDE_DIR "${ef_stb_SOURCE_DIR}")
endif()
if(EXISTS "${EF_STB_VORBIS_INCLUDE_DIR}/stb_vorbis.c")
    set(EF_STB_VORBIS_HEADER "stb_vorbis.c")
else()
    set(EF_STB_VORBIS_HEADER "stb_vorbis.h")
endif()
find_package(PNG REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    if(NOT EF_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "Install nlohmann-json3-dev or enable EF_FETCH_DEPENDENCIES")
    endif()
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
        URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

pkg_check_modules(WEBP QUIET IMPORTED_TARGET libwebp>=1.3)
if(WEBP_FOUND)
    set(EF_WEBP_TARGET PkgConfig::WEBP)
else()
    if(NOT EF_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "Install libwebp-dev or enable EF_FETCH_DEPENDENCIES")
    endif()
    foreach(feature ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO WEBPMUX EXTRAS)
        set(WEBP_BUILD_${feature} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(webp
        URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
        URL_HASH SHA256=e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564)
    FetchContent_MakeAvailable(webp)
    # HD lossless images must remain interactive in developer builds too.
    # Keep vendor debug symbols, but optimize decoder/resampler hot loops.
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        foreach(codec_target webpdecode webpdsp webputils)
            if(TARGET ${codec_target})
                target_compile_options(${codec_target} PRIVATE $<$<CONFIG:Debug>:-O2>)
            endif()
        endforeach()
    endif()
    set(EF_WEBP_TARGET webp)
endif()

if(EF_BUILD_APP)
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(Freetype REQUIRED)
    # Older SDL 3.2 can terminate on X11 BadWindow during fullscreen reparenting.
    # Use the verified stable release with the upstream nonfatal error handler.
    find_package(SDL3 3.4.18 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        if(NOT EF_FETCH_DEPENDENCIES)
            message(FATAL_ERROR "Install SDL3 >= 3.4.18 or enable EF_FETCH_DEPENDENCIES")
        endif()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            URL https://www.libsdl.org/release/SDL3-3.4.18.tar.gz
            URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
        FetchContent_MakeAvailable(SDL3)
    endif()

    # Optional: movie playback (H.264/MPEG via FFmpeg). The runtime decodes content/video
    # files; without the development headers .movie stays a strict unsupported error.
    find_path(EF_FFMPEG_INCLUDE_DIR NAMES libavcodec/avcodec.h
        HINTS "${EF_FFMPEG_ROOT}/include" "${EF_FFMPEG_ROOT}/include/x86_64-linux-gnu"
        PATH_SUFFIXES x86_64-linux-gnu)
    foreach(_ef_ffmpeg_lib avcodec avformat avutil swscale swresample)
        string(TOUPPER "${_ef_ffmpeg_lib}" _ef_ffmpeg_upper)
        find_library(EF_FFMPEG_${_ef_ffmpeg_upper}_LIBRARY NAMES "${_ef_ffmpeg_lib}"
            HINTS "${EF_FFMPEG_ROOT}/lib" "${EF_FFMPEG_ROOT}/lib/x86_64-linux-gnu")
    endforeach()
    set(EF_FFMPEG_FOUND OFF)
    if(EF_FFMPEG_INCLUDE_DIR AND EF_FFMPEG_AVCODEC_LIBRARY AND EF_FFMPEG_AVFORMAT_LIBRARY
       AND EF_FFMPEG_AVUTIL_LIBRARY AND EF_FFMPEG_SWSCALE_LIBRARY AND EF_FFMPEG_SWRESAMPLE_LIBRARY)
        set(EF_FFMPEG_FOUND ON)
    endif()
endif()
