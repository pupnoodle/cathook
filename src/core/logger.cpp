/*
/^-----^\   data: 2026-04-02
V  o o  V  file: src/core/logger.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "logger.hpp"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace cathook::core
{

namespace
{

std::unique_ptr<logger> g_logger{};

std::filesystem::path fallback_root_directory()
{
    if (const char* const xdg_config_home{ std::getenv("XDG_CONFIG_HOME") })
    {
        if (*xdg_config_home != '\0')
        {
            return std::filesystem::path{ xdg_config_home } / "cathook";
        }
    }

    if (const char* const home{ std::getenv("HOME") })
    {
        if (*home != '\0')
        {
            return std::filesystem::path{ home } / ".config" / "cathook";
        }
    }

    return std::filesystem::temp_directory_path() / "cathook";
}

bool can_use_root_directory(const std::filesystem::path& directory)
{
    std::error_code error{};
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return false;
    }

    const auto probe_path{ directory / ".write_test" };
    std::FILE* probe{ std::fopen(probe_path.string().c_str(), "ab") };
    if (!probe)
    {
        return false;
    }

    std::fclose(probe);
    std::filesystem::remove(probe_path, error);
    return true;
}

}

logger::logger(std::filesystem::path file_path)
{
    std::error_code error{};
    std::filesystem::create_directories(file_path.parent_path(), error);
    m_stream.open(file_path, std::ios::out | std::ios::app);
}

void logger::write(const std::string_view message)
{
    if (!m_stream.is_open())
    {
        return;
    }

    m_stream << message << '\n';
}

void logger::write_raw(const std::string_view message)
{
    if (!m_stream.is_open())
    {
        return;
    }

    m_stream << message;
}

bool logger::is_open() const
{
    return m_stream.is_open();
}

std::filesystem::path root_directory()
{
    static const std::filesystem::path value = []()
    {
        if (const char* const override_path{ std::getenv("CATHOOK_ROOT_DIR") })
        {
            if (*override_path != '\0')
            {
                return std::filesystem::path{ override_path };
            }
        }

        const std::filesystem::path preferred{ "/opt/cathook" };
        if (can_use_root_directory(preferred))
        {
            return preferred;
        }

        return fallback_root_directory();
    }();

    return value;
}

std::filesystem::path log_directory()
{
    return root_directory() / "logs";
}

std::filesystem::path config_directory()
{
    return root_directory() / "configs";
}

void initialize_logger(const std::filesystem::path& file_path)
{
    if (g_logger && g_logger->is_open())
    {
        return;
    }

    g_logger = std::make_unique<logger>(file_path);
}

void shutdown_logger()
{
    g_logger.reset();
}

logger* get_logger()
{
    if (!g_logger)
    {
        initialize_logger(log_directory() / "cathook.log");
    }

    return g_logger.get();
}

void log_line(const std::string_view message)
{
    if (logger* instance{ get_logger() })
    {
        instance->write(message);
    }
}

void log_raw(const std::string_view message)
{
    if (logger* instance{ get_logger() })
    {
        instance->write_raw(message);
    }
}

void vlog_raw(const char* const fmt, std::va_list args)
{
    if (!fmt)
    {
        return;
    }

    std::va_list copied_args{};
    va_copy(copied_args, args);
    const int required_length{ std::vsnprintf(nullptr, 0, fmt, copied_args) };
    va_end(copied_args);

    if (required_length <= 0)
    {
        return;
    }

    std::vector<char> buffer(static_cast<std::size_t>(required_length) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    log_raw(std::string_view{ buffer.data(), static_cast<std::size_t>(required_length) });
}

void log_raw(const char* const fmt, ...)
{
    std::va_list args{};
    va_start(args, fmt);
    vlog_raw(fmt, args);
    va_end(args);
}

}
