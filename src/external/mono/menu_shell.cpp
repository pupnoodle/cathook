#include "menu_shell.hpp"
#include "window_chrome.hpp"

#include <algorithm>
#include <cmath>

namespace mono
{
namespace
{
void drag_from_header(const menu_config &config, menu_state &state, const ImVec2 minimum, const ImVec2 maximum);

void render_header(const menu_config &config, const header_status &status, menu_state &state,
	const ImVec2 minimum, const ImVec2 maximum)
{
	const ImVec2 position{ ImGui::GetCursorScreenPos() };
	const ImVec2 size{ std::max(ImGui::GetContentRegionAvail().x, 1.0f), window_header_height() };
	const float action_width{ !status.value.empty() && !status.action.empty() && status.on_action ? 44.0f * window_scale() : 0.0f };
	ImGui::InvisibleButton("##menu_header", { std::max(size.x - action_width, 1.0f), size.y });
	ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
	draw_window_header(*draw_list, position, size.x, { status.value.empty() ? config.title : status.label, status.value, true });
	drag_from_header(config, state, minimum, maximum);

	if (!status.value.empty()) {
		if (!status.action.empty() && status.on_action) {
			const float scale{ window_scale() };
			ImGui::SetCursorScreenPos({ position.x + size.x - 42.0f * scale, position.y + 2.0f * scale });
			if (ImGui::Button(status.action.data(), { 38.0f * scale, 18.0f * scale })) {
				status.on_action();
			}
		}
	}
}

void drag_from_header(const menu_config &config, menu_state &state, const ImVec2 minimum, const ImVec2 maximum)
{
	if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		if (config.on_drag_header) {
			config.on_drag_header();
			return;
		}
		if (config.viewport_padding > 0.0f) {
			state.dragging = true;
			const ImVec2 mouse{ ImGui::GetMousePos() };
			const ImVec2 window{ ImGui::GetWindowPos() };
			state.drag_offset = { mouse.x - window.x, mouse.y - window.y };
		}
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		state.dragging = false;
	}
	if (state.dragging && config.viewport_padding > 0.0f) {
		const ImVec2 mouse{ ImGui::GetMousePos() };
		const ImVec2 target{ mouse.x - state.drag_offset.x, mouse.y - state.drag_offset.y };
		ImGui::SetWindowPos({
			std::clamp(target.x, minimum.x, maximum.x),
			std::clamp(target.y, minimum.y, maximum.y)
		});
	}
}

bool menu_body_hovered_for_wheel()
{
	ImGuiWindow *const body{ GImGui->CurrentWindow };
	for (ImGuiWindow *window{ GImGui->HoveredWindow }; window != nullptr; window = window->ParentWindow) {
		if (window == GImGui->WheelingWindow) {
			return false;
		}
		if (window == body) {
			return true;
		}
	}
	return false;
}

void smooth_menu_scroll(menu_state &state)
{
	const float current{ ImGui::GetScrollY() };
	const float maximum{ std::max(ImGui::GetScrollMaxY(), 0.0f) };
	const ImGuiIO &io{ ImGui::GetIO() };

	if (!state.scroll_initialized) {
		state.scroll_position = current;
		state.scroll_target = current;
		state.scroll_initialized = true;
	}

	if (io.MouseWheel != 0.0f && menu_body_hovered_for_wheel()) {
		state.scroll_target -= io.MouseWheel * ImGui::GetFontSize() * 7.0f;
	} else if (io.MouseWheel == 0.0f && std::abs(current - state.scroll_position) > 0.001f) {
		state.scroll_position = current;
		state.scroll_target = current;
	}

	state.scroll_target = std::clamp(state.scroll_target, 0.0f, maximum);
	const float delta_time{ std::clamp(io.DeltaTime, 0.0f, 0.1f) };
	const float amount{ 1.0f - std::exp(-30.0f * delta_time) };
	state.scroll_position = std::lerp(state.scroll_position, state.scroll_target, amount);
	if (std::abs(state.scroll_target - state.scroll_position) < 0.1f) {
		state.scroll_position = state.scroll_target;
	}
	ImGui::SetScrollY(state.scroll_position);
}
}

