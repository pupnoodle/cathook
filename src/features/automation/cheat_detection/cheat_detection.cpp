#include "cheat_detection.hpp"

#include "core/math/math.hpp"
#include "core/player_manager.hpp"
#include "core/print.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <unordered_map>

namespace cheat_detection {

namespace {

constexpr float k_legal_pitch = 89.0f;
constexpr int k_infraction_cooldown_ticks = 120;
constexpr int k_choke_repeat_count = 4;
constexpr int k_max_usercmd_process_ticks = 24;
constexpr int k_simtime_wrap_ticks = 127;

enum tf_dmg_custom {
  tf_dmg_custom_none = 0,
  tf_dmg_custom_headshot = 1,
  tf_dmg_custom_backstab = 2,
  tf_dmg_custom_burning = 3
};

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
  int duck_over_ticks = 0;
  float last_sim_time = -1.0f;
  Vec3 last_origin{};
  bool have_origin = false;
  int stall_ticks = 0;
  std::deque<int> choke_sizes{};
  std::deque<int> rewind_ticks{};
  std::unordered_map<int, crit_history> crit_by_weapon{};
  std::uint32_t account_id = 0;
  int detections = 0;
  int last_infraction_tick = -100000;
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

int event_custom(GameEvent* event) {
  const int custom = event->get_int("custom");
  return custom != 0 ? custom : event->get_int("customkill");
}

bool speed_unreliable(Player* player) {
  return player->in_cond(TF_COND_SPEED_BOOST) ||
      player->in_cond(TF_COND_SHIELD_CHARGE) ||
      player->in_cond(TF_COND_HALLOWEEN_KART) ||
      player->in_cond(TF_COND_HALLOWEEN_KART_DASH) ||
      player->in_cond(TF_COND_HALLOWEEN_SPEED_BOOST) ||
      player->in_cond(TF_COND_SODAPOPPER_HYPE) ||
      player->in_cond(TF_COND_TELEPORTED) ||
      player->in_cond(TF_COND_RUNE_HASTE) ||
      player->in_cond(TF_COND_RUNE_AGILITY) ||
      player->in_cond(TF_COND_GRAPPLINGHOOK) ||
      player->in_cond(TF_COND_GRAPPLINGHOOK_LATCHED) ||
      player->in_cond(TF_COND_GRAPPLED_TO_PLAYER) ||
      player->in_cond(TF_COND_GRAPPLED_BY_PLAYER) ||
      player->in_cond(TF_COND_ROCKETPACK) ||
      player->in_cond(TF_COND_LOST_FOOTING) ||
      player->in_cond(TF_COND_AIR_CURRENT) ||
      player->in_cond(TF_COND_BLASTJUMPING) ||
      player->in_cond(TF_COND_KNOCKED_INTO_AIR) ||
      player->get_water_level() > 1;
}

bool look_unreliable(Player* player) {
  return player->in_cond(TF_COND_TAUNTING) ||
      player->in_cond(TF_COND_STUNNED) ||
      player->in_cond(TF_COND_HALLOWEEN_KART) ||
      player->in_cond(TF_COND_HALLOWEEN_THRILLER) ||
      player->in_cond(TF_COND_FREEZE_INPUT) ||
      player->in_cond(TF_COND_HALLOWEEN_GHOST_MODE);
}

bool mini_crit_boosted(Player* player) {
  return player->in_cond(TF_COND_OFFENSEBUFF) ||
      player->in_cond(TF_COND_ENERGY_BUFF) ||
      player->in_cond(TF_COND_MINICRITBOOSTED_ON_KILL) ||
      player->in_cond(TF_COND_NOHEALINGDAMAGEBUFF) ||
      player->in_cond(TF_COND_CRITBOOSTED_DEMO_CHARGE);
}

bool is_hitscan_weapon(Weapon* weapon) {
  if (weapon == nullptr || weapon->is_melee() || weapon->is_flamethrower()) {
    return false;
  }
  return weapon->get_projectile_type() == 0;
}

bool guaranteed_crit_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }
  if (weapon->get_weapon_id() == TF_WEAPON_SENTRY_REVENGE ||
      weapon->get_weapon_id() == TF_WEAPON_KNIFE) {
    return true;
  }
  return weapon->get_def_id() == Spy_m_TheDiamondback;
}

void infract(int index, player_detection& data, const char* reason) {
  if (global_vars != nullptr &&
      global_vars->tickcount - data.last_infraction_tick < k_infraction_cooldown_ticks) {
    return;
  }
  if (global_vars != nullptr) {
    data.last_infraction_tick = global_vars->tickcount;
  }

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
  if (look_unreliable(player)) {
    return;
  }

  const Vec3 angles = player->get_eye_angles();
  if (!std::isfinite(angles.x) || !std::isfinite(angles.y)) {
    infract(index, data, "invalid pitch");
    return;
  }

  const float pitch = azimuth_to_signed(angles.x);
  if (std::fabs(pitch) > k_legal_pitch) {
    infract(index, data, "invalid pitch");
  }
}

