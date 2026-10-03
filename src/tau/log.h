#pragma once
#include "defines.h"

#include <fmt/format.h>
#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>

#ifdef ERROR
    #undef ERROR
#endif

namespace tau::log
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

    struct entry_t
    {
        level_e level;
        std::string category;
        std::string message;
    };

    TAU_ENGINE_API void set_level(level_e level);
    TAU_ENGINE_API void set_max_file_size(size_t size);

    // a path with an extension names the file, anything else is a directory for a dated file
    // rotates by size once max_file_size is passed
    TAU_ENGINE_API bool to_file(const std::string& path);

    // keeps the most recent lines in memory for a UI. off by default
    // the editor enables it before anything else logs, so its console has the whole session
    TAU_ENGINE_API void set_history_capacity(size_t max_lines);

    // appends everything logged since `cursor` and moves it forward
    // a cursor older than the oldest held line resumes from that line
    TAU_ENGINE_API void read_history(u64_t& cursor, std::vector<entry_t>& out);

    // strips escape sequences from child output (the cooker, xmake) before it is logged, and a trailing carriage return
    TAU_ENGINE_API std::string strip_ansi(std::string_view text);

    TAU_ENGINE_API void init();
    TAU_ENGINE_API void shutdown();
    TAU_ENGINE_API void write(level_e level, const char* category, std::string_view msg);
} // namespace tau::log

#ifndef TAU_LOG_LEVEL
    #define TAU_LOG_LEVEL tau::log::level_e::LOG_DEBUG
#endif

#define TAU_LOG_TRACE(cat, fmtstr, ...)                                                                                \
    if constexpr (tau::log::level_e::LOG_TRACE >= TAU_LOG_LEVEL)                                                       \
    tau::log::write(tau::log::level_e::LOG_TRACE, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define TAU_LOG_DEBUG(cat, fmtstr, ...)                                                                                \
    if constexpr (tau::log::level_e::LOG_DEBUG >= TAU_LOG_LEVEL)                                                       \
    tau::log::write(tau::log::level_e::LOG_DEBUG, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define TAU_LOG_INFO(cat, fmtstr, ...)                                                                                 \
    if constexpr (tau::log::level_e::LOG_INFO >= TAU_LOG_LEVEL)                                                        \
    tau::log::write(tau::log::level_e::LOG_INFO, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define TAU_LOG_WARN(cat, fmtstr, ...)                                                                                 \
    if constexpr (tau::log::level_e::LOG_WARN >= TAU_LOG_LEVEL)                                                        \
    tau::log::write(tau::log::level_e::LOG_WARN, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define TAU_LOG_ERROR(cat, fmtstr, ...)                                                                                \
    if constexpr (tau::log::level_e::LOG_ERROR >= TAU_LOG_LEVEL)                                                       \
    tau::log::write(tau::log::level_e::LOG_ERROR, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

#define TAU_LOG_FATAL(cat, fmtstr, ...)                                                                                \
    if constexpr (tau::log::level_e::LOG_FATAL >= TAU_LOG_LEVEL)                                                       \
    tau::log::write(tau::log::level_e::LOG_FATAL, cat, fmt::format(fmtstr __VA_OPT__(, ) __VA_ARGS__))

/*TAU_LOG_DEBUG(
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