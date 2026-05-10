# Reads INPUT and writes it into OUTPUT as a C++ raw string literal.
# Called by CMakeLists via add_custom_command.
file(READ "${INPUT}" content)
file(WRITE "${OUTPUT}"
"/* Auto-generated — edit src/build/default_builds.json, not this file. */
#pragma once
namespace BuildCache {
static const char* const DEFAULT_BUILDS_JSON = R\"BUILDS(
${content}
)BUILDS\";
} /* namespace BuildCache */
")