void check_duck_speed(int index, Player* player, player_detection& data, float origin_speed,
                      int sim_ticks) {
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_duck_speed) == 0) {
    data.duck_over_ticks = 0;
    return;
  }

  const float max_speed = player->get_max_speed();
  const bool ducked = player->is_ducking() && player->is_on_ground();
  const bool overspeed = ducked && !speed_unreliable(player) &&
      std::isfinite(origin_speed) && std::isfinite(max_speed) &&
      max_speed > 1.0f && origin_speed > max_speed * 1.35f;

  if (!overspeed) {
    data.duck_over_ticks = 0;
    return;
  }

  data.duck_over_ticks += std::max(1, sim_ticks);
  const int ticks_per_second =
      global_vars != nullptr && global_vars->interval_per_tick > 0.0f
      ? static_cast<int>(1.0f / global_vars->interval_per_tick)
      : 66;
  if (data.duck_over_ticks > ticks_per_second * 2) {
    data.duck_over_ticks = 0;
    infract(index, data, "duck speed");
  }
}

void check_flick(int index, Player* player, player_detection& data) {
  auto& samples = data.angles;
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_aim_flick) == 0) {
    samples.clear();
    return;
  }
  if (look_unreliable(player)) {
    samples.clear();
    return;
  }

  samples.push_front({player->get_eye_angles(), false});
  if (samples.size() > 3) {
    samples.pop_back();
  }
  if (samples.size() != 3) {
    return;
  }
  if (!samples[0].attacking && !samples[1].attacking) {
    return;
  }
  if (!vec3_finite(samples[0].angles) || !vec3_finite(samples[1].angles) ||
      !vec3_finite(samples[2].angles)) {
    return;
  }

  const float flick = angle_separation(samples[0].angles, samples[1].angles);
  const float noise = angle_separation(samples[0].angles, samples[2].angles);
  if (flick >= config.misc.cheat_detection.min_flick &&
      noise <= config.misc.cheat_detection.max_noise &&
      flick > noise + 10.0f) {
    samples.clear();
    infract(index, data, "aim flick");
  }
}

void check_simtime(int index, Player* player, player_detection& data, float* origin_speed_out,
                   int* origin_ticks_out) {
  *origin_speed_out = 0.0f;
  *origin_ticks_out = 0;

  const bool want_choke =
      (config.misc.cheat_detection.methods & Misc::CheatDetection::method_packet_choking) != 0;
  const bool want_lagcomp =
      (config.misc.cheat_detection.methods & Misc::CheatDetection::method_lagcomp_abuse) != 0;
  const bool want_duck =
      (config.misc.cheat_detection.methods & Misc::CheatDetection::method_duck_speed) != 0;
  if ((!want_choke && !want_lagcomp && !want_duck) || global_vars == nullptr) {
    data.choke_sizes.clear();
    data.rewind_ticks.clear();
    data.last_sim_time = -1.0f;
    data.stall_ticks = 0;
    data.have_origin = false;
    return;
  }

  const float sim_time = player->get_simulation_time();
  const Vec3 origin = player->get_network_origin();
  if (!std::isfinite(sim_time)) {
    return;
  }

  if (data.last_sim_time < 0.0f) {
    data.last_sim_time = sim_time;
    data.last_origin = origin;
    data.have_origin = true;
    data.stall_ticks = 0;
    return;
  }

  const int delta_ticks = time_to_ticks(sim_time - data.last_sim_time);
  if (delta_ticks == 0) {
    ++data.stall_ticks;
    return;
  }

  if (data.have_origin && delta_ticks > 0) {
    const float dt = ticks_to_time(delta_ticks);
    if (dt > 0.0f) {
      *origin_speed_out = distance_2d(origin, data.last_origin) / dt;
      *origin_ticks_out = delta_ticks;
    }
  }
  data.last_origin = origin;
  data.have_origin = true;

  const int stall = data.stall_ticks;
  data.stall_ticks = 0;
  data.last_sim_time = sim_time;

  if (want_lagcomp && delta_ticks < 0) {
    const int rewind = -delta_ticks;
    if (rewind <= k_simtime_wrap_ticks &&
        rewind >= std::max(3, config.misc.cheat_detection.lagcomp_min_delta)) {
      data.rewind_ticks.push_back(global_vars->tickcount);
      const int window = global_vars->interval_per_tick > 0.0f
          ? static_cast<int>(std::max(0.1f, config.misc.cheat_detection.lagcomp_window) /
                             global_vars->interval_per_tick)
          : 66;
      while (!data.rewind_ticks.empty() &&
             global_vars->tickcount - data.rewind_ticks.front() > window) {
        data.rewind_ticks.pop_front();
      }
      if (static_cast<int>(data.rewind_ticks.size()) >=
          std::max(2, config.misc.cheat_detection.lagcomp_burst_count)) {
        data.rewind_ticks.clear();
        infract(index, data, "lag-comp abuse");
      }
    }
    return;
  }

  if (!want_choke || stall <= 0 || delta_ticks < 0) {
    return;
  }

  const int minimum = std::max(8, config.misc.cheat_detection.min_choking_ticks);
  const int jump = std::min(delta_ticks, k_max_usercmd_process_ticks);
  if (stall < minimum || jump < minimum || jump > stall + 2) {
    data.choke_sizes.clear();
    return;
  }

  data.choke_sizes.push_back(stall);
  if (static_cast<int>(data.choke_sizes.size()) > k_choke_repeat_count) {
    data.choke_sizes.pop_front();
  }
  if (static_cast<int>(data.choke_sizes.size()) < k_choke_repeat_count) {
    return;
  }

  const auto minmax = std::minmax_element(data.choke_sizes.begin(), data.choke_sizes.end());
  if (*minmax.second - *minmax.first <= 2) {
    data.choke_sizes.clear();
    infract(index, data, "choking packets");
  }
}

