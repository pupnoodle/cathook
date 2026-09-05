#include "window_chrome.hpp"

#include <algorithm>

namespace mono
{
namespace
{
float configured_scale{};
}

void set_window_scale(const float scale)
{
	configured_scale = scale > 0.0f ? scale : 0.0f;
}

float window_scale()
{
	if (configured_scale > 0.0f) {
		return configured_scale;
	}
	const float font_size{ ImGui::GetFontSize() };
	if (font_size <= 0.0f) {
		return 1.0f;
	}
	return std::max(font_size / 13.0f, 0.5f);
}

float window_header_height()
{
	return 24.0f * window_scale();
}

void draw_window_header(ImDrawList &draw_list, const ImVec2 position, const float width, const window_header &header)
{
	const float scale{ window_scale() };
	const float height{ window_header_height() };
	const ImVec2 end{ position.x + width, position.y + height };
	ImVec4 background{ ImGui::GetStyleColorVec4(ImGuiCol_CheckMark) };
	background.w = 0.50f;
	draw_list.AddRectFilled(position, end, ImGui::ColorConvertFloat4ToU32(background), 4.0f * scale, ImDrawFlags_RoundCornersTop);
	ImVec4 edge{ ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) };
	edge.w = 0.62f;
	draw_list.AddLine({ position.x, end.y - scale }, { end.x, end.y - scale }, ImGui::ColorConvertFloat4ToU32(edge));

	const float font_size{ ImGui::GetFontSize() };
	const float horizontal_padding{ 4.0f * scale };
	if (header.detail.empty()) {
		const ImVec2 text_size{ ImGui::CalcTextSize(header.title.data(), header.title.data() + header.title.size()) };
		const float text_x{ header.center_title ? position.x + (width - text_size.x) * 0.5f : position.x + horizontal_padding };
		draw_list.AddText({ text_x, position.y + (height - font_size) * 0.5f }, ImGui::GetColorU32(ImGuiCol_Text), header.title.data(), header.title.data() + header.title.size());
		return;
	}
	const ImVec2 title_size{ ImGui::CalcTextSize(header.title.data(), header.title.data() + header.title.size()) };
	const ImVec2 detail_size{ ImGui::CalcTextSize(header.detail.data(), header.detail.data() + header.detail.size()) };
	const float title_x{ header.center_title ? position.x + (width - title_size.x) * 0.5f : position.x + horizontal_padding };
	const float detail_x{ header.center_title ? position.x + (width - detail_size.x) * 0.5f : position.x + horizontal_padding };
	draw_list.AddText({ title_x, position.y + 2.0f * scale }, ImGui::GetColorU32(ImGuiCol_TextDisabled), header.title.data(), header.title.data() + header.title.size());
	draw_list.AddText({ detail_x, position.y + height - font_size - 2.0f * scale }, ImGui::GetColorU32(ImGuiCol_CheckMark), header.detail.data(), header.detail.data() + header.detail.size());
}
}
