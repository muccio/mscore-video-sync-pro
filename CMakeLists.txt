cmake_minimum_required(VERSION 3.22)

project(MScoreVideoSyncPro
    VERSION 1.0.0
    DESCRIPTION "Vienna Ensemble Pro 8 Architecture Video Score Player for MuseScore 4"
    LANGUAGES C CXX OBJC OBJCXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# macOS Deployment & Architecture Configuration
if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target" FORCE)
    endif()

    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures" FORCE)
    endif()
endif()

# ==============================================================================
# JUCE Framework Integration (FetchContent / System / Submodule)
# ==============================================================================
include(FetchContent)

find_package(JUCE CONFIG QUIET)

if(NOT TARGET juce::juce_audio_processors)
    message(STATUS "JUCE not found locally. Fetching JUCE 8.0.6 via FetchContent...")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ==============================================================================
# FFmpeg & macOS Hardware Acceleration Dependencies
# ==============================================================================
find_package(PkgConfig QUIET)

set(FFMPEG_FOUND FALSE)

if(PKG_CONFIG_FOUND)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
    set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    
    pkg_check_modules(FFMPEG IMPORTED_TARGET libavcodec libavformat libswscale libavutil libswresample)
    if(FFMPEG_FOUND)
        message(STATUS "Found FFmpeg via pkg-config: ${FFMPEG_LIBRARIES}")
    endif()
endif()

# Fallback manual discovery if pkg-config is not present or failed
if(NOT FFMPEG_FOUND)
    message(STATUS "Searching for FFmpeg manually in Homebrew/System paths...")
    find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h
        PATHS /opt/homebrew/include /usr/local/include /opt/homebrew/Cellar/ffmpeg/*/include
    )
    find_library(AVCODEC_LIB avcodec PATHS /opt/homebrew/lib /usr/local/lib /opt/homebrew/Cellar/ffmpeg/*/lib)
    find_library(AVFORMAT_LIB avformat PATHS /opt/homebrew/lib /usr/local/lib /opt/homebrew/Cellar/ffmpeg/*/lib)
    find_library(SWSCALE_LIB swscale PATHS /opt/homebrew/lib /usr/local/lib /opt/homebrew/Cellar/ffmpeg/*/lib)
    find_library(AVUTIL_LIB avutil PATHS /opt/homebrew/lib /usr/local/lib /opt/homebrew/Cellar/ffmpeg/*/lib)
    find_library(SWRESAMPLE_LIB swresample PATHS /opt/homebrew/lib /usr/local/lib /opt/homebrew/Cellar/ffmpeg/*/lib)

    if(AVCODEC_INCLUDE_DIR AND AVCODEC_LIB AND AVFORMAT_LIB AND SWSCALE_LIB AND AVUTIL_LIB AND SWRESAMPLE_LIB)
        set(FFMPEG_FOUND TRUE)
        add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
        target_include_directories(FFmpeg::FFmpeg INTERFACE ${AVCODEC_INCLUDE_DIR})
        target_link_libraries(FFmpeg::FFmpeg INTERFACE
            ${AVCODEC_LIB}
            ${AVFORMAT_LIB}
            ${SWSCALE_LIB}
            ${AVUTIL_LIB}
            ${SWRESAMPLE_LIB}
        )
        message(STATUS "Found FFmpeg manually: ${AVCODEC_LIB}")
    else()
        message(FATAL_ERROR "FFmpeg development libraries not found! Please run: brew install ffmpeg pkg-config")
    endif()
endif()

# macOS Native Frameworks
if(APPLE)
    find_library(VIDEOTOOLBOX_FW VideoToolbox REQUIRED)
    find_library(COREMEDIA_FW CoreMedia REQUIRED)
    find_library(COREVIDEO_FW CoreVideo REQUIRED)
    find_library(AUDIOTOOLBOX_FW AudioToolbox REQUIRED)
    find_library(OPENGL_FW OpenGL REQUIRED)
    find_library(COCOA_FW Cocoa REQUIRED)
    find_library(CARBON_FW Carbon REQUIRED)
endif()

