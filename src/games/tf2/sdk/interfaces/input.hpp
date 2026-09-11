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
  static constexpr std::ptrdiff_t commands_offset = 0x108;
  static constexpr std::ptrdiff_t verified_commands_offset = 0x110;
  static constexpr int command_buffer_size = 90;

  void create_move(int sequence_number, float input_sample_frametime, bool active) {
    auto** vtable = *reinterpret_cast<void***>(this);
    auto create_move_fn = reinterpret_cast<void (*)(void*, int, float, bool)>(vtable[3]);
    create_move_fn(this, sequence_number, input_sample_frametime, active);
  }

  auto commands() -> user_cmd* {
    return *reinterpret_cast<user_cmd**>(reinterpret_cast<std::uintptr_t>(this) + commands_offset);
  }

  auto verified_commands() -> verified_user_cmd* {
    return *reinterpret_cast<verified_user_cmd**>(reinterpret_cast<std::uintptr_t>(this) + verified_commands_offset);
  }

  user_cmd* get_user_cmd(int sequence_number) {
    if (sequence_number <= 0 || sequence_number > INT_MAX - command_buffer_size) {
      return nullptr;
    }

    auto* command_buffer = commands();
    if (command_buffer == nullptr) return nullptr;

    auto* usercmd = &command_buffer[sequence_number % command_buffer_size];
    return usercmd->command_number == sequence_number ? usercmd : nullptr;
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
};

inline static Input* input;

static_assert(sizeof(user_cmd) == 0x48, "user_cmd layout mismatch");
static_assert(sizeof(verified_user_cmd) == 0x50, "verified_user_cmd layout mismatch");

#endif
