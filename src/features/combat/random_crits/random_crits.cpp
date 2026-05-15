/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/random_crits/random_crits.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

// random_crits.cpp - pupnoodle/2026-1-5
#include "random_crits.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>

#include <dlfcn.h>

#include "MD5/MD5.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace random_crits
{
namespace
{

constexpr int weapon_random_range = 10000;
constexpr int melee_noncrit_threshold = 6000;
constexpr float crit_multiplier = 3.0f;
constexpr float base_crit_chance = 0.02f;
constexpr float melee_crit_chance = 0.15f;
constexpr float rapid_crit_duration = 2.0f;
constexpr float default_bucket_cap = 1000.0f;
constexpr int bucket_attempts = 1000;
constexpr int min_seed_scan = 256;
constexpr int max_seed_scan = 8192;

int selected_command_number = 0;
Weapon* selected_weapon = nullptr;
int selected_seed = 0;
int selected_roll = -1;
int selected_offset = 0;
std::atomic<int> indicator_command_number{0};

enum class selected_mode {
  none,
  force,
  skip
};

selected_mode mode = selected_mode::none;

using tier0_random_seed_fn = void (*)(int);
using tier0_random_int_fn = int (*)(int, int);

tier0_random_seed_fn g_tier0_random_seed = nullptr;
tier0_random_int_fn g_tier0_random_int = nullptr;
bool g_tier0_random_resolved = false;

bool resolve_tier0_random()
{
  if (g_tier0_random_resolved) {
    return g_tier0_random_seed != nullptr && g_tier0_random_int != nullptr;
  }

  g_tier0_random_resolved = true;
  g_tier0_random_seed = reinterpret_cast<tier0_random_seed_fn>(dlsym(RTLD_DEFAULT, "RandomSeed"));
  g_tier0_random_int = reinterpret_cast<tier0_random_int_fn>(dlsym(RTLD_DEFAULT, "RandomInt"));
  if (g_tier0_random_seed == nullptr || g_tier0_random_int == nullptr) {
    g_tier0_random_seed = reinterpret_cast<tier0_random_seed_fn>(dlsym(RTLD_DEFAULT, "_Z10RandomSeedi"));
    g_tier0_random_int = reinterpret_cast<tier0_random_int_fn>(dlsym(RTLD_DEFAULT, "_Z10RandomIntii"));
  }
  return g_tier0_random_seed != nullptr && g_tier0_random_int != nullptr;
}

struct crit_bucket_info
{
  bool valid = false;
  bool rapid_fire = false;
  bool crit_banned = false;
  float bucket_cap = default_bucket_cap;
  float crit_chance = 0.0f;
  float crit_chance_mult = 1.0f;
  float damage = 0.0f;
  float crit_cost = 0.0f;
  float damage_to_crit = 0.0f;
  float rapid_wait = 0.0f;
  float streaming_time = 0.0f;
  int available_crits = 0;
  int potential_crits = 0;
  int next_crit = 0;
};

float remap_value(float value, float input_min, float input_max, float output_min, float output_max)
{
  if (input_max == input_min) {
    return output_min;
  }

  const float progress = (value - input_min) / (input_max - input_min);
  return output_min + ((output_max - output_min) * std::clamp(progress, 0.0f, 1.0f));
}

float crit_bucket_cap()
{
  if (convar_system == nullptr) {
    return default_bucket_cap;
  }

  static Convar* bucket_cap = convar_system->find_var("tf_weapon_criticals_bucket_cap");
  return bucket_cap != nullptr ? bucket_cap->get_float() : default_bucket_cap;
}

int scaled_threshold(const int base_threshold, Weapon* weapon)
{
  const float crit_chance_mult = weapon != nullptr ? weapon->get_mult_crit_chance() : 1.0f;
  return std::clamp(static_cast<int>(static_cast<float>(base_threshold) * crit_chance_mult), 0, weapon_random_range);
}

float weapon_crit_chance(Player* localplayer, Weapon* weapon)
{
  if (localplayer == nullptr || weapon == nullptr) {
    return 0.0f;
  }

  const float crit_chance_mult = weapon->get_mult_crit_chance();
  if (weapon->is_melee()) {
    return melee_crit_chance * localplayer->get_crit_mult() * crit_chance_mult;
  }

  if (weapon->is_rapid_fire()) {
    const float rapid_base = base_crit_chance * localplayer->get_crit_mult();
    const float noncrit_duration = (rapid_crit_duration / std::clamp(rapid_base, 0.01f, 0.99f)) - rapid_crit_duration;
    return (1.0f / noncrit_duration) * crit_chance_mult;
  }

  return base_crit_chance * localplayer->get_crit_mult() * crit_chance_mult;
}

int crit_threshold(Player* localplayer, Weapon* weapon)
{
  return std::clamp(static_cast<int>(weapon_crit_chance(localplayer, weapon) * static_cast<float>(weapon_random_range)), 0, weapon_random_range);
}

int noncrit_threshold(Player* localplayer, Weapon* weapon)
{
  if (weapon != nullptr && weapon->is_melee()) {
    return scaled_threshold(melee_noncrit_threshold, weapon);
  }

  return crit_threshold(localplayer, weapon);
}

crit_bucket_info build_bucket_info(Player* localplayer, Weapon* weapon)
{
  crit_bucket_info info{};
  if (localplayer == nullptr || weapon == nullptr) {
    return info;
  }

  const bool melee = weapon->is_melee();
  const float bucket = weapon->crit_token_bucket();
  const int checks = std::max(weapon->crit_checks(), 0);
  const int seed_requests = std::max(weapon->crit_seed_requests(), 0);
  const int bullets = std::max(weapon->get_bullets_per_shot(), 1);
  const float base_damage = static_cast<float>(std::max(weapon->get_damage(), 0) * bullets);
  if (base_damage <= 0.0f) {
    return info;
  }

  info.valid = true;
  info.bucket_cap = std::max(crit_bucket_cap(), 1.0f);
  info.rapid_fire = weapon->is_rapid_fire();
  info.crit_chance_mult = weapon->get_mult_crit_chance();
  info.crit_chance = weapon_crit_chance(localplayer, weapon);

  float crit_damage = base_damage;
  if (info.rapid_fire) {
    const float fire_rate = std::max(weapon->get_fire_rate(), 0.01f);
    crit_damage *= rapid_crit_duration / fire_rate;
    if (crit_damage * crit_multiplier > info.bucket_cap) {
      crit_damage = info.bucket_cap / crit_multiplier;
    }
  }

  const float crit_ratio = checks > 0 ? static_cast<float>(seed_requests + 1) / static_cast<float>(checks + 1) : 0.0f;
  const float cost_mult = melee ? 0.5f : remap_value(crit_ratio, 0.1f, 1.0f, 1.0f, 3.0f);
  const float cost = crit_damage * crit_multiplier;
  const float cost_per_crit = (crit_multiplier * crit_damage / (melee ? 2.0f : 1.0f)) - base_damage;

  info.damage = base_damage;
  info.crit_cost = cost * cost_mult;
  info.damage_to_crit = std::max(0.0f, info.crit_cost - bucket);

  const auto count_affordable_crits = [&](float starting_bucket) {
    int test_checks = checks;
    int test_seed_requests = seed_requests;
    int affordable_crits = 0;

    for (int attempt = 0; attempt < bucket_attempts; ++attempt) {
      ++test_checks;
      ++test_seed_requests;

      const float test_ratio = test_checks > 0 ? static_cast<float>(test_seed_requests) / static_cast<float>(test_checks) : 0.0f;
      const float test_mult = melee ? 0.5f : remap_value(test_ratio, 0.1f, 1.0f, 1.0f, 3.0f);
      if (starting_bucket < info.bucket_cap) {
        starting_bucket = std::min(starting_bucket + base_damage, info.bucket_cap);
      }

      starting_bucket -= cost * test_mult;
      if (starting_bucket < 0.0f) {
        break;
      }

      ++affordable_crits;
    }

    return affordable_crits;
  };

  info.available_crits = count_affordable_crits(bucket);
  info.potential_crits = count_affordable_crits(info.bucket_cap);

  if (info.available_crits != info.potential_crits) {
    int test_checks = checks;
    int test_seed_requests = seed_requests;
    float test_bucket = bucket;
    float tick_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
    float last_rapid_check = weapon->last_rapid_fire_crit_check_time();

    for (int attempt = 0; attempt < bucket_attempts; ++attempt) {
      int future_checks = test_checks;
      int future_seed_requests = test_seed_requests;
      float future_bucket = test_bucket;
      int future_crits = 0;

      for (int future = 0; future < bucket_attempts; ++future) {
        ++future_checks;
        ++future_seed_requests;

        const float future_ratio = future_checks > 0 ? static_cast<float>(future_seed_requests) / static_cast<float>(future_checks) : 0.0f;
        const float future_mult = melee ? 0.5f : remap_value(future_ratio, 0.1f, 1.0f, 1.0f, 3.0f);
        if (future_bucket < info.bucket_cap) {
          future_bucket = std::min(future_bucket + base_damage, info.bucket_cap);
        }

        future_bucket -= cost * future_mult;
        if (future_bucket < 0.0f) {
          break;
        }

        ++future_crits;
      }

      if (info.available_crits < future_crits) {
        break;
      }

      if (!info.rapid_fire) {
        ++test_checks;
      } else if (global_vars != nullptr) {
        const float fire_rate = std::max(weapon->get_fire_rate(), global_vars->interval_per_tick);
        tick_time += std::ceil(fire_rate / global_vars->interval_per_tick) * global_vars->interval_per_tick;
        if (tick_time >= last_rapid_check + 1.0f || (attempt == 0 && test_bucket == info.bucket_cap)) {
          ++test_checks;
          last_rapid_check = tick_time;
        }
      }

      if (test_bucket < info.bucket_cap) {
        test_bucket = std::min(test_bucket + base_damage, info.bucket_cap);
      }

      ++info.next_crit;
    }
  }

  if (!melee && weapon->observed_crit_chance() > info.crit_chance + 0.1f) {
    info.crit_banned = true;
  }

  if (info.rapid_fire && global_vars != nullptr) {
    info.rapid_wait = std::max(0.0f, weapon->last_rapid_fire_crit_check_time() + 1.0f - global_vars->curtime);
  }

  if (weapon->crit_time() > 0.0f && global_vars != nullptr) {
    info.streaming_time = std::max(0.0f, weapon->crit_time() - global_vars->curtime);
  }

  return info;
}

int crit_mask(Player* localplayer, Weapon* weapon)
{
  const int weapon_index = weapon->to_entity()->get_index();
  const int player_index = localplayer->to_entity()->get_index();

  if (weapon->is_melee()) {
    return (weapon_index << 16) | (player_index << 8);
  }

  return (weapon_index << 8) | player_index;
}

int masked_crit_seed(Player* localplayer, Weapon* weapon, int seed)
{
  return seed ^ crit_mask(localplayer, weapon);
}

int crit_roll(Player* localplayer, Weapon* weapon, int seed)
{
  const int masked = masked_crit_seed(localplayer, weapon, seed);
  if (resolve_tier0_random()) {
    g_tier0_random_seed(masked);
    return g_tier0_random_int(0, weapon_random_range - 1);
  }

  int stream = masked;
  stream = stream * 214013 + 2531011;
  const int r = (stream >> 16) & 0x7fff;
  return r % weapon_random_range;
}

bool seed_matches(Player* localplayer, Weapon* weapon, int seed, bool want_crit, int roll)
{
  if (localplayer == nullptr || weapon == nullptr || masked_crit_seed(localplayer, weapon, seed) == weapon->current_seed()) {
    return false;
  }

  const int threshold = crit_threshold(localplayer, weapon);
  if (want_crit && roll >= threshold) {
    return false;
  }
  if (!want_crit && roll < noncrit_threshold(localplayer, weapon)) {
    return false;
  }

  return true;
}

int find_command_number(Player* localplayer, Weapon* weapon, int current_command_number, bool want_crit)
{
  const int seed_scan = std::clamp(config.random_crits.seed_scan, min_seed_scan, max_seed_scan);

  for (int offset = 0; offset <= seed_scan; ++offset) {
    const int command_number = current_command_number + offset;
    const int seed = MD5_PseudoRandom(static_cast<unsigned int>(command_number)) & INT_MAX;
    const int roll = crit_roll(localplayer, weapon, seed);

    if (seed_matches(localplayer, weapon, seed, want_crit, roll)) {
      return command_number;
    }
  }

  return 0;
}

bool wants_forced_crit(Weapon* weapon)
{
  if (weapon == nullptr) {
    return false;
  }

  if (config.debug.insider_settings_unlocked && config.random_crits.force_crits) {
    return true;
  }

  return config.debug.insider_settings_unlocked && config.random_crits.always_melee_crit && weapon->is_melee();
}

void clear_selection()
{
  selected_command_number = 0;
  selected_weapon = nullptr;
  selected_seed = 0;
  selected_roll = -1;
  selected_offset = 0;
  mode = selected_mode::none;
}

void apply_command_number(user_cmd* cmd, Weapon* weapon, int command_number, int roll, int offset, selected_mode new_mode)
{
  cmd->command_number = command_number;
  cmd->random_seed = MD5_PseudoRandom(static_cast<unsigned int>(cmd->command_number)) & INT_MAX;

  selected_command_number = cmd->command_number;
  selected_weapon = weapon;
  selected_seed = cmd->random_seed;
  selected_roll = roll;
  selected_offset = offset;
  mode = new_mode;
}

} // namespace

void run(user_cmd* cmd)
{
  if (cmd != nullptr && cmd->command_number > 0) {
    indicator_command_number.store(cmd->command_number, std::memory_order_relaxed);
  }

  clear_selection();

  if (cmd == nullptr || (cmd->buttons & IN_ATTACK) == 0) {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive() || localplayer->is_crit_boosted()) {
    return;
  }

  auto* weapon = localplayer->get_weapon();
  if (weapon == nullptr || !weapon->can_primary_attack()) {
    return;
  }

  if (!weapon->are_random_crits_enabled()) {
    return;
  }

  const auto bucket_info = build_bucket_info(localplayer, weapon);
  const bool force_crit = wants_forced_crit(weapon);

  if (force_crit && config.random_crits.respect_bucket) {
    const bool bucket_ready = !bucket_info.valid || (bucket_info.available_crits > 0 && !bucket_info.crit_banned && bucket_info.rapid_wait <= 0.0f);
    if (!weapon->can_fire_random_critical_shot(bucket_info.crit_chance) || !bucket_ready) {
      return;
    }
  }

  if (!force_crit && !config.random_crits.save_bucket) {
    return;
  }

  const int command_number = find_command_number(localplayer, weapon, cmd->command_number, force_crit);
  if (command_number != 0) {
    const int old_command_number = cmd->command_number;
    const int seed = MD5_PseudoRandom(static_cast<unsigned int>(command_number)) & INT_MAX;
    const int roll = crit_roll(localplayer, weapon, seed);
    apply_command_number(cmd, weapon, command_number, roll, std::max(0, command_number - old_command_number), force_crit ? selected_mode::force : selected_mode::skip);
    return;
  }

  if (force_crit && bucket_info.rapid_fire) {
    const int roll = crit_roll(localplayer, weapon, cmd->random_seed);
    apply_command_number(cmd, weapon, cmd->command_number, roll, 0, selected_mode::force);
    return;
  }

  if (!force_crit) {
    cmd->buttons &= ~IN_ATTACK;
  }
}

bool command_matches_selection(user_cmd* cmd)
{
  return cmd != nullptr
    && selected_command_number != 0
    && cmd->command_number == selected_command_number
    && cmd->random_seed == selected_seed;
}

bool should_force_attack(Weapon* weapon, user_cmd* cmd)
{
  return weapon != nullptr
    && weapon == selected_weapon
    && mode == selected_mode::force
    && command_matches_selection(cmd);
}

bool should_skip_attack(Weapon* weapon, user_cmd* cmd)
{
  return weapon != nullptr
    && weapon == selected_weapon
    && mode == selected_mode::skip
    && command_matches_selection(cmd);
}

indicator_state get_indicator_state()
{
  auto state = indicator_state{};
  state.force_mode = config.debug.insider_settings_unlocked && config.random_crits.force_crits;
  state.save_mode = config.debug.insider_settings_unlocked && config.random_crits.save_bucket;
  state.enabled = config.debug.insider_settings_unlocked && (config.random_crits.force_crits || config.random_crits.always_melee_crit || config.random_crits.save_bucket);
  state.seed_scan = std::clamp(config.random_crits.seed_scan, min_seed_scan, max_seed_scan);

  if (entity_list == nullptr) {
    return state;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return state;
  }

  auto* weapon = localplayer->get_weapon();
  if (weapon == nullptr) {
    return state;
  }

  state.available = true;
  state.melee_weapon = weapon->is_melee();
  state.crit_boosted = localplayer->is_crit_boosted();
  state.can_attack = weapon->can_primary_attack();
  state.can_random_crit = weapon->are_random_crits_enabled();
  state.selected = weapon == selected_weapon && selected_command_number != 0;
  state.forcing = state.selected && mode == selected_mode::force;
  state.skipping = state.selected && mode == selected_mode::skip;
  state.bucket = weapon->crit_token_bucket();
  state.checks = weapon->crit_checks();
  state.seed_requests = weapon->crit_seed_requests();
  state.current_seed = weapon->current_seed();
  state.selected_command = selected_command_number;
  state.selected_seed = selected_seed;
  state.selected_roll = selected_roll;
  state.selected_offset = selected_offset;

  const auto bucket_info = build_bucket_info(localplayer, weapon);
  state.bucket_cap = bucket_info.bucket_cap;
  state.bucket_progress = std::clamp(state.bucket / std::max(bucket_info.bucket_cap, 1.0f), 0.0f, 1.0f);
  state.rapid_fire = bucket_info.rapid_fire;
  state.crit_banned = bucket_info.crit_banned;
  state.crit_chance = bucket_info.crit_chance;
  state.crit_chance_mult = bucket_info.crit_chance_mult;
  state.damage = bucket_info.damage;
  state.crit_cost = bucket_info.crit_cost;
  state.damage_to_crit = bucket_info.damage_to_crit;
  state.rapid_wait = bucket_info.rapid_wait;
  state.streaming_time = bucket_info.streaming_time;
  state.available_crits = bucket_info.available_crits;
  state.potential_crits = bucket_info.potential_crits;
  state.next_crit = bucket_info.next_crit;

  const int current_command_number = indicator_command_number.load(std::memory_order_relaxed);
  if (current_command_number > 0 && state.available_crits > 0 && state.can_random_crit && !state.crit_banned && state.rapid_wait <= 0.0f) {
    const int command_number = find_command_number(localplayer, weapon, current_command_number, true);
    if (command_number != 0) {
      const int seed = MD5_PseudoRandom(static_cast<unsigned int>(command_number)) & INT_MAX;
      state.next_crit_seed_offset = std::max(0, command_number - current_command_number);
      state.next_crit_seed_roll = crit_roll(localplayer, weapon, seed);
    }
  }

  return state;
}

} // namespace random_crits
