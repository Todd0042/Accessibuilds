#pragma once
#include "../build/types.h"
#include <string>

namespace ShareCode {

/* Encode a build to an "AB:"-prefixed share code string.
 * Returns empty string on failure (e.g. spec data not loaded). */
std::string Encode(const GW2::SCBuild& build);

/* Decode an "AB:"-prefixed share code string into an SCBuild.
 * Returns true on success. On failure, out is unchanged and LastError() is set. */
bool Decode(const std::string& code, GW2::SCBuild& out);

/* Last error message from a failed Decode() call. */
const std::string& LastError();

} /* namespace ShareCode */
