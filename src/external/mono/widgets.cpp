#include "widgets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace mono
{
namespace
{
constexpr float vertical_padding{ 2.0f };
input_adapter configured_input{};
item_interaction configured_item_interaction{};

void remember_item_interaction()
{
	configured_item_interaction.hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
}

int resize_string(ImGuiInputTextCallbackData *const data)
{
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		std::string *const value{ static_cast<std::string *>(data->UserData) };
		value->resize(data->BufTextLen);
		data->Buf = value->data();
	}
	return 0;
}

bool input_text(const char *const label, std::string *const value, const char *const hint, const ImVec2 size, const ImGuiInputTextFlags flags, const bool multiline)
{
	if (!label || !value) {
		return false;
	}
	if (value->capacity() == 0) {
		value->reserve(32);
	}
	const ImGuiInputTextFlags actual_flags{ flags | ImGuiInputTextFlags_CallbackResize };
	bool changed{};
	if (multiline) {
		changed = ImGui::InputTextMultiline(label, value->data(), value->capacity() + 1, size, actual_flags, resize_string, value);
	}
	else if (hint) {
		changed = ImGui::InputTextWithHint(label, hint, value->data(), value->capacity() + 1, actual_flags, resize_string, value);
	}
	else {
		changed = ImGui::InputText(label, value->data(), value->capacity() + 1, actual_flags, resize_string, value);
	}
	remember_item_interaction();
	ImGui::Dummy({ 0.0f, vertical_padding });
	return changed;
}

const char *selected_label(const int value, const std::vector<std::pair<std::string, int>> &items)
{
	const auto iterator{ std::ranges::find(items, value, &std::pair<std::string, int>::second) };
	return iterator == items.end() ? "select..." : iterator->first.c_str();
}
}

void set_input_adapter(input_adapter adapter)
{
	configured_input = std::move(adapter);
}

item_interaction last_item_interaction()
{
	return configured_item_interaction;
}

disabled_scope::disabled_scope(const bool disabled) : m_disabled{ disabled }
{
	if (m_disabled) {
		ImGui::BeginDisabled();
	}
}

disabled_scope::~disabled_scope()
{
	if (m_disabled) {
		ImGui::EndDisabled();
	}
}

