#include "log.h"

#include "smol/defines.h"

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ostream>
#include <atomic>
#include <deque>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef SMOL_PLATFORM_WIN
    #include <windows.h>
    #include <wtypes.h>
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

        // the ring a UI reads. history_next_seq counts every line ever kept
        // so a reader's cursor stays meaningful after old lines are dropped off the front
        std::mutex history_mutex;
        std::deque<entry_t> history;
        std::atomic<size_t> history_capacity = 0;
        u64_t history_next_seq = 0;

        const char* level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
        const char* ansi_level_colors[] = {
            "\033[90m", // TRACE gray
            "\033[36m", // DEBUG cyan
            "\033[37m", // INFO white
            "\033[33m", // WARN yellow
            "\033[31m", // ERROR red
            "\033[91m"  // FATAL bright red
        };

        const char* ansi_color_reset = "\033[0m";

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
            if (log_file.is_open()) log_file.close();

            std::string path_to_open = crt_file_index == 0 ? base_log_path : rotated_log_path(crt_file_index);

            std::filesystem::create_directories(std::filesystem::path(path_to_open).parent_path());

            log_file.open(path_to_open, std::ios::out | std::ios::app);
            if (!log_file.is_open()) { return false; }

            log_file.seekp(0, std::ios::end);
            crt_file_size = (size_t)log_file.tellp();

            return true;
        }

        std::string format_line(level_e level, const char* category, std::string_view msg, bool include_date)
        {
            std::chrono::time_point cur_time = time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            std::string time_str;

            if (include_date) { time_str = fmt::format("{:%Y-%m-%d %H:%M:%S}", cur_time); }
            else
            {
                time_str = fmt::format("{:%H:%M:%S}", cur_time);
            }

            return fmt::format("[{}] [{}] [{}] {}", time_str, level_names[(u8)level], category, msg);
        }

#if SMOL_PLATFORM_WIN
        // Windows consoles need escape sequence processing enabled manually
        void setup_ansi_console_colors_windows()
        {
            HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h_out == INVALID_HANDLE_VALUE) return;

            DWORD dw_mode = 0;
            if (!GetConsoleMode(h_out, &dw_mode)) return;

            dw_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(h_out, dw_mode);
        }
#endif

    } // namespace

    void set_level(level_e level) { crt_level = level; }

    void set_max_file_size(size_t new_max_file_size) { max_file_size = new_max_file_size; }

    std::string strip_ansi(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());

        for (std::size_t i = 0; i < text.size(); i++)
        {
            if (text[i] != '\x1b')
            {
                out += text[i];
                continue;
            }

            // CSI: ESC [ parameters, then one final byte in @..~
            // colour codes end in 'm' but cursor controls do not, so stopping at the next 'm' would eat real text
            if (i + 1 < text.size() && text[i + 1] == '[')
            {
                i += 2;
                while (i < text.size() && (text[i] < '@' || text[i] > '~')) { i++; }
            }
            else if (i + 1 < text.size())
            {
                i++; // a two byte escape
            }
        }

        if (!out.empty() && out.back() == '\r') { out.pop_back(); }
        return out;
    }

    void set_history_capacity(size_t max_lines)
    {
        history_capacity.store(max_lines, std::memory_order_relaxed);

        std::scoped_lock lock(history_mutex);
        while (max_lines > 0 && history.size() > max_lines) { history.pop_front(); }
        if (max_lines == 0) { history.clear(); }
    }

    void read_history(u64_t& cursor, std::vector<entry_t>& out)
    {
        std::scoped_lock lock(history_mutex);

        const u64_t oldest = history_next_seq - history.size();
        const u64_t first = (cursor < oldest) ? oldest : cursor;

        for (u64_t seq = first; seq < history_next_seq; seq++) { out.push_back(history[seq - oldest]); }

        cursor = history_next_seq;
    }

    bool to_file(const std::string& path)
    {
        std::filesystem::path fs_path(path);

        // an extension names the file itself, anything else is the directory for a dated file
        // make the directory from the parent: from the whole path, a file name created a directory with that name
        if (!fs_path.has_extension())
        {
            const std::chrono::time_point cur_time =
                time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            fs_path /= fmt::format("{:%Y-%m-%d}_log.txt", cur_time);
        }

        std::error_code ec;
        std::filesystem::create_directories(fs_path.parent_path(), ec);

        base_log_path = fs_path.string();
        if (!open_new_log_file()) { return false; }

        // the file is appended to across runs, so mark where each one starts
        const std::chrono::time_point started =
            time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
        log_file << fmt::format("---- session started {:%Y-%m-%d %H:%M:%S} ----", started) << std::endl;

        return true;
    }

    void write(level_e level, const char* category, std::string_view msg)
    {
        if (level < crt_level) return;

        if (const size_t capacity = history_capacity.load(std::memory_order_relaxed); capacity > 0)
        {
            std::scoped_lock lock(history_mutex);

            history.push_back({level, category != nullptr ? category : "", std::string(msg)});
            history_next_seq++;

            while (history.size() > capacity) { history.pop_front(); }
        }

        if (level == level_e::LOG_FATAL)
        {
            std::string console_msg =
                ansi_level_colors[(u8)level] + format_line(level, category, msg, false) + ansi_color_reset;
            std::cout << console_msg << std::endl;
            if (log_file.is_open())
            {
                std::string file_msg = format_line(level, category, msg, true);
                log_file << file_msg << std::endl;
            }

            return;
        }

        {
            const std::lock_guard<std::mutex> lock(queue_mutex);
            log_msg_t console_msg;
            console_msg.text =
                ansi_level_colors[(u8)level] + format_line(level, category, msg, false) + ansi_color_reset;
            console_msg.to_console = true;

            msg_queue.push(console_msg);

            if (log_file.is_open()) { msg_queue.push({format_line(level, category, msg, true), false}); }
        }

        queue_vc.notify_one();
    }

    void init()
    {
        if (is_running) { return; }

        is_running = true;
#if SMOL_PLATFORM_WIN
        setup_ansi_console_colors_windows();
#endif
        worker = std::thread(
            []
            {
                while (true)
                {
                    std::queue<log_msg_t> local_queue;

                    {
                        std::unique_lock lock(queue_mutex);
                        queue_vc.wait(lock, [] { return !msg_queue.empty() || !is_running; });

                        if (!is_running && msg_queue.empty()) { break; }
                        std::swap(local_queue, msg_queue);
                    }

                    bool wrote_console = false;
                    while (!local_queue.empty())
                    {
                        log_msg_t msg = local_queue.front();
                        local_queue.pop();

                        if (msg.to_console)
                        {
                            std::cout << msg.text << "\n";
                            wrote_console = true;
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

                    // once per batch: a pipe (the cooker under the editor) is block buffered
                    // and readers see nothing until exit
                    if (wrote_console) { std::cout.flush(); }
                }
            });
    }

    void shutdown()
    {
        {
            const std::lock_guard<std::mutex> lock(queue_mutex);
            is_running = false;
        }

        queue_vc.notify_all();
        if (worker.joinable()) worker.join();
    }

} // namespace smol::log
