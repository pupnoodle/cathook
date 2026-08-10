#ifndef BINDS_HPP
#define BINDS_HPP
#include "config.hpp"
#include "core/config/config_store.hpp"
#include "imgui/imgui.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cat_bind
{

inline bool disabled()
{
  return are_binds_disabled();
}

enum class value_type
{
  boolean,
  integer,
  floating,
  color,
  string
};

enum class widget_type
{
  checkbox,
  combo_int,
  slider_int,
  slider_float,
  color_picker,
  string_input
};

enum class bind_condition
{
  key,
  player_class,
  weapon_type,
  item_slot,
  misc
};

enum class bind_key_mode
{
  hold,
  toggle,
  double_click
};

enum class bind_visibility
{
  always,
  while_active,
  hidden
};

using bind_value = std::variant<bool, int, float, RGBA_float, std::string>;

struct target_entry
{
  std::string target_key{};
  std::string label{};
  std::string default_label{};
  void* target{};
  value_type type{ value_type::boolean };
  widget_type widget{ widget_type::checkbox };
  bind_value baseline{ false };
  bind_value last_effective{ false };
  bool baseline_initialized{};
  bool overridden{};
  bool changed_in_menu{};
  int int_min{};
  int int_max{};
  float float_min{};
  float float_max{};
  std::string format{};
  std::vector<std::pair<std::string, int>> options{};
};

struct bind_entry
{
  uint32_t id{};
  uint32_t parent_id{};
  std::string name{ "new bind" };
  bind_condition condition{ bind_condition::key };
  bind_key_mode key_mode{ bind_key_mode::hold };
  int key{ SDLK_UNKNOWN };
  int condition_value{};
  bool enabled{ true };
  bool inverted{};
  bool active{};
  bool toggle_state{};
  bool was_down{};
  bool waiting{};
  bind_visibility visibility{ bind_visibility::always };
  double last_press_time{};
  std::unordered_map<std::string, bind_value> overrides{};
};

enum class popup_target_type
{
  value_bind
};

struct indicator_row
{
  std::string label{};
  std::string key{};
  std::string state{};
  std::string target_key{};
  popup_target_type popup_type{ popup_target_type::value_bind };
  bool active{};
};

inline std::vector<bind_entry>& entries()
{
  static std::vector<bind_entry> value{};
  return value;
}

inline std::vector<target_entry>& targets()
{
  static std::vector<target_entry> value{};
  return value;
}

inline std::unordered_map<void*, std::string>& pointer_to_key()
{
  static std::unordered_map<void*, std::string> value{};
  return value;
}

inline bool& targets_warmed();

inline void clear_registered_targets()
{
  targets().clear();
  pointer_to_key().clear();
  targets_warmed() = false;
}

inline uint32_t& next_id()
{
  static uint32_t value{ 1 };
  return value;
}

inline uint32_t& editing_id()
{
  static uint32_t value{};
  return value;
}

inline bool& menu_open_state()
{
  static bool value{};
  return value;
}

inline bool& autosave_dirty()
{
  static bool value{};
  return value;
}

inline std::recursive_mutex& bind_mutex()
{
  static std::recursive_mutex value{};
  return value;
}

inline bool& target_registration_pass()
{
  static bool value{};
  return value;
}

inline bool& targets_warmed()
{
  static bool value{};
  return value;
}

inline bool registering_targets()
{
  return target_registration_pass();
}

class target_registration_scope final
{
public:
  target_registration_scope() : m_previous{ target_registration_pass() }
  {
    target_registration_pass() = true;
  }

  ~target_registration_scope()
  {
    target_registration_pass() = m_previous;
  }

private:
  bool m_previous{};
};

inline std::array<bool, SDL_NUM_SCANCODES>& keyboard_down()
{
  static std::array<bool, SDL_NUM_SCANCODES> value{};
  return value;
}

inline std::array<bool, SDL_NUM_SCANCODES>& keyboard_seen()
{
  static std::array<bool, SDL_NUM_SCANCODES> value{};
  return value;
}

inline std::array<bool, SDL_BUTTON_X2 + 1>& mouse_down()
{
  static std::array<bool, SDL_BUTTON_X2 + 1> value{};
  return value;
}

inline std::array<bool, SDL_BUTTON_X2 + 1>& mouse_seen()
{
  static std::array<bool, SDL_BUTTON_X2 + 1> value{};
  return value;
}

inline bool raw_key_down(const int key)
{
  if (key == SDLK_UNKNOWN) return false;
  if (key >= 0) {
    int key_count{};
    const Uint8* state = SDL_GetKeyboardState(&key_count);
    if (key < SDL_NUM_SCANCODES && keyboard_seen()[static_cast<size_t>(key)]) {
      return keyboard_down()[static_cast<size_t>(key)];
    }
    return state != nullptr && key < key_count && state[key] != 0;
  }
  const int button = -key;
  if (button <= 0 || button > SDL_BUTTON_X2) return false;
  if (mouse_seen()[static_cast<size_t>(button)]) {
    return mouse_down()[static_cast<size_t>(button)];
  }
  const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
  return (mouse & SDL_BUTTON(button)) != 0;
}

inline void mark_dirty()
{
  autosave_dirty() = true;
}

inline void autosave_if_dirty();
inline void capture_menu_changes();
inline void restore_active_overrides();

inline void set_menu_open(const bool open)
{
  std::lock_guard lock{ bind_mutex() };
  if (menu_open_state() == open) return;

  if (open) restore_active_overrides();
  if (!open && menu_open_state()) capture_menu_changes();

  menu_open_state() = open;
  for (bind_entry& entry : entries()) {
    entry.was_down = raw_key_down(entry.key);
    entry.active = false;
    entry.last_press_time = 0.0;
  }
}

inline bind_entry* find_entry(const uint32_t id)
{
  const auto iterator = std::ranges::find(entries(), id, &bind_entry::id);
  return iterator == entries().end() ? nullptr : &*iterator;
}

inline target_entry* find_target(const std::string_view key)
{
  for (target_entry& target : targets()) {
    if (target.target_key == key) return &target;
  }
  return nullptr;
}

inline const char* condition_label(const bind_condition condition)
{
  switch (condition) {
  case bind_condition::key: return "key";
  case bind_condition::player_class: return "class";
  case bind_condition::weapon_type: return "weapon type";
  case bind_condition::item_slot: return "item slot";
  case bind_condition::misc: return "misc";
  }
  return "key";
}

inline const char* visibility_label(const bind_visibility visibility)
{
  switch (visibility) {
  case bind_visibility::always: return "always";
  case bind_visibility::while_active: return "while active";
  case bind_visibility::hidden: return "hidden";
  }
  return "always";
}

inline const char* mode_label(const bind_key_mode mode)
{
  switch (mode) {
  case bind_key_mode::hold: return "hold";
  case bind_key_mode::toggle: return "toggle";
  case bind_key_mode::double_click: return "double click";
  }
  return "hold";
}

inline std::string& popup_target_key()
{
  static std::string value{};
  return value;
}

inline popup_target_type& popup_target_type_value()
{
  static popup_target_type value{ popup_target_type::value_bind };
  return value;
}

inline bool& popup_open_requested()
{
  static bool value{};
  return value;
}

inline std::vector<std::string>& panel_label_stack()
{
  static std::vector<std::string> value{};
  return value;
}

inline void push_panel_label(const std::string& name)
{
  if (!disabled()) panel_label_stack().push_back(name);
}

inline void pop_panel_label()
{
  if (!disabled() && !panel_label_stack().empty()) panel_label_stack().pop_back();
}

inline std::string current_panel_path()
{
  std::string out{};
  for (const std::string& name : panel_label_stack()) {
    if (!out.empty()) out += '/';
    out += name;
  }
  return out;
}

inline std::string make_target_key_from_label(const char* label)
{
  std::string key = current_panel_path();
  if (!key.empty()) key += '/';
  key += label != nullptr ? label : "";
  for (char& character : key) {
    if (character == ' ') character = '_';
    else if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
  }
  return key;
}

inline value_type get_value_type(bool*) { return value_type::boolean; }
inline value_type get_value_type(int*) { return value_type::integer; }
inline value_type get_value_type(float*) { return value_type::floating; }
inline value_type get_value_type(RGBA_float*) { return value_type::color; }
inline value_type get_value_type(std::string*) { return value_type::string; }

inline bind_value read_value(const target_entry& target)
{
  switch (target.type) {
  case value_type::boolean: return *static_cast<bool*>(target.target);
  case value_type::integer: return *static_cast<int*>(target.target);
  case value_type::floating: return *static_cast<float*>(target.target);
  case value_type::color: return *static_cast<RGBA_float*>(target.target);
  case value_type::string: return *static_cast<std::string*>(target.target);
  }
  return false;
}

inline bind_value read_value(void* target, const value_type type)
{
  switch (type) {
  case value_type::boolean: return *static_cast<bool*>(target);
  case value_type::integer: return *static_cast<int*>(target);
  case value_type::floating: return *static_cast<float*>(target);
  case value_type::color: return *static_cast<RGBA_float*>(target);
  case value_type::string: return *static_cast<std::string*>(target);
  }
  return false;
}

inline void relink_saved_overrides(const target_entry& target)
{
  const std::size_t separator = target.target_key.rfind('/');
  const std::string suffix = separator == std::string::npos ? target.target_key : target.target_key.substr(separator);
  for (bind_entry& bind : entries()) {
    if (bind.overrides.contains(target.target_key)) continue;
    for (auto iterator = bind.overrides.begin(); iterator != bind.overrides.end(); ++iterator) {
      const bool saved_is_dynamic = iterator->first.find("/group_") != std::string::npos || iterator->first.find("/layer_") != std::string::npos;
      if (iterator->first == target.target_key || (!saved_is_dynamic && iterator->first.size() >= suffix.size() && iterator->first.ends_with(suffix))) {
        bind.overrides[target.target_key] = iterator->second;
        bind.overrides.erase(iterator);
        break;
      }
    }
  }
}

inline void write_value(target_entry& target, const bind_value& value)
{
  switch (target.type) {
  case value_type::boolean:
    if (const bool* item = std::get_if<bool>(&value)) *static_cast<bool*>(target.target) = *item;
    break;
  case value_type::integer:
    if (const int* item = std::get_if<int>(&value)) {
      const bool bounded = target.widget == widget_type::slider_int ||
        (target.widget == widget_type::combo_int && !target.options.empty());
      *static_cast<int*>(target.target) = bounded
        ? std::clamp(*item, target.int_min, target.int_max) : *item;
    } else if (const float* item = std::get_if<float>(&value)) {
      const int converted = static_cast<int>(*item);
      const bool bounded = target.widget == widget_type::slider_int ||
        (target.widget == widget_type::combo_int && !target.options.empty());
      *static_cast<int*>(target.target) = bounded
        ? std::clamp(converted, target.int_min, target.int_max) : converted;
    }
    break;
  case value_type::floating:
    if (const float* item = std::get_if<float>(&value)) {
      *static_cast<float*>(target.target) = target.widget == widget_type::slider_float
        ? std::clamp(*item, target.float_min, target.float_max) : *item;
    } else if (const int* item = std::get_if<int>(&value)) {
      const float converted = static_cast<float>(*item);
      *static_cast<float*>(target.target) = target.widget == widget_type::slider_float
        ? std::clamp(converted, target.float_min, target.float_max) : converted;
    }
    break;
  case value_type::color:
    if (const RGBA_float* item = std::get_if<RGBA_float>(&value)) *static_cast<RGBA_float*>(target.target) = *item;
    break;
  case value_type::string:
    if (const std::string* item = std::get_if<std::string>(&value)) *static_cast<std::string*>(target.target) = *item;
    break;
  }
}

inline bool values_equal(const bind_value& left, const bind_value& right)
{
  if (left.index() != right.index()) return false;
  if (const auto* item = std::get_if<bool>(&left)) return *item == std::get<bool>(right);
  if (const auto* item = std::get_if<int>(&left)) return *item == std::get<int>(right);
  if (const auto* item = std::get_if<float>(&left)) return *item == std::get<float>(right);
  if (const auto* item = std::get_if<RGBA_float>(&left)) {
    const RGBA_float& other = std::get<RGBA_float>(right);
    return item->r == other.r && item->g == other.g && item->b == other.b && item->a == other.a && item->rainbow == other.rainbow;
  }
  return std::get<std::string>(left) == std::get<std::string>(right);
}

template <typename value_t>
inline target_entry* ensure_entry(value_t* target, const char* label)
{
  if (disabled() || target == nullptr) return nullptr;

  auto pointer = pointer_to_key().find(target);
  if (pointer == pointer_to_key().end()) {
    const std::string target_key = make_target_key_from_label(label);
    pointer_to_key()[target] = target_key;
    pointer = pointer_to_key().find(target);
  }

  target_entry* entry = find_target(pointer->second);
  if (entry == nullptr) {
    targets().push_back({
      .target_key = pointer->second,
      .label = label != nullptr ? label : "",
      .default_label = label != nullptr ? label : "",
      .target = target,
      .type = get_value_type(target),
      .baseline = read_value(target, get_value_type(target)),
      .last_effective = read_value(target, get_value_type(target)),
      .baseline_initialized = true,
      .overridden = false
    });
    entry = &targets().back();
  }

  entry->target = target;
  entry->type = get_value_type(target);
  if (entry->label.empty() || entry->label == entry->default_label) entry->label = label != nullptr ? label : "";
  entry->default_label = label != nullptr ? label : "";
  relink_saved_overrides(*entry);
  return entry;
}

template <typename value_t>
inline void register_target_metadata(value_t* target, const char* label, const widget_type widget,
                                     const bool changed, const int int_min = 0, const int int_max = 0,
                                     const float float_min = 0.0f, const float float_max = 0.0f,
                                     const char* format = nullptr, const char* const items[] = nullptr,
                                     const int item_count = 0)
{
  target_entry* entry = ensure_entry(target, label);
  if (entry == nullptr) return;
  entry->widget = widget;
  entry->int_min = int_min;
  entry->int_max = int_max;
  entry->float_min = float_min;
  entry->float_max = float_max;
  entry->format = format != nullptr ? format : (entry->type == value_type::floating ? "%.3f" : "%d");
  if (items != nullptr && item_count > 0) {
    entry->options.clear();
    for (int index = 0; index < item_count; ++index) entry->options.emplace_back(items[index] != nullptr ? items[index] : "", index);
  }

  if (changed) {
    entry->baseline = read_value(*entry);
    entry->last_effective = entry->baseline;
    entry->baseline_initialized = true;
    entry->overridden = false;
    entry->changed_in_menu = true;
  }
}

inline uint32_t add(std::string name = "new bind", const uint32_t parent_id = 0)
{
  std::lock_guard lock{ bind_mutex() };
  bind_entry entry{};
  entry.id = next_id()++;
  entry.parent_id = find_entry(parent_id) != nullptr ? parent_id : 0;
  entry.name = std::move(name);
  entries().push_back(std::move(entry));
  mark_dirty();
  return entries().back().id;
}

inline uint32_t add_and_capture_key(std::string name = "new bind", const uint32_t parent_id = 0)
{
  std::lock_guard lock{ bind_mutex() };
  const uint32_t id = add(std::move(name), parent_id);
  for (bind_entry& entry : entries()) entry.waiting = false;
  if (bind_entry* entry = find_entry(id)) {
    entry->waiting = true;
    mark_dirty();
  }
  return id;
}

inline bool remove(const uint32_t id)
{
  std::lock_guard lock{ bind_mutex() };
  if (find_entry(id) == nullptr) return false;
  std::vector<uint32_t> removed{ id };
  for (size_t index{}; index < removed.size(); ++index) {
    for (const bind_entry& entry : entries()) {
      if (entry.parent_id == removed[index]) removed.push_back(entry.id);
    }
  }
  std::erase_if(entries(), [&removed](const bind_entry& entry) {
    return std::ranges::find(removed, entry.id) != removed.end();
  });
  if (editing_id() && std::ranges::find(removed, editing_id()) != removed.end()) editing_id() = 0;
  mark_dirty();
  return true;
}

inline bool reparent(const uint32_t id, const uint32_t parent_id)
{
  std::lock_guard lock{ bind_mutex() };
  bind_entry* entry = find_entry(id);
  if (entry == nullptr || id == parent_id || (parent_id && find_entry(parent_id) == nullptr)) return false;
  for (uint32_t cursor = parent_id; cursor;) {
    if (cursor == id) return false;
    const bind_entry* parent = find_entry(cursor);
    cursor = parent != nullptr ? parent->parent_id : 0;
  }
  entry->parent_id = parent_id;
  mark_dirty();
  return true;
}

inline bool set_editing(const uint32_t id)
{
  std::lock_guard lock{ bind_mutex() };
  if (id && find_entry(id) == nullptr) return false;
  editing_id() = id;
  return true;
}

inline uint32_t editing()
{
  return editing_id();
}

inline std::string editing_name()
{
  std::lock_guard lock{ bind_mutex() };
  const bind_entry* entry = find_entry(editing_id());
  return entry != nullptr ? entry->name : std::string{};
}

inline bool set_override(const uint32_t id, const std::string_view target_key, const bind_value& value)
{
  std::lock_guard lock{ bind_mutex() };
  if (find_entry(id) == nullptr || find_target(target_key) == nullptr) return false;
  find_entry(id)->overrides[std::string{ target_key }] = value;
  mark_dirty();
  return true;
}

inline void clear_override(const uint32_t id, const std::string_view target_key)
{
  std::lock_guard lock{ bind_mutex() };
  if (bind_entry* entry = find_entry(id)) {
    entry->overrides.erase(std::string{ target_key });
    mark_dirty();
  }
}

inline const bind_value* override_value(const bind_entry& entry, const std::string_view target_key)
{
  const auto iterator = entry.overrides.find(std::string{ target_key });
  return iterator == entry.overrides.end() ? nullptr : &iterator->second;
}

inline bool target_has_active_override(const target_entry& target)
{
  for (const bind_entry& entry : entries()) {
    if (!entry.active || !entry.enabled) continue;
    if (entry.overrides.contains(target.target_key)) return true;
  }
  return false;
}

inline void restore_active_overrides()
{
  for (target_entry& target : targets()) {
    if (target.target == nullptr || !target.baseline_initialized) continue;
    if (target.overridden || target_has_active_override(target)) write_value(target, target.baseline);
    target.overridden = false;
    target.last_effective = read_value(target);
  }
}

inline void capture_menu_changes()
{
  for (target_entry& target : targets()) {
    if (target.target == nullptr || !target.baseline_initialized) continue;

    // A render/menu transition can happen between two create-move calls. In
    // that case the target may still be marked as overridden when the menu is
    // closed. Never promote the runtime override to the user baseline.
    const bool changed_in_menu = target.changed_in_menu;
    if (!changed_in_menu && (target.overridden || target_has_active_override(target))) {
      write_value(target, target.baseline);
      target.overridden = false;
    }

    const bind_value current = read_value(target);
    if (changed_in_menu || !values_equal(current, target.last_effective)) target.baseline = current;
    target.changed_in_menu = false;
    target.last_effective = current;
  }
}

inline void recapture_baselines()
{
  for (target_entry& target : targets()) {
    if (target.target == nullptr) continue;
    target.baseline = read_value(target);
    target.last_effective = target.baseline;
    target.baseline_initialized = true;
    target.overridden = false;
    target.changed_in_menu = false;
  }
}

inline bool condition_active(bind_entry& entry)
{
  bool result{};
  switch (entry.condition) {
  case bind_condition::key:
  {
    if (entry.key == SDLK_UNKNOWN) {
      entry.was_down = false;
      return false;
    }
    const bool down = raw_key_down(entry.key);
    const bool pressed = down && !entry.was_down;
    switch (entry.key_mode) {
    case bind_key_mode::hold:
      result = down;
      break;
    case bind_key_mode::toggle:
      if (pressed) entry.toggle_state = !entry.toggle_state;
      result = entry.toggle_state;
      break;
    case bind_key_mode::double_click:
      if (pressed) {
        const double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
        if (now - entry.last_press_time <= 0.25) {
          entry.toggle_state = !entry.toggle_state;
          entry.last_press_time = 0.0;
        } else {
          entry.last_press_time = now;
        }
      }
      result = entry.toggle_state;
      break;
    }
    entry.was_down = down;
    break;
  }
  case bind_condition::player_class:
  {
    Player* local = entity_list != nullptr && engine != nullptr ? entity_list->get_localplayer() : nullptr;
    result = local != nullptr && static_cast<int>(local->get_tf_class()) == entry.condition_value;
    break;
  }
  case bind_condition::weapon_type:
  {
    Player* local = entity_list != nullptr && engine != nullptr ? entity_list->get_localplayer() : nullptr;
    Weapon* weapon = local != nullptr ? local->get_weapon() : nullptr;
    if (weapon != nullptr) {
      const bool throwable = weapon->get_weapon_id() == TF_WEAPON_THROWABLE || weapon->get_weapon_id() == TF_WEAPON_GRENADE_THROWABLE;
      const bool melee = weapon->is_melee();
      const bool projectile = !melee && !throwable && weapon->get_projectile_type() > 1;
      result = entry.condition_value == 0 ? !melee && !throwable && !projectile
        : entry.condition_value == 1 ? projectile
        : entry.condition_value == 2 ? melee
        : throwable;
    }
    break;
  }
  case bind_condition::item_slot:
  {
    Player* local = entity_list != nullptr && engine != nullptr ? entity_list->get_localplayer() : nullptr;
    Weapon* weapon = local != nullptr ? local->get_weapon() : nullptr;
    result = weapon != nullptr && weapon->get_slot() == entry.condition_value;
    break;
  }
  case bind_condition::misc:
  {
    Player* local = entity_list != nullptr && engine != nullptr ? entity_list->get_localplayer() : nullptr;
    if (local != nullptr) {
      switch (entry.condition_value) {
      case 0: result = local->get_observer_mode() == observer_mode::in_eye || local->get_observer_mode() == observer_mode::chase; break;
      case 1: result = local->get_observer_mode() == observer_mode::in_eye; break;
      case 2: result = local->get_observer_mode() == observer_mode::chase; break;
      case 3: result = local->in_cond(TF_COND_ZOOMED); break;
      case 4: result = local->in_cond(TF_COND_AIMING); break;
      default: break;
      }
    }
    break;
  }
  }
  return entry.inverted ? !result : result;
}

inline void apply_children(const uint32_t parent_id)
{
  for (bind_entry& entry : entries()) {
    if (entry.parent_id != parent_id) continue;
    if (!entry.enabled) {
      entry.active = false;
      entry.toggle_state = false;
      continue;
    }
    entry.active = condition_active(entry);
    if (!entry.active) continue;
    for (const auto& [target_key, value] : entry.overrides) {
      if (target_entry* target = find_target(target_key); target != nullptr && target->target != nullptr) {
        write_value(*target, value);
        target->overridden = true;
      }
    }
    apply_children(entry.id);
  }
}

inline void handle_input(SDL_Event* event)
{
  if (event == nullptr) return;
  std::lock_guard lock{ bind_mutex() };

  if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
    const int scancode = static_cast<int>(event->key.keysym.scancode);
    if (scancode >= SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES) {
      keyboard_down()[static_cast<size_t>(scancode)] = event->type == SDL_KEYDOWN;
      keyboard_seen()[static_cast<size_t>(scancode)] = true;
    }
  } else if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
    const int button = static_cast<int>(event->button.button);
    if (button > 0 && button <= SDL_BUTTON_X2) {
      mouse_down()[static_cast<size_t>(button)] = event->type == SDL_MOUSEBUTTONDOWN;
      mouse_seen()[static_cast<size_t>(button)] = true;
    }
  }

  if (disabled()) return;

  for (bind_entry& entry : entries()) {
    if (!entry.waiting) continue;
    if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
      entry.key = event->key.keysym.sym == SDLK_ESCAPE ? static_cast<int>(SDLK_UNKNOWN) : static_cast<int>(event->key.keysym.scancode);
      entry.waiting = false;
      entry.was_down = entry.key != SDLK_UNKNOWN && raw_key_down(entry.key);
      mark_dirty();
    } else if (event->type == SDL_MOUSEBUTTONDOWN) {
      entry.key = -static_cast<int>(event->button.button);
      entry.waiting = false;
      entry.was_down = raw_key_down(entry.key);
      mark_dirty();
    }
  }
}