void check_crit_manipulation(int index, Player* player, player_detection& data) {
  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_crit_manipulation) == 0) {
    data.crit_by_weapon.clear();
    return;
  }
  if (player->is_crit_boosted() || mini_crit_boosted(player)) {
    return;
  }

  for (auto& [weapon_id, history] : data.crit_by_weapon) {
    const int window = std::max(16, config.misc.cheat_detection.crit_window);
    if (static_cast<int>(history.hits.size()) < window) {
      continue;
    }
    const float rate = static_cast<float>(history.crits) / static_cast<float>(history.hits.size()) * 100.0f;
    if (rate >= config.misc.cheat_detection.crit_threshold) {
      history.hits.clear();
      history.crits = 0;
      infract(index, data, "crit manipulation");
    }
  }
}

void preserve_progress(player_detection& data) {
  const std::uint32_t account_id = data.account_id;
  const int detections = data.detections;
  const int last_infraction_tick = data.last_infraction_tick;
  const bool marked = data.marked;
  data = player_detection{};
  data.account_id = account_id;
  data.detections = detections;
  data.last_infraction_tick = last_infraction_tick;
  data.marked = marked;
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
      preserve_progress(data);
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

    float origin_speed = 0.0f;
    int origin_ticks = 0;
    check_simtime(index, player, data, &origin_speed, &origin_ticks);
    check_invalid_pitch(index, player, data);
    if (origin_ticks > 0) {
      check_duck_speed(index, player, data, origin_speed, origin_ticks);
    }
    check_flick(index, player, data);
    check_crit_manipulation(index, player, data);
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
  const int victim = engine->get_player_index_from_id(event->get_int("userid"));
  if (attacker <= 0 || attacker == engine->get_localplayer_index() || attacker == victim) {
    return;
  }

  Entity* entity = entity_list->entity_from_index(static_cast<unsigned int>(attacker));
  auto* player = static_cast<Player*>(entity);
  if (player == nullptr || entity->get_class_id() != class_id::PLAYER || player->is_dormant()) {
    return;
  }

  player_detection& data = players_[attacker];
  const int custom = event_custom(event);
  Weapon* weapon = player->get_weapon();

  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_aim_flick) != 0 &&
      !data.angles.empty() &&
      (custom == tf_dmg_custom_none || custom == tf_dmg_custom_headshot) &&
      is_hitscan_weapon(weapon)) {
    data.angles.front().attacking = true;
  }

  if ((config.misc.cheat_detection.methods & Misc::CheatDetection::method_crit_manipulation) != 0 &&
      !player->is_crit_boosted() && !mini_crit_boosted(player) &&
      !event->get_bool("minicrit") && custom == tf_dmg_custom_none &&
      weapon != nullptr && !weapon->is_melee() && !guaranteed_crit_weapon(weapon) &&
      weapon->are_random_crits_enabled() &&
      (attribute_manager == nullptr || weapon->get_mult_crit_chance() > 0.0f)) {
    crit_history& history = data.crit_by_weapon[weapon->get_def_id()];
    const bool crit = event->get_bool("crit");
    history.hits.push_back(crit);
    if (crit) {
      ++history.crits;
    }
    const int window = std::max(16, config.misc.cheat_detection.crit_window);
    while (static_cast<int>(history.hits.size()) > window) {
      if (history.hits.front()) {
        --history.crits;
      }
      history.hits.pop_front();
    }
  }
}

}
