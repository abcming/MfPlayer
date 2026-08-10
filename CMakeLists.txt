cmake_minimum_required(VERSION 3.22)

# vcpkg 支持：通过 VCPKG_ROOT 环境变量自动检测
if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    if(EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain" FORCE)
        message(STATUS "Using vcpkg toolchain from $ENV{VCPKG_ROOT}")
    endif()
endif()

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION.txt" MFPLAYER_VERSION LIMIT_COUNT 1)
string(STRIP "${MFPLAYER_VERSION}" MFPLAYER_VERSION)
project(MfPlayer VERSION ${MFPLAYER_VERSION} LANGUAGES CXX)

configure_file(
    "${CMAKE_SOURCE_DIR}/common/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/common/version.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

# Default to Qt 6.11 from aqtinstall if available and not overridden
if(NOT CMAKE_PREFIX_PATH AND EXISTS "/root/qt/6.11.0/gcc_64")
    set(CMAKE_PREFIX_PATH "/root/qt/6.11.0/gcc_64")
    message(STATUS "Auto-detected Qt at /root/qt/6.11.0/gcc_64 (override with -DCMAKE_PREFIX_PATH=...)")
endif()
set(QT_FIND_PRIVATE_MODULES ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Qml QuickControls2 Network OpenGL Sql ShaderTools Gui)
qt_standard_project_setup()
qt_policy(SET QTP0004 NEW)
qt_policy(SET QTP0001 NEW)

if(WIN32)
    set(MPV_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/mpv-msvc/include")
    set(MPV_LIBRARY "${CMAKE_SOURCE_DIR}/third_party/mpv-msvc/lib/mpv.lib")
    add_library(mpv SHARED IMPORTED)
    set_target_properties(mpv PROPERTIES
        IMPORTED_IMPLIB "${MPV_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}"
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/build_mpv_msvc.ps1")
        add_custom_target(build_mpv
            COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tools/build_mpv_msvc.ps1"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Building libmpv with MSVC + vcpkg"
            USES_TERMINAL
        )
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
endif()

# CURL：vcpkg 自动处理，Windows/Linux 统一用 find_package
find_package(CURL REQUIRED)

qt_add_executable(mfplayer_app
    app/main.cpp
    core/settings/settingsstore.cpp
    platform/rendering/mpv/mpvcontroller.cpp
    platform/rendering/mpv/mpvrenderitem.cpp
    platform/rendering/vulkandevice.cpp
    core/providers/emby/embyclient.cpp
    core/media/models/mediamodel.cpp
    core/playback/playbackcontroller.cpp
    core/library/librarybrowser.cpp
    core/detail/detailmanager.cpp
    core/server/servermanager.cpp
    core/server/credentialstore.cpp
    core/cache/cachestore.cpp
    core/cache/dbworker.cpp
    core/cache/imagecacheprovider.cpp
    core/network/curlengine.cpp
)

if(WIN32)
    target_sources(mfplayer_app PRIVATE resources/appicon.rc)
    # Make MSVC recompile .rc when .ico changes (rc compiler only tracks .rc mtime)
    set_source_files_properties(resources/appicon.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/appicon.ico"
    )
    # Replace clang-cl's broken manifest post-link
    add_custom_command(TARGET mfplayer_app POST_BUILD
        COMMAND mt -manifest "${CMAKE_SOURCE_DIR}/resources/app.manifest"
                -outputresource:"$<TARGET_FILE:mfplayer_app>"
        COMMENT "Embedding correct manifest into MfPlayer.exe"
    )
endif()

set_source_files_properties(ui/qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(ui/qml/theme/Str.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(ui/qml/theme/Nav.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(ui/qml/components/ImageLoadQueue.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# ── SVG → QML Shape conversion (GPU CurveRenderer) ──
set(ICON_NAMES arrow_back check chevron_right close fullscreen fullscreen_exit
    home movie pause play_arrow playlist_play replay search skip_next
    skip_previous star subtitles toc volume_up volume_off heart heart_filled sort funnel music_note)

set(GENERATED_ICON_QMLS "")
set(SVGTOQML_SCRIPT "${CMAKE_SOURCE_DIR}/tools/svg_to_icon_qml.sh")
set(QT6_BIN_DIR "${Qt6_DIR}/../../../bin")
if(WIN32)
    set(SVGTOQML_BASH "$ENV{ProgramFiles}/Git/bin/bash.exe")
    if(NOT EXISTS "${SVGTOQML_BASH}")
        message(FATAL_ERROR "Git Bash not found at ${SVGTOQML_BASH}")
    endif()
    set(SVGTOQML_PATH "${QT6_BIN_DIR};$ENV{PATH}")
else()
    find_program(SVGTOQML_BASH bash REQUIRED)
    set(SVGTOQML_PATH "${QT6_BIN_DIR}:$ENV{PATH}")
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated_icons")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/mfplayer")

foreach(name ${ICON_NAMES})
    set(qml_out "${CMAKE_BINARY_DIR}/generated_icons/MFIcon_${name}.qml")
    # Qt inspects QML module files while CMake is still configuring. Generate
    # missing icons immediately for a clean build tree; the custom command
    # below keeps them incremental after that.
    if(NOT EXISTS "${qml_out}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env "PATH=${SVGTOQML_PATH}"
                    "${SVGTOQML_BASH}" "${SVGTOQML_SCRIPT}"
                    "${CMAKE_SOURCE_DIR}/resources/icons/${name}.svg"
                    "${qml_out}"
            RESULT_VARIABLE svgtoqml_result
        )
        if(NOT svgtoqml_result EQUAL 0)
            message(FATAL_ERROR "svgtoqml failed for ${name}.svg")
        endif()
    endif()
    file(COPY_FILE "${qml_out}"
        "${CMAKE_BINARY_DIR}/mfplayer/MFIcon_${name}.qml"
        ONLY_IF_DIFFERENT)
    add_custom_command(
        OUTPUT "${qml_out}"
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${SVGTOQML_PATH}"
                "${SVGTOQML_BASH}" "${SVGTOQML_SCRIPT}"
                "${CMAKE_SOURCE_DIR}/resources/icons/${name}.svg"
                "${qml_out}"
        DEPENDS "${CMAKE_SOURCE_DIR}/resources/icons/${name}.svg"
                "${SVGTOQML_SCRIPT}"
        COMMENT "svgtoqml: ${name}.svg → MFIcon_${name}.qml"
    )
    set_source_files_properties("${qml_out}" PROPERTIES
        QT_RESOURCE_ALIAS "MFIcon_${name}.qml")
    list(APPEND GENERATED_ICON_QMLS "${qml_out}")
endforeach()

qt_add_qml_module(mfplayer_app
    URI "mfplayer"
    VERSION 1.0
    QML_FILES
        ui/qml/theme/Str.qml
        ui/qml/theme/Theme.qml
        ui/qml/theme/Nav.qml
        ui/qml/pages/Main.qml
        ui/qml/pages/BrowsePage.qml
        ui/qml/dialogs/ServerPanel.qml
        ui/qml/dialogs/SettingsDialog.qml
        ui/qml/views/HomeView.qml
        ui/qml/views/SuggestionsView.qml
        ui/qml/views/LibraryGridView.qml
        ui/qml/pages/DetailPage.qml
        ui/qml/pages/PlayerPage.qml
        ui/qml/views/PlayerControls.qml
        ui/qml/components/CachedImage.qml
        ui/qml/components/ImageLoadQueue.qml
        ui/qml/components/RoundedImage.qml
        ui/qml/components/TrackSelector.qml
        ui/qml/components/HorizontalMediaRow.qml
        ui/qml/components/Icon.qml
        ui/qml/components/StyledPopup.qml
        ui/qml/components/StudiosSection.qml
        ui/qml/components/PersonBioSection.qml
        ui/qml/components/SimilarItemsSection.qml
        ui/qml/components/CardPlayButton.qml
        ui/qml/components/CardActionButtons.qml
        ui/qml/components/CastAndCrewRow.qml
        ui/qml/components/MediaInfoSection.qml
        ui/qml/components/SeriesSection.qml
        ui/qml/components/HdrPqOverlay.qml
        ui/qml/components/PlayControlsRow.qml
        ${GENERATED_ICON_QMLS}
    RESOURCES
        resources/appicon.ico
)

qt6_add_shaders(mfplayer_app "mfplayer-shaders"
    PREFIX "/qt/qml/mfplayer"
    BASE "ui/qml/shaders"
    FILES
        ui/qml/shaders/roundedmask.frag
        ui/qml/shaders/hdr_pq.vert
        ui/qml/shaders/hdr_pq.frag
)

target_include_directories(mfplayer_app PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
    ${CMAKE_SOURCE_DIR}
)

if(NOT MSVC)
    target_compile_options(mfplayer_app PRIVATE -Wall -Wextra)
endif()

if(NOT WIN32)
    find_program(LLD_LINKER ld.lld)
    if(LLD_LINKER)
        target_link_options(mfplayer_app PRIVATE -fuse-ld=lld)
    endif()
endif()

if(WIN32)
    target_include_directories(mfplayer_app PRIVATE ${MPV_INCLUDE_DIR})
    target_link_libraries(mfplayer_app PRIVATE
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        Qt6::Network
        Qt6::OpenGL
        Qt6::Sql
        Qt6::ShaderTools
        Qt6::Gui
        Qt6::GuiPrivate
        mpv
        dwmapi
        d3d11
        CURL::libcurl
    )
else()
    target_link_libraries(mfplayer_app PRIVATE
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        Qt6::Network
        Qt6::OpenGL
        Qt6::Sql
        Qt6::ShaderTools
        Qt6::GuiPrivate
        ${MPV_LIBRARIES}
        CURL::libcurl
    )
endif()

set_target_properties(mfplayer_app PROPERTIES
    OUTPUT_NAME "MfPlayer"
    WIN32_EXECUTABLE TRUE
)

file(GLOB FONT_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/fonts/*.ttc"
    "${CMAKE_SOURCE_DIR}/resources/fonts/*.ttf"
    "${CMAKE_SOURCE_DIR}/resources/fonts/*.otf"
)
add_custom_command(TARGET mfplayer_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:mfplayer_app>/fonts"
)
foreach(font ${FONT_FILES})
    add_custom_command(TARGET mfplayer_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${font}"
        "$<TARGET_FILE_DIR:mfplayer_app>/fonts"
    )
endforeach()

# Copy mpv + vcpkg runtime DLLs to build output (runs at build time, not configure time)
if(WIN32)
    file(GLOB MPV_DEP_DLLS CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/third_party/mpv-msvc/lib/deps/*.dll"
    )
    add_custom_command(TARGET mfplayer_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/mpv-msvc/bin/mpv-2.dll"
        "$<TARGET_FILE_DIR:mfplayer_app>/mpv-2.dll"
        COMMENT "Deploying mpv + dependency DLLs"
    )
    foreach(mpv_dep ${MPV_DEP_DLLS})
        add_custom_command(TARGET mfplayer_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${mpv_dep}"
            "$<TARGET_FILE_DIR:mfplayer_app>"
        )
    endforeach()
endif()