# ==============================================================================
# Plugin & Standalone Targets Definition
# ==============================================================================
juce_add_plugin(MScoreVideoSyncPro
    COMPANY_NAME "MScoreTools"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    PLUGIN_NAME "MScore Video Sync Pro"
    PLUGIN_DESCRIPTION "Vienna Ensemble Pro 8 Architecture Video Score Player for MuseScore 4"
    PLUGIN_MANUFACTURER_CODE MscT
    PLUGIN_CODE MsVs
    FORMATS VST3 AU Standalone VST3_MANIFEST_METADATA
    PRODUCT_NAME "MScore Video Sync Pro"
    VST3_CATEGORIES "Tools" "Fx"
    BUNDLE_ID "com.mscoretools.videosyncpro"
)

juce_generate_juce_header(MScoreVideoSyncPro)

# Source files
target_sources(MScoreVideoSyncPro PRIVATE
    Source/Core/CommonTypes.h
    Source/Core/ProjectState.h
    Source/Core/ProjectState.cpp
    Source/Core/PluginProcessor.h
    Source/Core/PluginProcessor.cpp

    Source/Sync/TransportSyncEngine.h
    Source/Sync/TransportSyncEngine.cpp
    Source/Sync/SharedMemoryIPC.h
    Source/Sync/SharedMemoryIPC.cpp
    Source/Sync/NetworkBridge.h
    Source/Sync/NetworkBridge.cpp

    Source/Video/LockFreeFrameQueue.h
    Source/Video/VideoDecodeEngine.h
    Source/Video/VideoDecodeEngine.cpp
    Source/Video/FilmstripGenerator.h
    Source/Video/FilmstripGenerator.cpp
    Source/Video/WaveformExtractor.h
    Source/Video/WaveformExtractor.cpp

    Source/Audio/EmbeddedAudioPlayer.h
    Source/Audio/EmbeddedAudioPlayer.cpp
    Source/Audio/TwoPopGenerator.h
    Source/Audio/TwoPopGenerator.cpp

    Source/Cues/CueMarkerModel.h
    Source/Cues/CueMarkerModel.cpp
    Source/Cues/MidiMarkerImporter.h
    Source/Cues/MidiMarkerImporter.cpp

    Source/UI/Theme/Vep8Theme.h
    Source/UI/Viewport/OverlayRenderer.h
    Source/UI/Viewport/OverlayRenderer.cpp
    Source/UI/Viewport/VideoViewport.h
    Source/UI/Viewport/VideoViewport.cpp
    Source/UI/Window/FloatingVideoWindow.h
    Source/UI/Window/FloatingVideoWindow.cpp
    Source/UI/Timeline/TimelineComponent.h
    Source/UI/Timeline/TimelineComponent.cpp
    Source/UI/Inspector/MarkerInspector.h
    Source/UI/Inspector/MarkerInspector.cpp
    Source/UI/PluginEditor.h
    Source/UI/PluginEditor.cpp
)

target_include_directories(MScoreVideoSyncPro PRIVATE
    Source
    Source/Core
    Source/Sync
    Source/Video
    Source/Audio
    Source/Cues
    Source/UI
    Source/UI/Theme
    Source/UI/Viewport
    Source/UI/Window
    Source/UI/Timeline
    Source/UI/Inspector
)

target_compile_definitions(MScoreVideoSyncPro PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    __STDC_CONSTANT_MACROS
)

target_link_libraries(MScoreVideoSyncPro PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_osc
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(TARGET PkgConfig::FFMPEG)
    target_link_libraries(MScoreVideoSyncPro PRIVATE PkgConfig::FFMPEG)
elseif(TARGET FFmpeg::FFmpeg)
    target_link_libraries(MScoreVideoSyncPro PRIVATE FFmpeg::FFmpeg)
endif()

if(APPLE)
    target_link_libraries(MScoreVideoSyncPro PRIVATE
        ${VIDEOTOOLBOX_FW}
        ${COREMEDIA_FW}
        ${COREVIDEO_FW}
        ${AUDIOTOOLBOX_FW}
        ${OPENGL_FW}
        ${COCOA_FW}
        ${CARBON_FW}
    )
endif()

# ==============================================================================
# Post-Build Deployment to macOS User VST3 Directory
# ==============================================================================
if(APPLE)
    set(USER_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
    add_custom_command(TARGET MScoreVideoSyncPro_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${USER_VST3_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:MScoreVideoSyncPro_VST3>"
            "${USER_VST3_DIR}/$<TARGET_BUNDLE_DIR_NAME:MScoreVideoSyncPro_VST3>"
        COMMENT "Deploying VST3 bundle to ${USER_VST3_DIR}..."
    )
endif()
