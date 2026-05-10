# Reads every PNG in ICONS_DIR and writes two files:
#   OUTPUT_H   — struct declaration (icon_bundle.h)
#   OUTPUT_CPP — byte arrays + table (icon_bundle.cpp)
# Called by CMakeLists via add_custom_command.

file(GLOB icon_files "${ICONS_DIR}/*.png")
list(SORT icon_files)

set(hdr
"/* Auto-generated — do not edit. */
#pragma once
#include <cstddef>
struct IconEntry {
    const char*          name;
    const unsigned char* data;
    size_t               size;
};
extern const IconEntry BUNDLED_ICONS[];
extern const size_t    BUNDLED_ICON_COUNT;
")

set(src "/* Auto-generated — do not edit. */\n#include \"icon_bundle.h\"\n\n")
set(table "")

foreach(f IN LISTS icon_files)
    get_filename_component(stem "${f}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${stem}" ident)
    file(READ "${f}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    string(LENGTH "${hex}" hexlen)
    math(EXPR trim "${hexlen} - 1")
    string(SUBSTRING "${hex}" 0 ${trim} hex)
    string(APPEND src "static const unsigned char s_${ident}[] = {${hex}};\n")
    string(APPEND table "    {\"${stem}\", s_${ident}, sizeof(s_${ident})},\n")
endforeach()

string(APPEND src
"\nconst IconEntry BUNDLED_ICONS[] = {\n${table}};\n"
"const size_t BUNDLED_ICON_COUNT = sizeof(BUNDLED_ICONS) / sizeof(BUNDLED_ICONS[0]);\n")

file(WRITE "${OUTPUT_H}"   "${hdr}")
file(WRITE "${OUTPUT_CPP}" "${src}")
