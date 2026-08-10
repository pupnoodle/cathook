/*
/^-----^\   data: 2026-05-01
V  o o  V  file: src/games/tf2/sdk/net_messages.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef NET_MESSAGES_HPP
#define NET_MESSAGES_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

#include "games/tf2/sdk/bitbuf.hpp"
#include "games/tf2/sdk/interfaces/net_channel.hpp"

constexpr int netmsg_type_bits = 6;
constexpr int net_tick = 3;
constexpr int net_set_convar = 5;
constexpr int clc_move = 9;
constexpr int svc_packet_entities = 26;
constexpr int num_new_command_bits = 4;
constexpr int max_new_commands = 15;
constexpr int num_backup_command_bits = 3;
constexpr int max_backup_commands = (1 << num_backup_command_bits) - 1;
constexpr float net_tick_scale = 100000.0f;

class net_message {
public:
  virtual ~net_message() = default;
  virtual void set_net_channel(net_channel_info* channel) = 0;
  virtual void set_reliable(bool state) = 0;
  virtual auto process() -> bool = 0;
  virtual auto read_from_buffer(bf_read& buffer) -> bool = 0;
  virtual auto write_to_buffer(bf_write& buffer) -> bool = 0;
  virtual auto is_reliable() const -> bool = 0;
  virtual auto get_type() const -> int = 0;
  virtual auto get_group() const -> int = 0;
  virtual auto get_name() const -> const char* = 0;
  virtual auto get_net_channel() const -> net_channel_info* = 0;
  virtual auto to_string() const -> const char* = 0;
  virtual auto incoming_message_for_processing(double net_time, int num_bytes) -> bool = 0;
  virtual auto get_size() const -> std::size_t = 0;
};

class net_message_base : public net_message {
public:
  net_message_base() = default;

  void set_net_channel(net_channel_info* channel) override {
    channel_ = channel;
  }

  void set_reliable(bool state) override {
    reliable_ = state;
  }

  auto process() -> bool override {
    return false;
  }

  auto is_reliable() const -> bool override {
    return reliable_;
  }

  auto get_group() const -> int override {
    return net_channel_info::generic;
  }

  auto get_net_channel() const -> net_channel_info* override {
    return channel_;
  }

  auto incoming_message_for_processing(double net_time, int num_bytes) -> bool override {
    (void)net_time;
    (void)num_bytes;
    return false;
  }

  auto get_size() const -> std::size_t override {
    return sizeof(net_message_base);
  }

protected:
  bool reliable_ = true;
  net_channel_info* channel_ = nullptr;
};

class clc_move_message final : public net_message_base {
public:
  clc_move_message() {
    reliable_ = false;
  }

  auto read_from_buffer(bf_read& buffer) -> bool override {
    (void)buffer;
    return false;
  }

  auto write_to_buffer(bf_write& buffer) -> bool override {
    buffer.write_u_bit_long(get_type(), netmsg_type_bits);
    length = data_out.get_num_bits_written();
    buffer.write_u_bit_long(static_cast<std::uint32_t>(new_commands), num_new_command_bits);
    buffer.write_u_bit_long(static_cast<std::uint32_t>(backup_commands), num_backup_command_bits);
    buffer.write_word(length);
    return buffer.write_bits(data_out.get_data(), length);
  }

  auto get_type() const -> int override {
    return clc_move;
  }

  auto get_size() const -> std::size_t override {
    return sizeof(*this);
  }

  auto get_group() const -> int override {
    return net_channel_info::move;
  }

  auto get_name() const -> const char* override {
    return "clc_Move";
  }

  auto to_string() const -> const char* override {
    return "clc_Move";
  }

  void* message_handler = nullptr;
  int backup_commands = 0;
  int new_commands = 0;
  int length = 0;
  bf_read data_in{};
  bf_write data_out{};
  std::array<std::uint8_t, 8> reserved{};
};

static_assert(sizeof(clc_move_message) == 0x78, "clc_move_message size mismatch");

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

static_assert(offsetof(clc_move_message, message_handler) == 0x18, "clc_move_message handler offset mismatch");
static_assert(offsetof(clc_move_message, backup_commands) == 0x20, "clc_move_message command offset mismatch");

class net_tick_message final : public net_message_base {
public:
  net_tick_message(int tick, float host_frame_time, float host_frame_time_std_deviation)
    : tick_(tick),
      host_frame_time_(host_frame_time),
      host_frame_time_std_deviation_(host_frame_time_std_deviation) {
    reliable_ = false;
  }

  auto read_from_buffer(bf_read& buffer) -> bool override {
    (void)buffer;
    return false;
  }

  auto write_to_buffer(bf_write& buffer) -> bool override {
    buffer.write_u_bit_long(get_type(), netmsg_type_bits);
    buffer.write_long(tick_);
    buffer.write_u_bit_long(static_cast<std::uint32_t>(std::clamp(static_cast<int>(net_tick_scale * host_frame_time_), 0, 65535)), 16);
    buffer.write_u_bit_long(static_cast<std::uint32_t>(std::clamp(static_cast<int>(net_tick_scale * host_frame_time_std_deviation_), 0, 65535)), 16);
    return !buffer.is_overflowed();
  }

  auto get_type() const -> int override {
    return net_tick;
  }

  auto get_size() const -> std::size_t override {
    return sizeof(*this);
  }

  auto get_name() const -> const char* override {
    return "net_Tick";
  }

  auto to_string() const -> const char* override {
    return "net_Tick";
  }

  void* message_handler = nullptr;

private:
  int tick_ = 0;
  float host_frame_time_ = 0.0f;
  float host_frame_time_std_deviation_ = 0.0f;
};

static_assert(offsetof(net_tick_message, message_handler) == 0x18, "net_tick_message handler offset mismatch");

class svc_packet_entities_message final : public net_message_base {
public:
  auto read_from_buffer(bf_read& buffer) -> bool override {
    (void)buffer;
    return false;
  }

  auto write_to_buffer(bf_write& buffer) -> bool override {
    (void)buffer;
    return false;
  }

  auto get_type() const -> int override {
    return svc_packet_entities;
  }

  auto get_group() const -> int override {
    return net_channel_info::entities;
  }

  auto get_name() const -> const char* override {
    return "svc_PacketEntities";
  }

  auto to_string() const -> const char* override {
    return "svc_PacketEntities";
  }

  void* message_handler = nullptr;
  void* unknown = nullptr;
  int max_entries = 0;
  int updated_entries = 0;
  bool is_delta = false;
  bool update_baseline = false;
  std::uint16_t padding = 0;
  int baseline = 0;
  int delta_from = 0;
  int length = 0;
  bf_read data_in{};
  bf_write data_out{};
};

static_assert(offsetof(svc_packet_entities_message, is_delta) == 0x30, "svc_packet_entities_message delta offset mismatch");
static_assert(offsetof(svc_packet_entities_message, data_in) == 0x40, "svc_packet_entities_message data offset mismatch");

class net_set_convar_message final : public net_message_base {
public:
  net_set_convar_message(const char* name, const char* value) {
    if (name != nullptr) {
      std::strncpy(name_, name, sizeof(name_) - 1);
    }
    if (value != nullptr) {
      std::strncpy(value_, value, sizeof(value_) - 1);
    }
  }

  auto read_from_buffer(bf_read& buffer) -> bool override {
    (void)buffer;
    return false;
  }

  auto write_to_buffer(bf_write& buffer) -> bool override {
    buffer.write_u_bit_long(get_type(), netmsg_type_bits);
    buffer.write_byte(1);
    buffer.write_string(name_);
    buffer.write_string(value_);
    return !buffer.is_overflowed();
  }

  auto get_type() const -> int override {
    return net_set_convar;
  }

  auto get_group() const -> int override {
    return net_channel_info::stringcmd;
  }

  auto get_name() const -> const char* override {
    return "net_SetConVar";
  }

  auto to_string() const -> const char* override {
    return "net_SetConVar";
  }

  void* message_handler = nullptr;

private:
  char name_[260]{};
  char value_[260]{};
};

static_assert(offsetof(net_set_convar_message, message_handler) == 0x18, "net_set_convar_message handler offset mismatch");

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif
