/*
/^-----^\   data: 2026-04-06
V  o o  V  file: src/games/tf2/sdk/bitbuf.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef BITBUF_HPP
#define BITBUF_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#pragma pack(push, 4)
struct bf_read {
  const std::uint8_t* data = nullptr;
  int data_bytes = 0;
  int data_bits = 0;
  int current_bit = 0;
  bool overflow = false;
  bool assert_on_overflow = false;
  const char* debug_name = nullptr;

  [[nodiscard]] bool is_valid() const {
    return data != nullptr && data_bits >= 0 && data_bytes >= 0;
  }

  [[nodiscard]] bool is_overflowed() const {
    return overflow;
  }

  [[nodiscard]] int bits_left() const {
    return data_bits - current_bit;
  }

  [[nodiscard]] int bytes_left() const {
    const auto remaining_bits = bits_left();
    if (remaining_bits <= 0) {
      return 0;
    }

    return (remaining_bits + 7) / 8;
  }

  [[nodiscard]] const std::uint8_t* current_data() const {
    if (!is_valid()) {
      return nullptr;
    }

    const auto current_byte = current_bit / 8;
    if (current_byte < 0 || current_byte >= data_bytes) {
      return nullptr;
    }

    return data + current_byte;
  }

  int read_u_bit_long(int bits) {
    if (bits <= 0 || bits > 32 || data == nullptr || current_bit < 0 ||
        current_bit + bits > data_bits) {
      overflow = true;
      if (data_bits >= 0) {
        current_bit = data_bits;
      }
      return 0;
    }

    std::uint32_t value = 0;
    for (int i = 0; i < bits; ++i) {
      const int bit = current_bit + i;
      value |= static_cast<std::uint32_t>((data[bit >> 3] >> (bit & 7)) & 1u) << i;
    }
    current_bit += bits;
    return static_cast<int>(value);
  }

  int read_byte() {
    return read_u_bit_long(8) & 0xFF;
  }

  bool read_string(char* out, int max_len, bool line = false) {
    if (out == nullptr || max_len <= 0) {
      return false;
    }
    int written = 0;
    while (written + 1 < max_len) {
      if (bits_left() < 8) {
        overflow = true;
        break;
      }
      const int ch = read_byte();
      if (overflow || ch == 0 || (line && (ch == '\n' || ch == '\r'))) {
        break;
      }
      out[written++] = static_cast<char>(ch);
    }
    out[written] = '\0';
    return !overflow;
  }
};
#pragma pack(pop)

static_assert(sizeof(bf_read) == 0x20, "bf_read size mismatch");
static_assert(offsetof(bf_read, data) == 0, "bf_read data offset mismatch");
static_assert(offsetof(bf_read, data_bytes) == 8, "bf_read data_bytes offset mismatch");
static_assert(offsetof(bf_read, data_bits) == 12, "bf_read data_bits offset mismatch");
static_assert(offsetof(bf_read, current_bit) == 16, "bf_read current_bit offset mismatch");
static_assert(offsetof(bf_read, overflow) == 20, "bf_read overflow offset mismatch");
static_assert(offsetof(bf_read, debug_name) == 24, "bf_read debug_name offset mismatch");

class bf_write {
public:
  bf_write() = default;

  bf_write(void* data, int bytes, int bits = -1) {
    start_writing(data, bytes, 0, bits);
  }

  void start_writing(void* data, int bytes, int start_bit = 0, int bits = -1) {
    if (data == nullptr || bytes <= 0) {
      data_ = nullptr;
      data_bytes_ = 0;
      data_bits_ = 0;
      current_bit_ = 0;
      overflow_ = true;
      return;
    }

    bytes &= ~3;
    data_ = static_cast<std::uint8_t*>(data);
    data_bytes_ = bytes;
    data_bits_ = bits >= 0 ? std::min(bits, bytes * 8) : bytes * 8;
    current_bit_ = std::clamp(start_bit, 0, data_bits_);
    overflow_ = false;
  }

  void reset() {
    current_bit_ = 0;
    overflow_ = false;
  }

  [[nodiscard]] auto get_num_bytes_written() const -> int {
    return (current_bit_ + 7) >> 3;
  }

  [[nodiscard]] auto get_num_bits_written() const -> int {
    return current_bit_;
  }

  [[nodiscard]] auto get_max_num_bits() const -> int {
    return data_bits_;
  }

  [[nodiscard]] auto get_num_bits_left() const -> int {
    return data_bits_ - current_bit_;
  }

  [[nodiscard]] auto get_data() -> std::uint8_t* {
    return data_;
  }

  [[nodiscard]] auto get_data() const -> const std::uint8_t* {
    return data_;
  }

  [[nodiscard]] auto is_overflowed() const -> bool {
    return overflow_;
  }

  void write_one_bit(int value) {
    if (current_bit_ >= data_bits_ || data_ == nullptr) {
      set_overflow();
      return;
    }

    const int byte_index = current_bit_ >> 3;
    const int bit_index = current_bit_ & 7;
    const auto mask = static_cast<std::uint8_t>(1u << bit_index);

    if (value != 0) {
      data_[byte_index] |= mask;
    } else {
      data_[byte_index] &= static_cast<std::uint8_t>(~mask);
    }

    ++current_bit_;
  }

  void write_u_bit_long(std::uint32_t value, int bits) {
    if (bits < 0 || bits > 32 || get_num_bits_left() < bits) {
      current_bit_ = data_bits_;
      set_overflow();
      return;
    }

    for (int bit = 0; bit < bits; ++bit) {
      write_one_bit((value >> bit) & 1u);
    }
  }

  void write_byte(int value) {
    write_u_bit_long(static_cast<std::uint32_t>(value), 8);
  }

  void write_word(int value) {
    write_u_bit_long(static_cast<std::uint32_t>(value), 16);
  }

  void write_long(std::int32_t value) {
    write_u_bit_long(static_cast<std::uint32_t>(value), 32);
  }

  void write_float(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u_bit_long(bits, 32);
  }

  auto write_bits(const void* input_data, int bits) -> bool {
    if (input_data == nullptr || bits < 0 || get_num_bits_left() < bits) {
      current_bit_ = data_bits_;
      set_overflow();
      return false;
    }

    const auto* input = static_cast<const std::uint8_t*>(input_data);
    for (int bit = 0; bit < bits; ++bit) {
      const int byte_index = bit >> 3;
      const int bit_index = bit & 7;
      write_one_bit((input[byte_index] >> bit_index) & 1u);
    }

    return !overflow_;
  }

  auto write_string(const char* value) -> bool {
    if (value == nullptr) {
      write_byte(0);
      return !overflow_;
    }

    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
      write_byte(static_cast<unsigned char>(*cursor));
      if (overflow_) {
        return false;
      }
    }

    write_byte(0);
    return !overflow_;
  }

private:
  void set_overflow() {
    overflow_ = true;
  }

  std::uint8_t* data_ = nullptr;
  int data_bytes_ = 0;
  int data_bits_ = -1;
  int current_bit_ = 0;
  bool overflow_ = false;
  bool assert_on_overflow_ = true;
  const char* debug_name_ = nullptr;
};

static_assert(sizeof(bf_write) == 0x20, "bf_write size mismatch");

#endif
