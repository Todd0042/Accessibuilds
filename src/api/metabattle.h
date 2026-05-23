#pragma once
#include "../build/types.h"
#include <string>

namespace MetaBattle {

bool ParseBuildPage(const std::string& url, GW2::SCBuild& out);

} /* namespace MetaBattle */