inline void run()
{
  std::lock_guard lock{ bind_mutex() };

  if (disabled()) {
    for (target_entry& target : targets()) {
      if (target.target == nullptr || !target.baseline_initialized) continue;
      if (target.overridden) {
        write_value(target, target.baseline);
      } else {
        const bind_value current = read_value(target);
        if (!values_equal(current, target.baseline)) target.baseline = current;
      }
      target.overridden = false;
    }
    for (bind_entry& entry : entries()) {
      entry.active = false;
      entry.toggle_state = false;
      entry.was_down = false;
    }
    return;
  }

  if (menu_open_state()) {
    restore_active_overrides();
    capture_menu_changes();
    for (bind_entry& entry : entries()) entry.active = false;
    return;
  }

  for (target_entry& target : targets()) {
    if (target.target == nullptr || !target.baseline_initialized) continue;
    if (target.overridden || target_has_active_override(target)) write_value(target, target.baseline);
    target.overridden = false;
  }
  for (bind_entry& entry : entries()) entry.active = false;
  apply_children(0);
  for (bind_entry& entry : entries()) {
    if (entry.condition == bind_condition::key) entry.was_down = raw_key_down(entry.key);
  }
  for (target_entry& target : targets()) {
    if (target.target != nullptr) target.last_effective = read_value(target);
  }
  autosave_if_dirty();
}

