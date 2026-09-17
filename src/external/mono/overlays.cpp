#include "overlays.hpp"

#include "theme.hpp"
#include "window_chrome.hpp"

#include <algorithm>
#include <cmath>

namespace mono
{
namespace
{
ImU32 to_u32(const color value)
{
	return ImGui::ColorConvertFloat4ToU32({ value.r, value.g, value.b, value.a });
}

std::string wrap_text(const std::string_view text)
{
	std::string result{};
	result.reserve(text.size());
	size_t column{};
	for (const char character : text) {
		if (column >= 60 && (character == ' ' || character == '.' || character == '!' || character == '?')) {
			result.push_back('\n');
			column = 0;
			if (character == ' ') {
				continue;
			}
		}
		result.push_back(character);
		++column;
	}
	return result;
}

ImVec2 clamp_indicator_position(const ImVec2 position, const ImVec2 size)
{
	const ImGuiViewport *const viewport{ ImGui::GetMainViewport() };
	if (viewport == nullptr) {
		return position;
	}

	const ImVec2 minimum{ viewport->WorkPos };
	const ImVec2 maximum{
		std::max(minimum.x, viewport->WorkPos.x + viewport->WorkSize.x - size.x),
		std::max(minimum.y, viewport->WorkPos.y + viewport->WorkSize.y - size.y)
	};
	return {
		std::clamp(position.x, minimum.x, maximum.x),
		std::clamp(position.y, minimum.y, maximum.y)
	};
}
}

void corner_text(const std::vector<overlay_text_line> &lines, const ImVec2 origin, ImFont *const requested_font)
{
	ImFont *const font{ requested_font ? requested_font : ImGui::GetFont() };
	if (!font || lines.empty()) {
		return;
	}

	const float line_step{ ImGui::GetFontSize() + 1.0f };
	float y{ origin.y };
	ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
	for (const overlay_text_line &line : lines) {
		const ImU32 color{ to_u32(line.text_color) };
		const ImU32 shadow{ ImGui::ColorConvertFloat4ToU32({ 0.0f, 0.0f, 0.0f, line.text_color.a }) };
		const ImVec2 position{ origin.x, y };
		draw_list->AddText(font, ImGui::GetFontSize(), { position.x - 1.0f, position.y }, shadow, line.text.data(), line.text.data() + line.text.size());
		draw_list->AddText(font, ImGui::GetFontSize(), { position.x + 1.0f, position.y }, shadow, line.text.data(), line.text.data() + line.text.size());
		draw_list->AddText(font, ImGui::GetFontSize(), { position.x, position.y - 1.0f }, shadow, line.text.data(), line.text.data() + line.text.size());
		draw_list->AddText(font, ImGui::GetFontSize(), { position.x, position.y + 1.0f }, shadow, line.text.data(), line.text.data() + line.text.size());
		draw_list->AddText(font, ImGui::GetFontSize(), position, color, line.text.data(), line.text.data() + line.text.size());
		y += line_step;
	}
}

ImVec2 indicator_panel(
	const char *const id,
	const std::string_view title,
	const std::vector<indicator_row> &rows,
	const ImVec2 position,
	ImFont *const requested_font,
	const bool movable,
	const indicator_row_callback on_row)
{
	if (rows.empty()) {
		return position;
	}

	const float scale{ window_scale() };
	const float header_height{ window_header_height() };
	const float horizontal_padding{ 10.0f * scale };
	const float vertical_padding{ 8.0f * scale };
	const float row_height{ ImGui::GetFontSize() + 6.0f * scale };
	const float font_size{ ImGui::GetFontSize() };
	const float column_gap{ 12.0f * scale };
	ImFont *const font{ requested_font ? requested_font : ImGui::GetFont() };
	if (!font) {
		return position;
	}

	const auto text_width = [font, font_size](const std::string_view text) {
		return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
	};
	float name_width{ text_width(title) };
	float type_width{};
	float value_width{};
	for (const indicator_row &row : rows) {
		name_width = std::max(name_width, text_width(row.name));
		type_width = std::max(type_width, text_width(row.type));
		value_width = std::max(value_width, text_width(row.value));
	}

	const float width{ std::ceil(horizontal_padding * 2.0f + name_width + type_width + value_width + column_gap * 2.0f + 20.0f * scale) };
	const float height{ header_height + vertical_padding * 2.0f + row_height * static_cast<float>(rows.size()) };
	const float type_x{ horizontal_padding + name_width + column_gap };
	const float value_x{ type_x + type_width + column_gap };
	const ImVec2 panel_size{ width, height };
	const ImVec2 safe_position{ clamp_indicator_position(position, panel_size) };

	ImGui::SetNextWindowPos(safe_position, ImGuiCond_Always);
	ImGui::SetNextWindowSize({ width, height }, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGuiWindowFlags flags{ ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse };
	if (!movable) flags |= ImGuiWindowFlags_NoInputs;
	ImVec2 result{ safe_position };
	if (ImGui::Begin(id, nullptr, flags)) {
		const ImVec2 panel_position{ ImGui::GetWindowPos() };
		const ImVec2 panel_end{ panel_position.x + width, panel_position.y + height };
		ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
		draw_list->AddRectFilled(panel_position, panel_end, ImGui::GetColorU32(ImGuiCol_WindowBg), 4.0f * scale);
		draw_window_header(*draw_list, panel_position, width, { title });
		draw_list->AddRect({ panel_position.x + 0.5f * scale, panel_position.y + 0.5f * scale }, { panel_end.x - 0.5f * scale, panel_end.y - 0.5f * scale }, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f * scale, 0, scale);

		for (size_t index{}; index < rows.size(); ++index) {
			const indicator_row &row{ rows[index] };
			const float y{ panel_position.y + header_height + vertical_padding + row_height * static_cast<float>(index) };
			const ImU32 active_color{ ImGui::GetColorU32(row.active ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled) };
			const ImU32 detail_color{ ImGui::GetColorU32(row.active ? ImGuiCol_Text : ImGuiCol_TextDisabled) };
			draw_list->AddText(font, font_size, { panel_position.x + horizontal_padding, y }, active_color, row.name.data(), row.name.data() + row.name.size());
			draw_list->AddText(font, font_size, { panel_position.x + type_x, y }, detail_color, row.type.data(), row.type.data() + row.type.size());
			draw_list->AddText(font, font_size, { panel_position.x + value_x, y }, detail_color, row.value.data(), row.value.data() + row.value.size());

			if (on_row) {
				ImGui::SetCursorScreenPos({ panel_position.x, y });
				ImGui::PushID(static_cast<int>(index));
				ImGui::InvisibleButton("##indicator_row", { width, row_height }, ImGuiButtonFlags_MouseButtonRight);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
					on_row(index, row);
				}
				ImGui::PopID();
			}
		}

		static ImGuiID dragging_id{};
		static ImVec2 drag_offset{};
		const ImGuiID window_id{ ImGui::GetID("##mono_indicator_drag") };
		const ImVec2 header_end{ panel_end.x, panel_position.y + header_height };
		if (movable && ImGui::IsMouseHoveringRect(panel_position, header_end) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			dragging_id = window_id;
			const ImVec2 mouse{ ImGui::GetMousePos() };
			drag_offset = { mouse.x - panel_position.x, mouse.y - panel_position.y };
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && dragging_id == window_id) {
			dragging_id = 0;
		}
		if (dragging_id == window_id) {
			const ImVec2 mouse{ ImGui::GetMousePos() };
			ImGui::SetWindowPos(clamp_indicator_position(
				{ mouse.x - drag_offset.x, mouse.y - drag_offset.y }, panel_size));
		}
		result = ImGui::GetWindowPos();
		ImGui::SetCursorScreenPos(panel_position);
		ImGui::Dummy({ width, height });
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	return result;
}

ImVec2 drag_overlay(
	const char *const id,
	const std::string_view label,
	const ImVec2 position,
	const ImVec2 size)
{
	ImGui::SetNextWindowPos(position, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.18f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	const ImGuiWindowFlags flags{ ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar };

	ImVec2 result{ position };
	if (ImGui::Begin(id, nullptr, flags)) {
		const ImVec2 window_position{ ImGui::GetWindowPos() };
		const ImVec2 window_end{ window_position.x + size.x, window_position.y + size.y };
		ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
		draw_list->AddRect(window_position, window_end, ImGui::GetColorU32(ImGuiCol_CheckMark));

		const ImVec2 label_size{ ImGui::CalcTextSize(label.data(), label.data() + label.size()) };
		draw_list->AddText({ window_position.x + (size.x - label_size.x) * 0.5f, window_position.y + (size.y - label_size.y) * 0.5f }, ImGui::GetColorU32(ImGuiCol_TextDisabled), label.data(), label.data() + label.size());

		static ImGuiID dragging_id{};
		static ImVec2 drag_offset{};
		const ImGuiID overlay_id{ ImGui::GetID("##mono_drag_overlay") };
		if (ImGui::IsMouseHoveringRect(window_position, window_end) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			dragging_id = overlay_id;
			const ImVec2 mouse{ ImGui::GetMousePos() };
			drag_offset = { mouse.x - window_position.x, mouse.y - window_position.y };
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && dragging_id == overlay_id) {
			dragging_id = 0;
		}
		if (dragging_id == overlay_id) {
			const ImVec2 mouse{ ImGui::GetMousePos() };
			ImGui::SetWindowPos({ mouse.x - drag_offset.x, mouse.y - drag_offset.y });
		}
		result = ImGui::GetWindowPos();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	return result;
}

void notifications::push(const std::string_view text, const notification_kind kind, const float now, const float duration, const color text_color)
{
	m_items.push_back({
		.text = wrap_text(text),
		.text_color = text_color,
		.kind = kind,
		.expires_at = now + duration
	});
}

void notifications::clear()
{
	m_items.clear();
}

void notifications::render(const float now, const float, const ImVec2 origin, ImFont *const requested_font, const size_t maximum_visible)
{
	std::erase_if(m_items, [now](const item &entry) { return now >= entry.expires_at; });
	ImFont *const font{ requested_font ? requested_font : ImGui::GetFont() };
	const size_t count{ std::min(m_items.size(), maximum_visible) };
	if (!font || count == 0) {
		return;
	}

	const float scale{ window_scale() };
	const float font_size{ ImGui::GetFontSize() };
	const float horizontal_padding{ 8.0f * scale };
	const float vertical_padding{ 6.0f * scale };
	const float header_height{ window_header_height() };
	float width{ 120.0f * scale };
	float content_height{};
	for (size_t index{}; index < count; ++index) {
		const ImVec2 text_size{ font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, m_items[index].text.c_str()) };
		width = std::max(width, text_size.x + horizontal_padding * 2.0f);
		content_height += text_size.y + vertical_padding * 2.0f;
	}
	const float height{ header_height + content_height };

	ImGui::SetNextWindowPos(origin, ImGuiCond_Always);
	ImGui::SetNextWindowSize({ width, height }, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags{
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMouseInputs
	};
	if (ImGui::Begin("##mono_notifications", nullptr, flags)) {
		const ImVec2 position{ ImGui::GetWindowPos() };
		const ImVec2 end{ position.x + width, position.y + height };
		ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
		ImVec4 background{ ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) };
		background.w = 0.80f;
		draw_list->AddRectFilled(position, end, ImGui::ColorConvertFloat4ToU32(background), 4.0f * scale);
		draw_window_header(*draw_list, position, width, { "notifications" });
		draw_list->AddRect({ position.x + 0.5f * scale, position.y + 0.5f * scale }, { end.x - 0.5f * scale, end.y - 0.5f * scale }, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f * scale, 0, scale);

		float y{ position.y + header_height };
		for (size_t index{}; index < count; ++index) {
			const item &entry{ m_items[index] };
			const ImVec2 text_size{ font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, entry.text.c_str()) };
			const float row_height{ text_size.y + vertical_padding * 2.0f };
			const ImU32 text_color{ to_u32(entry.text_color) };
			if (entry.kind == notification_kind::warning) {
				draw_list->AddRectFilled({ position.x, y }, { position.x + 2.0f * scale, y + row_height }, text_color);
			}
			draw_list->AddText(font, font_size, { position.x + horizontal_padding, y + vertical_padding }, text_color, entry.text.c_str());
			if (index + 1 < count) {
				ImVec4 separator{ ImGui::GetStyleColorVec4(ImGuiCol_Border) };
				separator.w = 0.35f;
				draw_list->AddLine(
					{ position.x + horizontal_padding, y + row_height },
					{ position.x + width - horizontal_padding, y + row_height },
					ImGui::ColorConvertFloat4ToU32(separator),
					scale);
			}
			y += row_height;
		}
		ImGui::Dummy({ width, height });
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}
}
