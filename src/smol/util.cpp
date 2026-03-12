#include "util.h"

#include "log.h"

#include <cstddef>
#include <fstream>
#include <vector>

namespace smol::util
{
    std::string read_file(const std::string& path)
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
        return "";
    }

    std::vector<i8> read_file_raw(const std::string& path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            SMOL_LOG_ERROR("UTIL", "Could not read file: {}", path);
            return {};
        }
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<i8> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), size);
        file.close();
        return buffer;
    }
} // namespace smol::util