inline void request_popup(const std::string& target_key, const popup_target_type type)
{
  std::lock_guard lock{ bind_mutex() };
  popup_target_key() = target_key;
  popup_target_type_value() = type;
  popup_open_requested() = true;
}

inline std::string popup_key_name(const int key)
{
  return get_button_name(key);
}

inline bind_entry* popup_bind()
{
  if (popup_target_type_value() != popup_target_type::value_bind) return nullptr;
  try {
    const uint32_t id = static_cast<uint32_t>(std::stoul(popup_target_key()));
    if (bind_entry* entry = find_entry(id)) return entry;
  } catch (...) {}
  for (bind_entry& entry : entries()) {
    if (entry.overrides.contains(popup_target_key())) return &entry;
  }
  return nullptr;
}

inline void draw_popup()
{
  if (disabled()) return;
  std::lock_guard lock{ bind_mutex() };
  if (popup_open_requested()) {
    ImGui::OpenPopup("bind_popup_context");
    popup_open_requested() = false;
  }
  if (!ImGui::BeginPopup("bind_popup_context")) return;

  bind_entry* entry = popup_bind();
  target_entry* target = popup_target_type_value() == popup_target_type::value_bind ? find_target(popup_target_key()) : nullptr;
  if (entry == nullptr && target != nullptr) {
    ImGui::Text("Feature: %s", target->label.c_str());
    ImGui::Separator();
    if (ImGui::Button("Add to new bind")) {
      const uint32_t id = add_and_capture_key(target->label);
      entry = find_entry(id);
      if (entry != nullptr) {
        bind_value value = read_value(*target);

        if (const bool* boolean = std::get_if<bool>(&value)) value = !*boolean;
        entry->overrides[target->target_key] = std::move(value);
        mark_dirty();
      }
    }
  }
  if (entry == nullptr && target == nullptr) {
    ImGui::EndPopup();
    return;
  }
  if (entry != nullptr) {
    ImGui::TextUnformatted(entry->name.c_str());
    ImGui::Separator();
    ImGui::Text("Condition: %s", condition_label(entry->condition));
    if (entry->waiting) ImGui::TextUnformatted("Press a key or mouse button...");
    else if (ImGui::Button(popup_key_name(entry->key).c_str())) entry->waiting = true;
    int mode = static_cast<int>(entry->key_mode);
    const char* mode_names[] = { "hold", "toggle", "double click" };
    if (ImGui::Combo("Mode", &mode, mode_names, IM_ARRAYSIZE(mode_names))) {
      entry->key_mode = static_cast<bind_key_mode>(mode);
      entry->toggle_state = false;
      entry->was_down = false;
      mark_dirty();
    }
    if (ImGui::Checkbox("Enabled", &entry->enabled)) mark_dirty();
    if (ImGui::Checkbox("Invert", &entry->inverted)) mark_dirty();
    int visibility = static_cast<int>(entry->visibility);
    const char* visibility_names[] = { "always", "while active", "hidden" };
    if (ImGui::Combo("Indicator", &visibility, visibility_names, IM_ARRAYSIZE(visibility_names))) {
      entry->visibility = static_cast<bind_visibility>(visibility);
      mark_dirty();
    }
    target = target != nullptr ? target : (entry->overrides.empty() ? nullptr : find_target(entry->overrides.begin()->first));
    if (target != nullptr) {
      auto iterator = entry->overrides.find(target->target_key);
      if (iterator == entry->overrides.end()) iterator = entry->overrides.emplace(target->target_key, read_value(*target)).first;
      const bool type_matches = (target->type == value_type::boolean && std::holds_alternative<bool>(iterator->second)) ||
        (target->type == value_type::integer && std::holds_alternative<int>(iterator->second)) ||
        (target->type == value_type::floating && std::holds_alternative<float>(iterator->second)) ||
        (target->type == value_type::color && std::holds_alternative<RGBA_float>(iterator->second)) ||
        (target->type == value_type::string && std::holds_alternative<std::string>(iterator->second));
      if (!type_matches) iterator->second = read_value(*target);
      if (target->type == value_type::boolean) {
        bool value = std::get<bool>(iterator->second);
        if (ImGui::Checkbox("When active", &value)) { iterator->second = value; mark_dirty(); }
      } else if (target->type == value_type::integer) {
        int value = std::get<int>(iterator->second);
        if (target->widget == widget_type::combo_int && !target->options.empty()) {
          int selected{};
          for (size_t index{}; index < target->options.size(); ++index) if (target->options[index].second == value) selected = static_cast<int>(index);
          std::vector<const char*> names{};
          for (const auto& option : target->options) names.push_back(option.first.c_str());
          if (ImGui::Combo("When active", &selected, names.data(), static_cast<int>(names.size()))) { iterator->second = target->options[static_cast<size_t>(selected)].second; mark_dirty(); }
        } else if (ImGui::SliderInt("When active", &value, target->int_min, target->int_max, target->format.c_str())) { iterator->second = value; mark_dirty(); }
      } else if (target->type == value_type::floating) {
        float value = std::get<float>(iterator->second);
        if (ImGui::SliderFloat("When active", &value, target->float_min, target->float_max, target->format.c_str())) { iterator->second = value; mark_dirty(); }
      } else if (target->type == value_type::color) {
        RGBA_float value = std::get<RGBA_float>(iterator->second);
        const auto display_color = value.resolved();
        float color[4]{ display_color.r, display_color.g, display_color.b, display_color.a };
        if (ImGui::ColorEdit4("When active", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf)) {
          iterator->second = RGBA_float{ color[0], color[1], color[2], color[3], value.rainbow };
          mark_dirty();
        }
        if (ImGui::Checkbox("Rainbow", &value.rainbow)) {
          iterator->second = value;
          mark_dirty();
        }
      } else {
        static std::string editing_target{};
        static std::array<char, 256> text{};
        if (editing_target != target->target_key) {
          editing_target = target->target_key;
          text.fill('\0');
          std::snprintf(text.data(), text.size(), "%s", std::get<std::string>(iterator->second).c_str());
        }
        if (ImGui::InputText("When active", text.data(), text.size())) {
          iterator->second = std::string{ text.data() };
          mark_dirty();
        }
      }
    }
    ImGui::Text("State: %s", entry->active ? "active" : "idle");
    if (ImGui::Button("Remove bind")) remove(entry->id);
  }
  ImGui::EndPopup();
}

