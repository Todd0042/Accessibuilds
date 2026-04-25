cmake_minimum_required(VERSION 3.20)
project(BuildCoach CXX)

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
    src/api/snowcrows.cpp
    src/api/wingman.cpp

    src/build/comparator.cpp
    src/build/cache.cpp

    src/arcdps/arcdps.cpp

    src/ui/main_window.cpp
    src/ui/build_panel.cpp
    src/ui/gear_panel.cpp
    src/ui/dps_panel.cpp
    src/ui/coaching_panel.cpp
    src/ui/icon_cache.cpp
)

# ── Target ────────────────────────────────────────────────────────────────────
add_library(BuildCoach SHARED
    ${ADDON_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(BuildCoach PRIVATE
    ${CMAKE_SOURCE_DIR}/external/nexus
    ${IMGUI_DIR}
    ${CMAKE_SOURCE_DIR}/external   # resolves <nlohmann/json.hpp>
)

# ── Compile options ───────────────────────────────────────────────────────────
target_compile_options(BuildCoach PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-stringop-truncation
    -O2
    -Wno-unknown-pragmas
)

# ── Link ──────────────────────────────────────────────────────────────────────
target_link_libraries(BuildCoach PRIVATE
    winhttp
    shell32
)

target_link_options(BuildCoach PRIVATE
    -static-libgcc
    -static-libstdc++
    -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
)

# ── Output name ───────────────────────────────────────────────────────────────
set_target_properties(BuildCoach PROPERTIES
    OUTPUT_NAME "BuildCoach"
    PREFIX      ""
    SUFFIX      ".dll"
)

# ── Install helper ────────────────────────────────────────────────────────────
if(DEFINED ENV{GW2_ADDONS_DIR})
    install(TARGETS BuildCoach
            RUNTIME DESTINATION "$ENV{GW2_ADDONS_DIR}/BuildCoach")
endif()
