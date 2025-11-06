#include "log.h"

#include "smol/defines.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#ifdef SMOL_PLATFORM_WIN
#    include <windows.h>
#    include <wtypes.h>
#endif

namespace smol::log
{
    namespace
    {
        struct log_msg_t
        {
            std::string text;
            bool to_console;
        };

        level_e crt_level = level_e::LOG_INFO;

        std::ofstream log_file;
        std::string base_log_path;
        u32_t crt_file_index = 0;
        size_t crt_file_size = 0;
        size_t max_file_size = 5 * 1024 * 1024;

        std::mutex queue_mutex;
        std::condition_variable queue_vc;
        std::queue<log_msg_t> msg_queue;
        bool is_running = false;
        std::thread worker;

        const char *level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
        const char *ansi_level_colors[] = {
            "\033[90m", // TRACE - Gray
            "\033[36m", // DEBUG - Cyan
            "\033[37m", // INFO - White
            "\033[33m", // WARN - Yellow
            "\033[31m", // ERROR - Red
            "\033[91m" // FATAL - Bright Red
        };

        const char *ansi_color_reset = "\033[0m";

        std::string rotated_log_path(u32_t index)
        {
            std::filesystem::path path(base_log_path);

            std::string stem = path.stem().string();
            std::string ext = path.extension().string();
            std::filesystem::path parent = path.parent_path();

            std::ostringstream oss;
            oss << stem << "_" << std::setw(3) << std::setfill('0') << index << ext;
            return (parent / oss.str()).string();
        }

        bool open_new_log_file()
        {
            if (log_file.is_open())
                log_file.close();

            std::string path_to_open = crt_file_index == 0 ? base_log_path : rotated_log_path(crt_file_index);

            std::filesystem::create_directories(std::filesystem::path(path_to_open).parent_path());

            log_file.open(path_to_open, std::ios::out | std::ios::app);
            if (!log_file.is_open())
            {
                return false;
            }

            log_file.seekp(0, std::ios::end);
            crt_file_size = (size_t)log_file.tellp();

            return true;
        }

        std::string format_line(level_e level, const char *category, std::string_view msg, bool include_date)
        {
            std::chrono::time_point cur_time = time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            std::string time_str;

            if (include_date)
            {
                time_str = fmt::format("{:%Y-%m-%d %H:%M:%S}", cur_time);
            }
            else
            {
                time_str = fmt::format("{:%H:%M:%S}", cur_time);
            }

            return fmt::format("[{}] [{}] [{}] {}", time_str, level_names[(u8)level], category, msg);
        }

#if SMOL_PLATFORM_WIN
        // Need to manually enable the processing of escape sequences in Windows in console (Windows why...)
        void setup_ansi_console_colors_windows()
        {
            HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h_out == INVALID_HANDLE_VALUE)
                return;

            DWORD dw_mode = 0;
            if (!GetConsoleMode(h_out, &dw_mode))
                return;

            dw_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(h_out, dw_mode);
        }
#endif
    } // namespace

    void set_level(level_e level)
    {
        crt_level = level;
    }

    void set_max_file_size(size_t new_max_file_size)
    {
        max_file_size = new_max_file_size;
    }

    bool to_file(const std::string &path)
    {
        std::filesystem::path fs_path(path);

        if (!std::filesystem::is_directory(fs_path))
        {
            std::filesystem::create_directories(fs_path);
        }

        if (path.back() == '/' || path.back() == '\\' || !fs_path.has_extension())
        {
            std::chrono::time_point cur_time = time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            std::string filename = fmt::format("{:%Y-%m-%d}_log.txt", cur_time);
            fs_path /= filename;
        }

        base_log_path = fs_path.string();
        return open_new_log_file();
    }

    void write(level_e level, const char *category, std::string_view msg)
    {
        if (level < crt_level)
            return;

        {
            const std::lock_guard<std::mutex> lock(queue_mutex);
            msg_queue.push({ansi_level_colors[(u8)level] + format_line(level, category, msg, false) + ansi_color_reset, true});
            if (log_file.is_open())
            {
                msg_queue.push({format_line(level, category, msg, true), false});
            }
        }

        queue_vc.notify_one();
    }

    void start()
    {
        is_running = true;
#if SMOL_PLATFORM_WIN
        setup_ansi_console_colors_windows();
#endif
        worker = std::thread([]
                             {
            while (is_running || !msg_queue.empty())
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_vc.wait(lock, [] { return !msg_queue.empty() || !is_running; });

                while (!msg_queue.empty())
                {
                    log_msg_t msg = msg_queue.front();
                    msg_queue.pop();

                    if (msg.to_console)
                    {
                        std::cout << msg.text << "\n";
                        continue;
                    }

                    if (log_file.is_open())
                    {
                        log_file << msg.text << "\n";
                        crt_file_size += msg.text.size() + 1;
                        log_file.flush();

                        if (crt_file_size >= max_file_size)
                        {
                            crt_file_index++;
                            open_new_log_file();
                        }
                    }
                }
            } });
    }

    void shutdown()
    {
        {
            const std::lock_guard<std::mutex> lock(queue_mutex);
            is_running = false;
        }

        queue_vc.notify_all();
        if (worker.joinable())
            worker.join();
    }

} // namespace smol::log