inline void maybe_open_popup(const std::string& target_key)
{
  if (!disabled() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) request_popup(target_key, popup_target_type::value_bind);
}

template <typename value_t>
inline void maybe_open_popup(value_t* target, const char* label, const bool hovered = false)
{
  if (disabled() || (!hovered && !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))) return;
  if (!ImGui::IsMouseReleased(ImGuiMouseButton_Right)) return;
  if (target_entry* entry = ensure_entry(target, label)) request_popup(entry->target_key, popup_target_type::value_bind);
}

template <typename value_t>
inline void bindable_target(value_t* target, const char* label, const bool changed, const bool hovered,
                            const widget_type widget, const int int_min = 0, const int int_max = 0,
                            const float float_min = 0.0f, const float float_max = 0.0f,
                            const char* format = nullptr, const char* const items[] = nullptr, const int item_count = 0)
{
  std::lock_guard lock{ bind_mutex() };
  register_target_metadata(target, label, widget, changed, int_min, int_max, float_min, float_max, format, items, item_count);
  target_entry* entry = ensure_entry(target, label);
  if (entry != nullptr && editing_id() && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    set_override(editing_id(), entry->target_key, read_value(*entry));
  }
  if (entry != nullptr && hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) request_popup(entry->target_key, popup_target_type::value_bind);
}

