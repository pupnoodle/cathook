#pragma once

#include "config.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace mono
{
struct rgba8 final
{
	uint8_t r{};
	uint8_t g{};
	uint8_t b{};
	uint8_t a{ 255 };
};

struct key_state final
{
	bool pressed{};
	bool held{};
	bool released{};
};

struct item_interaction final
{
	bool hovered{};
};

struct input_adapter final
{
	std::function<key_state(int)> state{};
	std::function<std::string(int)> name{};
	int first_key{};
	int last_key{ 255 };
	int first_mouse_key{};
	int last_mouse_key{};
	int escape_key{ 27 };
	int opening_mouse_key{ 1 };
};

void set_input_adapter(input_adapter adapter);
item_interaction last_item_interaction();

class disabled_scope final
{
public:
	explicit disabled_scope(bool disabled);
	~disabled_scope();

	disabled_scope(const disabled_scope &) = delete;
	disabled_scope &operator=(const disabled_scope &) = delete;

private:
	bool m_disabled{};
};

bool toggle(const char *label, bool *value);
bool button(const char *label, ImVec2 size = {}, bool danger = false);
bool list_item(const char *label, bool selected, ImVec2 size = {});
bool select_single(const char *label, int *value, const std::vector<std::pair<std::string, int>> &items);
bool select_multi(const char *label, const std::vector<std::pair<std::string, bool *>> &items);
bool slider_int(const char *label, int *value, int minimum, int maximum, const char *format = "%d");
bool slider_float(const char *label, float *value, float minimum, float maximum, const char *format = "%.3f");
bool color_picker(const char *label, rgba8 *value, bool *rainbow = nullptr);
bool input_string(const char *label, std::string *value, ImGuiInputTextFlags flags = 0);
bool input_string_with_hint(const char *label, std::string *value, const char *hint, ImGuiInputTextFlags flags = 0);
bool input_string_multiline(const char *label, std::string *value, ImVec2 size, ImGuiInputTextFlags flags = 0);
bool input_key(const char *label, int *value);
void begin_panel(const char *label, ImVec2 size = {}, ImGuiWindowFlags window_flags = 0);
void end_panel();
void group_separator();
void control_spacing();
}
