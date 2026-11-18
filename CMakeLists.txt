cmake_minimum_required(VERSION 3.18)
project(SimpleMediaPlayer LANGUAGES CXX)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build directory")
endif()
if(NOT WIN32)
    message(FATAL_ERROR "Simple Media Player requires Windows")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8 /EHsc /W4")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
endif()

set(CMAKE_SHARED_MODULE_PREFIX "")
set(CMAKE_EXECUTABLE_SUFFIX ".exe")

set(THREADS_PREFER_PTHREAD_FLAG OFF)
find_package(Threads REQUIRED)

# Source files
set(SOURCES
    src/entrypoint.cpp
    src/core/Application.cpp
    src/core/Application.h
    src/core/AppContext.h
    src/core/Types.h
    src/config/Config.cpp
    src/config/Config.h
    src/media/MediaSession.cpp
    src/media/MediaSession.h
    src/media/VolumeController.cpp
    src/media/VolumeController.h
    src/audio/LyricsEngine.cpp
    src/audio/LyricsEngine.h
    src/audio/AsrEngine.h
    src/audio/AsrEngine.cpp
    src/audio/StreamingWhisper.cpp
    src/ui/PopupWindow.cpp
    src/ui/PopupWindow.h
    src/ui/Layout.cpp
    src/ui/Layout.h
    src/ui/Appearance.cpp
    src/ui/Appearance.h
    src/ui/PlayerControls.cpp
    src/ui/PlayerControls.h
    src/ui/SettingsUI.h
    src/ui/SettingsUI.cpp
    src/utils/Logger.cpp
    src/utils/Logger.h
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    src
    src/core
    src/config
    src/media
    src/audio
    src/ui
    src/utils
)

# Windows libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    user32.lib
    gdi32.lib
    comdlg32.lib
    ole32.lib
    uuid.lib
    winhttp.lib
    windowsapp.lib
    shell32.lib
    Threads::Threads
)

# Compiler definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
)

# WinRT support
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /Zc:__cplusplus)
endif()

# Set Windows subsystem
set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:WINDOWS /DEBUG:NONE /PDBALTPATH:%_PDB%"
    WIN32_EXECUTABLE TRUE
)
if(MSVC)
    # Do not embed this machine's source path in the public executable.
    target_compile_options(${PROJECT_NAME} PRIVATE "/d1trimfile:${CMAKE_SOURCE_DIR}\\")
endif()

# Resource file (if exists)
if (EXISTS "${CMAKE_SOURCE_DIR}/resources/app.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/resources/app.rc")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ConfigPersistenceTests tests/ConfigPersistenceTests.cpp src/config/Config.cpp)
    target_compile_definitions(ConfigPersistenceTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(ConfigPersistenceTests PRIVATE shell32 ole32)
    add_test(NAME ConfigPersistence COMMAND ConfigPersistenceTests)
    add_executable(SessionSelectionTests tests/SessionSelectionTests.cpp)
    add_test(NAME SessionSelection COMMAND SessionSelectionTests)
    add_executable(MediaSessionProbe tests/MediaSessionProbe.cpp src/media/MediaSession.cpp)
    target_compile_definitions(MediaSessionProbe PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(MediaSessionProbe PRIVATE windowsapp)
    add_executable(AudioAsrTests tests/AudioAsrTests.cpp src/audio/AsrEngine.cpp src/audio/StreamingWhisper.cpp)
    add_executable(UILayoutTests tests/UILayoutTests.cpp src/ui/Layout.cpp)
    target_link_libraries(UILayoutTests PRIVATE user32 windowsapp)
    add_executable(SettingsUITests tests/SettingsUITests.cpp src/ui/SettingsUI.cpp)
    target_include_directories(SettingsUITests PRIVATE src src/ui src/core src/config)
    target_compile_definitions(SettingsUITests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(SettingsUITests PRIVATE user32 gdi32 comdlg32 ole32 shell32 windowsapp gdiplus)
    target_link_libraries(AudioAsrTests PRIVATE ole32 uuid windowsapp)
    add_executable(AudioLyricsTests tests/AudioLyricsTests.cpp src/audio/LyricsEngine.cpp)
    target_link_libraries(AudioLyricsTests PRIVATE winhttp windowsapp)
    # Shared sources for rendering tests (compiled as object library to avoid batch compilation issues)
add_library(RenderingTestSources OBJECT
    src/ui/Appearance.cpp
    src/ui/PlayerControls.cpp
    src/ui/SettingsUI.cpp
    src/ui/Layout.cpp
    src/config/Config.cpp
    src/core/Types.h)
target_include_directories(RenderingTestSources PRIVATE src src/ui src/core src/config)
target_compile_definitions(RenderingTestSources PRIVATE UNICODE _UNICODE NOMINMAX)

# Main test executable - RenderingTests.cpp compiled separately
add_executable(RenderingTests tests/RenderingTests.cpp)
target_include_directories(RenderingTests PRIVATE src src/ui src/core src/config)
target_compile_definitions(RenderingTests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(RenderingTests PRIVATE RenderingTestSources gdiplus comdlg32 user32 gdi32 windowsapp ole32 uuid shell32)
# Ensure Windows SDK include paths are propagated for shlobj.h etc.
if (CMAKE_SYSTEM_VERSION)
    target_compile_options(RenderingTests PRIVATE /D_WIN32_WINNT=0x0A00)
endif()
# Explicitly add Windows SDK include directories
if (CMAKE_SYSTEM_VERSION AND CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    find_path(WINSDK_INCLUDE_DIR "Windows.h" PATHS "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/um" "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/shared" "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/winrt" NO_DEFAULT_PATH)
    if (WINSDK_INCLUDE_DIR)
        target_include_directories(RenderingTests PRIVATE ${WINSDK_INCLUDE_DIR})
        target_include_directories(RenderingTests PRIVATE "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/shared")
        target_include_directories(RenderingTests PRIVATE "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/winrt")
        target_include_directories(RenderingTestSources PRIVATE ${WINSDK_INCLUDE_DIR})
        target_include_directories(RenderingTestSources PRIVATE "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/shared")
        target_include_directories(RenderingTestSources PRIVATE "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/winrt")
    endif()
endif()
add_test(NAME RenderingTests COMMAND RenderingTests)
    foreach(test AudioAsrTests AudioLyricsTests UILayoutTests SettingsUITests)
        target_compile_definitions(${test} PRIVATE UNICODE _UNICODE NOMINMAX)
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
    add_executable(RuntimeProbe tests/RuntimeProbe.cpp src/audio/AsrEngine.cpp src/audio/StreamingWhisper.cpp src/audio/LyricsEngine.cpp src/media/MediaSession.cpp src/media/VolumeController.cpp)
    target_compile_definitions(RuntimeProbe PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(RuntimeProbe PRIVATE ole32 uuid windowsapp winhttp)
endif()

option(SMP_DEPLOY_RUNTIME "Copy locally installed optional ASR runtime beside the application" ON)
if(SMP_DEPLOY_RUNTIME AND EXISTS "${CMAKE_SOURCE_DIR}/runtime/whisper" AND EXISTS "${CMAKE_SOURCE_DIR}/runtime/models")
    add_custom_command(TARGET SimpleMediaPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/runtime/whisper" "$<TARGET_FILE_DIR:SimpleMediaPlayer>/runtime/whisper"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/runtime/models" "$<TARGET_FILE_DIR:SimpleMediaPlayer>/runtime/models"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/runtime/licenses" "$<TARGET_FILE_DIR:SimpleMediaPlayer>/runtime/licenses")
endif()