inline void bindable_checkbox(const char* label, bool* target, const bool changed, const bool hovered = false)
{
  bindable_target(target, label, changed, hovered, widget_type::checkbox);
}

inline void bindable_combo_int(const char* label, int* target, const bool changed, const char* const items[], const int item_count, const bool hovered = false)
{
  bindable_target(target, label, changed, hovered, widget_type::combo_int, 0, item_count > 0 ? item_count - 1 : 0, 0.0f, 0.0f, "%d", items, item_count);
}

inline void bindable_slider_int(const char* label, int* target, const bool changed, const int minimum, const int maximum, const char* format, const bool hovered = false)
{
  bindable_target(target, label, changed, hovered, widget_type::slider_int, minimum, maximum, 0.0f, 0.0f, format);
}

inline void bindable_slider_float(const char* label, float* target, const bool changed, const float minimum, const float maximum, const char* format, const bool hovered = false)
{
  bindable_target(target, label, changed, hovered, widget_type::slider_float, 0, 0, minimum, maximum, format);
}

inline void bindable_color(const char* label, RGBA_float* target, const bool changed, const bool hovered = false)
{
  bindable_target(target, label, changed, hovered, widget_type::color_picker);
}

inline void bindable_string(const char* label, std::string* target, const bool changed, const bool hovered = false)
{
  bindable_target(target, label, changed, hovered, widget_type::string_input);
}

