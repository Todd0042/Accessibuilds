cmake_minimum_required(VERSION 3.20)
project(VipWvWBuildCoach CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies (local — no network required) ────────────────────────────────

set(IMGUI_DIR     ${CMAKE_SOURCE_DIR}/external/imgui)
set(NLOHMANN_DIR  ${CMAKE_SOURCE_DIR}/external/nlohmann)

# ── Sources ───────────────────────────────────────────────────────────────────
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
)

set(ADDON_SOURCES
    src/addon.cpp

    src/build/types.cpp

    src/api/http_client.cpp
    src/api/gw2api.cpp
    src/api/gw2names.cpp
    src/api/gw2skills.cpp
    src/api/item_lookup.cpp
    src/api/wingman.cpp

    src/build/comparator.cpp
    src/build/cache.cpp

    src/arcdps/arcdps.cpp

    src/ui/main_window.cpp
    src/ui/build_panel.cpp
    src/ui/gear_panel.cpp
    src/ui/dps_panel.cpp
    src/ui/coaching_panel.cpp
    src/ui/build_editor.cpp
    src/ui/coach_window.cpp
    src/ui/debug_window.cpp
    src/ui/icon_cache.cpp
)

# ── Embed version ─────────────────────────────────────────────────────────────
set(VERSION_FILE   ${CMAKE_SOURCE_DIR}/version.txt)
set(VERSION_HEADER ${CMAKE_BINARY_DIR}/version_data.h)

add_custom_command(
    OUTPUT  ${VERSION_HEADER}
    COMMAND ${CMAKE_COMMAND}
            -D VERSION_FILE=${VERSION_FILE}
            -D OUTPUT=${VERSION_HEADER}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_version.cmake
    DEPENDS ${VERSION_FILE}
            ${CMAKE_SOURCE_DIR}/cmake/embed_version.cmake
    COMMENT "Embedding version"
)

# ── Embed default builds JSON ─────────────────────────────────────────────────
set(DEFAULT_BUILDS_JSON   ${CMAKE_SOURCE_DIR}/src/build/default_builds.json)
set(DEFAULT_BUILDS_HEADER ${CMAKE_BINARY_DIR}/default_builds_data.h)

add_custom_command(
    OUTPUT  ${DEFAULT_BUILDS_HEADER}
    COMMAND ${CMAKE_COMMAND}
            -D INPUT=${DEFAULT_BUILDS_JSON}
            -D OUTPUT=${DEFAULT_BUILDS_HEADER}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_json.cmake
    DEPENDS ${DEFAULT_BUILDS_JSON}
    COMMENT "Embedding default_builds.json"
)

# ── Embed icons ───────────────────────────────────────────────────────────────
file(GLOB ICON_PNG_FILES "${CMAKE_SOURCE_DIR}/icons/*.png")
set(ICON_BUNDLE_H   ${CMAKE_BINARY_DIR}/icon_bundle.h)
set(ICON_BUNDLE_CPP ${CMAKE_BINARY_DIR}/icon_bundle.cpp)

add_custom_command(
    OUTPUT  ${ICON_BUNDLE_H} ${ICON_BUNDLE_CPP}
    COMMAND ${CMAKE_COMMAND}
            -D ICONS_DIR=${CMAKE_SOURCE_DIR}/icons
            -D OUTPUT_H=${ICON_BUNDLE_H}
            -D OUTPUT_CPP=${ICON_BUNDLE_CPP}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_icons.cmake
    DEPENDS ${ICON_PNG_FILES}
            ${CMAKE_SOURCE_DIR}/cmake/embed_icons.cmake
    COMMENT "Embedding placeholder icons"
)
set_source_files_properties(${ICON_BUNDLE_CPP} PROPERTIES GENERATED TRUE)
set_source_files_properties(${ICON_BUNDLE_H}   PROPERTIES GENERATED TRUE)

# ── Target ────────────────────────────────────────────────────────────────────
add_library(VipWvWBuildCoach SHARED
    ${ADDON_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(VipWvWBuildCoach PRIVATE
    ${CMAKE_SOURCE_DIR}/external/nexus
    ${IMGUI_DIR}
    ${CMAKE_SOURCE_DIR}/external   # resolves <nlohmann/json.hpp>
    ${CMAKE_BINARY_DIR}            # resolves <default_builds_data.h>
)

target_sources(VipWvWBuildCoach PRIVATE ${VERSION_HEADER})
target_sources(VipWvWBuildCoach PRIVATE ${DEFAULT_BUILDS_HEADER})
target_sources(VipWvWBuildCoach PRIVATE ${ICON_BUNDLE_CPP} ${ICON_BUNDLE_H})

# ── Compile options ───────────────────────────────────────────────────────────
target_compile_options(VipWvWBuildCoach PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-stringop-truncation
    -Wno-maybe-uninitialized
    -O2
    -Wno-unknown-pragmas
)

# ── Link ──────────────────────────────────────────────────────────────────────
target_link_libraries(VipWvWBuildCoach PRIVATE
    winhttp
    shell32
)

target_link_options(VipWvWBuildCoach PRIVATE
    -static-libgcc
    -static-libstdc++
    -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
)

# ── Output name ───────────────────────────────────────────────────────────────
set_target_properties(VipWvWBuildCoach PROPERTIES
    OUTPUT_NAME "ViP-WvW-Build-Coach"
    PREFIX      ""
    SUFFIX      ".dll"
)

# ── Install helper ────────────────────────────────────────────────────────────
if(DEFINED ENV{GW2_ADDONS_DIR})
    install(TARGETS VipWvWBuildCoach
            RUNTIME DESTINATION "$ENV{GW2_ADDONS_DIR}/ViP-WvW-Build-Coach")
endif()
