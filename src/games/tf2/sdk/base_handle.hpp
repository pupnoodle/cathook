/*
/^-----^\   data: 2026-05-27
V  o o  V  file: src/games/tf2/sdk/base_handle.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef TF2_SDK_BASE_HANDLE_HPP
#define TF2_SDK_BASE_HANDLE_HPP

#include <cstdint>

constexpr std::uint32_t invalid_ehandle_index = 0xffffffffu;
constexpr int num_serial_num_shift_bits = 16;
constexpr std::uint32_t ent_entry_mask = (1u << num_serial_num_shift_bits) - 1u;

class CBaseHandle {
public:
  CBaseHandle() = default;

  explicit CBaseHandle(std::uint32_t value) : m_Index(value) {}

  CBaseHandle(int entry, int serial_number) {
    Init(entry, serial_number);
  }

  void Init(int entry, int serial_number) {
    m_Index = static_cast<std::uint32_t>(entry) |
      (static_cast<std::uint32_t>(serial_number) << num_serial_num_shift_bits);
  }

  void Term() {
    m_Index = invalid_ehandle_index;
  }

  bool IsValid() const {
    return m_Index != invalid_ehandle_index;
  }

  int GetEntryIndex() const {
    return static_cast<int>(m_Index & ent_entry_mask);
  }

  int GetSerialNumber() const {
    return static_cast<int>(m_Index >> num_serial_num_shift_bits);
  }

  int ToInt() const {
    return static_cast<int>(m_Index);
  }

  bool operator!=(const CBaseHandle& other) const {
    return m_Index != other.m_Index;
  }

  bool operator==(const CBaseHandle& other) const {
    return m_Index == other.m_Index;
  }

  bool operator<(const CBaseHandle& other) const {
    return m_Index < other.m_Index;
  }

  std::uint32_t m_Index = invalid_ehandle_index;
};

using EntityHandle_t = CBaseHandle;

#endif
