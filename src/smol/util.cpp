#include "util.h"

#include "log.h"

#include <fstream>

namespace smol::util
{
    std::string get_file_content(const std::string& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (in)
        {
            std::string content;
            in.seekg(0, std::ios::end);
            content.resize(in.tellg());
            in.seekg(0, std::ios::beg);
            in.read(&content[0], content.size());
            in.close();
            return content;
        }

        SMOL_LOG_ERROR("UTIL", "Could not load file with path: {}", path);
        return nullptr;
    }
} // namespace smol::util