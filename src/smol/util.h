#pragma once

#include "smol/defines.h"

#include <string>
#include <vector>

namespace smol::util
{
    std::string read_file(const std::string& path);
    std::vector<i8> read_file_raw(const std::string& path);
} // namespace smol::util