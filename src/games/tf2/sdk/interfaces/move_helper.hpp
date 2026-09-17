/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/interfaces/move_helper.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef MOVE_HELPER_HPP
#define MOVE_HELPER_HPP

#include <cstdint>
#include "core/memory/code_scan.hpp"

class Player;

class MoveHelper {
public:
  Player* get_host() {
    if (this == nullptr) {
      return nullptr;
    }
    static const int host_offset = [this] {
      void** vtable = *reinterpret_cast<void***>(this);
      if (vtable == nullptr || vtable[12] == nullptr) {
        return 0;
      }
      const auto* fn = static_cast<const std::uint8_t*>(vtable[12]);
      const auto* end = fn + 0x40;
      for (const std::uint8_t* p = fn; p < end;
           p += cathook::core::memory::insn_length(p, end)) {
        cathook::core::memory::mem_insn insn{};
        if (!cathook::core::memory::decode_mem_insn(p, end, insn)) {
          continue;
        }
        if (insn.opcode == 0x89 && insn.reg == 6 && insn.base == 7) {
          return insn.disp;
        }
      }
      return 0;
    }();
    if (host_offset <= 0) {
      return nullptr;
    }
    return *reinterpret_cast<Player**>(reinterpret_cast<char*>(this) + host_offset);
  }

  void set_host(Player* host) {
    if (this == nullptr) {
      return;
    }

    void** vtable = *(void***)this;
    if (vtable == nullptr) {
      return;
    }

    auto set_host_fn = reinterpret_cast<void (*)(void*, Player*)>(vtable[12]);
    if (set_host_fn == nullptr) {
      return;
    }

    set_host_fn(this, host);
  }
};

inline static MoveHelper* move_helper;

#endif
