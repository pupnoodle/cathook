/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/netvars.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef TF2_SDK_NETVARS_HPP
#define TF2_SDK_NETVARS_HPP

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>

#include "games/tf2/sdk/interfaces/client.hpp"
#include "core/memory/resolve.hpp"

namespace tf2_netvars
{

inline auto offset_cache() -> std::unordered_map<std::string, int>&
{
  static std::unordered_map<std::string, int> cache{};
  return cache;
}

inline auto offset_cache_mutex() -> std::mutex&
{
  static std::mutex mutex{};
  return mutex;
}

struct recv_prop;

struct recv_table
{
  recv_prop* props = nullptr;
  int prop_count = 0;
  void* decoder = nullptr;
  const char* net_table_name = nullptr;
  bool initialized = false;
  bool in_main_list = false;
};

struct recv_prop
{
  const char* var_name = nullptr;
  int recv_type = 0;
  int flags = 0;
  int string_buffer_size = 0;
  bool inside_array = false;
  const void* extra_data = nullptr;
  recv_prop* array_prop = nullptr;
  void* array_length_proxy = nullptr;
  void* proxy_fn = nullptr;
  void* data_table_proxy_fn = nullptr;
  recv_table* data_table = nullptr;
  int offset = 0;
  int element_stride = 0;
  int element_count = 0;
  const char* parent_array_prop_name = nullptr;
};

static_assert(offsetof(recv_prop, proxy_fn) == 0x30, "RecvProp::m_ProxyFn");
static_assert(offsetof(recv_prop, offset) == 0x48, "RecvProp::m_Offset");

struct client_class
{
  void* create_fn = nullptr;
  void* create_event_fn = nullptr;
  const char* network_name = nullptr;
  recv_table* recv_table_ptr = nullptr;
  client_class* next = nullptr;
  int class_id = 0;
};

inline int find_offset_in_table(recv_table* table, const std::vector<const char*>& path, std::size_t depth, int accumulated_offset,
  std::vector<recv_table*>& visiting, int recursion_depth = 0)
{
  if (table == nullptr || table->props == nullptr || depth >= path.size() || recursion_depth > 64 ||
      std::find(visiting.begin(), visiting.end(), table) != visiting.end()) {
    return 0;
  }
  visiting.push_back(table);

  for (int index = 0; index < table->prop_count; ++index) {
    auto* prop = &table->props[index];
    if (prop == nullptr || prop->var_name == nullptr || prop->var_name[0] == '\0') {
      continue;
    }

    const int current_offset = accumulated_offset + prop->offset;
    if (std::strcmp(prop->var_name, path[depth]) == 0) {
      if (depth + 1 >= path.size()) {
        visiting.pop_back();
        return current_offset;
      }

      if (prop->data_table != nullptr) {
        if (const int nested_offset = find_offset_in_table(prop->data_table, path, depth + 1, current_offset, visiting, recursion_depth + 1)) {
          return nested_offset;
        }
      }
    }

    if (prop->data_table != nullptr) {
        if (const int nested_offset = find_offset_in_table(prop->data_table, path, depth, current_offset, visiting, recursion_depth + 1)) {
        return nested_offset;
      }
    }
  }

  visiting.pop_back();
  return 0;
}

inline int find_offset(const char* table_name, const char* const* props, std::size_t prop_count)
{
  if (client == nullptr || table_name == nullptr || prop_count == 0) {
    return 0;
  }

  std::string cache_key{ table_name };
  for (std::size_t index = 0; index < prop_count; ++index) {
    const char* prop_name = props[index];
    cache_key += "->";
    cache_key += prop_name != nullptr ? prop_name : "";
  }

  std::scoped_lock lock{offset_cache_mutex()};
  auto& cache = offset_cache();
  if (const auto found = cache.find(cache_key); found != cache.end()) {
    return found->second;
  }

  const std::vector<const char*> path{ props, props + prop_count };
  auto* classes = reinterpret_cast<client_class*>(client->get_all_classes());
  for (auto* current = classes; current != nullptr; current = current->next) {
    if (current->recv_table_ptr == nullptr || current->recv_table_ptr->net_table_name == nullptr) {
      continue;
    }

    if (std::strcmp(current->recv_table_ptr->net_table_name, table_name) != 0) {
      continue;
    }

    std::vector<recv_table*> visiting{};
    const int offset = find_offset_in_table(current->recv_table_ptr, path, 0, 0, visiting);
    if (offset != 0) cache.emplace(cache_key, offset);
    return offset;
  }

  return 0;
}

inline int find_offset(const char* table_name, std::initializer_list<const char*> props)
{
  return find_offset(table_name, props.begin(), props.size());
}

inline recv_prop* find_prop_in_table(recv_table* table, const std::vector<const char*>& path, std::size_t depth,
  std::vector<recv_table*>& visiting, int recursion_depth = 0)
{
  if (table == nullptr || table->props == nullptr || depth >= path.size() || recursion_depth > 64 ||
      std::find(visiting.begin(), visiting.end(), table) != visiting.end()) {
    return nullptr;
  }
  visiting.push_back(table);

  for (int index = 0; index < table->prop_count; ++index) {
    auto* prop = &table->props[index];
    if (prop == nullptr || prop->var_name == nullptr || prop->var_name[0] == '\0') {
      continue;
    }

    if (std::strcmp(prop->var_name, path[depth]) == 0) {
      if (depth + 1 >= path.size()) {
        visiting.pop_back();
        return prop;
      }

      if (prop->data_table != nullptr) {
        if (auto* nested = find_prop_in_table(prop->data_table, path, depth + 1, visiting, recursion_depth + 1)) {
          return nested;
        }
      }
    }

    if (prop->data_table != nullptr) {
      if (auto* nested = find_prop_in_table(prop->data_table, path, depth, visiting, recursion_depth + 1)) {
        return nested;
      }
    }
  }

  visiting.pop_back();
  return nullptr;
}

inline recv_prop* find_prop(const char* table_name, std::initializer_list<const char*> props)
{
  if (client == nullptr || table_name == nullptr || props.size() == 0) {
    return nullptr;
  }

  const std::vector<const char*> path{ props.begin(), props.end() };
  auto* classes = reinterpret_cast<client_class*>(client->get_all_classes());
  for (auto* current = classes; current != nullptr; current = current->next) {
    if (current->recv_table_ptr == nullptr || current->recv_table_ptr->net_table_name == nullptr) {
      continue;
    }

    if (std::strcmp(current->recv_table_ptr->net_table_name, table_name) != 0) {
      continue;
    }

    std::vector<recv_table*> visiting{};
    return find_prop_in_table(current->recv_table_ptr, path, 0, visiting);
  }

  return nullptr;
}

inline int proxy_store_disp(const void* fn)
{
  const auto* code = static_cast<const std::uint8_t*>(fn);
  if (code == nullptr) {
    return 0;
  }
  for (std::size_t i = 0; i < 64; ++i) {
    std::size_t j = i;
    bool rex_b = false;
    while (code[j] >= 0x40 && code[j] <= 0x4F && j - i < 3) {
      rex_b = rex_b || (code[j] & 1) != 0;
      ++j;
    }
    if (code[j] != 0x88 && code[j] != 0x89) {
      continue;
    }
    const std::uint8_t modrm = code[j + 1];
    const int mod = modrm >> 6;
    const int rm = modrm & 7;
    if (rm != 6 || mod == 0 || mod == 3 || rex_b) {
      continue;
    }
    if (mod == 1) {
      return code[j + 2];
    }
    return puphook::core::memory::read_disp32(code + j, 2);
  }
  return 0;
}

inline void** pointer_datatable_slot(const char* table_name, const char* prop_name)
{
  const recv_prop* prop = find_prop(table_name, { prop_name });
  if (prop == nullptr || prop->data_table_proxy_fn == nullptr) {
    return nullptr;
  }
  const auto* code = static_cast<const std::uint8_t*>(prop->data_table_proxy_fn);
  for (std::size_t i = 0; i + 7 <= 32; ++i) {
    if (code[i] == 0x48 && code[i + 1] == 0x8D && (code[i + 2] & 0xC7) == 0x05) {
      return reinterpret_cast<void**>(puphook::core::memory::resolve_rip_relative(code + i, 3, 7));
    }
  }
  return nullptr;
}

inline void* game_rules_object()
{
  static void** slot = nullptr;
  if (slot == nullptr) {
    slot = pointer_datatable_slot("DT_TFGameRulesProxy", "tf_gamerules_data");
  }
  return slot != nullptr ? *slot : nullptr;
}

struct lazy_offset
{
  const char* table;
  std::vector<const char*> path;
  std::atomic<int> value{0};

  lazy_offset(const char* table_name, std::initializer_list<const char*> prop_path)
      : table(table_name), path(prop_path)
  {
  }

  lazy_offset(const lazy_offset&) = delete;
  lazy_offset& operator=(const lazy_offset&) = delete;

  operator int()
  {
    int current = value.load(std::memory_order_acquire);
    if (current <= 0) {
      current = find_offset(table, path.data(), path.size());
      if (current > 0) value.store(current, std::memory_order_release);
    }
    return current;
  }
};

}

#endif
