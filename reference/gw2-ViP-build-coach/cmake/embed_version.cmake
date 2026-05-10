# Reads VERSION_FILE (x.y.z) and writes version constants to OUTPUT.
# Called by CMakeLists via add_custom_command.
file(READ "${VERSION_FILE}" version_str)
string(STRIP "${version_str}" version_str)
string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${version_str}")
set(VER_MAJOR "${CMAKE_MATCH_1}")
set(VER_MINOR "${CMAKE_MATCH_2}")
set(VER_PATCH "${CMAKE_MATCH_3}")
file(WRITE "${OUTPUT}"
"/* Auto-generated from version.txt — do not edit. */
#pragma once
#define ADDON_VER_MAJOR ${VER_MAJOR}
#define ADDON_VER_MINOR ${VER_MINOR}
#define ADDON_VER_PATCH ${VER_PATCH}
")
