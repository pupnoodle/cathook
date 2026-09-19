#ifndef TF2_SDK_PREDICTION_COPY_HPP
#define TF2_SDK_PREDICTION_COPY_HPP

#include <cstdint>
#include <cstring>
#include <vector>

#include "games/tf2/sdk/combat_offsets.hpp"
#include "games/tf2/sdk/datamap.hpp"

namespace pred_copy {

enum class mode : int {
  everything = 0,
  non_networked = 1,
  networked = 2
};

struct field {
  int offset = 0;
  int size = 0;
};

inline int packed_bytes(const datamap_t* map) {
  if (map == nullptr) {
    return 0;
  }
  return map->packed_size > 4 ? map->packed_size : 0;
}

inline bool packed_copy_ready(const datamap_t* map) {
  if (map == nullptr || !map->packed_offsets_computed) {
    return false;
  }
  return map->packed_size >= 4 && map->packed_size <= 0x10000;
}

inline bool engine_transfer(int type, void* dest, bool dest_packed, const void* src, bool src_packed,
  datamap_t* map, const char* operation, int entindex) {
  void* const ctor = tf2_combat::get().prediction_copy_ctor_fn;
  void* const transfer = tf2_combat::get().prediction_copy_transfer_fn;
  if (ctor == nullptr || transfer == nullptr || dest == nullptr || src == nullptr || map == nullptr) {
    return false;
  }

  using ctor_fn = void (*)(void*, int, void*, std::uint8_t, const void*, std::uint8_t, char, char,
    char, char, void*);
  using transfer_fn = int (*)(void*, const char*, int, datamap_t*);

  alignas(16) std::uint8_t object[160]{};
  reinterpret_cast<ctor_fn>(ctor)(object, type, dest, dest_packed ? 1 : 0, src, src_packed ? 1 : 0,
    0, 0, 1, 0, nullptr);
  return reinterpret_cast<transfer_fn>(transfer)(
           object, operation != nullptr ? operation : "", entindex, map) > 0;
}

inline void collect_fields(const datamap_t* map, int base_offset, std::vector<field>& fields,
                           int depth) {
  if (map == nullptr || map->dataDesc == nullptr || depth > 16) {
    return;
  }
  if (map->dataNumFields <= 0 || map->dataNumFields > 1024) {
    return;
  }
  if (map->baseMap != nullptr) {
    collect_fields(map->baseMap, base_offset, fields, depth + 1);
  }
  for (int index = 0; index < map->dataNumFields; ++index) {
    const typedescription_t& desc = map->dataDesc[index];
    if (desc.fieldType == FIELD_VOID || desc.fieldType == FIELD_FUNCTION ||
        desc.fieldType == FIELD_INPUT || desc.fieldType == FIELD_CUSTOM) {
      continue;
    }
    const int offset = base_offset + desc.fieldOffset[TD_OFFSET_NORMAL];
    if (offset < 0 || offset > 0x40000) {
      continue;
    }
    if (desc.fieldType == FIELD_EMBEDDED && desc.td != nullptr &&
        (desc.flags & FTYPEDESC_PTR) == 0) {
      collect_fields(desc.td, offset, fields, depth + 1);
      continue;
    }
    if (desc.fieldSizeInBytes <= 0 || desc.fieldSizeInBytes > 4096) {
      continue;
    }
    fields.push_back({offset, desc.fieldSizeInBytes});
  }
}

inline bool fallback_capture(const void* entity, const datamap_t* map, std::vector<std::uint8_t>& data) {
  std::vector<field> fields;
  collect_fields(map, 0, fields, 0);
  if (entity == nullptr || fields.empty()) {
    return false;
  }
  int bytes = 0;
  for (const field& item : fields) {
    bytes += item.size;
  }
  data.resize(static_cast<std::size_t>(bytes));
  std::size_t cursor = 0;
  const auto* base = static_cast<const std::uint8_t*>(entity);
  for (const field& item : fields) {
    std::memcpy(data.data() + cursor, base + item.offset, static_cast<std::size_t>(item.size));
    cursor += static_cast<std::size_t>(item.size);
  }
  return true;
}

inline bool fallback_restore(void* entity, const datamap_t* map, const std::vector<std::uint8_t>& data) {
  std::vector<field> fields;
  collect_fields(map, 0, fields, 0);
  if (entity == nullptr || fields.empty() || data.empty()) {
    return false;
  }
  std::size_t needed = 0;
  for (const field& item : fields) {
    needed += static_cast<std::size_t>(item.size);
  }
  if (needed != data.size()) {
    return false;
  }
  std::size_t cursor = 0;
  auto* base = static_cast<std::uint8_t*>(entity);
  for (const field& item : fields) {
    std::memcpy(base + item.offset, data.data() + cursor, static_cast<std::size_t>(item.size));
    cursor += static_cast<std::size_t>(item.size);
  }
  return true;
}

inline bool player_pred_map(const datamap_t* map) {
  if (map == nullptr || map->dataDesc == nullptr || map->dataClassName == nullptr) {
    return false;
  }
  if (map->packed_size < 0 || map->packed_size > 1 << 20) {
    return false;
  }
  if (map->dataNumFields <= 0 || map->dataNumFields > 1024) {
    return false;
  }
  return std::strstr(map->dataClassName, "Player") != nullptr;
}

inline bool capture(void* entity, datamap_t* map, std::vector<std::uint8_t>& data,
  mode copy_mode = mode::everything, int entindex = 0) {
  data.clear();
  if (entity == nullptr || map == nullptr || !player_pred_map(map)) {
    return false;
  }

  const int type = static_cast<int>(copy_mode);

  data.assign(0x10000, 0);
  if (engine_transfer(type, data.data(), false, entity, false, map, "PredictionCopyStore",
        entindex)) {
    return true;
  }
  data.clear();
  return fallback_capture(entity, map, data);
}

inline bool restore(void* entity, datamap_t* map, const std::vector<std::uint8_t>& data,
  mode copy_mode = mode::everything, int entindex = 0) {
  if (entity == nullptr || map == nullptr || data.empty() || !player_pred_map(map)) {
    return false;
  }

  const int type = static_cast<int>(copy_mode);
  if (data.size() == 0x10000) {
    return engine_transfer(type, entity, false, data.data(), false, map, "PredictionCopyReset",
      entindex);
  }
  return fallback_restore(entity, map, data);
}

}

#endif
