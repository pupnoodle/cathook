/*
data: 2026-08-10
file: src/core/developer_console_config.hpp
author: HappyKuro
*/
// Note: Original made by HappyKuro, modified by pupnoodle
#ifndef PUPHOOK_DEVELOPER_CONSOLE_CONFIG_HPP
#define PUPHOOK_DEVELOPER_CONSOLE_CONFIG_HPP

#include "core/config/config_store.hpp"
#include "features/menu/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace puphook::core::developer_console_config
{

enum class value_type
{
    boolean,
    integer,
    decimal,
    color,
    text,
};

inline const char* value_type_name(const value_type type)
{
    switch (type)
    {
    case value_type::boolean: return "bool";
    case value_type::integer: return "int";
    case value_type::decimal: return "float";
    case value_type::color: return "color";
    case value_type::text: return "text";
    }

    return "text";
}

inline std::string lower(std::string_view value)
{
    std::string result{ value };
    std::ranges::transform(result, result.begin(), [](const unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

inline std::string trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    {
        value.remove_suffix(1);
    }
    return std::string{ value };
}

inline std::optional<bool> parse_bool(std::string_view value)
{
    const std::string normalized = lower(trim(value));
    if (normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes" ||
        normalized == "enable" || normalized == "enabled")
    {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "off" || normalized == "no" ||
        normalized == "disable" || normalized == "disabled")
    {
        return false;
    }
    return std::nullopt;
}

inline bool parse_int(std::string_view value, int& output)
{
    const std::string normalized = trim(value);
    if (normalized.empty())
    {
        return false;
    }

    int parsed = 0;
    const auto result = std::from_chars(normalized.data(), normalized.data() + normalized.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != normalized.data() + normalized.size())
    {
        return false;
    }

    output = parsed;
    return true;
}

inline bool parse_float(std::string_view value, float& output)
{
    const std::string normalized = trim(value);
    if (normalized.empty())
    {
        return false;
    }

    float parsed = 0.0f;
    const auto result = std::from_chars(normalized.data(), normalized.data() + normalized.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != normalized.data() + normalized.size())
    {
        return false;
    }

    output = parsed;
    return true;
}

inline std::vector<std::string> split_components(std::string_view value)
{
    std::vector<std::string> components{};
    std::string current{};

    for (const char character : value)
    {
        if (character == ',' || std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            if (!current.empty())
            {
                components.emplace_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(character);
    }

    if (!current.empty())
    {
        components.emplace_back(std::move(current));
    }
    return components;
}

inline std::optional<RGBA_float> parse_color(std::string_view value)
{
    const auto components = split_components(value);
    if (components.size() < 3 || components.size() > 5)
    {
        return std::nullopt;
    }

    float channels[4]{};
    for (std::size_t index = 0; index < 4 && index < components.size(); ++index)
    {
        if (!parse_float(components[index], channels[index]))
        {
            return std::nullopt;
        }
    }
    if (components.size() == 3)
    {
        channels[3] = 1.0f;
    }

    bool rainbow = false;
    if (components.size() == 5)
    {
        const auto parsed = parse_bool(components[4]);
        if (!parsed.has_value())
        {
            return std::nullopt;
        }
        rainbow = *parsed;
    }

    const bool byte_scale = std::any_of(std::begin(channels), std::end(channels),
        [](const float channel) { return channel > 1.0f; });
    const float scale = byte_scale ? 1.0f / 255.0f : 1.0f;
    return RGBA_float{
        std::clamp(channels[0] * scale, 0.0f, 1.0f),
        std::clamp(channels[1] * scale, 0.0f, 1.0f),
        std::clamp(channels[2] * scale, 0.0f, 1.0f),
        std::clamp(channels[3] * scale, 0.0f, 1.0f),
        rainbow,
    };
}

inline value_type classify(std::string_view key, std::string_view stored)
{
    const std::string normalized = lower(trim(stored));
    if (normalized == "true" || normalized == "false")
    {
        return value_type::boolean;
    }

    if (key.ends_with("color") && parse_color(stored).has_value())
    {
        return value_type::color;
    }

    int integer = 0;
    if (parse_int(stored, integer))
    {
        return value_type::integer;
    }

    float decimal = 0.0f;
    if (parse_float(stored, decimal))
    {
        return value_type::decimal;
    }

    return value_type::text;
}

inline const char* accepted_input(const value_type type)
{
    switch (type)
    {
    case value_type::boolean: return "true/false, on/off, or 1/0";
    case value_type::integer: return "a whole number";
    case value_type::decimal: return "a number";
    case value_type::color: return "r,g,b[,a[,rainbow]]; channels may be 0-1 or 0-255";
    case value_type::text: return "any text";
    }
    return "any text";
}

inline bool write_value(config_store& store, const std::string& key, const value_type type,
    std::string_view input)
{
    switch (type)
    {
    case value_type::boolean:
    {
        const auto parsed = parse_bool(input);
        if (!parsed.has_value()) return false;
        store.set_bool(key, *parsed);
        return true;
    }
    case value_type::integer:
    {
        int parsed = 0;
        if (!parse_int(input, parsed)) return false;
        store.set_int(key, parsed);
        return true;
    }
    case value_type::decimal:
    {
        float parsed = 0.0f;
        if (!parse_float(input, parsed)) return false;
        store.set_float(key, parsed);
        return true;
    }
    case value_type::color:
    {
        const auto parsed = parse_color(input);
        if (!parsed.has_value()) return false;
        store.set_color(key, *parsed);
        return true;
    }
    case value_type::text:
        store.set_string(key, input);
        return true;
    }
    return false;
}

inline std::vector<std::string> setting_keys(config_store& store, const Config& config)
{
    store.import_config(config);
    return store.keys();
}

inline std::optional<std::string> default_value(const config_store& store, const std::string& key)
{
    config_store defaults = store.scoped_store("configs");
    defaults.import_config(Config{});
    if (!defaults.has_key(key))
    {
        return std::nullopt;
    }
    return defaults.get_string(key, "");
}

}

#endif