inline void multi_select_target(uint32_t* target, const char* label, const bool changed, const bool hovered)
{
  bindable_target(reinterpret_cast<int*>(target), label, changed, hovered, widget_type::combo_int);
}

inline std::vector<indicator_row> collect_indicator_rows()
{
  std::lock_guard lock{ bind_mutex() };
  std::vector<indicator_row> rows{};
  const auto add_children = [&rows](const auto& self, const uint32_t parent_id) -> void {
    for (const bind_entry& entry : entries()) {
      if (entry.parent_id != parent_id || !entry.enabled) continue;

      if (entry.visibility == bind_visibility::always || (entry.visibility == bind_visibility::while_active && entry.active)) {
        std::string type{};
        std::string value{};
        switch (entry.condition) {
        case bind_condition::key:
          type = mode_label(entry.key_mode);
          value = get_button_name(entry.key);
          break;
        case bind_condition::player_class:
        {
          static constexpr const char* class_names[] = { "scout", "sniper", "soldier", "demoman", "medic", "heavy", "pyro", "spy", "engineer" };
          type = "class";
          value = entry.condition_value >= 1 && entry.condition_value <= static_cast<int>(std::size(class_names)) ? class_names[entry.condition_value - 1] : "unknown";
          break;
        }
        case bind_condition::weapon_type:
        {
          static constexpr const char* weapon_names[] = { "hitscan", "projectile", "melee", "throwable" };
          type = "weapon";
          value = entry.condition_value >= 0 && entry.condition_value < static_cast<int>(std::size(weapon_names)) ? weapon_names[entry.condition_value] : "unknown";
          break;
        }
        case bind_condition::item_slot:
          type = "slot";
          value = std::to_string(entry.condition_value + 1);
          break;
        case bind_condition::misc:
        {
          static constexpr const char* misc_values[] = { "any", "first person", "third person", "zoomed", "aiming" };
          type = entry.condition_value <= 2 ? "spectated" : "condition";
          value = entry.condition_value >= 0 && entry.condition_value < static_cast<int>(std::size(misc_values)) ? misc_values[entry.condition_value] : "unknown";
          break;
        }
        }
        if (entry.inverted) value = "not " + value;
        rows.push_back({ entry.name, std::move(type), std::move(value), std::to_string(entry.id), popup_target_type::value_bind, entry.active });
      }

      if (entry.active) self(self, entry.id);
    }
  };
  add_children(add_children, 0);
  return rows;
}

