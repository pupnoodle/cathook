#include "theme.hpp"
#include "window_chrome.hpp"

#include <algorithm>
#include <cmath>

namespace mono
{
namespace
{
ImVec4 mix(const ImVec4 &from, const ImVec4 &to, const float amount)
{
	const float value{ std::clamp(amount, 0.0f, 1.0f) };
	return {
		std::lerp(from.x, to.x, value),
		std::lerp(from.y, to.y, value),
		std::lerp(from.z, to.z, value),
		std::lerp(from.w, to.w, value)
	};
}
}

void apply_layout()
{
	const float scale{ window_scale() };
	ImGuiStyle &style{ ImGui::GetStyle() };
	style.WindowPadding = { 0.0f, 0.0f };
	style.FramePadding = { 8.0f * scale, 5.0f * scale };
	style.ItemSpacing = { 8.0f * scale, 8.0f * scale };
	style.ItemInnerSpacing = { 6.0f * scale, 4.0f * scale };
	style.TouchExtraPadding = { 0.0f, 0.0f };
	style.IndentSpacing = 20.0f * scale;
	style.ScrollbarSize = 10.0f * scale;
	style.GrabMinSize = 10.0f * scale;
	style.WindowBorderSize = 0.0f;
	style.ChildBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.TabBorderSize = 0.0f;
	style.TabBarBorderSize = 0.0f;
	style.TabBarOverlineSize = 2.0f;
	style.WindowRounding = 10.0f * scale;
	style.ChildRounding = 7.0f * scale;
	style.FrameRounding = 6.0f * scale;
	style.PopupRounding = 7.0f * scale;
	style.ScrollbarRounding = 6.0f * scale;
	style.GrabRounding = 6.0f * scale;
	style.TabRounding = 6.0f * scale;
	style.AntiAliasedFill = true;
	style.AntiAliasedLines = true;
	style.AntiAliasedLinesUseTex = true;
}

void apply_colors(const color accent_value)
{
	ImVec4 *const colors{ ImGui::GetStyle().Colors };
	const ImVec4 accent{ accent_value.r, accent_value.g, accent_value.b, 1.0f };
	const ImVec4 black{ 0.015f, 0.017f, 0.020f, 1.0f };
	const ImVec4 white{ 0.90f, 0.90f, 0.90f, 1.0f };
	const ImVec4 background{ mix(black, accent, 0.14f) };
	const ImVec4 active{ mix(white, accent, 0.08f) };
	const ImVec4 inactive{ mix(background, active, 0.55f) };
	const ImVec4 surface_low{ mix(background, accent, 0.08f) };
	const ImVec4 surface_mid{ mix(background, accent, 0.15f) };
	const ImVec4 surface_high{ mix(background, accent, 0.24f) };
	const ImVec4 border{ mix(background, accent, 0.46f) };
	const ImVec4 accent_low{ mix(background, accent, 0.30f) };
	const ImVec4 accent_mid{ mix(background, accent, 0.46f) };

	colors[ImGuiCol_Text] = active;
	colors[ImGuiCol_TextDisabled] = inactive;
	colors[ImGuiCol_ScrollbarBg] = { background.x, background.y, background.z, 0.0f };
	colors[ImGuiCol_WindowBg] = { background.x, background.y, background.z, 0.91f };
	colors[ImGuiCol_ChildBg] = { surface_low.x, surface_low.y, surface_low.z, 0.78f };
	colors[ImGuiCol_PopupBg] = { surface_mid.x, surface_mid.y, surface_mid.z, 0.96f };
	colors[ImGuiCol_Border] = border;
	colors[ImGuiCol_FrameBg] = surface_mid;
	colors[ImGuiCol_FrameBgHovered] = surface_high;
	colors[ImGuiCol_FrameBgActive] = accent_low;
	colors[ImGuiCol_TitleBg] = accent_low;
	colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_CheckMark] = accent;
	colors[ImGuiCol_ScrollbarGrab] = colors[ImGuiCol_Border];
	colors[ImGuiCol_ScrollbarGrabHovered] = accent_mid;
	colors[ImGuiCol_ScrollbarGrabActive] = accent;
	colors[ImGuiCol_NavHighlight] = accent;
	colors[ImGuiCol_DragDropTarget] = accent;
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = mix(accent, active, 0.18f);
	colors[ImGuiCol_Button] = surface_mid;
	colors[ImGuiCol_ButtonHovered] = accent_low;
	colors[ImGuiCol_ButtonActive] = accent_mid;
	colors[ImGuiCol_Header] = surface_mid;
	colors[ImGuiCol_HeaderHovered] = accent_low;
	colors[ImGuiCol_HeaderActive] = accent_mid;
	colors[ImGuiCol_Separator] = colors[ImGuiCol_WindowBg];
	colors[ImGuiCol_SeparatorHovered] = colors[ImGuiCol_WindowBg];
	colors[ImGuiCol_SeparatorActive] = colors[ImGuiCol_WindowBg];
	colors[ImGuiCol_ResizeGrip] = colors[ImGuiCol_Button];
	colors[ImGuiCol_ResizeGripHovered] = colors[ImGuiCol_ButtonHovered];
	colors[ImGuiCol_ResizeGripActive] = colors[ImGuiCol_ButtonActive];
	colors[ImGuiCol_TabHovered] = colors[ImGuiCol_ButtonHovered];
	colors[ImGuiCol_Tab] = colors[ImGuiCol_WindowBg];
	colors[ImGuiCol_TabSelected] = colors[ImGuiCol_ButtonActive];
	colors[ImGuiCol_TableHeaderBg] = colors[ImGuiCol_FrameBg];
	colors[ImGuiCol_TableRowBg] = colors[ImGuiCol_ChildBg];
	colors[ImGuiCol_TableRowBgAlt] = colors[ImGuiCol_FrameBg];
}
}
