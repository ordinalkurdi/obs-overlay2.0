cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# This plugin needs the frontend API (recording, scenes, hotkeys...) and Qt. Forced here so a stock template preset
# (which sets both OFF) cannot silently build an empty plugin.
set(ENABLE_FRONTEND_API ON CACHE BOOL "Use obs-frontend-api for UI functionality" FORCE)
set(ENABLE_QT ON CACHE BOOL "Use Qt functionality" FORCE)
option(BUILD_OVL_TESTS "Build the unit tests for the pure-logic core (no OBS needed)" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON COMPILE_WARNING_AS_ERROR OFF
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE OVL_VERSION="${_version}")

# Video thumbnails in the gallery use the FFmpeg that ships with OBS's prebuilt dependencies (found through the
# template's CMAKE_PREFIX_PATH). If it is not found the gallery falls back to the Windows shell thumbnails.
option(OVL_USE_FFMPEG "Decode gallery video thumbnails with FFmpeg (turn OFF if OBS cannot load the plugin)" ON)
if(OVL_USE_FFMPEG)
  find_path(OVL_FFMPEG_INCLUDE libavformat/avformat.h)
  find_library(OVL_AVFORMAT avformat)
  find_library(OVL_AVCODEC avcodec)
  find_library(OVL_AVUTIL avutil)
  find_library(OVL_SWSCALE swscale)
endif()
if(OVL_USE_FFMPEG AND OVL_FFMPEG_INCLUDE AND OVL_AVFORMAT AND OVL_AVCODEC AND OVL_AVUTIL AND OVL_SWSCALE)
  message(STATUS "obs-overlay: FFmpeg found, video thumbnails enabled")
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${OVL_FFMPEG_INCLUDE})
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${OVL_AVFORMAT} ${OVL_AVCODEC} ${OVL_AVUTIL} ${OVL_SWSCALE})
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE OVL_HAVE_FFMPEG)
elseif(OVL_USE_FFMPEG)
  message(WARNING "obs-overlay: FFmpeg not found, video thumbnails fall back to the OS (Windows only)")
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/core/video_thumb.cpp
    src/core/video_thumb.hpp
    src/app.cpp
    src/app.hpp
    src/core/settings.cpp
    src/core/settings.hpp
    src/core/service.cpp
    src/core/service.hpp
    src/core/live_backend.cpp
    src/core/backend.hpp
    src/core/sysmetrics.cpp
    src/core/hotkeys.cpp
    src/core/hotkeys.hpp
    src/core/gallery.cpp
    src/core/gallery.hpp
    src/ui/style.cpp
    src/ui/common.cpp
    src/ui/common.hpp
    src/ui/overlay.cpp
    src/ui/overlay.hpp
    src/ui/gallery_view.cpp
    src/ui/gallery_view.hpp
    src/ui/settings_page.cpp
    src/ui/settings_page.hpp
    src/ui/edit_overlays.cpp
    src/ui/edit_overlays.hpp
    src/ui/widgets_dock.cpp
    src/ui/widgets_dock.hpp
    src/ui/layout_presets.cpp
    src/ui/layout_presets.hpp
    src/ui/layout_edit.cpp
    src/ui/layout_edit.hpp
    src/ui/widgets_page.cpp
    src/ui/widgets_page.hpp
)

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ole32 shell32 gdi32 uuid)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(BUILD_OVL_TESTS)
  enable_testing()
  add_executable(ovl_core_tests tests/test_core.cpp)
  target_include_directories(ovl_core_tests PRIVATE src/core)
  target_compile_features(ovl_core_tests PRIVATE cxx_std_17)
  add_test(NAME ovl_core COMMAND ovl_core_tests)
endif()
