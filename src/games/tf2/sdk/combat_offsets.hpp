#ifndef TF2_SDK_COMBAT_OFFSETS_HPP
#define TF2_SDK_COMBAT_OFFSETS_HPP

#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "core/memory/code_scan.hpp"
#include "core/memory/maps.hpp"
#include "core/shared/sigs.hpp"
#include "external/libsigscan/libsigscan.h"
#include "games/tf2/sdk/netvars.hpp"

namespace tf2_combat {

struct layout {
  int crit_token_bucket = 0;
  int crit_checks = 0;
  int crit_seed_requests = 0;
  int weapon_mode = 0;
  int current_attack_is_crit = 0;
  int current_crit_is_random = 0;
  int current_attack_is_during_demo_charge = 0;
  int crit_time = 0;
  int last_crit_check_time = 0;
  int last_crit_check_frame = 0;
  int current_seed = 0;
  int last_rapid_fire_crit_check_time = 0;
  int gun_weapon_data = 0;
  int melee_weapon_data = 0;
  int weapon_info_data = 0;
  int weapon_info_stride = 0;
  int weapon_info_spread = 0;
  int weapon_info_fire_delay = 0;
  int weapon_info_range = 0;
  int weapon_info_damage = 0;
  int weapon_info_bullets = 0;
  int weapon_info_projectile = 0;
  int weapon_info_proj_speed = 0;
  int weapon_info_smack_delay = 0;
  int weapon_info_rapid_fire = 0;
  int abs_origin = 0;
  int abs_angles = 0;
  int eflags = 0;

  std::size_t get_weapon_id = 0;
  std::size_t get_slot = 0;
  std::size_t get_damage_type = 0;
  std::size_t get_spread_angles = 0;
  std::size_t get_weapon_spread = 0;
  std::size_t get_range = 0;
  std::size_t get_swing_range = 0;
  std::size_t can_fire_critical_shot = 0;
  std::size_t can_fire_random_critical_shot = 0;
  std::size_t get_abs_origin = 0;
  std::size_t get_abs_angles = 0;
  std::size_t build_transformations = 0;
  std::size_t standard_blending_rules = 0;
  std::size_t update_client_side_animation = 0;
  std::size_t eye_position = 0;
  std::size_t eye_angles = 0;
  std::size_t setup_bones = 0;
  std::size_t check_stuck = 0;
  std::size_t get_view_vectors = 0;
  std::size_t process_movement = 2;
  std::size_t game_movement_player = 0;
  std::size_t game_movement_mv = 0;
  std::size_t game_movement_tf_player = 0;

  int reload_to_weapon_mode = 0;
  int reload_to_token_bucket = 0;
  int reload_to_crit_checks = 0;
  int reload_to_seed_requests = 0;
  int last_to_crit_time = 0;
  int last_to_check_frame = 0;
  int last_to_current_seed = 0;
  int last_to_rapid_fire = 0;
  int reload_prior_to_gun_info = 0;
  int inspect_to_smack_time_rel = 0;
  bool netvar_rels_ready = false;

