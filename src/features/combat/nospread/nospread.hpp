#ifndef NOSPREAD_HPP
#define NOSPREAD_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <dlfcn.h>
#include <limits>
#include <regex>
#include <string>
#include <cstdlib>

#include "core/types.hpp"
#include "core/shared/sigs.hpp"
#include "external/MD5/MD5.hpp"
#include "external/libsigscan/libsigscan.h"
#include "features/combat/random_crits/crit_hack.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/bitbuf.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/net_channel.hpp"
#include "games/tf2/sdk/interfaces/steam_runtime.hpp"

namespace nospread {

inline constexpr int k_text_msg = 5;
inline constexpr std::size_t k_delta_history = 12;

inline bool compensated_this_command = false;
inline bool waiting = false;
inline bool synced = false;
inline float server_time = 0.0f;
inline float mantissa_step = 0.0f;
inline double time_delta = 0.0;
inline double request_time = 0.0;
inline float next_ask = 0.0f;
inline std::deque<double> deltas{};

[[nodiscard]] inline double plat_float_time() {
  using fn = double (*)();
  static const fn resolved = []() -> fn {
    if (auto* symbol = dlsym(RTLD_DEFAULT, "Plat_FloatTime")) {
      return reinterpret_cast<fn>(symbol);
    }
    void* tier0 = open_loaded_library("libtier0.so");
    if (tier0 == nullptr) {
      return nullptr;
    }
    auto* symbol = reinterpret_cast<fn>(dlsym(tier0, "Plat_FloatTime"));
    dlclose(tier0);
    return symbol;
  }();
  return resolved != nullptr ? resolved() : 0.0;
}

[[nodiscard]] inline bool loopback() {
  return client_state != nullptr && client_state->m_NetChannel != nullptr &&
    client_state->m_NetChannel->is_loopback();
}

[[nodiscard]] inline bool custom_random_seed() {
  if (convar_system == nullptr) {
    return false;
  }
  static Convar* cvar = nullptr;
  if (cvar == nullptr) {
    cvar = convar_system->find_var("sv_usercmd_custom_random_seed");
  }
  return cvar != nullptr && cvar->get_int() != 0;
}

[[nodiscard]] inline float tick_dt() {
  return global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : (1.0f / 66.0f);
}

inline void begin_command() {
  compensated_this_command = false;
}

inline void reset() {
  waiting = false;
  synced = false;
  server_time = 0.0f;
  mantissa_step = 0.0f;
  time_delta = 0.0;
  request_time = 0.0;
  next_ask = 0.0f;
  deltas.clear();
}

[[nodiscard]] inline float calc_mantissa_step(float value) {
  const float next = std::nextafter(value, std::numeric_limits<float>::infinity());
  const float step = (next - value) * 1000.0f;
  if (!std::isfinite(step) || step <= 0.0f) {
    return 0.0f;
  }
  return std::pow(2.0f, std::ceil(std::log(step) / std::log(2.0f)));
}

[[nodiscard]] inline double median_delta() {
  if (deltas.empty()) {
    return 0.0;
  }
  std::deque<double> sorted = deltas;
  std::sort(sorted.begin(), sorted.end());
  const std::size_t mid = sorted.size() / 2;
  if ((sorted.size() % 2u) == 0u) {
    return (sorted[mid - 1] + sorted[mid]) * 0.5;
  }
  return sorted[mid];
}

[[nodiscard]] inline int time_seed() {
  const double float_time = plat_float_time() + time_delta;
  float ms = static_cast<float>(float_time * 1000.0);
  int bits = 0;
  std::memcpy(&bits, &ms, sizeof(bits));
  return bits & 255;
}

[[nodiscard]] inline int cmd_seed(const user_cmd* cmd) {
  if (cmd == nullptr) {
    return 0;
  }
  const int predicted = crit_hack::predict_cmd_num(cmd);
  if (predicted > 0) {
    return static_cast<int>(MD5_PseudoRandom(static_cast<unsigned int>(predicted)) & 0x7fffffff);
  }
  if (cmd->random_seed != 0) {
    return cmd->random_seed;
  }
  return static_cast<int>(MD5_PseudoRandom(static_cast<unsigned int>(cmd->command_number)) & 0x7fffffff);
}

[[nodiscard]] inline int hitscan_seed(user_cmd* cmd) {
  if (custom_random_seed()) {
    return time_seed();
  }
  if (cmd == nullptr) {
    return 0;
  }
  const int predicted = crit_hack::predict_cmd_num(cmd);
  if (predicted > 0 && predicted != cmd->command_number) {
    return static_cast<int>(MD5_PseudoRandom(static_cast<unsigned int>(predicted)) & 255);
  }
  if (cmd->random_seed != 0) {
    return cmd->random_seed & 255;
  }
  return cmd_seed(cmd) & 255;
}

inline void send_string_cmd(const char* command) {
  if (command == nullptr || *command == '\0') {
    return;
  }
  using send_fn = void (*)(ClientState*, const char*);
  static const send_fn send = reinterpret_cast<send_fn>(
    sigscan_module("engine.so", sigs::client_state_send_string_cmd));
  if (send != nullptr && client_state != nullptr) {
    send(client_state, command);
    return;
  }
  if (engine != nullptr) {
    engine->client_cmd_unrestricted(command);
  }
}

inline void ask() {
  if (engine == nullptr || !engine->is_in_game() || client_state == nullptr) {
    reset();
    return;
  }
  if (!config.aimbot.spread_compensation) {
    return;
  }
  if (!custom_random_seed()) {
    synced = true;
    return;
  }
  if (client_state->chokedcommands > 0) {
    return;
  }
  const float realtime = global_vars != nullptr ? global_vars->realtime : 0.0f;
  const float interval = waiting ? 0.35f : (synced ? 2.0f : 0.5f);
  if (realtime < next_ask) {
    return;
  }
  send_string_cmd("playerperf");
  waiting = true;
  request_time = plat_float_time();
  next_ask = realtime + interval;
}

inline bool parse_message(const std::string& text) {
  if (text.empty() || !custom_random_seed()) {
    return false;
  }
  static const std::regex perf{
    R"((\d+\.\d+)\s+\d+\s+\d+\s+\d+\.\d+\s+\d+\.\d+\s+vel\s+\d+\.\d+)"};
  static const std::regex extra{R"(\d+\.\d+\s+\d+\s+\d+)"};
  std::smatch match;
  if (std::regex_search(text, match, perf) && match.size() >= 2) {
    waiting = false;
    const float parsed = std::strtof(match[1].str().c_str(), nullptr);
    if (!std::isfinite(parsed) || parsed < server_time) {
      return true;
    }
    server_time = parsed;
    if (loopback()) {
      time_delta = 0.0;
      deltas.clear();
      synced = true;
    } else {
      deltas.push_back(static_cast<double>(server_time) - request_time + static_cast<double>(tick_dt()));
      while (deltas.size() > k_delta_history) {
        deltas.pop_front();
      }
      time_delta = median_delta() + static_cast<double>(tick_dt());
    }
    mantissa_step = calc_mantissa_step(server_time);
    synced = mantissa_step >= 1.0f || loopback();
    return true;
  }
  return std::regex_search(text, extra);
}

inline bool on_text_msg(bf_read* message) {
  if (message == nullptr || !message->is_valid() || message->data == nullptr) {
    return false;
  }
  bf_read copy = *message;
  copy.read_byte();
  char first[256]{};
  if (!copy.read_string(first, sizeof(first), true)) {
    return false;
  }
  if (parse_message(first)) {
    return true;
  }
  for (int i = 0; i < 4; ++i) {
    char extra[256]{};
    if (!copy.read_string(extra, sizeof(extra), true) || extra[0] == '\0') {
      break;
    }
    if (parse_message(extra)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline const char* status_text() {
  if (!custom_random_seed()) {
    return "MD5";
  }
  if (synced) {
    return "SYNC";
  }
  if (waiting) {
    return "WAIT";
  }
  return "NONE";
}

[[nodiscard]] inline float status_progress() {
  if (!custom_random_seed() || synced) {
    return 1.0f;
  }
  return std::clamp(static_cast<float>(deltas.size()) / static_cast<float>(k_delta_history), 0.0f, 1.0f);
}

}

#endif
