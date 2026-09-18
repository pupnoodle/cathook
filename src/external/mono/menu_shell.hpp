#pragma once

#include "config.hpp"

#include <functional>
#include <string_view>

namespace mono
{
struct menu_config final
{
	const char *id{ "menu" };
	std::string_view title{};
	ImVec2 preferred_size{ 820.0f, 600.0f };
	float navbar_height{ 42.0f };
	float subnavbar_height{ 32.0f };
	bool show_subnavbar{ true };
	float viewport_padding{ 8.0f };
	float background_alpha{ 0.80f };
	std::function<void()> on_drag_header{};
};

struct menu_state final
{
	ImVec2 position{};
	bool position_initialized{};
	bool dragging{};
	ImVec2 drag_offset{};
	bool reset_scroll{};
};

struct header_status final
{
	std::string_view label{};
	std::string_view value{};
	std::string_view action{};
	std::function<void()> on_action{};
};

bool begin_menu(const menu_config &config, menu_state &state, const std::function<void()> &render_navbar,
	const std::function<void()> &render_subnavbar = {}, const header_status &status = {});
void end_menu(bool content_visible);

bool navbar_entry(const char *label, bool active, const char *icon, ImFont *icon_font = nullptr);
bool subnavbar_entry(const char *label, bool active, const char *icon, ImFont *icon_font = nullptr);
void begin_transparent_child(const char *id, ImVec2 size = {});
}