inline const std::vector<bind_entry>& indicator_entries()
{
  return entries();
}

inline void save_to_store(cathook::core::config_store* store)
{
  std::lock_guard lock{ bind_mutex() };
  if (store == nullptr || disabled()) return;
  store->set_int("binds.version", 2);
  store->set_int("binds.count", static_cast<int>(entries().size()));
  for (size_t index{}; index < entries().size(); ++index) {
    const bind_entry& entry = entries()[index];
    const std::string prefix = "binds." + std::to_string(index) + ".";
    store->set_int(prefix + "id", static_cast<int>(entry.id));
    store->set_int(prefix + "parent", static_cast<int>(entry.parent_id));
    store->set_string(prefix + "name", entry.name);
    store->set_int(prefix + "condition", static_cast<int>(entry.condition));
    store->set_int(prefix + "key_mode", static_cast<int>(entry.key_mode));
    store->set_int(prefix + "key", entry.key);
    store->set_int(prefix + "value", entry.condition_value);
    store->set_bool(prefix + "enabled", entry.enabled);
    store->set_bool(prefix + "inverted", entry.inverted);
    store->set_int(prefix + "visibility", static_cast<int>(entry.visibility));
    store->set_int(prefix + "overrides.count", static_cast<int>(entry.overrides.size()));
    size_t override_index{};
    for (const auto& [target_key, value] : entry.overrides) {
      const std::string override_prefix = prefix + "overrides." + std::to_string(override_index++) + ".";
      store->set_string(override_prefix + "target_key", target_key);
      if (std::holds_alternative<bool>(value)) {
        store->set_int(override_prefix + "type", 0);
        store->set_bool(override_prefix + "bool", std::get<bool>(value));
      } else if (std::holds_alternative<int>(value)) {
        store->set_int(override_prefix + "type", 1);
        store->set_int(override_prefix + "int", std::get<int>(value));
      } else if (std::holds_alternative<float>(value)) {
        store->set_int(override_prefix + "type", 2);
        store->set_float(override_prefix + "float", std::get<float>(value));
      } else if (std::holds_alternative<RGBA_float>(value)) {
        store->set_int(override_prefix + "type", 3);
        store->set_color(override_prefix + "color", std::get<RGBA_float>(value));
      } else {
        store->set_int(override_prefix + "type", 4);
        store->set_string(override_prefix + "string", std::get<std::string>(value));
      }
    }
  }
}

