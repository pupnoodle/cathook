/*
/^-----^\   data: 2026-04-02
V  o o  V  file: src/core/config/config_store.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#pragma once

#include "features/menu/config.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cathook::core
{

class config_store
{
public:
    explicit config_store(std::filesystem::path root_directory);
    config_store(std::filesystem::path root_directory, std::filesystem::path config_subdirectory);

    bool load_file(std::string_view name);
    bool save_file(std::string_view name);
    bool delete_file(std::string_view name);

    [[nodiscard]] std::vector<std::string> list_files() const;
    [[nodiscard]] const std::string& current_name() const;
    [[nodiscard]] config_store scoped_store(std::filesystem::path config_subdirectory) const;

    void import_config(const Config& config);
    void export_config(Config& config) const;

    void erase_key(std::string_view key);
    void erase_prefix(std::string_view prefix);

    void set_bool(std::string key, bool value);
    void set_int(std::string key, int value);
    void set_float(std::string key, float value);
    void set_string(std::string key, std::string_view value);
    void set_color(std::string key, const RGBA_float& value);

    [[nodiscard]] bool get_bool(std::string_view key, bool fallback) const;
    [[nodiscard]] int get_int(std::string_view key, int fallback) const;
    [[nodiscard]] float get_float(std::string_view key, float fallback) const;
    [[nodiscard]] std::string get_string(std::string_view key, std::string_view fallback) const;
    [[nodiscard]] RGBA_float get_color(std::string_view key, RGBA_float fallback) const;

private:
    [[nodiscard]] std::filesystem::path config_directory() const;
    [[nodiscard]] std::filesystem::path config_path(std::string_view name) const;
    [[nodiscard]] static std::string trim(std::string value);
    [[nodiscard]] static std::optional<RGBA_float> parse_color(std::string_view value);

    std::filesystem::path m_root_directory{};
    std::filesystem::path m_config_subdirectory{ "configs" };
    std::unordered_map<std::string, std::string> m_values{};
    std::string m_current_name{ "default" };
};

void initialize_config_store(const std::filesystem::path& root_directory);
void shutdown_config_store();
[[nodiscard]] config_store* get_config_store();
void load_default_config(Config& config);

} // namespace cathook::core
