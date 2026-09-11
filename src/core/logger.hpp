/*
/^-----^\   data: 2026-04-02
V  o o  V  file: src/core/logger.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#pragma once

#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>

namespace cathook::core
{

class logger
{
public:
    explicit logger(std::filesystem::path file_path);

    void write(std::string_view message);
    void write_raw(std::string_view message);
    [[nodiscard]] bool is_open() const;

private:
    mutable std::mutex m_mutex{};
    std::ofstream m_stream{};
};

[[nodiscard]] std::filesystem::path root_directory();
[[nodiscard]] std::filesystem::path log_directory();
[[nodiscard]] std::filesystem::path config_directory();

void initialize_logger(const std::filesystem::path& file_path);
void shutdown_logger();
[[nodiscard]] logger* get_logger();

void log_line(std::string_view message);
void log_raw(std::string_view message);
void vlog_raw(const char* fmt, std::va_list args);
void log_raw(const char* fmt, ...);

}