  void* eye_position_fn = nullptr;
  void* eye_angles_fn = nullptr;
  void* setup_bones_fn = nullptr;
  void* get_abs_origin_fn = nullptr;
  void* get_abs_angles_fn = nullptr;
  void* set_abs_origin_fn = nullptr;
  void* set_abs_angles_fn = nullptr;
  void* get_weapon_spread_fn = nullptr;
  void* get_range_fn = nullptr;
  void* get_slot_fn = nullptr;
  void* get_damage_type_fn = nullptr;
  void* get_spread_angles_fn = nullptr;
  void* update_client_side_animation_fn = nullptr;
  void* standard_blending_rules_fn = nullptr;
  void* build_transformations_fn = nullptr;
  void* get_view_vectors_fn = nullptr;
  void* check_stuck_fn = nullptr;
  void* process_movement_fn = nullptr;
  void* prediction_copy_ctor_fn = nullptr;
  void* prediction_copy_transfer_fn = nullptr;
};

namespace detail {

inline const std::uint8_t* follow_jmp(const std::uint8_t* fn, const std::uint8_t* end) {
  if (fn == nullptr || fn + 5 > end) {
    return fn;
  }
  if (fn[0] == 0xE9) {
    std::int32_t rel = 0;
    std::memcpy(&rel, fn + 1, 4);
    return fn + 5 + rel;
  }
  return fn;
}

inline const std::uint8_t* function_end(const std::uint8_t* fn, std::size_t span = 0x500) {
  if (fn == nullptr) {
    return nullptr;
  }
  return fn + span;
}

inline void track_this_reg(std::uint32_t& this_regs, const puphook::core::memory::mem_insn& insn) {
  if (insn.mod != 3) {
    return;
  }
  if (insn.opcode == 0x89 && insn.reg >= 0 && insn.reg < 16 &&
      ((this_regs >> insn.reg) & 1u) != 0 && insn.rm >= 0 && insn.rm < 16) {
    this_regs |= 1u << insn.rm;
  } else if (insn.opcode == 0x8B && insn.rm >= 0 && insn.rm < 16 &&
             ((this_regs >> insn.rm) & 1u) != 0 && insn.reg >= 0 && insn.reg < 16) {
    this_regs |= 1u << insn.reg;
  }
}

inline void collect_this_disps(const std::uint8_t* fn, int* out, int& count, int max_count,
  int min_disp, int max_disp) {
  if (fn == nullptr || out == nullptr || max_count <= 0) {
    return;
  }
  const std::uint8_t* const end = function_end(fn);
  std::uint32_t this_regs = 1u << 7;
  if (count < 0) {
    count = 0;
  }
  for (const std::uint8_t* p = fn; p < end;) {
    if (puphook::core::memory::is_flow_exit(p, end) && p != fn) {
      break;
    }
    puphook::core::memory::mem_insn insn{};
    if (puphook::core::memory::decode_mem_insn(p, end, insn)) {
      track_this_reg(this_regs, insn);
      if (insn.base >= 0 && insn.base < 16 && ((this_regs >> insn.base) & 1u) != 0 &&
          insn.disp >= min_disp && insn.disp < max_disp) {
        bool seen = false;
        for (int i = 0; i < count; ++i) {
          if (out[i] == insn.disp) {
            seen = true;
            break;
          }
        }
        if (!seen && count < max_count) {
          out[count++] = insn.disp;
        }
      }
    }
    const std::size_t len = puphook::core::memory::insn_length(p, end);
    p += len != 0 ? len : 1;
  }
}

inline void collect_xmmword_this_stores(const std::uint8_t* fn, int* out, int& count, int max_count,
  int min_disp, int max_disp) {
  if (fn == nullptr || out == nullptr || max_count <= 0) {
    return;
  }
  const std::uint8_t* const end = function_end(fn, 0x300);
  std::uint32_t this_regs = 1u << 7;
  for (const std::uint8_t* p = fn; p < end;) {
    if (puphook::core::memory::is_flow_exit(p, end) && p != fn) {
      break;
    }
    puphook::core::memory::mem_insn insn{};
    if (puphook::core::memory::decode_mem_insn(p, end, insn)) {
      track_this_reg(this_regs, insn);
      if (insn.opcode == 0x0F &&
          (insn.opcode2 == 0x11 || insn.opcode2 == 0x29 || insn.opcode2 == 0x7F) &&
          insn.base >= 0 && insn.base < 16 && ((this_regs >> insn.base) & 1u) != 0 &&
          insn.disp >= min_disp && insn.disp < max_disp) {
        bool seen = false;
        for (int i = 0; i < count; ++i) {
          if (out[i] == insn.disp) {
            seen = true;
            break;
          }
        }
        if (!seen && count < max_count) {
          out[count++] = insn.disp;
        }
      }
    }
    const std::size_t len = puphook::core::memory::insn_length(p, end);
    p += len != 0 ? len : 1;
  }
}

inline void collect_arg_ptr_stores(const std::uint8_t* fn, int src_reg, int* out, int& count,
  int max_count, int min_disp, int max_disp) {
  if (fn == nullptr || out == nullptr || src_reg < 0 || src_reg >= 16 || max_count <= 0) {
    return;
  }
  const std::uint8_t* const end = function_end(fn, 0x300);
  std::uint32_t this_regs = 1u << 7;
  std::uint32_t src_regs = 1u << src_reg;
  for (const std::uint8_t* p = fn; p < end;) {
    if (puphook::core::memory::is_flow_exit(p, end) && p != fn) {
      break;
    }
    puphook::core::memory::mem_insn insn{};
    if (puphook::core::memory::decode_mem_insn(p, end, insn)) {
      track_this_reg(this_regs, insn);
      track_this_reg(src_regs, insn);
      if (insn.rex_w && insn.opcode == 0x89 && insn.base >= 0 && insn.base < 16 &&
          ((this_regs >> insn.base) & 1u) != 0 && insn.reg >= 0 && insn.reg < 16 &&
          ((src_regs >> insn.reg) & 1u) != 0 && insn.disp >= min_disp && insn.disp < max_disp) {
        bool seen = false;
        for (int i = 0; i < count; ++i) {
          if (out[i] == insn.disp) {
            seen = true;
            break;
          }
        }
        if (!seen && count < max_count) {
          out[count++] = insn.disp;
        }
      }
    }
    const std::size_t len = puphook::core::memory::insn_length(p, end);
    p += len != 0 ? len : 1;
  }
}

inline int parse_eflags_from_clear_dirty(const std::uint8_t* fn) {
  if (fn == nullptr) {
    return 0;
  }
  const std::uint8_t* const end = function_end(fn, 0x180);
  for (const std::uint8_t* p = fn; p + 10 <= end; ++p) {
    if (p[0] != 0x81) {
      continue;
    }
    const std::uint8_t modrm = p[1];
    const int mod = (modrm >> 6) & 3;
    const int reg = (modrm >> 3) & 7;
    if (reg != 4) {
      continue;
    }
    std::int32_t disp = 0;
    std::size_t imm_at = 0;
    if (mod == 1) {
      disp = static_cast<std::int8_t>(p[2]);
      imm_at = 3;
    } else if (mod == 2) {
      std::memcpy(&disp, p + 2, 4);
      imm_at = 6;
    } else {
      continue;
    }
    if (p + imm_at + 4 > end) {
      continue;
    }
    std::uint32_t imm = 0;
    std::memcpy(&imm, p + imm_at, 4);
    if (imm == 0xFFFFF7FFu && disp > 0x80 && disp < 0x800) {
      return disp;
    }
  }
  return 0;
}

inline int parse_return_lea_disp(const std::uint8_t* fn) {
  if (fn == nullptr) {
    return 0;
  }
  const std::uint8_t* const end = function_end(fn, 0x40);
  std::uint32_t this_regs = 1u << 7;
  for (const std::uint8_t* p = fn; p < end;) {
    if (puphook::core::memory::is_flow_exit(p, end) && p != fn) {
      break;
    }
    puphook::core::memory::mem_insn insn{};
    if (puphook::core::memory::decode_mem_insn(p, end, insn)) {
      track_this_reg(this_regs, insn);
      if (insn.rex_w && insn.opcode == 0x8D && insn.reg == 0 && insn.base >= 0 &&
          insn.base < 16 && ((this_regs >> insn.base) & 1u) != 0 && insn.disp > 0x100 &&
          insn.disp < 0x2000) {
        return insn.disp;
      }
    }
    const std::size_t len = puphook::core::memory::insn_length(p, end);
    p += len != 0 ? len : 1;
  }
  return 0;
}

inline const std::uint8_t* first_direct_call(const std::uint8_t* fn) {
  if (fn == nullptr) {
    return nullptr;
  }
  const std::uint8_t* const end = function_end(fn, 0x80);
  for (const std::uint8_t* p = fn; p + 5 <= end; ++p) {
    if (p[0] == 0xE8) {
      std::int32_t rel = 0;
      std::memcpy(&rel, p + 1, 4);
      return p + 5 + rel;
    }
  }
  return nullptr;
}

inline void collect_direct_calls(const std::uint8_t* fn, const std::uint8_t** out, int& count,
  int max_count, std::size_t span = 0x700) {
  if (fn == nullptr || out == nullptr || max_count <= 0) {
    return;
  }
  const std::uint8_t* const end = function_end(fn, span);
  for (const std::uint8_t* p = fn; p + 5 <= end && count < max_count;) {
    if (puphook::core::memory::is_flow_exit(p, end) && p != fn) {
      break;
    }
    if (p[0] == 0xE8) {
      std::int32_t rel = 0;
      std::memcpy(&rel, p + 1, 4);
      const std::uint8_t* target = p + 5 + rel;
      bool seen = false;
      for (int i = 0; i < count; ++i) {
        if (out[i] == target) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        out[count++] = target;
      }
      p += 5;
      continue;
    }
    const std::size_t len = puphook::core::memory::insn_length(p, end);
    p += len != 0 ? len : 1;
  }
}

inline void* vtable_fn_at(void* method, std::size_t slot) {
  if (method == nullptr) {
    return nullptr;
  }
  const auto executable = puphook::core::memory::module_ranges("client.so", PROT_EXEC | PROT_READ);
  const auto fn_addr = reinterpret_cast<std::uintptr_t>(method);
  void* found = nullptr;
  puphook::core::memory::for_each([&](const puphook::core::memory::entry& map) {
    if ((map.protection & PROT_READ) == 0 || (map.protection & PROT_EXEC) != 0 ||
        !puphook::core::memory::module_name_matches(map.path, "client.so", false)) {
      return true;
    }
    const auto* begin = reinterpret_cast<const std::uintptr_t*>(map.start);
    const auto* end = reinterpret_cast<const std::uintptr_t*>(
      map.end - ((map.end - map.start) % sizeof(void*)));
    for (const auto* cursor = begin; cursor < end; ++cursor) {
      if (*cursor != fn_addr) {
        continue;
      }
      const auto* first = cursor;
      while (first > begin) {
        const auto prev = first[-1];
        if (!puphook::core::memory::contains(executable, prev, 1)) {
          break;
        }
        --first;
      }
      if (slot < static_cast<std::size_t>(end - first) &&
          puphook::core::memory::contains(executable, first[slot], 1)) {
        found = reinterpret_cast<void*>(first[slot]);
        return false;
      }
    }
    return true;
  });
  return found;
}

inline void collect_virtual_slots(const std::uint8_t* fn, std::size_t* out, int& count, int max_count) {
  if (fn == nullptr || out == nullptr) {
    return;
  }
  const std::uint8_t* const end = function_end(fn);
  count = 0;
  for (const std::uint8_t* p = fn; p + 6 < end;) {
    if (p[0] == 0xFF && p[1] == 0x90) {
      std::int32_t disp = 0;
      std::memcpy(&disp, p + 2, 4);
      if (disp >= 16 && (disp & 7) == 0 && disp < 0x2000) {
        const std::size_t slot = static_cast<std::size_t>(disp / 8);
        bool seen = false;
        for (int i = 0; i < count; ++i) {
          if (out[i] == slot) {
            seen = true;
            break;
          }
        }
        if (!seen && count < max_count) {
          out[count++] = slot;
        }
      }
      p += 6;
      continue;
    }
    p += 1;
  }
}

inline bool parse_weapon_spread(const std::uint8_t* fn, int& mode, int& info_ptr, int& stride) {
  if (fn == nullptr) {
    return false;
  }
  const std::uint8_t* const end = function_end(fn, 0x80);
  for (const std::uint8_t* p = fn; p + 18 < end; ++p) {
    if (p[0] != 0x48 || p[1] != 0x63 || p[2] != 0x87) {
      continue;
    }
    const std::uint8_t* q = p + 7;
    if (q[0] != 0x48 || q[1] != 0xC1 || q[2] != 0xE0) {
      continue;
    }
    const int shift = q[3];
    q += 4;
    if (q[0] != 0x48 || q[1] != 0x03 || q[2] != 0x87) {
      continue;
    }
    std::int32_t mode_disp = 0;
    std::int32_t info_disp = 0;
    std::memcpy(&mode_disp, p + 3, 4);
    std::memcpy(&info_disp, q + 3, 4);
    if (mode_disp > 0x200 && mode_disp < 0x3000 && info_disp > 0x200 && info_disp < 0x3000 &&
        shift >= 1 && shift <= 8) {
      mode = mode_disp;
      info_ptr = info_disp;
      stride = 1 << shift;
      return true;
    }
  }
  return false;
}

inline int first_info_disp(const std::uint8_t* fn, int min_disp, int max_disp) {
  if (fn == nullptr) {
    return 0;
  }
  const std::uint8_t* const end = function_end(fn);
  int best = 0;
  for (const std::uint8_t* p = fn; p < end;) {
    puphook::core::memory::mem_insn insn{};
    if (puphook::core::memory::decode_mem_insn(p, end, insn) && insn.base >= 0 &&
        insn.disp >= min_disp && insn.disp < max_disp) {
      if (best == 0 || insn.disp < best) {
        best = insn.disp;
      }
    }
    const std::size_t len = puphook::core::memory::insn_length(p, end);
    p += len != 0 ? len : 1;
  }
  return best;
}

inline int info_disp_near(const std::uint8_t* fn, int exact) {
  if (fn == nullptr || exact <= 0) {
    return 0;
  }
  const std::uint8_t* const end = function_end(fn);
  for (const std::uint8_t* p = fn; p < end;) {
    puphook::core::memory::mem_insn insn{};
    if (puphook::core::memory::decode_mem_insn(p, end, insn) && insn.disp == exact) {
      return exact;
    }
    const std::size_t len = puphook::core::memory::insn_length(p, end);
    p += len != 0 ? len : 1;
  }
  return 0;
}

inline bool contains_disp(const int* disps, int count, int value) {
  for (int i = 0; i < count; ++i) {
    if (disps[i] == value) {
      return true;
    }
  }
  return false;
}

inline bool confirm_rel(int& dest_delta, int base, int candidate_delta, const int* disps, int count) {
  if (base < 256 || base >= 8192) {
    return false;
  }
  const int absolute = base + candidate_delta;
  if (absolute < 256 || absolute >= 8192 || !contains_disp(disps, count, absolute)) {
    return false;
  }
  dest_delta = candidate_delta;
  return true;
}

inline void collect_raw_disp32(const std::uint8_t* fn, int* out, int& count, int max_count,
  int min_disp, int max_disp, std::size_t span = 0x800) {
  if (fn == nullptr || out == nullptr || max_count <= 0) {
    return;
  }
  const std::uint8_t* const end = function_end(fn, span);
  for (const std::uint8_t* p = fn; p + 4 <= end; ++p) {
    std::int32_t disp = 0;
    std::memcpy(&disp, p, 4);
    if (disp < min_disp || disp >= max_disp) {
      continue;
    }
    bool seen = false;
    for (int i = 0; i < count; ++i) {
      if (out[i] == disp) {
        seen = true;
        break;
      }
    }
    if (!seen && count < max_count) {
      out[count++] = disp;
    }
  }
}

inline void pick_token_cluster(int reload, const int* disps, int count, layout& out) {
  if (reload < 256 || reload >= 8192 || disps == nullptr || count <= 0) {
    return;
  }
  if (confirm_rel(out.reload_to_crit_checks, reload, -236, disps, count)) {
    out.reload_to_token_bucket = -240;
    out.reload_to_seed_requests = -232;
    return;
  }
  int best = 0;
  for (int i = 0; i < count; ++i) {
    const int checks = disps[i];
    if (checks < reload - 256 || checks > reload - 16 || (checks & 3) != 0) {
      continue;
    }
    if (best == 0 || std::abs((reload - 236) - checks) < std::abs((reload - 236) - best)) {
      best = checks;
    }
  }
  if (best > 0) {
    out.reload_to_crit_checks = best - reload;
    out.reload_to_token_bucket = out.reload_to_crit_checks - 4;
    out.reload_to_seed_requests = out.reload_to_crit_checks + 4;
  }
}

inline std::size_t vtable_index_of(void* fn) {
  if (fn == nullptr) {
    return 0;
  }
  const auto executable = puphook::core::memory::module_ranges("client.so", PROT_EXEC | PROT_READ);
  const auto fn_addr = reinterpret_cast<std::uintptr_t>(fn);
  std::size_t found = 0;
  puphook::core::memory::for_each([&](const puphook::core::memory::entry& map) {
    if ((map.protection & PROT_READ) == 0 || (map.protection & PROT_EXEC) != 0 ||
        !puphook::core::memory::module_name_matches(map.path, "client.so", false)) {
      return true;
    }
    const auto* begin = reinterpret_cast<const std::uintptr_t*>(map.start);
    const auto* end = reinterpret_cast<const std::uintptr_t*>(
      map.end - ((map.end - map.start) % sizeof(void*)));
    for (const auto* slot = begin; slot < end; ++slot) {
      if (*slot != fn_addr) {
        continue;
      }
      const auto* first = slot;
      while (first > begin) {
        const auto prev = first[-1];
        if (!puphook::core::memory::contains(executable, prev, 1)) {
          break;
        }
        --first;
      }
      found = static_cast<std::size_t>(slot - first);
      return false;
    }
    return true;
  });
  return found;
}

inline void* find_fn(const char* pattern) {
  return pattern != nullptr ? sigscan_module("client.so", pattern) : nullptr;
}

inline void assign_if(int& dest, int value) {
  if (value > 0) {
    dest = value;
  }
}

inline void assign_if(std::size_t& dest, std::size_t value) {
  if (value > 0) {
    dest = value;
  }
}

inline void resolve(layout& out) {
  auto* const spread_fn = static_cast<const std::uint8_t*>(find_fn(sigs::ctf_weapon_base_gun_get_weapon_spread));
  auto* const spread_fn_fallback =
    spread_fn != nullptr ? spread_fn
                         : static_cast<const std::uint8_t*>(find_fn(sigs::tf_weapon_base_gun_get_bullet_spread));
  out.get_weapon_spread_fn = const_cast<void*>(static_cast<const void*>(spread_fn_fallback));
  assign_if(out.get_weapon_spread, vtable_index_of(out.get_weapon_spread_fn));
  out.get_range_fn = find_fn(sigs::ctf_weapon_base_get_range);
  assign_if(out.get_range, vtable_index_of(out.get_range_fn));
  assign_if(out.weapon_info_range,
    first_info_disp(static_cast<const std::uint8_t*>(out.get_range_fn), 1700, 2000));

  int stride = 0;
  if (parse_weapon_spread(spread_fn_fallback, out.weapon_mode, out.gun_weapon_data, stride)) {
    out.weapon_info_stride = stride;
  }

  auto* const gun_crit = static_cast<const std::uint8_t*>(find_fn(sigs::ctf_weapon_base_calc_is_attack_critical));
  auto* const melee_crit =
    static_cast<const std::uint8_t*>(find_fn(sigs::ctf_weapon_base_melee_calc_is_attack_critical));
  static tf2_netvars::lazy_offset last_crit{"DT_TFWeaponBase", {"m_flLastCritCheckTime"}};
  const int last = last_crit;
  if (last >= 256 && last < 8192) {
    if (out.gun_weapon_data <= 0) {
      int disps[32]{};
      int count = 0;
      collect_this_disps(gun_crit, disps, count, 32, last - 80, last);
      for (int i = 0; i < count; ++i) {
        if ((disps[i] & 7) == 0) {
          out.gun_weapon_data = disps[i];
        }
      }
    }
    if (out.melee_weapon_data <= 0) {
      int melee_disps[32]{};
      int melee_count = 0;
      collect_this_disps(melee_crit, melee_disps, melee_count, 32, last, last + 400);
      for (int i = 0; i < melee_count; ++i) {
        if ((melee_disps[i] & 7) == 0) {
          out.melee_weapon_data = melee_disps[i];
          break;
        }
      }
    }
  }

  const int spread_from_info = first_info_disp(spread_fn_fallback, 1700, 2000);
  if (spread_from_info > 0) {
    out.weapon_info_spread = spread_from_info;
  }
  if (out.weapon_info_range > 8) {
    out.weapon_info_data = out.weapon_info_range - 8;
  } else if (out.weapon_info_spread > 12) {
    out.weapon_info_data = out.weapon_info_spread - 12;
  } else {
    assign_if(out.weapon_info_data, first_info_disp(gun_crit, 1700, 2000));
  }
  const int fire_from_crit = info_disp_near(gun_crit, out.weapon_info_data > 0 ? out.weapon_info_data + 20 : 0);
  if (fire_from_crit > 0) {
    out.weapon_info_fire_delay = fire_from_crit;
  } else if (out.weapon_info_data > 0) {
    const int guessed = first_info_disp(gun_crit, out.weapon_info_data + 16, out.weapon_info_data + 32);
    assign_if(out.weapon_info_fire_delay, guessed);
  }
  if (out.weapon_info_data > 0) {
    if (out.weapon_info_range == out.weapon_info_data + 8) {
      out.weapon_info_damage = out.weapon_info_data;
      out.weapon_info_bullets = out.weapon_info_data + 4;
    }
    if (out.weapon_info_stride == 64 && out.weapon_info_range == out.weapon_info_data + 8) {
      out.weapon_info_projectile = out.weapon_info_data + 44;
      out.weapon_info_proj_speed = out.weapon_info_data + 52;
      out.weapon_info_smack_delay = out.weapon_info_data + 56;
      out.weapon_info_rapid_fire = out.weapon_info_data + 60;
    }
  }

  std::size_t crit_virtuals[16]{};
  int crit_virtual_count = 0;
  collect_virtual_slots(gun_crit, crit_virtuals, crit_virtual_count, 16);
  for (int i = 0; i < crit_virtual_count; ++i) {
    if (crit_virtuals[i] >= 480 && crit_virtuals[i] < 520) {
      if (out.can_fire_critical_shot == 0) {
        out.can_fire_critical_shot = crit_virtuals[i];
      } else if (out.can_fire_random_critical_shot == 0 &&
                 crit_virtuals[i] != out.can_fire_critical_shot) {
        out.can_fire_random_critical_shot = crit_virtuals[i];
      }
    }
  }

  out.get_slot_fn = find_fn(sigs::base_combat_weapon_get_slot);
  assign_if(out.get_slot, vtable_index_of(out.get_slot_fn));

  out.get_abs_origin_fn = find_fn(sigs::base_entity_get_abs_origin);
  out.get_abs_angles_fn = find_fn(sigs::base_entity_get_abs_angles);
  out.set_abs_origin_fn = find_fn(sigs::base_entity_set_abs_origin);
  out.set_abs_angles_fn = find_fn(sigs::base_entity_set_abs_angles);
  out.eye_position_fn = find_fn(sigs::tf_player_eye_position);
  out.setup_bones_fn = find_fn(sigs::base_animating_setup_bones);
  out.eye_angles_fn = find_fn(sigs::tf_player_eye_angles);
  out.get_damage_type_fn = find_fn(sigs::base_combat_weapon_get_damage_type);
  out.get_spread_angles_fn = find_fn(sigs::ctf_weapon_base_get_spread_angles);
  out.update_client_side_animation_fn = find_fn(sigs::tfplayer_update_client_side_animation);
  out.standard_blending_rules_fn = find_fn(sigs::base_animating_standard_blending_rules);
  out.build_transformations_fn = find_fn(sigs::tf_player_build_transformations);
  out.get_view_vectors_fn = find_fn(sigs::tf_game_rules_get_view_vectors);
  out.check_stuck_fn = find_fn(sigs::tf_game_movement_check_stuck);
  out.prediction_copy_ctor_fn = find_fn(sigs::prediction_copy_ctor);
  out.prediction_copy_transfer_fn = find_fn(sigs::prediction_copy_transfer);

  assign_if(out.get_abs_origin, vtable_index_of(out.get_abs_origin_fn));
  assign_if(out.get_abs_angles, vtable_index_of(out.get_abs_angles_fn));
  assign_if(out.abs_angles, parse_return_lea_disp(static_cast<const std::uint8_t*>(out.get_abs_angles_fn)));
  assign_if(out.abs_origin, parse_return_lea_disp(static_cast<const std::uint8_t*>(out.get_abs_origin_fn)));
  if (out.abs_angles > 12) {
    const int expected_origin = out.abs_angles - 12;
    if (out.abs_origin != expected_origin) {
      out.get_abs_origin_fn = nullptr;
      out.get_abs_origin = 0;
    }
    out.abs_origin = expected_origin;
  }
  assign_if(out.eflags,
    parse_eflags_from_clear_dirty(static_cast<const std::uint8_t*>(out.set_abs_origin_fn)));
  if (out.eflags <= 0) {
    assign_if(out.eflags,
      parse_eflags_from_clear_dirty(static_cast<const std::uint8_t*>(out.set_abs_angles_fn)));
  }
  assign_if(out.eye_position, vtable_index_of(out.eye_position_fn));
  assign_if(out.eye_angles, vtable_index_of(out.eye_angles_fn));
  assign_if(out.setup_bones, vtable_index_of(out.setup_bones_fn));
  assign_if(out.update_client_side_animation, vtable_index_of(out.update_client_side_animation_fn));
  assign_if(out.standard_blending_rules, vtable_index_of(out.standard_blending_rules_fn));
  assign_if(out.build_transformations, vtable_index_of(out.build_transformations_fn));
  assign_if(out.get_spread_angles, vtable_index_of(out.get_spread_angles_fn));
  assign_if(out.get_damage_type, vtable_index_of(out.get_damage_type_fn));
  assign_if(out.get_view_vectors, vtable_index_of(out.get_view_vectors_fn));
  assign_if(out.check_stuck, vtable_index_of(out.check_stuck_fn));

  {
    std::size_t damage_virtuals[4]{};
    int damage_virtual_count = 0;
    collect_virtual_slots(static_cast<const std::uint8_t*>(out.get_damage_type_fn),
      damage_virtuals, damage_virtual_count, 4);
    if (damage_virtual_count > 0) {
      out.get_weapon_id = damage_virtuals[0];
    } else if (out.get_damage_type > 0) {
      out.get_weapon_id = out.get_damage_type - 1;
    }
  }
  {
    const std::uint8_t* swing = static_cast<const std::uint8_t*>(find_fn(sigs::ctf_weapon_base_melee_do_swing));
    std::size_t swing_virtuals[4]{};
    int swing_virtual_count = 0;
    collect_virtual_slots(swing, swing_virtuals, swing_virtual_count, 4);
    if (swing_virtual_count > 0) {
      out.get_swing_range = swing_virtuals[0];
    }
  }

  out.process_movement_fn = vtable_fn_at(out.check_stuck_fn, out.process_movement);
  const std::uint8_t* process_fn =
    follow_jmp(static_cast<const std::uint8_t*>(out.process_movement_fn),
      function_end(static_cast<const std::uint8_t*>(out.process_movement_fn), 0x20));
  const std::uint8_t* process_parent = first_direct_call(process_fn);
  {
    int mv_probe[4]{};
    int mv_probe_count = 0;
    collect_arg_ptr_stores(process_parent, 2, mv_probe, mv_probe_count, 4, 1, 64);
    if (mv_probe_count == 0) {
      process_parent = nullptr;
    }
  }
  int packed_stores[8]{};
  int packed_store_count = 0;
  collect_xmmword_this_stores(process_fn, packed_stores, packed_store_count, 8, 1, 64);
  collect_xmmword_this_stores(process_parent, packed_stores, packed_store_count, 8, 1, 64);
  if (packed_store_count > 0) {
    assign_if(out.game_movement_player, static_cast<std::size_t>(packed_stores[0]));
    assign_if(out.game_movement_mv, static_cast<std::size_t>(packed_stores[0] + 8));
  }

  int player_stores[8]{};
  int player_store_count = 0;
  collect_arg_ptr_stores(process_fn, 6, player_stores, player_store_count, 8, 1, 0x4000);
  collect_arg_ptr_stores(process_parent, 6, player_stores, player_store_count, 8, 1, 0x4000);
  int mv_stores[8]{};
  int mv_store_count = 0;
  collect_arg_ptr_stores(process_fn, 2, mv_stores, mv_store_count, 8, 1, 64);
  collect_arg_ptr_stores(process_parent, 2, mv_stores, mv_store_count, 8, 1, 64);
  if (player_store_count > 0) {
    int player_disp = 0;
    int tf_disp = 0;
    for (int i = 0; i < player_store_count; ++i) {
      if (player_stores[i] > 0 && player_stores[i] <= 32 &&
          (player_disp == 0 || player_stores[i] < player_disp)) {
        player_disp = player_stores[i];
      }
      if (player_stores[i] > 32 && (tf_disp == 0 || player_stores[i] < tf_disp)) {
        tf_disp = player_stores[i];
      }
    }
    assign_if(out.game_movement_player, static_cast<std::size_t>(player_disp));
    assign_if(out.game_movement_tf_player, static_cast<std::size_t>(tf_disp));
  }
  if (mv_store_count > 0) {
    int mv_disp = mv_stores[0];
    for (int i = 1; i < mv_store_count; ++i) {
      if (mv_stores[i] > 0 && mv_stores[i] < mv_disp) {
        mv_disp = mv_stores[i];
      }
    }
    assign_if(out.game_movement_mv, static_cast<std::size_t>(mv_disp));
  }

  if (out.game_movement_player > 32) {
    out.game_movement_player = 0;
    out.game_movement_mv = 0;
  }
  if (out.game_movement_tf_player != 0 &&
      (out.game_movement_tf_player < 0x1000 || out.game_movement_tf_player > 0x4000)) {
    out.game_movement_tf_player = 0;
  }
}

inline void ensure_netvar_rels(layout& out) {
  if (out.netvar_rels_ready) {
    return;
  }

  static tf2_netvars::lazy_offset reload_nv{"DT_TFWeaponBase", {"m_iReloadMode"}};
  static tf2_netvars::lazy_offset last_nv{"DT_TFWeaponBase", {"m_flLastCritCheckTime"}};
  static tf2_netvars::lazy_offset prior_nv{"DT_TFWeaponBase", {"m_flReloadPriorNextFire"}};
  const int reload = reload_nv;
  const int last = last_nv;
  const int prior = prior_nv;
  if (reload < 256 || last < 256) {
    return;
  }

  auto* const gun_crit =
    static_cast<const std::uint8_t*>(find_fn(sigs::ctf_weapon_base_calc_is_attack_critical));
  auto* const melee_crit =
    static_cast<const std::uint8_t*>(find_fn(sigs::ctf_weapon_base_melee_calc_is_attack_critical));
  auto* const spread_fn = static_cast<const std::uint8_t*>(out.get_weapon_spread_fn);

  int disps[64]{};
  int count = 0;
  collect_this_disps(gun_crit, disps, count, 64, reload - 256, last + 32);
  collect_this_disps(spread_fn, disps, count, 64, reload - 16, last + 32);
  collect_raw_disp32(gun_crit, disps, count, 64, reload - 256, last + 32);
  collect_raw_disp32(spread_fn, disps, count, 64, reload - 16, last + 32);

  confirm_rel(out.reload_to_weapon_mode, reload, -4, disps, count);
  pick_token_cluster(reload, disps, count, out);
  confirm_rel(out.last_to_crit_time, last, -4, disps, count);
  confirm_rel(out.last_to_current_seed, last, 8, disps, count);
  confirm_rel(out.last_to_rapid_fire, last, 12, disps, count);
  if (out.last_to_current_seed != 0 || contains_disp(disps, count, last)) {
    out.last_to_check_frame = 4;
  }

  static tf2_netvars::lazy_offset inspect_nv{"DT_TFWeaponBase", {"m_nInspectStage"}};
  const int inspect = inspect_nv;
  if (inspect >= 256 && inspect < 8192) {
    auto* const swing = static_cast<const std::uint8_t*>(find_fn(sigs::ctf_weapon_base_melee_do_swing));
    int smack_disps[32]{};
    int smack_count = 0;
    collect_this_disps(swing, smack_disps, smack_count, 32, inspect, inspect + 64);
    collect_raw_disp32(swing, smack_disps, smack_count, 32, inspect, inspect + 64, 0x2000);
    confirm_rel(out.inspect_to_smack_time_rel, inspect, 28, smack_disps, smack_count);
    if (out.inspect_to_smack_time_rel == 0) {
      for (int i = 0; i < smack_count; ++i) {
        const int delta = smack_disps[i] - inspect;
        if (delta >= 16 && delta <= 48 && (delta % 4) == 0) {
          out.inspect_to_smack_time_rel = delta;
          break;
        }
      }
    }
  }

  if (prior >= 256 && prior < 8192) {
    if (out.gun_weapon_data > 0 &&
        out.gun_weapon_data - prior >= 4 &&
        out.gun_weapon_data - prior <= 16) {
      out.reload_prior_to_gun_info = out.gun_weapon_data - prior;
    } else if (contains_disp(disps, count, prior + 8)) {
      out.reload_prior_to_gun_info = 8;
      out.gun_weapon_data = prior + 8;
    } else if (contains_disp(disps, count, prior + 4)) {
      out.reload_prior_to_gun_info = 4;
      out.gun_weapon_data = prior + 4;
    }
  }

  if (out.melee_weapon_data <= 0) {
    int melee_disps[32]{};
    int melee_count = 0;
    collect_this_disps(melee_crit, melee_disps, melee_count, 32, last, last + 400);
    for (int i = 0; i < melee_count; ++i) {
      if ((melee_disps[i] & 7) == 0) {
        out.melee_weapon_data = melee_disps[i];
        break;
      }
    }
  }

  out.netvar_rels_ready = out.reload_to_token_bucket != 0 && out.last_to_current_seed != 0;
}

inline const layout& cached() {
  static layout resolved{};
  static std::once_flag once;
  std::call_once(once, [] { resolve(resolved); });
  ensure_netvar_rels(resolved);
  return resolved;
}

}

inline const layout& get() {
  return detail::cached();
}

namespace weapon {
inline int field_sane(int offset) {
  return offset >= 256 && offset < 8192 ? offset : 0;
}

inline int netvar_rel(int base, int delta) {
  return field_sane(base > 0 ? base + delta : 0);
}

inline int reload_mode() {
  static tf2_netvars::lazy_offset offset{"DT_TFWeaponBase", {"m_iReloadMode"}};
  return field_sane(offset);
}

inline int last_crit_check_time() {
  static tf2_netvars::lazy_offset offset{"DT_TFWeaponBase", {"m_flLastCritCheckTime"}};
  return field_sane(offset);
}

inline int reload_prior_next_fire() {
  static tf2_netvars::lazy_offset offset{"DT_TFWeaponBase", {"m_flReloadPriorNextFire"}};
  return field_sane(offset);
}

inline int observed_crit_chance() {
  static tf2_netvars::lazy_offset offset{"DT_TFWeaponBase", {"m_flObservedCritChance"}};
  return field_sane(offset);
}

inline int inspect_stage() {
  static tf2_netvars::lazy_offset offset{"DT_TFWeaponBase", {"m_nInspectStage"}};
  return field_sane(offset);
}

inline int last_fire_time() {
  static tf2_netvars::lazy_offset offset{"DT_TFWeaponBase", {"m_flLastFireTime"}};
  return field_sane(offset);
}

inline int rel_or_zero(int base, int delta) {
  return delta != 0 ? netvar_rel(base, delta) : 0;
}

inline int smack_time() {
  return rel_or_zero(inspect_stage(), get().inspect_to_smack_time_rel);
}

inline int weapon_mode() { return rel_or_zero(reload_mode(), get().reload_to_weapon_mode); }
inline int crit_token_bucket() { return rel_or_zero(reload_mode(), get().reload_to_token_bucket); }
inline int crit_checks() { return rel_or_zero(reload_mode(), get().reload_to_crit_checks); }
inline int crit_seed_requests() { return rel_or_zero(reload_mode(), get().reload_to_seed_requests); }
inline int crit_time() { return rel_or_zero(last_crit_check_time(), get().last_to_crit_time); }
inline int last_crit_check_frame() { return rel_or_zero(last_crit_check_time(), get().last_to_check_frame); }
inline int current_seed() { return rel_or_zero(last_crit_check_time(), get().last_to_current_seed); }
inline int last_rapid_fire_crit_check_time() {
  return rel_or_zero(last_crit_check_time(), get().last_to_rapid_fire);
}

inline bool fields_ready() {
  return field_sane(crit_token_bucket()) && field_sane(crit_checks()) &&
    field_sane(crit_seed_requests()) && field_sane(crit_time()) &&
    field_sane(current_seed()) && field_sane(last_rapid_fire_crit_check_time());
}
inline int current_attack_is_crit() { return 0; }
inline int current_crit_is_random() { return 0; }
inline int current_attack_is_during_demo_charge() { return 0; }
inline int gun_weapon_data() {
  const int from_netvar = rel_or_zero(reload_prior_next_fire(), get().reload_prior_to_gun_info);
  return from_netvar != 0 ? from_netvar : field_sane(get().gun_weapon_data);
}
inline int melee_weapon_data() { return get().melee_weapon_data; }
inline int weapon_info_data() { return get().weapon_info_data; }
inline int weapon_info_stride() { return get().weapon_info_stride; }
inline int weapon_info_spread() { return get().weapon_info_spread; }
inline int weapon_info_fire_delay() { return get().weapon_info_fire_delay; }
inline int weapon_info_range() { return get().weapon_info_range; }
inline int weapon_data_rel(int absolute) {
  const int data = weapon_info_data();
  return data > 0 && absolute >= data ? absolute - data : 0;
}
inline int weapon_data_fire_delay() { return weapon_data_rel(weapon_info_fire_delay()); }
inline int weapon_data_range() { return weapon_data_rel(weapon_info_range()); }
inline int weapon_data_spread() { return weapon_data_rel(weapon_info_spread()); }
inline int weapon_data_damage() { return weapon_data_rel(get().weapon_info_damage); }
inline int weapon_data_bullets() { return weapon_data_rel(get().weapon_info_bullets); }
inline int weapon_data_projectile_type() { return weapon_data_rel(get().weapon_info_projectile); }
inline int weapon_data_projectile_speed() { return weapon_data_rel(get().weapon_info_proj_speed); }
inline int weapon_data_smack_delay() { return weapon_data_rel(get().weapon_info_smack_delay); }
inline int weapon_data_rapid_fire() { return weapon_data_rel(get().weapon_info_rapid_fire); }
inline std::size_t get_weapon_id() { return get().get_weapon_id; }
inline std::size_t get_slot() { return get().get_slot; }
inline std::size_t get_damage_type() { return get().get_damage_type; }
inline std::size_t get_spread_angles() { return get().get_spread_angles; }
inline std::size_t get_weapon_spread() { return get().get_weapon_spread; }
inline std::size_t get_range() { return get().get_range; }
inline std::size_t get_swing_range() { return get().get_swing_range; }
inline std::size_t can_fire_critical_shot() { return get().can_fire_critical_shot; }
inline std::size_t can_fire_random_critical_shot() { return get().can_fire_random_critical_shot; }
}

namespace entity {
inline int collision() {
  static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_Collision"}};
  return offset;
}
inline int vec_mins() {
  static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_Collision", "m_vecMins"}};
  return offset;
}
inline int vec_maxs() {
  static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_Collision", "m_vecMaxs"}};
  return offset;
}
inline int abs_origin() { return get().abs_origin; }
inline int abs_angles() { return get().abs_angles; }
inline int eflags() { return get().eflags; }
}

namespace player {
inline std::size_t get_abs_origin() { return get().get_abs_origin; }
inline std::size_t get_abs_angles() { return get().get_abs_angles; }
inline std::size_t setup_bones() { return get().setup_bones; }
inline std::size_t build_transformations() { return get().build_transformations; }
inline std::size_t standard_blending_rules() { return get().standard_blending_rules; }
inline std::size_t update_client_side_animation() { return get().update_client_side_animation; }
inline std::size_t eye_position() { return get().eye_position; }
inline std::size_t eye_angles() { return get().eye_angles; }
inline std::size_t weapon_shoot_position() { return get().eye_position; }
}

namespace game_movement {
inline std::size_t player() { return get().game_movement_player; }
inline std::size_t move_data() { return get().game_movement_mv; }
inline std::size_t tf_player() { return get().game_movement_tf_player; }
inline std::size_t process_movement() { return get().process_movement; }
inline std::size_t check_stuck() { return get().check_stuck; }
inline std::size_t get_view_vectors() { return get().get_view_vectors; }
}

}

#endif
