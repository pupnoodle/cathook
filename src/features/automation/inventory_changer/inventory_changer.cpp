#if 0 // Inventory changer temporarily disabled.

#include "core/memory/code_scan.hpp"

namespace inventory_changer
{
namespace
{

constexpr std::array<float, 6> wears{{ 0.0f, 0.001f, 0.12f, 0.37f, 0.45f, 0.90f }};
constexpr std::array<std::uint16_t, 8> sheens{{ 0, 1, 2, 3, 4, 5, 6, 7 }};
constexpr std::array<std::uint16_t, 4> killstreaks{{ 0, 1, 2, 3 }};
constexpr std::array<std::uint16_t, 5> styles{{ 0, 1, 2, 3, 4 }};
constexpr std::array<std::uint16_t, 17> seeds{{
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
}};
struct modifier
{
  std::uint32_t redirect = 0;
  std::unordered_map<std::uint16_t, float> attributes;
};

struct parsed_config
{
  std::string key;
  std::unordered_map<std::uint32_t, modifier> modifiers;
};

parsed_config cache{};

struct schema_option_cache
{
  std::uintptr_t schema = 0;
  bool ready = false;
  std::array<std::vector<schema_item_option>, 4> options{};
  std::array<std::unordered_set<std::uint16_t>, 4> definitions{};
};

schema_option_cache schema_cache{};
std::uintptr_t effect_schema = 0;
std::vector<schema_item_option> effects{};
std::uintptr_t paintkit_schema = 0;
std::vector<schema_item_option> paintkit_items{};
std::filesystem::path client_module_path{};

using localization_map = std::unordered_map<std::string, std::string>;

bool quoted_pair(const std::string& line, std::string& key, std::string& value)
{
  const auto key_begin = line.find('"');
  if (key_begin == std::string::npos) return false;
  const auto key_end = line.find('"', key_begin + 1);
  if (key_end == std::string::npos) return false;
  const auto value_begin = line.find('"', key_end + 1);
  if (value_begin == std::string::npos) return false;
  const auto value_end = line.find('"', value_begin + 1);
  if (value_end == std::string::npos) return false;
  key = line.substr(key_begin + 1, key_end - key_begin - 1);
  value = line.substr(value_begin + 1, value_end - value_begin - 1);
  return true;
}

localization_map load_localization(const std::filesystem::path& tf_root)
{
  localization_map result{};
  std::ifstream english{tf_root / "resource/tf_english.txt"};
  std::string line{};
  while (std::getline(english, line)) {
    std::string key{};
    std::string value{};
    if (!quoted_pair(line, key, value)) continue;
    result[key] = value;
    if (!key.empty() && key.front() == '#') result[key.substr(1)] = value;
    else result['#' + key] = value;
  }
  return result;
}

std::string localized_item_name(const char* item_name, const localization_map& localization)
{
  if (item_name == nullptr || item_name[0] == '\0') return {};
  const auto found = localization.find(item_name);
  if (found != localization.end()) return found->second;
  return item_name;
}

std::filesystem::path tf_root_from_client();

int category_index(const item_category category)
{
  return static_cast<int>(category);
}

bool contains_insensitive(const char* value, const char* needle)
{
  if (value == nullptr || needle == nullptr) return false;
  std::string haystack{value};
  std::string target{needle};
  std::ranges::transform(haystack, haystack.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::ranges::transform(target, target.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return haystack.find(target) != std::string::npos;
}

void refresh_schema_options(const std::uintptr_t schema)
{
  schema_cache = {};
  schema_cache.schema = schema;
  for (auto& options : schema_cache.options) options.push_back({0, "Disabled"});
  if (schema == 0 || item_definition_lookup_original == nullptr) return;

  const auto localization = load_localization(tf_root_from_client());
  bool found_definition = false;
  for (unsigned int definition_index = 0; definition_index <= std::numeric_limits<std::uint16_t>::max(); ++definition_index) {
    const auto definition = item_definition_lookup_original(schema, definition_index);
    if (definition == 0) continue;
    found_definition = true;
    static const int item_name_offset = cathook::core::memory::keyed_store_offset("client.so", "item_name");
    static const int item_type_offset = cathook::core::memory::keyed_store_offset("client.so", "item_type_name");
    static const int acts_as_wearable_offset = cathook::core::memory::keyed_store_offset("client.so", "act_as_wearable");
    static const int acts_as_weapon_offset = cathook::core::memory::keyed_store_offset("client.so", "act_as_weapon");
    if (item_name_offset <= 0 || item_type_offset <= 0 ||
        acts_as_wearable_offset <= 0 || acts_as_weapon_offset <= 0) break;
    const auto* item_name = *reinterpret_cast<const char* const*>(definition + item_name_offset);
    const auto* item_type = *reinterpret_cast<const char* const*>(definition + item_type_offset);
    if (item_name == nullptr || item_name[0] == '\0') continue;
    const bool acts_as_wearable = *reinterpret_cast<const std::uint8_t*>(definition + acts_as_wearable_offset) != 0;
    const bool acts_as_weapon = *reinterpret_cast<const std::uint8_t*>(definition + acts_as_weapon_offset) != 0;
    std::string label = localized_item_name(item_name, localization);
    if (label.empty()) label = item_name;
    const bool is_crate = contains_insensitive(item_name, "crate") || contains_insensitive(item_name, "case") ||
      contains_insensitive(item_type, "crate") || contains_insensitive(item_type, "case") ||
      contains_insensitive(label.c_str(), "crate") || contains_insensitive(label.c_str(), "case");
    const bool is_key = contains_insensitive(item_name, "key") || contains_insensitive(item_type, "key") ||
      contains_insensitive(label.c_str(), "key");
    label += " [";
    label += std::to_string(definition_index);
    label += ']';
    const schema_item_option option{static_cast<std::uint16_t>(definition_index), std::move(label)};
    const auto add_option = [&](const item_category category) {
      const auto index = category_index(category);
      schema_cache.options[index].push_back(option);
      schema_cache.definitions[index].insert(static_cast<std::uint16_t>(definition_index));
    };
    if (is_crate) add_option(item_category::crate);
    if (is_key) add_option(item_category::key);
    if (acts_as_weapon) add_option(item_category::weapon);
    if (acts_as_wearable && !acts_as_weapon) add_option(item_category::wearable);
  }
  for (auto& options : schema_cache.options) {
    std::sort(options.begin() + 1, options.end(), [](const auto& left, const auto& right) {
      return left.label < right.label;
    });
  }
  schema_cache.ready = found_definition;
}

void refresh_schema_cache_if_needed()
{
  if (item_schema_lookup_map_original == nullptr || item_definition_lookup_original == nullptr) return;
  const std::uintptr_t schema = item_schema_lookup_map_original();
  if (schema_cache.schema != schema || !schema_cache.ready) refresh_schema_options(schema);
}

std::filesystem::path tf_root_from_client()
{
  if (!client_module_path.empty()) {
    return client_module_path.parent_path().parent_path().parent_path();
  }
  Dl_info info{};
  if (item_schema_lookup_map_original == nullptr || dladdr(reinterpret_cast<void*>(item_schema_lookup_map_original), &info) == 0 || info.dli_fname == nullptr) {
    return {};
  }
  const std::filesystem::path client_path{info.dli_fname};
  return client_path.parent_path().parent_path().parent_path();
}

int quoted_integer_after(const std::string& line, const std::string_view key)
{
  const auto key_position = line.find(key);
  if (key_position == std::string::npos) return -1;
  const auto quote_begin = line.find('"', key_position + key.size());
  if (quote_begin == std::string::npos) return -1;
  const auto quote_end = line.find('"', quote_begin + 1);
  if (quote_end == std::string::npos) return -1;
  int value = -1;
  const auto* begin = line.data() + quote_begin + 1;
  const auto* end = line.data() + quote_end;
  std::from_chars(begin, end, value);
  return value;
}

void refresh_effect_options(const std::uintptr_t schema)
{
  effect_schema = schema;
  effects.clear();
  effects.push_back({0, "Disabled"});
  const auto tf_root = tf_root_from_client();
  if (tf_root.empty()) return;

  std::unordered_set<int> effect_ids{};
  std::ifstream items{tf_root / "scripts/items/items_game.txt"};
  std::string line{};
  while (std::getline(items, line)) {
    for (const std::string_view key : {"\"hat only unusual effect\"", "\"taunt only unusual effect\""}) {
      const int value = quoted_integer_after(line, key);
      if (value > 0 && value <= 0xffff) effect_ids.insert(value);
    }
  }

  const auto localization = load_localization(tf_root);
  std::unordered_map<int, std::string> localized_names{};
  for (const auto& [key, value] : localization) {
    constexpr std::string_view prefix = "Attrib_Particle";
    if (!key.starts_with(prefix)) continue;
    int id = -1;
    const auto* begin = key.data() + prefix.size();
    const auto* end = key.data() + key.size();
    if (std::from_chars(begin, end, id).ec == std::errc{} && id > 0 && id <= 0xffff) {
      effect_ids.insert(id);
      localized_names[id] = value;
    }
  }

  for (const int id : effect_ids) {
    const auto found = localized_names.find(id);
    std::string label = found != localized_names.end() ? found->second : "Particle effect";
    label += " [" + std::to_string(id) + ']';
    effects.push_back({static_cast<std::uint16_t>(id), std::move(label)});
  }
  std::sort(effects.begin() + 1, effects.end(), [](const auto& left, const auto& right) {
    return left.label < right.label;
  });

  paintkit_schema = schema;
  paintkit_items.clear();
  paintkit_items.push_back({0, "Default"});
  std::unordered_set<int> paintkit_ids{};
  std::ifstream paintkit_source{tf_root / "scripts/items/items_game.txt"};
  while (std::getline(paintkit_source, line)) {
    const auto begin = line.find("\"Paintkit ");
    if (begin == std::string::npos) continue;
    int id = -1;
    const auto* number_begin = line.data() + begin + std::strlen("\"Paintkit ");
    const auto number_end_position = line.find('"', begin + 1);
    if (number_end_position == std::string::npos) continue;
    const auto* number_end = line.data() + number_end_position;
    if (number_end <= number_begin || std::from_chars(number_begin, number_end, id).ec != std::errc{} || id <= 0 || id > 0xffff) continue;
    paintkit_ids.insert(id);
  }
  for (const int id : paintkit_ids) {
    paintkit_items.push_back({static_cast<std::uint16_t>(id), "War paint " + std::to_string(id)});
  }
  std::sort(paintkit_items.begin() + 1, paintkit_items.end(), [](const auto& left, const auto& right) {
    return left.definition < right.definition;
  });
}

template <typename Array>
std::uint16_t choice(const int index, const Array& values)
{
  if (index < 0 || static_cast<std::size_t>(index) >= values.size()) return 0;
  return values[static_cast<std::size_t>(index)];
}

std::string config_key()
{
  const auto& c = config.misc.inventory_changer;
  std::string key = c.redirects + "\n" + c.boxes + "\n" + c.attributes;
  const std::array<const Misc::InventorySlot*, 6> slots{{
    &c.primary, &c.secondary, &c.melee, &c.hat1, &c.hat2, &c.hat3
  }};
  for (const auto* slot : slots) {
    key += ':' + std::to_string(slot->item) + ':' + std::to_string(slot->paintkit);
    key += ':' + std::to_string(slot->wear) + ':' + std::to_string(slot->seed);
    key += ':' + std::to_string(slot->style) + ':' + std::to_string(slot->sheen);
    key += ':' + std::to_string(slot->killstreak) + ':' + std::to_string(slot->unusual);
  }
  key += ':' + std::to_string(c.taunt1_unusual) + ':' + std::to_string(c.crate) + ':' + std::to_string(c.key);
  return key;
}

void add_redirects(std::string_view input)
{
  std::size_t begin = 0;
  while (begin < input.size()) {
    const std::size_t end = input.find(',', begin);
    const std::string_view token = input.substr(begin, end == std::string_view::npos ? end : end - begin);
    const std::size_t separator = token.find(':');
    if (separator != std::string_view::npos) {
      const auto source = std::strtoul(std::string(token.substr(0, separator)).c_str(), nullptr, 10);
      const auto target = std::strtoul(std::string(token.substr(separator + 1)).c_str(), nullptr, 10);
      if (source > 0 && source <= 0xffff && target > 0 && target <= 0xffff) {
        cache.modifiers[static_cast<std::uint32_t>(source)].redirect = static_cast<std::uint32_t>(target);
      }
    }
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
}

void add_attributes(std::string_view input)
{
  std::size_t begin = 0;
  while (begin < input.size()) {
    const std::size_t end = input.find(';', begin);
    const std::string_view item = input.substr(begin, end == std::string_view::npos ? end : end - begin);
    const std::size_t separator = item.find('=');
    if (separator != std::string_view::npos) {
      const auto definition = std::strtoul(std::string(item.substr(0, separator)).c_str(), nullptr, 10);
      if (definition > 0 && definition <= 0xffff) {
        auto& modifier = cache.modifiers[static_cast<std::uint32_t>(definition)];
        std::size_t attr_begin = separator + 1;
        while (attr_begin < item.size()) {
          const std::size_t attr_end = item.find('|', attr_begin);
          const std::string_view attr = item.substr(attr_begin, attr_end == std::string_view::npos ? attr_end : attr_end - attr_begin);
          const std::size_t attr_separator = attr.find(':');
          if (attr_separator != std::string_view::npos) {
            const auto id = std::strtoul(std::string(attr.substr(0, attr_separator)).c_str(), nullptr, 10);
            const auto value = std::strtof(std::string(attr.substr(attr_separator + 1)).c_str(), nullptr);
            if (id > 0 && id <= 0xffff) modifier.attributes[static_cast<std::uint16_t>(id)] = value;
          }
          if (attr_end == std::string_view::npos) break;
          attr_begin = attr_end + 1;
        }
      }
    }
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
}

void add_slot_modifier(const Misc::InventorySlot& slot, const std::uint16_t definition,
                       const bool include_definition)
{
  if (definition == 0) return;
  auto& modifier = cache.modifiers[definition];
  if (include_definition) modifier.redirect = definition;
  if (slot.paintkit > 0 && slot.paintkit <= 0xffff) modifier.attributes[834] = static_cast<float>(slot.paintkit);
  if (slot.wear > 0 && slot.wear < static_cast<int>(wears.size())) modifier.attributes[725] = wears[slot.wear];
  if (const auto value = choice(slot.seed, seeds); value != 0) modifier.attributes[866] = value;
  if (const auto value = choice(slot.style, styles); value != 0) modifier.attributes[542] = value;
  if (const auto value = choice(slot.sheen, sheens); value != 0) modifier.attributes[2014] = value;
  if (const auto value = choice(slot.killstreak, killstreaks); value != 0) modifier.attributes[2025] = value;
  if (slot.unusual > 0 && slot.unusual <= 0xffff) modifier.attributes[134] = static_cast<float>(slot.unusual);
}

void rebuild_cache()
{
  const std::string key = config_key();
  if (cache.key == key) return;
  cache = {};
  cache.key = key;
  const auto& c = config.misc.inventory_changer;
  add_redirects(c.redirects);
  add_redirects(c.boxes);
  add_attributes(c.attributes);
  add_slot_modifier(c.primary, static_cast<std::uint16_t>(std::clamp(c.primary.item, 0, 0xffff)), true);
  add_slot_modifier(c.secondary, static_cast<std::uint16_t>(std::clamp(c.secondary.item, 0, 0xffff)), true);
  add_slot_modifier(c.melee, static_cast<std::uint16_t>(std::clamp(c.melee.item, 0, 0xffff)), true);
  add_slot_modifier(c.hat1, static_cast<std::uint16_t>(std::clamp(c.hat1.item, 0, 0xffff)), true);
  add_slot_modifier(c.hat2, static_cast<std::uint16_t>(std::clamp(c.hat2.item, 0, 0xffff)), true);
  add_slot_modifier(c.hat3, static_cast<std::uint16_t>(std::clamp(c.hat3.item, 0, 0xffff)), true);

  if (c.crate > 0 && c.crate <= 0xffff) {
    cache.modifiers[5022].redirect = static_cast<std::uint32_t>(c.crate);
  }
  if (c.key > 0 && c.key <= 0xffff) {
    cache.modifiers[5021].redirect = static_cast<std::uint32_t>(c.key);
  }
}

bool is_local_item(Entity* entity)
{
  if (entity == nullptr || config.misc.inventory_changer.apply_to_all) return true;
  if (entity_list == nullptr) return false;
  auto* local = entity_list->get_localplayer();
  return local != nullptr && (entity == reinterpret_cast<Entity*>(local) || entity->get_owner_entity() == reinterpret_cast<Entity*>(local));
}

int definition_offset(Entity* entity)
{
  static tf2_netvars::lazy_offset weapon_offset{"DT_TFWeaponBase", {"m_Item", "m_iItemDefinitionIndex"}};
  static tf2_netvars::lazy_offset wearable_offset{"DT_TFWearable", {"m_Item", "m_iItemDefinitionIndex"}};
  static tf2_netvars::lazy_offset econ_offset{"DT_EconEntity", {"m_Item", "m_iItemDefinitionIndex"}};
  if (entity != nullptr && entity->is_base_combat_weapon()) return weapon_offset;
  const int offset = wearable_offset;
  return offset > 0 ? offset : static_cast<int>(econ_offset);
}

std::uint16_t read_definition(Entity* entity)
{
  const int offset = definition_offset(entity);
  if (entity == nullptr || offset <= 0) return 0;
  return *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::byte*>(entity) + offset);
}

void write_definition(Entity* entity, const std::uint16_t definition)
{
  const int offset = definition_offset(entity);
  if (entity == nullptr || definition == 0 || offset <= 0) return;
  auto* address = reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::byte*>(entity) + offset);
  if (*address != definition) *address = definition;
}

const Misc::InventorySlot* slot_for_entity(Entity* entity)
{
  if (entity == nullptr || !is_local_item(entity) || entity_list == nullptr) return nullptr;
  auto* local = entity_list->get_localplayer();
  if (local == nullptr) return nullptr;

  if (entity->is_base_combat_weapon()) {
    auto* weapon = reinterpret_cast<Weapon*>(entity);
    switch (weapon->get_slot()) {
    case 0: return &config.misc.inventory_changer.primary;
    case 1: return &config.misc.inventory_changer.secondary;
    case 2: return &config.misc.inventory_changer.melee;
    default: break;
    }
    if (local->is_taunting() && local->get_weapon() == weapon && config.misc.inventory_changer.taunt1_unusual > 0) {
      return nullptr;
    }
    return nullptr;
  }

  std::array<Entity*, 3> wearables{{ nullptr, nullptr, nullptr }};
  int count = 0;
  for (unsigned int index = 1; index < entity_list->get_max_entities() && count < 3; ++index) {
    Entity* candidate = entity_list->entity_from_index(index);
    if (candidate != nullptr && candidate->is_wearable() && candidate->get_owner_entity() == reinterpret_cast<Entity*>(local)) {
      wearables[static_cast<std::size_t>(count++)] = candidate;
    }
  }
  for (int index = 0; index < count; ++index) {
    if (wearables[static_cast<std::size_t>(index)] != entity) continue;
    switch (index) {
    case 0: return &config.misc.inventory_changer.hat1;
    case 1: return &config.misc.inventory_changer.hat2;
    case 2: return &config.misc.inventory_changer.hat3;
    }
  }
  return nullptr;
}

bool attribute_name_matches(const std::uint16_t id, const char* name)
{
  if (name == nullptr) return false;
  switch (id) {
  case 134: return std::strcmp(name, "attach particle effect") == 0;
  case 542: return std::strcmp(name, "item style override") == 0;
  case 725: return std::strcmp(name, "set item texture wear") == 0;
  case 834: return std::strcmp(name, "set item texture prefab") == 0;
  case 866: return std::strcmp(name, "set item texture seed") == 0;
  case 2014: return std::strcmp(name, "sheen") == 0;
  case 2025: return std::strcmp(name, "killstreak tier") == 0;
  default: return false;
  }
}

}

const std::vector<schema_item_option>& item_options(const item_category category)
{
  static const std::vector<schema_item_option> disabled{{ {0, "Disabled"} }};
  if (item_schema_lookup_map_original == nullptr || item_definition_lookup_original == nullptr) return disabled;
  const std::uintptr_t schema = item_schema_lookup_map_original();
  if (schema_cache.schema != schema || !schema_cache.ready) refresh_schema_options(schema);
  const auto& options = schema_cache.options[static_cast<std::size_t>(category_index(category))];
  return options.empty() ? disabled : options;
}

void set_client_module_address(const void* address)
{
  if (address == nullptr) return;
  Dl_info info{};
  if (dladdr(address, &info) != 0 && info.dli_fname != nullptr) {
    client_module_path = std::filesystem::path{info.dli_fname};
  }
}

const std::vector<schema_item_option>& effect_options()
{
  static const std::vector<schema_item_option> disabled{{ {0, "Disabled"} }};
  if (item_schema_lookup_map_original == nullptr) return disabled;
  const std::uintptr_t schema = item_schema_lookup_map_original();
  if (effect_schema != schema) refresh_effect_options(schema);
  return effects.empty() ? disabled : effects;
}

const std::vector<schema_item_option>& paintkit_options()
{
  static const std::vector<schema_item_option> disabled{{ {0, "Default"} }};
  if (item_schema_lookup_map_original == nullptr) return disabled;
  const std::uintptr_t schema = item_schema_lookup_map_original();
  if (paintkit_schema != schema || paintkit_items.empty()) refresh_effect_options(schema);
  return paintkit_items.empty() ? disabled : paintkit_items;
}

void on_frame_stage(const int stage)
{
  if (stage != 3 || !config.misc.inventory_changer.enabled || entity_list == nullptr) return;
  rebuild_cache();
  auto* local = entity_list->get_localplayer();
  if (local == nullptr) return;
  const std::array<const Misc::InventorySlot*, 3> weapon_slots{{
    &config.misc.inventory_changer.primary, &config.misc.inventory_changer.secondary, &config.misc.inventory_changer.melee
  }};
  for (int index = 0; index < Player::max_weapon_count; ++index) {
    auto* weapon = local->get_weapon_at(index);
    if (weapon == nullptr) continue;
    const int slot = weapon->get_slot();
    if (slot >= 0 && slot < 3) write_definition(reinterpret_cast<Entity*>(weapon),
      static_cast<std::uint16_t>(std::clamp(weapon_slots[static_cast<std::size_t>(slot)]->item, 0, 0xffff)));
  }
  const std::array<const Misc::InventorySlot*, 3> hat_slots{{
    &config.misc.inventory_changer.hat1, &config.misc.inventory_changer.hat2, &config.misc.inventory_changer.hat3
  }};
  int hat_index = 0;
  for (unsigned int index = 1; index < entity_list->get_max_entities() && hat_index < 3; ++index) {
    Entity* entity = entity_list->entity_from_index(index);
    if (entity == nullptr || !entity->is_wearable() || entity->get_owner_entity() != reinterpret_cast<Entity*>(local)) continue;
    write_definition(entity, static_cast<std::uint16_t>(std::clamp(
      hat_slots[static_cast<std::size_t>(hat_index++)]->item, 0, 0xffff)));
  }
}

std::uint32_t redirect_item_definition(const std::uint32_t item_definition)
{
  if (!config.misc.inventory_changer.enabled) return item_definition;
  rebuild_cache();
  refresh_schema_cache_if_needed();
  const auto& changer = config.misc.inventory_changer;
  if (changer.crate > 0 && changer.crate <= 0xffff &&
      schema_cache.definitions[category_index(item_category::crate)].contains(static_cast<std::uint16_t>(item_definition))) {
    return static_cast<std::uint32_t>(changer.crate);
  }
  if (changer.key > 0 && changer.key <= 0xffff &&
      schema_cache.definitions[category_index(item_category::key)].contains(static_cast<std::uint16_t>(item_definition))) {
    return static_cast<std::uint32_t>(changer.key);
  }
  const auto found = cache.modifiers.find(item_definition);
  return found != cache.modifiers.end() && found->second.redirect != 0 ? found->second.redirect : item_definition;
}

float attribute_hook_value_float_hook(const float value, const char* attribute_name,
                                      Entity* entity, void* context, const bool is_global)
{
  const float original = attribute_hook_value_float_original != nullptr
    ? attribute_hook_value_float_original(value, attribute_name, entity, context, is_global)
    : value;
  if (!config.misc.inventory_changer.enabled || !is_local_item(entity)) return original;
  rebuild_cache();

  const auto* slot = slot_for_entity(entity);
  if (slot != nullptr) {
    const auto apply = [&](const std::uint16_t id, const float replacement) -> bool {
      return replacement != 0.0f && attribute_name_matches(id, attribute_name);
    };
    if (slot->paintkit > 0 && slot->paintkit <= 0xffff && apply(834, static_cast<float>(slot->paintkit))) {
      return static_cast<float>(slot->paintkit);
    }
    if (slot->wear > 0 && slot->wear < static_cast<int>(wears.size()) && apply(725, wears[slot->wear])) return wears[slot->wear];
    if (apply(866, static_cast<float>(choice(slot->seed, seeds)))) return static_cast<float>(choice(slot->seed, seeds));
    if (apply(542, static_cast<float>(choice(slot->style, styles)))) return static_cast<float>(choice(slot->style, styles));
    if (apply(2014, static_cast<float>(choice(slot->sheen, sheens)))) return static_cast<float>(choice(slot->sheen, sheens));
    if (apply(2025, static_cast<float>(choice(slot->killstreak, killstreaks)))) return static_cast<float>(choice(slot->killstreak, killstreaks));
    if (apply(134, static_cast<float>(slot->unusual))) return static_cast<float>(slot->unusual);
  }

  auto* local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (local != nullptr && local->is_taunting() && local->get_weapon() == reinterpret_cast<Weapon*>(entity)) {
    const auto unusual = config.misc.inventory_changer.taunt1_unusual;
    if (unusual != 0 && attribute_name_matches(134, attribute_name)) return static_cast<float>(unusual);
  }
  const auto definition = read_definition(entity);
  const auto found = cache.modifiers.find(definition);
  if (found != cache.modifiers.end()) {
    for (const auto& [id, replacement] : found->second.attributes) {
      if (attribute_name_matches(id, attribute_name)) return replacement;
    }
  }
  return original;
}

}

#endif
