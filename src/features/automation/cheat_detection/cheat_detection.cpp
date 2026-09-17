#include "cheat_detection.hpp"

#include "core/math/math.hpp"
#include "core/player_manager.hpp"
#include "core/print.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>

namespace cheat_detection {

namespace {

struct angle_sample {
  Vec3 angles{};
  bool attacking = false;
};

struct crit_history {
  std::deque<bool> hits{};
  int crits = 0;
};

struct player_detection {
  std::deque<angle_sample> angles{};
  int duck_start_tick = 0;
  float last_sim_time = -1.0f;
  int stall_ticks = 0;
  std::deque<int> choke_sizes{};
  std::deque<int> lagcomp_burst_ticks{};
  std::unordered_map<int, crit_history> crit_by_weapon{};
  std::uint32_t account_id = 0;
  int detections = 0;
  bool marked = false;
};

std::unordered_map<int, player_detection> players_{};
int last_tickcount = -1;

float angle_separation(const Vec3& a, const Vec3& b) {
  Vec3 fa, fb;
  angle_vectors(a, &fa, nullptr, nullptr);
  angle_vectors(b, &fb, nullptr, nullptr);
  const float d = std::clamp(dot(fa, fb), -1.0f, 1.0f);
  return std::acos(d) * (180.0f / 3.14159265f);
}

void infract(int index, player_detection& data, const char* reason) {
  ++data.detections;
  const int required = std::max(1, config.misc.cheat_detection.detections_required);
  const bool mark = data.detections >= required && !data.marked;

  player_info info{};
  const char* name = (engine != nullptr && engine->get_player_info(index, &info)) ? info.name : "?";
  print("[cheat_detection] %s %s: %s\n", mark ? "marked" : "infracted", name, reason);

  if (mark) {
    data.marked = true;
    if (data.account_id != 0) {
      static_cast<void>(
          cathook::core::players::add_role(data.account_id, cathook::core::players::cheater_role, name));
    }
  }
}

void check_invalid_pitch(int index, Player* player, player_detection& data) {
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_invalid_pitch) == 0) {
    return;
  }
  if (std::fabs(player->get_eye_angles().x) == 90.0f) {
    infract(index, data, "invalid pitch");
  }
}

void check_duck_speed(int index, Player* player, player_detection& data) {
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_duck_speed) == 0 ||
      global_vars == nullptr) {
    data.duck_start_tick = 0;
    return;
  }

  const Vec3 velocity = player->get_velocity();
  const float speed_2d = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
  const bool fast_ducking = player->is_ducking() && player->is_on_ground() &&
      speed_2d > player->get_max_speed() * 0.5f;
  if (!fast_ducking) {
    data.duck_start_tick = 0;
    return;
  }
  if (data.duck_start_tick == 0) {
    data.duck_start_tick = global_vars->tickcount;
  }
  const int ticks_per_second =
      global_vars->interval_per_tick > 0.0f ? static_cast<int>(1.0f / global_vars->interval_per_tick) : 66;
  if (global_vars->tickcount - data.duck_start_tick > ticks_per_second) {
    data.duck_start_tick = 0;
    infract(index, data, "duck speed");
  }
}

void check_flick(int index, Player* player, player_detection& data) {
  auto& samples = data.angles;
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_aim_flick) == 0) {
    samples.clear();
    return;
  }

  samples.push_front({player->get_eye_angles(), false});
  if (samples.size() > 3) {
    samples.pop_back();
  }
  if (samples.size() != 3 ||
      (!samples[0].attacking && !samples[1].attacking && !samples[2].attacking)) {
    return;
  }

  const float flick = angle_separation(samples[0].angles, samples[1].angles);
  const float noise = angle_separation(samples[0].angles, samples[2].angles);
  if (flick >= config.misc.cheat_detection.min_flick &&
      noise <= config.misc.cheat_detection.max_noise) {
    samples.clear();
    infract(index, data, "aim flick");
  }
}

void check_choking(int index, Player* player, player_detection& data) {
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_packet_choking) == 0 ||
      global_vars == nullptr) {
    data.choke_sizes.clear();
    data.last_sim_time = -1.0f;
    data.stall_ticks = 0;
    return;
  }

  const float sim_time = player->get_simulation_time();
  if (data.last_sim_time < 0.0f) {
    data.last_sim_time = sim_time;
    return;
  }

  if (sim_time > data.last_sim_time) {
    if (data.stall_ticks > 0) {
      data.choke_sizes.push_back(data.stall_ticks);
      if (data.choke_sizes.size() >= 3) {
        const int minimum = std::max(2, config.misc.cheat_detection.min_choking_ticks);
        bool infract_now = true;
        for (const int size : data.choke_sizes) {
          if (size < minimum) {
            infract_now = false;
          }
        }
        data.choke_sizes.clear();
        if (infract_now) {
          infract(index, data, "choking packets");
        }
      }
    }
    data.stall_ticks = 0;
    data.last_sim_time = sim_time;
  } else {
    ++data.stall_ticks;
  }
}