bool begin_menu(const menu_config &config, menu_state &state, const std::function<void()> &render_navbar,
	const std::function<void()> &render_subnavbar, const header_status &status)
{
	const float scale{ window_scale() };
	const float padding{ config.viewport_padding * scale };
	const ImGuiViewport *const viewport{ ImGui::GetMainViewport() };
	const ImVec2 available{
		std::max(1.0f, viewport->WorkSize.x - padding * 2.0f),
		std::max(1.0f, viewport->WorkSize.y - padding * 2.0f)
	};
	const ImVec2 size{
		std::min(config.preferred_size.x * scale, available.x),
		std::min(config.preferred_size.y * scale, available.y)
	};
	const ImVec2 minimum{ viewport->WorkPos.x + padding, viewport->WorkPos.y + padding };
	const ImVec2 maximum{
		viewport->WorkPos.x + viewport->WorkSize.x - padding - size.x,
		viewport->WorkPos.y + viewport->WorkSize.y - padding - size.y
	};
	state.position = {
		std::clamp(state.position.x, minimum.x, maximum.x),
		std::clamp(state.position.y, minimum.y, maximum.y)
	};

	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(config.background_alpha);
	if (config.viewport_padding == 0.0f || !state.position_initialized) {
		ImGui::SetNextWindowPos(config.viewport_padding == 0.0f ? minimum : state.position, ImGuiCond_Always);
		state.position_initialized = true;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, config.viewport_padding > 0.0f ? 4.0f * scale : 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 2.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, config.viewport_padding > 0.0f ? 1.0f : 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 4.0f * scale, 2.0f * scale });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 4.0f * scale, 4.0f * scale });
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 2.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 2.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 2.0f * scale);
	ImVec4 group_background{ ImGui::GetStyleColorVec4(ImGuiCol_ChildBg) };
	group_background.w = 0.52f;
	ImVec4 group_border{ ImGui::GetStyleColorVec4(ImGuiCol_Border) };
	group_border.w = 0.38f;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, group_background);
	ImGui::PushStyleColor(ImGuiCol_Border, group_border);

	const bool visible{ ImGui::Begin(config.id, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar) };
	if (!visible) {
		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(10);
		return false;
	}

	render_header(config, status, state, minimum, maximum);
	state.position = ImGui::GetWindowPos();
	ImGui::SetCursorPos({ 0.0f, window_header_height() });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.0f * scale, 4.0f * scale });
	if (ImGui::BeginChild("menu_navbar", { 0.0f, config.navbar_height * scale }, ImGuiChildFlags_Border,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 2.0f * scale, 0.0f });
		render_navbar();
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();
	if (config.show_subnavbar && render_subnavbar) {
		if (ImGui::BeginChild("menu_subnavbar", { 0.0f, config.subnavbar_height * scale }, ImGuiChildFlags_Border,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 2.0f * scale, 0.0f });
			render_subnavbar();
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
	}
	ImGui::PopStyleVar();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.0f * scale, 8.0f * scale });
	ImGui::BeginChild(
		"menu_body",
		{ 0.0f, 0.0f },
		ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollWithMouse);
	smooth_menu_scroll(state);
	return true;
}

void end_menu(const bool content_visible)
{
	if (content_visible) {
		ImGui::EndChild();
		ImGui::PopStyleVar();
	}
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(10);
}

bool navbar_entry(const char *const label, const bool active, const char *const icon, ImFont *const icon_font)
{
	const ImVec2 position{ ImGui::GetCursorScreenPos() };
	const float scale{ window_scale() };
	const float icon_width{ icon_font && icon ? 20.0f * scale : 0.0f };
	const float text_width{ ImGui::CalcTextSize(label).x };
	const ImVec2 size{ std::max(16.0f * scale + icon_width + text_width, 1.0f), 28.0f * scale };
	ImGui::PushID(label);
	const bool pressed{ ImGui::InvisibleButton("##entry", size) };
	const bool hovered{ ImGui::IsItemHovered() };
	ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
	if (active || hovered) {
		ImVec4 fill{ ImGui::GetStyleColorVec4(active ? ImGuiCol_CheckMark : ImGuiCol_ChildBg) };
		fill.w = active ? 0.16f : 0.58f;
		draw_list->AddRectFilled(position, { position.x + size.x, position.y + size.y }, ImGui::ColorConvertFloat4ToU32(fill), 2.0f * scale);
	}
	if (icon_font && icon) {
		draw_list->AddText(icon_font, 15.0f * scale, { position.x + 8.0f * scale, position.y + (size.y - 15.0f * scale) * 0.5f }, ImGui::GetColorU32(active || hovered ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled), icon);
	}
	const float text_offset{ icon_font && icon ? 8.0f * scale + icon_width : 8.0f * scale };
	draw_list->AddText({ position.x + text_offset, position.y + (size.y - ImGui::GetFontSize()) * 0.5f }, ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
	ImGui::SameLine(0.0f, 2.0f * scale);
	ImGui::PopID();
	return pressed;
}

bool subnavbar_entry(const char *const label, const bool active, const char *const icon, ImFont *const icon_font)
{
	const ImVec2 position{ ImGui::GetCursorScreenPos() };
	const float scale{ window_scale() };
	const float icon_width{ icon_font && icon ? 17.0f * scale : 0.0f };
	const float text_width{ ImGui::CalcTextSize(label).x };
	const ImVec2 size{ std::max(14.0f * scale + icon_width + text_width, 1.0f), 21.0f * scale };
	ImGui::PushID(label);
	const bool pressed{ ImGui::InvisibleButton("##subentry", size) };
	const bool hovered{ ImGui::IsItemHovered() };
	ImDrawList *const draw_list{ ImGui::GetWindowDrawList() };
	if (active || hovered) {
		ImVec4 fill{ ImGui::GetStyleColorVec4(active ? ImGuiCol_CheckMark : ImGuiCol_ChildBg) };
		fill.w = active ? 0.12f : 0.55f;
		draw_list->AddRectFilled(position, { position.x + size.x, position.y + size.y }, ImGui::ColorConvertFloat4ToU32(fill), 2.0f * scale);
	}
	if (icon_font && icon) {
		draw_list->AddText(icon_font, 13.0f * scale, { position.x + 7.0f * scale, position.y + (size.y - 13.0f * scale) * 0.5f }, ImGui::GetColorU32(active ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled), icon);
	}
	const float text_offset{ icon_font && icon ? 7.0f * scale + icon_width : 7.0f * scale };
	draw_list->AddText({ position.x + text_offset, position.y + (size.y - ImGui::GetFontSize()) * 0.5f }, ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
	ImGui::SameLine(0.0f, 2.0f * scale);
	ImGui::PopID();
	return pressed;
}

void begin_transparent_child(const char *const id, const ImVec2 size)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.0f, 0.0f, 0.0f, 0.0f });
	ImGui::BeginChild(id, size, ImGuiChildFlags_None);
	ImGui::PopStyleColor();
}
}
