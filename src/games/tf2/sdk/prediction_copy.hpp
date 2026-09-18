#ifndef TF2_SDK_PREDICTION_COPY_HPP
#define TF2_SDK_PREDICTION_COPY_HPP

#include <cstdint>
#include <cstring>
#include <vector>

#include "games/tf2/sdk/datamap.hpp"

namespace pred_copy {

struct field {
  int offset = 0;
  int size = 0;
};

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
    if ((desc.flags & FTYPEDESC_INSENDTABLE) == 0) {
      continue;
    }
    if (desc.fieldSizeInBytes <= 0 || desc.fieldSizeInBytes > 4096) {
      continue;
    }
    fields.push_back({offset, desc.fieldSizeInBytes});
  }
}

inline bool capture(const void* entity, const datamap_t* map, std::vector<field>& fields,
                    std::vector<std::uint8_t>& data) {
  fields.clear();
  data.clear();
  if (entity == nullptr || map == nullptr) {
    return false;
  }
  collect_fields(map, 0, fields, 0);
  if (fields.empty()) {
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

inline bool restore(void* entity, const std::vector<field>& fields,
                    const std::vector<std::uint8_t>& data) {
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

}

#endif
