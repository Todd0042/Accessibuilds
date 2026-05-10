#pragma once
#include "../build/types.h"
#include <string>

/*
 * gw2skills.net URL decoder.
 *
 * gw2skills.net builds are encoded in the URL query parameter.
 * Example: https://en.gw2skills.net/editor/?PegAUVlNweYOMPWJe2S3tUA-DWJYjRVfhkUikZFUdY471hQEg9zbp9NLA-w
 *
 * The code uses URL-safe base64 (A-Z a-z 0-9 - _).
 * Internal structure (established by community reverse-engineering):
 *   Section 1 (before first -): profession + 3 spec lines + skills
 *   Section 2 (between - separators): gear (stat IDs + upgrade IDs per slot)
 *   Suffix (after last -): format version letter
 *
 * Note: the binary format may need tuning as it is reverse-engineered.
 * Run a known build through ParseURL() and verify the output to confirm.
 */
namespace GW2Skills {

struct ParseResult {
    bool        ok            = false;
    std::string error;
    GW2::SCBuild build;
};

/* Extract the code from a full URL or return the raw code if no ?  */
std::string ExtractCode(const std::string& url_or_code);

/* Decode a gw2skills code (the part after ?) into a build.
 * Populates profession, elite_spec, traits, skills, and gear where possible. */
ParseResult ParseCode(const std::string& code);

/* Convenience: parse directly from a full URL */
ParseResult ParseURL(const std::string& url);

} /* namespace GW2Skills */