bool toggle(const char *const label, bool *const value)
{
	if (!label || !value) {
		return false;
	}
	const ImVec2 position{ ImGui::GetCursorScreenPos() };
	const ImVec2 size{ std::max(ImGui::GetContentRegionAvail().x, 1.0f), 22.0f };
	ImGui::PushID(label);
	const bool pressed{ ImGui::InvisibleButton("##toggle", size) };
	const bool hovered{ ImGui::IsItemHovered() };
	configured_item_interaction.hovered = hovered;
	if (pressed) {
		*value = !*value;
	}
	ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
	draw_list->AddText({ position.x + 2.0f, position.y + 3.0f }, ImGui::GetColorU32(*value ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
	constexpr float switch_width{ 30.0f };
	constexpr float switch_height{ 14.0f };
	const ImVec2 minimum{ position.x + size.x - switch_width - 2.0f, position.y + 4.0f };
	const ImVec2 maximum{ minimum.x + switch_width, minimum.y + switch_height };
	ImVec4 background{ ImGui::GetStyleColorVec4(*value ? ImGuiCol_CheckMark : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg) };
	background.w = *value ? 0.65f : 1.0f;
	draw_list->AddRectFilled(minimum, maximum, ImGui::ColorConvertFloat4ToU32(background), switch_height * 0.5f);
	const float knob_x{ *value ? maximum.x - switch_height + 2.0f : minimum.x + 2.0f };
	draw_list->AddCircleFilled({ knob_x + (switch_height - 4.0f) * 0.5f, minimum.y + switch_height * 0.5f }, (switch_height - 4.0f) * 0.5f, ImGui::GetColorU32(ImGuiCol_Text));
	ImGui::PopID();
	ImGui::Dummy({ 0.0f, vertical_padding });
	return pressed;
}

bool button(const char *const label, const ImVec2 size, const bool danger)
{
	if (!label) {
		return false;
	}
	ImVec4 accent{ ImGui::GetStyleColorVec4(ImGuiCol_CheckMark) };
	if (danger) {
		accent = { 0.84f, 0.30f, 0.32f, 1.0f };
	}
	ImGui::PushStyleColor(ImGuiCol_Button, accent);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { accent.x, accent.y, accent.z, 0.78f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, { accent.x, accent.y, accent.z, 1.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
	const bool pressed{ ImGui::Button(label, size) };
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(3);
	remember_item_interaction();
	return pressed;
}

bool list_item(const char *const label, const bool selected, const ImVec2 size)
{
	if (!label) {
		return false;
	}
	const ImVec2 position{ ImGui::GetCursorScreenPos() };
	const ImVec2 actual_size{
		size.x == 0.0f ? std::max(ImGui::GetContentRegionAvail().x, 1.0f) : size.x,
		size.y == 0.0f ? ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f : size.y
	};
	const bool pressed{ ImGui::InvisibleButton(label, actual_size) };
	const bool hovered{ ImGui::IsItemHovered() };
	ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
	if (selected || hovered) {
		const ImGuiCol color{ selected ? ImGuiCol_CheckMark : ImGuiCol_FrameBgHovered };
		draw_list->AddRectFilled(
			position,
			{ position.x + actual_size.x, position.y + actual_size.y },
			ImGui::GetColorU32(color),
			std::min(5.0f, actual_size.y * 0.5f));
	}
	draw_list->AddText(
		{ position.x + ImGui::GetStyle().FramePadding.x,
		  position.y + (actual_size.y - ImGui::GetTextLineHeight()) * 0.5f },
		ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled),
		label);
	configured_item_interaction.hovered = hovered;
	remember_item_interaction();
	return pressed;
}

bool select_single(const char *const label, int *const value, const std::vector<std::pair<std::string, int>> &items)
{
	if (!label || !value || items.empty()) {
		return false;
	}
	bool changed{};
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SetNextItemWidth(-1.0f);
	const bool combo_open{ ImGui::BeginCombo("##value", selected_label(*value, items)) };
	remember_item_interaction();
	if (combo_open) {
		for (const auto &[name, item_value] : items) {
			const bool selected{ *value == item_value };
			if (ImGui::Selectable(name.c_str(), selected)) {
				*value = item_value;
				changed = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopID();
	ImGui::Dummy({ 0.0f, vertical_padding });
	return changed;
}

bool select_multi(const char *const label, const std::vector<std::pair<std::string, bool *>> &items)
{
	if (!label || items.empty()) {
		return false;
	}
	std::string preview{};
	for (const auto &[name, selected] : items) {
		if (selected && *selected) {
			if (!preview.empty()) preview += ", ";
			preview += name;
		}
	}
	if (preview.empty()) preview = "none";
	bool changed{};
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SetNextItemWidth(-1.0f);
	const bool combo_open{ ImGui::BeginCombo("##value", preview.c_str()) };
	remember_item_interaction();
	if (combo_open) {
		for (const auto &[name, selected] : items) {
			if (!selected) continue;
			ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
			if (ImGui::Checkbox(name.c_str(), selected)) changed = true;
			ImGui::PopItemFlag();
		}
		ImGui::EndCombo();
	}
	ImGui::PopID();
	ImGui::Dummy({ 0.0f, vertical_padding });
	return changed;
}

bool slider_int(const char *const label, int *const value, const int minimum, const int maximum, const char *const format)
{
	if (!label || !value) return false;
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SetNextItemWidth(-1.0f);
	const bool changed{ ImGui::SliderInt("##value", value, minimum, maximum, format) };
	remember_item_interaction();
	ImGui::PopID();
	ImGui::Dummy({ 0.0f, vertical_padding });
	return changed;
}

bool slider_float(const char *const label, float *const value, const float minimum, const float maximum, const char *const format)
{
	if (!label || !value) return false;
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SetNextItemWidth(-1.0f);
	const bool changed{ ImGui::SliderFloat("##value", value, minimum, maximum, format) };
	remember_item_interaction();
	ImGui::PopID();
	ImGui::Dummy({ 0.0f, vertical_padding });
	return changed;
}

bool color_picker(const char *const label, rgba8 *const value, bool *const rainbow)
{
	if (!label || !value) return false;
	std::array color{
		value->r / 255.0f,
		value->g / 255.0f,
		value->b / 255.0f,
		value->a / 255.0f
	};
	ImVec4 preview{ color[0], color[1], color[2], color[3] };
	if (rainbow && *rainbow) {
		const float hue{ std::fmod(static_cast<float>(ImGui::GetTime()) * 0.15f, 1.0f) };
		ImGui::ColorConvertHSVtoRGB(hue, 0.95f, 0.95f, preview.x, preview.y, preview.z);
	}
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SameLine();
	ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 34.0f));
	if (ImGui::ColorButton("##preview", preview, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf)) {
		ImGui::OpenPopup("##picker");
	}
	remember_item_interaction();
	bool changed{};
	if (ImGui::BeginPopup("##picker")) {
		ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 12.0f);
		changed = ImGui::ColorPicker4(
			"##picker",
			color.data(),
			ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar);
		if (rainbow) {
			changed = toggle("Rainbow", rainbow) || changed;
		}
		ImGui::EndPopup();
	}
	if (changed) {
		value->r = static_cast<uint8_t>(std::clamp(color[0], 0.0f, 1.0f) * 255.0f);
		value->g = static_cast<uint8_t>(std::clamp(color[1], 0.0f, 1.0f) * 255.0f);
		value->b = static_cast<uint8_t>(std::clamp(color[2], 0.0f, 1.0f) * 255.0f);
		value->a = static_cast<uint8_t>(std::clamp(color[3], 0.0f, 1.0f) * 255.0f);
	}
	ImGui::PopID();
	ImGui::Dummy({ 0.0f, vertical_padding });
	return changed;
}

bool input_string(const char *const label, std::string *const value, const ImGuiInputTextFlags flags)
{
	return input_text(label, value, nullptr, {}, flags, false);
}

bool input_string_with_hint(const char *const label, std::string *const value, const char *const hint, const ImGuiInputTextFlags flags)
{
	return input_text(label, value, hint, {}, flags, false);
}

bool input_string_multiline(const char *const label, std::string *const value, const ImVec2 size, const ImGuiInputTextFlags flags)
{
	return input_text(label, value, nullptr, size, flags, true);
}

bool input_key(const char *const label, int *const value)
{
	if (!label || !value || !configured_input.state) {
		return false;
	}
	static int *active{};
	static bool wait_for_release{};
	ImGui::PushID(label);
	const bool pressed{ ImGui::Button(active == value ? "press a key..." : configured_input.name ? configured_input.name(*value).c_str() : "bind key", { -1.0f, 0.0f }) };
	remember_item_interaction();
	if (pressed) {
		active = value;
		wait_for_release = true;
	}
	bool changed{};
	if (active == value) {
		if (wait_for_release) {
			if (configured_input.state(configured_input.opening_mouse_key).released) wait_for_release = false;
		}
		else {
			for (int key{ configured_input.first_key }; key <= configured_input.last_key; ++key) {
				if (!configured_input.state(key).pressed) continue;
				*value = key == configured_input.escape_key ? 0 : key;
				active = nullptr;
				changed = true;
				break;
			}
			if (active == value && configured_input.first_mouse_key != 0) {
				for (int key{ configured_input.first_mouse_key }; key >= configured_input.last_mouse_key; --key) {
					if (!configured_input.state(key).pressed) continue;
					*value = key;
					active = nullptr;
					changed = true;
					break;
				}
			}
		}
	}
	ImGui::PopID();
	ImGui::Dummy({ 0.0f, vertical_padding });
	return changed;
}

void begin_panel(const char *const label, const ImVec2 size, const ImGuiWindowFlags window_flags)
{
	if (!label) {
		return;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.0f, 8.0f });
	ImGui::BeginChild(label, size, ImGuiChildFlags_Border, window_flags);
	ImGui::TextUnformatted(label);
	ImGui::Separator();
}

void end_panel()
{
	ImGui::EndChild();
	ImGui::PopStyleVar();
}

void group_separator()
{
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

void control_spacing()
{
	ImGui::Dummy({ 0.0f, 6.0f });
}
}
