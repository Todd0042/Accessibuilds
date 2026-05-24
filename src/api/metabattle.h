#pragma once
#include "../build/types.h"
#include "snowcrows.h"
#include <string>

namespace MetaBattle {

bool ParseBuildPage(const std::string& url, GW2::SCBuild& out);
bool FetchRotationPage(const std::string& url, SnowCrows::ParsedRotation& out);

} /* namespace MetaBattle */
