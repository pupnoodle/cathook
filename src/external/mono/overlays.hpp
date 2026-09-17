#pragma once

#include "theme.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mono
{
struct indicator_row final
{
	std::string_view name{};
	std::string_view type{};
	std::string_view value{};
	bool active{};
};

using indicator_row_callback = std::function<void(size_t, const indicator_row &)>;

struct overlay_text_line final
{
	std::string_view text{};
	color text_color{};
};

void corner_text(const std::vector<overlay_text_line> &lines, ImVec2 origin = { 8.0f, 8.0f }, ImFont *font = nullptr);

ImVec2 indicator_panel(
	const char *id,
	std::string_view title,
	const std::vector<indicator_row> &rows,
	ImVec2 position,
	ImFont *font = nullptr,
	bool movable = true,
	indicator_row_callback on_row = {});

ImVec2 drag_overlay(
	const char *id,
	std::string_view label,
	ImVec2 position,
	ImVec2 size);

enum class notification_kind
{
	info,
	warning
};

class notifications final
{
public:
	void push(std::string_view text, notification_kind kind, float now, float duration, color text_color);
	void clear();
	void render(float now, float delta_time, ImVec2 origin = { 4.0f, 32.0f }, ImFont *font = nullptr, size_t maximum_visible = 4);

private:
	struct item final
	{
		std::string text{};
		color text_color{};
		notification_kind kind{};
		float expires_at{};
	};

	std::vector<item> m_items{};
};
}