void check_lagcomp(int index, Player* player, player_detection& data) {
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_lagcomp_abuse) == 0 ||
      global_vars == nullptr) {
    data.lagcomp_burst_ticks.clear();
    return;
  }

  const float delta = global_vars->curtime - player->get_simulation_time();
  const int delta_ticks = global_vars->interval_per_tick > 0.0f
      ? static_cast<int>(delta / global_vars->interval_per_tick)
      : 0;
  const int min_delta = std::max(2, config.misc.cheat_detection.lagcomp_min_delta);
  if (delta_ticks <= min_delta) {
    return;
  }

  data.lagcomp_burst_ticks.push_back(global_vars->tickcount);
  const int window = global_vars->interval_per_tick > 0.0f
      ? static_cast<int>(std::max(0.1f, config.misc.cheat_detection.lagcomp_window) /
                         global_vars->interval_per_tick)
      : 66;
  while (!data.lagcomp_burst_ticks.empty() &&
         global_vars->tickcount - data.lagcomp_burst_ticks.front() > window) {
    data.lagcomp_burst_ticks.pop_front();
  }

  if (static_cast<int>(data.lagcomp_burst_ticks.size()) >=
      std::max(1, config.misc.cheat_detection.lagcomp_burst_count)) {
    data.lagcomp_burst_ticks.clear();
    infract(index, data, "lag-comp abuse");
  }
}

void check_crit_manipulation(int index, player_detection& data) {
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_crit_manipulation) == 0) {
    data.crit_by_weapon.clear();
    return;
  }
  for (auto& [weapon_id, history] : data.crit_by_weapon) {
    const int window = std::max(1, config.misc.cheat_detection.crit_window);
    if (static_cast<int>(history.hits.size()) < window || history.hits.empty()) {
      continue;
    }
    const float rate = static_cast<float>(history.crits) / history.hits.size() * 100.0f;
    if (rate >= config.misc.cheat_detection.crit_threshold) {
      history.hits.clear();
      history.crits = 0;
      infract(index, data, "crit manipulation");
    }
  }
}

}

void reset() {
  players_.clear();
  last_tickcount = -1;
}

void on_net_update_end() {
  if (global_vars == nullptr || engine == nullptr || entity_list == nullptr ||
      !engine->is_in_game()) {
    return;
  }

  if (global_vars->tickcount != last_tickcount + 1 && last_tickcount != -1) {
    last_tickcount = global_vars->tickcount;
    return;
  }
  last_tickcount = global_vars->tickcount;

  if (config.misc.cheat_detection.methods == 0) {
    return;
  }

  const int local_index = engine->get_localplayer_index();
  const int max_clients = global_vars->max_clients;
  for (int index = 1; index <= max_clients; ++index) {
    if (index == local_index) {
      continue;
    }
    Entity* entity = entity_list->entity_from_index(static_cast<unsigned int>(index));
    auto* player = static_cast<Player*>(entity);
    player_detection& data = players_[index];

    player_info info{};
    const bool info_valid = engine->get_player_info(index, &info);
    if (entity == nullptr || entity->get_class_id() != class_id::PLAYER ||
        !player->is_alive() || player->is_dormant() ||
        (info_valid && info.fakeplayer)) {
      data = player_detection{};
      continue;
    }
    if (info_valid) {
      data.account_id = static_cast<std::uint32_t>(info.friends_id);
    }
    if (data.account_id != 0 &&
        cathook::core::players::has_role(data.account_id, cathook::core::players::cheater_role)) {
      data.marked = true;
      continue;
    }

    check_invalid_pitch(index, player, data);
    check_duck_speed(index, player, data);
    check_flick(index, player, data);
    check_choking(index, player, data);
    check_lagcomp(index, player, data);
    check_crit_manipulation(index, data);
  }
}

void on_game_event(GameEvent* event) {
  if (event == nullptr || engine == nullptr || entity_list == nullptr) {
    return;
  }

  const char* name = event->get_name();
  if (name == nullptr || std::strcmp(name, "player_hurt") != 0) {
    return;
  }

  const int attacker = engine->get_player_index_from_id(event->get_int("attacker"));
  if (attacker <= 0 || attacker == engine->get_localplayer_index()) {
    return;
  }

  Entity* entity = entity_list->entity_from_index(static_cast<unsigned int>(attacker));
  auto* player = static_cast<Player*>(entity);
  if (player == nullptr || entity->get_class_id() != class_id::PLAYER || player->is_dormant()) {
    return;
  }

  player_detection& data = players_[attacker];

  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_aim_flick) != 0 &&
      !data.angles.empty()) {
    data.angles.back().attacking = true;
  }

  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_crit_manipulation) != 0 &&
      !player->is_crit_boosted()) {
    Weapon* weapon = player->get_weapon();
    if (weapon != nullptr) {
      crit_history& history = data.crit_by_weapon[weapon->get_def_id()];
      const bool crit = event->get_bool("crit");
      history.hits.push_back(crit);
      if (crit) {
        ++history.crits;
      }
      const int window = std::max(1, config.misc.cheat_detection.crit_window);
      while (static_cast<int>(history.hits.size()) > window) {
        if (history.hits.front()) {
          --history.crits;
        }
        history.hits.pop_front();
      }
    }
  }
}

}