inline bool save(cathook::core::config_store* store, const std::string_view name)
{
  if (store == nullptr || disabled()) return store != nullptr;
  cathook::core::config_store bind_store = store->scoped_store("configs/binds");
  save_to_store(&bind_store);
  return bind_store.save_file(name);
}

inline bool save(cathook::core::config_store* store)
{
  return store != nullptr && save(store, store->current_name());
}

inline void autosave_if_dirty()
{
  if (!autosave_dirty() || disabled()) return;
  cathook::core::config_store* store = cathook::core::get_config_store();
  if (store == nullptr) return;
  const std::string name = store->current_name();
  store->import_config(config);
  if (store->save_file(name) && save(store, name)) autosave_dirty() = false;
}

inline void load_from_store(cathook::core::config_store* store)
{
  std::lock_guard lock{ bind_mutex() };
  if (store == nullptr || disabled()) return;
  entries().clear();
  clear_registered_targets();
  next_id() = 1;
  editing_id() = 0;
  const int count = std::clamp(store->get_int("binds.count", 0), 0, 512);
  for (int index{}; index < count; ++index) {
    const std::string prefix = "binds." + std::to_string(index) + ".";
    bind_entry entry{};
    entry.id = static_cast<uint32_t>(store->get_int(prefix + "id", index + 1));
    if (entry.id == 0) entry.id = static_cast<uint32_t>(index + 1);
    entry.parent_id = static_cast<uint32_t>(std::max(0, store->get_int(prefix + "parent", 0)));
    const std::string legacy_target = store->get_string(prefix + "target_key", "");
    entry.name = store->get_string(prefix + "name", store->get_string(prefix + "label", legacy_target.empty() ? "new bind" : legacy_target));
    entry.condition = static_cast<bind_condition>(std::clamp(store->get_int(prefix + "condition", 0), 0, 4));
    entry.key_mode = static_cast<bind_key_mode>(std::clamp(store->get_int(prefix + "key_mode", store->get_int(prefix + "mode", 0)), 0, 2));
    entry.key = store->get_int(prefix + "key", store->get_int(prefix + "button", SDLK_UNKNOWN));
    entry.condition_value = store->get_int(prefix + "value", 0);
    entry.enabled = store->get_bool(prefix + "enabled", true);
    entry.inverted = store->get_bool(prefix + "inverted", false);
    entry.visibility = static_cast<bind_visibility>(std::clamp(store->get_int(prefix + "visibility", 0), 0, 2));

    const int override_count = std::clamp(store->get_int(prefix + "overrides.count", 0), 0, 256);
    for (int override_index{}; override_index < override_count; ++override_index) {
      const std::string override_prefix = prefix + "overrides." + std::to_string(override_index) + ".";
      const std::string target_key = store->get_string(override_prefix + "target_key", "");
      if (target_key.empty()) continue;
      const int type = std::clamp(store->get_int(override_prefix + "type", 0), 0, 4);
      if (type == 0) entry.overrides[target_key] = store->get_bool(override_prefix + "bool", false);
      else if (type == 1) entry.overrides[target_key] = store->get_int(override_prefix + "int", 0);
      else if (type == 2) entry.overrides[target_key] = store->get_float(override_prefix + "float", 0.0f);
      else if (type == 3) entry.overrides[target_key] = store->get_color(override_prefix + "color", {});
      else entry.overrides[target_key] = store->get_string(override_prefix + "string", "");
    }

    if (override_count == 0 && !legacy_target.empty()) {
      const int type = std::clamp(store->get_int(prefix + "type", 0), 0, 2);
      if (type == 0) entry.overrides[legacy_target] = store->get_bool(prefix + "override_bool", false);
      else if (type == 1) entry.overrides[legacy_target] = store->get_int(prefix + "override_int", 0);
      else entry.overrides[legacy_target] = store->get_float(prefix + "override_float", 0.0f);
    }
    next_id() = std::max(next_id(), entry.id + 1);
    entries().push_back(std::move(entry));
  }
  for (bind_entry& entry : entries()) if (entry.parent_id && find_entry(entry.parent_id) == nullptr) entry.parent_id = 0;

  recapture_baselines();

  autosave_dirty() = false;
}

inline bool load(cathook::core::config_store* store)
{
  if (store == nullptr || disabled()) return store != nullptr;
  cathook::core::config_store bind_store = store->scoped_store("configs/binds");
  if (!bind_store.load_file(store->current_name())) {
    entries().clear();
    clear_registered_targets();
    next_id() = 1;
    recapture_baselines();
    return false;
  }
  load_from_store(&bind_store);
  return true;
}

inline bool delete_file(cathook::core::config_store* store, const std::string_view name)
{
  if (store == nullptr || disabled()) return store != nullptr;
  cathook::core::config_store bind_store = store->scoped_store("configs/binds");
  return bind_store.delete_file(name);
}

}
#endif
