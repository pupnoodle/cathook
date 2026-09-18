/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/interfaces/input.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef INPUT_HPP
#define INPUT_HPP

#include <cstddef>
#include <cstdint>
#include <climits>

#include "client.hpp"
#include "core/memory/code_scan.hpp"

struct verified_user_cmd {
  user_cmd cmd;
  unsigned int crc;
};

struct CameraThirdData_t {
  float pitch;
  float yaw;
  float dist;
  float lag;
  Vec3 hull_min;
  Vec3 hull_max;
};

static user_cmd* (*get_user_cmd_original)(void*, int sequence_number);

class Input {
public:
  static constexpr int command_buffer_size = 90;

  void create_move(int sequence_number, float input_sample_frametime, bool active) {
    auto** vtable = *reinterpret_cast<void***>(this);
    auto create_move_fn = reinterpret_cast<void (*)(void*, int, float, bool)>(vtable[3]);
    create_move_fn(this, sequence_number, input_sample_frametime, active);
  }

  auto commands() -> user_cmd* {
    static const std::ptrdiff_t off = [this] {
      auto** vtable = *reinterpret_cast<void***>(this);
      return vtable != nullptr ? decode_commands_offset(vtable) : std::ptrdiff_t{0};
    }();
    if (off <= 0) {
      return nullptr;
    }
    return *reinterpret_cast<user_cmd**>(reinterpret_cast<std::uintptr_t>(this) + off);
  }

  auto verified_commands() -> verified_user_cmd* {
    static const std::ptrdiff_t off = [this] {
      auto** vtable = *reinterpret_cast<void***>(this);
      if (vtable == nullptr) {
        return std::ptrdiff_t{0};
      }
      return decode_verified_offset(vtable);
    }();
    if (off <= 0) {
      return nullptr;
    }
    return *reinterpret_cast<verified_user_cmd**>(reinterpret_cast<std::uintptr_t>(this) + off);
  }

  user_cmd* get_user_cmd(int sequence_number) {
    if (sequence_number <= 0 || sequence_number > INT_MAX - command_buffer_size) {
      return nullptr;
    }

    auto* command_buffer = commands();
    if (command_buffer == nullptr) return nullptr;

    return &command_buffer[sequence_number % command_buffer_size];
  }

  verified_user_cmd* get_verified_user_cmd(int sequence_number) {
    if (sequence_number <= 0 || sequence_number > INT_MAX - command_buffer_size) {
      return nullptr;
    }

    auto* verified_buffer = verified_commands();
    if (verified_buffer == nullptr) {
      return nullptr;
    }

    auto* verified = &verified_buffer[sequence_number % command_buffer_size];
    return verified;
  }

  void to_thirdperson(void) {
    void** vtable = *(void***)this;

    void (*to_thirdperson_fn)(void*) = (void (*)(void*))vtable[32];

    to_thirdperson_fn(this);
  }

  bool is_thirdperson(void) {
    void** vtable = *(void***)this;

    bool (*is_thirdperson_fn)(void*) = (bool (*)(void*))vtable[31];

    return is_thirdperson_fn(this);
  }

  void to_firstperson(void) {
    void** vtable = *(void***)this;

    void (*to_firstperson_fn)(void*) = (void (*)(void*))vtable[33];

    to_firstperson_fn(this);
  }

private:
  static std::ptrdiff_t decode_member_offset_for_alloc(void** vtable, std::uint32_t alloc_size) {
    const auto* fn = static_cast<const std::uint8_t*>(vtable != nullptr ? vtable[0] : nullptr);
    if (fn == nullptr) {
      return 0;
    }
    return cathook::core::memory::member_store_after_alloc(fn, fn + 0x800, alloc_size);
  }

  static std::ptrdiff_t decode_commands_offset(void** vtable) {
    return decode_member_offset_for_alloc(vtable, command_buffer_size * sizeof(user_cmd) + 8);
  }

  static std::ptrdiff_t decode_verified_offset(void** vtable) {
    return decode_member_offset_for_alloc(vtable, command_buffer_size * sizeof(verified_user_cmd) + 8);
  }
};

inline static Input* input;

inline unsigned int crc32_process_byte(unsigned int crc, unsigned char value)
{
  crc ^= value;
  for (int bit = 0; bit < 8; ++bit) {
    const unsigned int mask = 0U - (crc & 1U);
    crc = (crc >> 1) ^ (0xEDB88320U & mask);
  }

  return crc;
}

inline unsigned int crc32_process_buffer(unsigned int crc, const void* data, int size)
{
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (int i = 0; i < size; ++i) {
    crc = crc32_process_byte(crc, bytes[i]);
  }

  return crc;
}

inline unsigned int user_cmd_checksum(const user_cmd& cmd)
{
  unsigned int crc = 0xFFFFFFFFU;
  crc = crc32_process_buffer(crc, &cmd.command_number, sizeof(cmd.command_number));
  crc = crc32_process_buffer(crc, &cmd.tick_count, sizeof(cmd.tick_count));
  crc = crc32_process_buffer(crc, &cmd.view_angles, sizeof(cmd.view_angles));
  crc = crc32_process_buffer(crc, &cmd.forwardmove, sizeof(cmd.forwardmove));
  crc = crc32_process_buffer(crc, &cmd.sidemove, sizeof(cmd.sidemove));
  crc = crc32_process_buffer(crc, &cmd.upmove, sizeof(cmd.upmove));
  crc = crc32_process_buffer(crc, &cmd.buttons, sizeof(cmd.buttons));
  crc = crc32_process_buffer(crc, &cmd.impulse, sizeof(cmd.impulse));
  crc = crc32_process_buffer(crc, &cmd.weapon_select, sizeof(cmd.weapon_select));
  crc = crc32_process_buffer(crc, &cmd.weapon_subtype, sizeof(cmd.weapon_subtype));
  crc = crc32_process_buffer(crc, &cmd.random_seed, sizeof(cmd.random_seed));
  crc = crc32_process_buffer(crc, &cmd.mouse_dx, sizeof(cmd.mouse_dx));
  crc = crc32_process_buffer(crc, &cmd.mouse_dy, sizeof(cmd.mouse_dy));
  return crc ^ 0xFFFFFFFFU;
}

inline void update_verified_user_cmd(int sequence_number, user_cmd* cmd)
{
  if (input == nullptr || cmd == nullptr) {
    return;
  }

  auto* verified_cmd = input->get_verified_user_cmd(sequence_number);
  if (verified_cmd == nullptr) {
    return;
  }

  verified_cmd->cmd = *cmd;
  verified_cmd->crc = user_cmd_checksum(*cmd);
}

static_assert(sizeof(user_cmd) == 0x48, "user_cmd layout mismatch");
static_assert(sizeof(verified_user_cmd) == 0x50, "verified_user_cmd layout mismatch");

#endif
