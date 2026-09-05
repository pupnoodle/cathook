#pragma once

#include "config.hpp"

#include <string_view>

namespace mono
{
struct window_header final
{
	std::string_view title{};
	std::string_view detail{};
	bool center_title{};
};

void set_window_scale(float scale);
float window_scale();
float window_header_height();
void draw_window_header(ImDrawList &draw_list, ImVec2 position, float width, const window_header &header);
}
