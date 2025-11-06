#pragma once
#include "defines.h"

#include <fmt/format.h>
#include <stddef.h>
#include <string>
#include <string_view>

#ifdef ERROR
#    undef ERROR
#endif

namespace smol::log
{
    enum class level_e : u8_t
    {
        LOG_TRACE,
        LOG_DEBUG,
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR,
        LOG_FATAL
    };

    SMOL_API void set_level(level_e level);
    SMOL_API void set_max_file_size(size_t size);
    SMOL_API bool to_file(const std::string& path);
    SMOL_API void start();
    SMOL_API void shutdown();
    SMOL_API void write(level_e level, const char* category, std::string_view msg);
} // namespace smol::log

#ifndef SMOL_LOG_LEVEL
#    define SMOL_LOG_LEVEL smol::log::level_e::LOG_DEBUG
#endif

#define SMOL_LOG_TRACE(cat, fmtstr, ...)                           \
    if constexpr (smol::log::level_e::LOG_TRACE >= SMOL_LOG_LEVEL) \
    smol::log::write(smol::log::level_e::LOG_TRACE, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define SMOL_LOG_DEBUG(cat, fmtstr, ...)                           \
    if constexpr (smol::log::level_e::LOG_DEBUG >= SMOL_LOG_LEVEL) \
    smol::log::write(smol::log::level_e::LOG_DEBUG, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define SMOL_LOG_INFO(cat, fmtstr, ...)                           \
    if constexpr (smol::log::level_e::LOG_INFO >= SMOL_LOG_LEVEL) \
    smol::log::write(smol::log::level_e::LOG_INFO, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define SMOL_LOG_WARN(cat, fmtstr, ...)                           \
    if constexpr (smol::log::level_e::LOG_WARN >= SMOL_LOG_LEVEL) \
    smol::log::write(smol::log::level_e::LOG_WARN, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define SMOL_LOG_ERROR(cat, fmtstr, ...)                           \
    if constexpr (smol::log::level_e::LOG_ERROR >= SMOL_LOG_LEVEL) \
    smol::log::write(smol::log::level_e::LOG_ERROR, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define SMOL_LOG_FATAL(cat, fmtstr, ...)                           \
    if constexpr (smol::log::level_e::LOG_FATAL >= SMOL_LOG_LEVEL) \
    smol::log::write(smol::log::level_e::LOG_FATAL, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

/*SMOL_LOG_DEBUG(
                "CAMERA", "\n{} {} {} {}\n{} {} {} {}\n{} {} {} {}\n{} {} {} {}\n",
                cached_world_matrix.m00,
                cached_world_matrix.m01,
                cached_world_matrix.m02,
                cached_world_matrix.m03,
                cached_world_matrix.m10,
                cached_world_matrix.m11,
                cached_world_matrix.m12,
                cached_world_matrix.m13,
                cached_world_matrix.m20,
                cached_world_matrix.m21,
                cached_world_matrix.m22,
                cached_world_matrix.m23,
                cached_world_matrix.m30,
                cached_world_matrix.m31,
                cached_world_matrix.m32,
                cached_world_matrix.m33
            );*/