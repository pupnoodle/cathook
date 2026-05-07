/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/aimbot/aimbot.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <vector>

#include "aimbot.hpp"

#include "MD5/MD5.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

#include "hitscan_aim.hpp"
#include "melee_aim.hpp"
#include "proj_aim.hpp"

namespace
{

bool g_aimbot_requested_shot = false;

using aimbot_random_seed_fn = void (*)(int);
using aimbot_random_float_fn = float (*)(float, float);

aimbot_random_seed_fn aimbot_random_seed = nullptr;
aimbot_random_float_fn aimbot_random_float = nullptr;
bool aimbot_random_initialized = false;

bool aimbot_init_random()
{
  if (aimbot_random_initialized) {
    return aimbot_random_seed != nullptr && aimbot_random_float != nullptr;
  }

  aimbot_random_initialized = true;
  aimbot_random_seed = reinterpret_cast<aimbot_random_seed_fn>(dlsym(RTLD_DEFAULT, "RandomSeed"));
  aimbot_random_float = reinterpret_cast<aimbot_random_float_fn>(dlsym(RTLD_DEFAULT, "RandomFloat"));
  return aimbot_random_seed != nullptr && aimbot_random_float != nullptr;
}

uint32_t aimbot_crc32_process_byte(uint32_t crc, uint8_t value)
{
  crc ^= value;
  for (int bit = 0; bit < 8; ++bit) {
    const uint32_t mask = 0U - (crc & 1U);
    crc = (crc >> 1) ^ (0xEDB88320U & mask);
  }

  return crc;
}

uint32_t aimbot_crc32_process_buffer(uint32_t crc, const void* data, int size)
{
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (int index = 0; index < size; ++index) {
    crc = aimbot_crc32_process_byte(crc, bytes[index]);
  }

  return crc;
}

int aimbot_seed_file_line_hash(int seed, const char* name, int additional_seed)
{
  uint32_t crc = 0xFFFFFFFFU;
  crc = aimbot_crc32_process_buffer(crc, &seed, sizeof(seed));
  crc = aimbot_crc32_process_buffer(crc, &additional_seed, sizeof(additional_seed));
  crc = aimbot_crc32_process_buffer(crc, name, static_cast<int>(std::strlen(name)));
  return static_cast<int>(~crc);
}

bool aimbot_projectile_is_syringe_weapon(Weapon* weapon)
{
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
    return true;
  default:
    return false;
  }
}

bool aimbot_projectile_has_pipe_random_velocity(Weapon* weapon)
{
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return true;
  default:
    return false;
  }
}

Vec3 aimbot_projectile_random_angle_offset(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const Vec3& view_angles)
{
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr || user_cmd->command_number <= 0) {
    return Vec3{};
  }

  if (!aimbot_projectile_is_syringe_weapon(weapon) && !aimbot_projectile_has_pipe_random_velocity(weapon)) {
    return Vec3{};
  }

  if (!aimbot_init_random()) {
    return Vec3{};
  }

  const int command_seed = MD5_PseudoRandom(static_cast<unsigned int>(user_cmd->command_number)) & INT_MAX;
  aimbot_random_seed(aimbot_seed_file_line_hash(command_seed, "SelectWeightedSequence", 0));
  for (int index = 0; index < 6; ++index) {
    aimbot_random_float(0.0f, 1.0f);
  }

  Vec3 angle_offset{};
  if (aimbot_projectile_is_syringe_weapon(weapon)) {
    angle_offset.x += aimbot_random_float(-1.5f, 1.5f);
    angle_offset.y += aimbot_random_float(-1.5f, 1.5f);
    return angle_offset;
  }

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f) {
    return Vec3{};
  }

  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(view_angles, &forward, &right, &up);

  const Vec3 velocity = (forward * profile.params.speed) + (up * profile.initial_lift);
  const Vec3 velocity_random = velocity +
    (right * aimbot_random_float(-10.0f, 10.0f)) +
    (up * aimbot_random_float(-10.0f, 10.0f));
  if (local_prediction_vec3_is_zero(velocity) || local_prediction_vec3_is_zero(velocity_random)) {
    return Vec3{};
  }

  return aimbot_normalize_angle_delta(
    local_prediction_direction_to_angles(local_prediction_normalize(velocity_random)),
    local_prediction_direction_to_angles(local_prediction_normalize(velocity)));
}

Vec3 aimbot_apply_projectile_random_compensation(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const Vec3& view_angles)
{
  const Vec3 angle_offset = aimbot_projectile_random_angle_offset(localplayer, weapon, user_cmd, view_angles);
  if (local_prediction_vec3_is_zero(angle_offset)) {
    return view_angles;
  }

  return aimbot_clamp_angles(view_angles - angle_offset);
}

struct aimbot_projectile_target_hint {
  Player* player = nullptr;
  float fov = FLT_MAX;
  float current_fov = FLT_MAX;
  float lead_fov = FLT_MAX;
  float distance = FLT_MAX;
  float target_speed = 0.0f;
  int health = 0;
  bool preferred = false;
  bool current = false;
};

int aimbot_projectile_target_attempt_cap(size_t target_count)
{
  int cap = 5;
  if (global_vars != nullptr) {
    if (global_vars->frametime > (1.0f / 45.0f)) {
      cap = 2;
    } else if (global_vars->frametime > (1.0f / 60.0f)) {
      cap = 3;
    } else if (global_vars->frametime > (1.0f / 75.0f)) {
      cap = 4;
    }
  }

  if (target_count >= 8) {
    cap = std::min(cap, 3);
  } else if (target_count >= 6) {
    cap = std::min(cap, 4);
  }

  return std::clamp(cap, 1, 5);
}

bool aimbot_projectile_target_hint_better(const aimbot_projectile_target_hint& left,
  const aimbot_projectile_target_hint& right)
{
  if (left.current != right.current) {
    return left.current;
  }
  if (left.preferred != right.preferred) {
    return left.preferred;
  }

  switch (config.aimbot.target_type) {
  case Aim::TargetType::DISTANCE:
    if (left.distance != right.distance) {
      return left.distance < right.distance;
    }
    return left.fov < right.fov;
  case Aim::TargetType::LEAST_HEALTH:
    if (left.health != right.health) {
      return left.health < right.health;
    }
    return left.fov < right.fov;
  case Aim::TargetType::MOST_HEALTH:
    if (left.health != right.health) {
      return left.health > right.health;
    }
    return left.fov < right.fov;
  case Aim::TargetType::FOV:
  default:
    if (left.fov != right.fov) {
      return left.fov < right.fov;
    }
    return left.distance < right.distance;
  }
}

aimbot_candidate aimbot_find_best_projectile_candidate(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const Vec3& original_view_angles)
{
  aimbot_candidate best_candidate{};
  std::vector<aimbot_projectile_target_hint> target_hints{};
  target_hints.reserve(entity_cache[class_id::PLAYER].size());

  const projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  const float projectile_speed = profile.valid ? std::max(profile.params.speed, 1.0f) : 1.0f;
  const float projectile_horizon = profile.valid ? std::max(profile.params.max_time, static_cast<float>(TICK_INTERVAL)) : 1.0f;
  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  for (Entity* entity : entity_cache[class_id::PLAYER]) {
    Player* player = static_cast<Player*>(entity);
    if (aimbot_should_skip_player(localplayer, player)) {
      continue;
    }

    const Vec3 mins = player->get_player_mins(player->is_ducking());
    const Vec3 maxs = player->get_player_maxs(player->is_ducking());
    const Vec3 aim_pos = player->get_origin() + Vec3{0.0f, 0.0f, mins.z + ((maxs.z - mins.z) * 0.5f)};
    Vec3 target_velocity = local_prediction_estimate_entity_velocity(player);
    if (local_prediction_vector_length(target_velocity) <= 0.001f) {
      target_velocity = player->get_velocity();
    }

    const float distance = distance_3d(shoot_pos, player->get_origin());
    const float approximate_time = std::clamp(distance / projectile_speed, static_cast<float>(TICK_INTERVAL), projectile_horizon);
    const Vec3 lead_pos = aim_pos + (target_velocity * approximate_time);
    const Vec3 aim_angles = aimbot_calculate_angles_to_position(shoot_pos, aim_pos);
    const Vec3 lead_angles = aimbot_calculate_angles_to_position(shoot_pos, lead_pos);
    const bool preferred = has_aimbot_preference(player);
    const bool current = player == target_player || preferred;
    const float current_fov = aimbot_calculate_fov(aim_angles, original_view_angles);
    const float lead_fov = aimbot_calculate_fov(lead_angles, original_view_angles);
    const float fov = std::min(current_fov, lead_fov);
    const float target_speed = local_prediction_velocity_2d_length(target_velocity);
    const float speed_lead_fov = std::atan2(target_speed * approximate_time, std::max(distance, 1.0f)) * radpi;
    const float fov_limit = std::max(config.aimbot.fov * (current ? 3.5f : 2.75f), current ? 90.0f : 64.0f) +
      (speed_lead_fov * 0.75f);
    if (!current && fov > fov_limit) {
      continue;
    }

    target_hints.push_back({
      .player = player,
      .fov = fov,
      .current_fov = current_fov,
      .lead_fov = lead_fov,
      .distance = distance,
      .target_speed = target_speed,
      .health = player->get_health(),
      .preferred = preferred,
      .current = current
    });
  }

  if (target_hints.empty()) {
    proj_aim_set_scan_debug_stats(0, 0, 0);
    return best_candidate;
  }

  const int max_attempts = aimbot_projectile_target_attempt_cap(target_hints.size());
  const size_t sort_cap = std::min(target_hints.size(), static_cast<size_t>(max_attempts));
  if (target_hints.size() > sort_cap) {
    std::partial_sort(
      target_hints.begin(),
      target_hints.begin() + static_cast<std::ptrdiff_t>(sort_cap),
      target_hints.end(),
      aimbot_projectile_target_hint_better);
  } else {
    std::stable_sort(target_hints.begin(), target_hints.end(), aimbot_projectile_target_hint_better);
  }

  int attempts = 0;
  for (const aimbot_projectile_target_hint& hint : target_hints) {
    if (attempts >= max_attempts) {
      break;
    }

    ++attempts;
    aimbot_candidate candidate = proj_aim_find_candidate(localplayer, weapon, hint.player, user_cmd, original_view_angles);
    if (candidate.player == nullptr) {
      continue;
    }

    const float fov_limit = config.aimbot.fov * (candidate.preferred ? 1.35f : 1.0f);
    if (!candidate.visible || candidate.fov > fov_limit) {
      continue;
    }

    if (aimbot_candidate_better(candidate, best_candidate)) {
      best_candidate = candidate;
      proj_aim_commit_debug_stats();
    }
  }

  proj_aim_set_scan_debug_stats(static_cast<int>(target_hints.size()), attempts, max_attempts);
  return best_candidate;
}

}

static void aimbot_apply_visible_view(user_cmd* user_cmd) {
  if (user_cmd == nullptr || config.aimbot.aim_mode == Aim::AimMode::PSILENT) {
    return;
  }

  auto applied_angles = user_cmd->view_angles;
  if (prediction != nullptr) {
    prediction->set_local_view_angles(applied_angles);
    prediction->set_view_angles(applied_angles);
  }

  if (engine != nullptr) {
    engine->set_view_angles(applied_angles);
  }
}

static void aimbot_consider_non_player_target(Player* localplayer,
  Weapon* weapon,
  Entity* entity,
  const Vec3& original_view_angles,
  aimbot_candidate* best_candidate) {
  if (best_candidate == nullptr) {
    return;
  }

  aimbot_candidate candidate = aimbot_find_non_player_candidate(localplayer, weapon, entity, original_view_angles);
  if (candidate.entity == nullptr) {
    return;
  }

  if (!candidate.visible || candidate.fov > config.aimbot.fov) {
    return;
  }

  if (aimbot_candidate_better(candidate, *best_candidate)) {
    *best_candidate = candidate;
  }
}

static aimbot_candidate aimbot_find_best_non_player_candidate(Player* localplayer, Weapon* weapon, const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};

  constexpr class_id building_ids[] = {
    class_id::SENTRY,
    class_id::DISPENSER,
    class_id::TELEPORTER
  };

  for (class_id building_id : building_ids) {
    for (Entity* entity : entity_cache[building_id]) {
      aimbot_consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
    }
  }

  for (Entity* entity : entity_cache[class_id::PUMPKIN]) {
    aimbot_consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
  }

  for (Entity* entity : entity_cache[class_id::PILL_OR_STICKY]) {
    aimbot_consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
  }

  return best_candidate;
}

static aimbot_candidate aimbot_find_best_candidate(Player* localplayer, Weapon* weapon, user_cmd* user_cmd, const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};

  if (aimbot_is_projectile_weapon(weapon)) {
    best_candidate = aimbot_find_best_projectile_candidate(localplayer, weapon, user_cmd, original_view_angles);
  } else {
    for (Entity* entity : entity_cache[class_id::PLAYER]) {
      Player* player = static_cast<Player*>(entity);
      if (aimbot_should_skip_player(localplayer, player)) {
        continue;
      }

      aimbot_candidate candidate{};
      if (aimbot_is_melee_weapon(weapon)) {
        candidate = melee_aim_find_candidate(localplayer, weapon, player, original_view_angles);
      } else {
        candidate = hitscan_aim_find_candidate(localplayer, weapon, player, original_view_angles);
      }

      if (candidate.entity == nullptr) {
        continue;
      }

      float fov_limit = config.aimbot.fov * (candidate.preferred ? 1.35f : 1.0f);
      if (!candidate.visible || candidate.fov > fov_limit) {
        continue;
      }

      if (aimbot_candidate_better(candidate, best_candidate)) {
        best_candidate = candidate;
      }
    }
  }

  const aimbot_candidate non_player_candidate = aimbot_find_best_non_player_candidate(localplayer, weapon, original_view_angles);
  if (aimbot_candidate_better(non_player_candidate, best_candidate)) {
    best_candidate = non_player_candidate;
  }

  return best_candidate;
}

static aimbot_candidate aimbot_find_best_scope_candidate(Player* localplayer, Weapon* weapon, const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};
  if (localplayer == nullptr || weapon == nullptr || localplayer->get_tf_class() != tf_class::SNIPER || !weapon->is_sniper_rifle()) {
    return best_candidate;
  }

  for (Entity* entity : entity_cache[class_id::PLAYER]) {
    Player* player = static_cast<Player*>(entity);
    if (aimbot_should_skip_player(localplayer, player)) {
      continue;
    }

    aimbot_candidate candidate = hitscan_aim_find_occluded_candidate(localplayer, weapon, player, original_view_angles);
    if (candidate.player == nullptr) {
      continue;
    }

    float fov_limit = config.aimbot.fov * (candidate.preferred ? 1.35f : 1.0f);
    if (candidate.fov > fov_limit) {
      continue;
    }

    if (aimbot_candidate_better(candidate, best_candidate)) {
      best_candidate = candidate;
    }
  }

  return best_candidate;
}

static bool aimbot_projectile_solution_ready(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const aimbot_candidate& candidate,
  const Vec3& applied_view_angles) {
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr || candidate.entity == nullptr) {
    return false;
  }

  if (!candidate.projectile_direct && !candidate.projectile_splash) {
    return true;
  }

  if (candidate.player == nullptr) {
    return false;
  }

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f || candidate.projectile_intercept_time <= 0.0f) {
    return false;
  }

  profile.params.max_time = std::min(
    profile.params.max_time,
    std::max(candidate.projectile_intercept_time + profile.params.time_step, profile.params.time_step));
  profile.lifetime = profile.params.max_time;

  const projectile_sim_launch launch = projectile_sim_build_launch_from_angles(localplayer, weapon, applied_view_angles, profile);
  const projectile_sim_result sim_result = projectile_sim_run(launch, profile);
  LocalPredictionProjectileTrace trace = local_prediction_trace_from_projectile_sim(sim_result);
  if (!trace.valid || trace.steps.empty()) {
    return false;
  }

  LocalPredictionInterceptResult adjusted_intercept{};
  adjusted_intercept.valid = true;
  adjusted_intercept.has_target_base_origin = candidate.projectile_has_target_base_origin;
  adjusted_intercept.intercept_time = candidate.projectile_intercept_time;
  adjusted_intercept.intercept_distance = candidate.distance;
  adjusted_intercept.miss_distance = candidate.projectile_miss_distance;
  adjusted_intercept.aim_angles = applied_view_angles;
  adjusted_intercept.target_origin = candidate.aim_position;
  adjusted_intercept.target_base_origin = candidate.projectile_target_base_origin;
  adjusted_intercept.target_offset = candidate.projectile_target_offset;
  adjusted_intercept.trace = trace;

  if (candidate.projectile_direct) {
    return proj_aim_trace_path(localplayer, candidate.player, weapon, adjusted_intercept);
  }

  const uint32_t hitbox_mask = proj_aim_effective_hitbox_mask(localplayer, weapon, candidate.player);
  const Vec3* predicted_target_origin = candidate.projectile_has_target_base_origin
    ? &candidate.projectile_target_base_origin
    : nullptr;
  return proj_aim_trace_splash_path(
    localplayer,
    candidate.player,
    weapon,
    adjusted_intercept,
    candidate.projectile_splash_radius,
    hitbox_mask,
    nullptr,
    predicted_target_origin);
}

bool aimbot(user_cmd* user_cmd, Vec3 original_view_angles) {
  g_aimbot_requested_shot = false;
  const Vec3 source_view_angles = user_cmd != nullptr ? user_cmd->view_angles : original_view_angles;

  if (!config.aimbot.master) {
    target_player = nullptr;
    target_entity = nullptr;
    clear_aimbot_preference();
    reset_autoscope_scope_state();
    reset_aimbot_input_history();
    return false;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    target_player = nullptr;
    target_entity = nullptr;
    reset_autoscope_scope_state();
    reset_aimbot_input_history();
    return false;
  }

  Weapon* weapon = localplayer->get_weapon();
  if (weapon == nullptr) {
    target_player = nullptr;
    target_entity = nullptr;
    reset_autoscope_scope_state();
    store_aimbot_input_angles(source_view_angles);
    return false;
  }

  if (std::find(entity_cache[class_id::PLAYER].begin(), entity_cache[class_id::PLAYER].end(), target_player) == entity_cache[class_id::PLAYER].end()) {
    target_player = nullptr;
  }

  if (aimbot_preference.preferred_target != nullptr &&
      std::find(entity_cache[class_id::PLAYER].begin(), entity_cache[class_id::PLAYER].end(), aimbot_preference.preferred_target) == entity_cache[class_id::PLAYER].end()) {
    clear_aimbot_preference();
  }

  if (target_player != nullptr && (target_player->is_ignored() || (target_player->is_friend() && config.aimbot.ignore_friends))) {
    target_player = nullptr;
  }

  aimbot_candidate best_candidate = aimbot_find_best_candidate(localplayer, weapon, user_cmd, source_view_angles);
  aimbot_candidate scope_candidate = best_candidate.player != nullptr
    ? best_candidate
    : aimbot_find_best_scope_candidate(localplayer, weapon, source_view_angles);
  target_entity = best_candidate.entity;
  target_player = best_candidate.player;

  if (aimbot_should_auto_unscope(localplayer, weapon, scope_candidate)) {
    user_cmd->buttons |= IN_ATTACK2;
  }

  if (aimbot_should_auto_unrev(localplayer, weapon, best_candidate)) {
    user_cmd->buttons |= IN_ATTACK2;
  }

  if (!aimbot_use_key_active()) {
    store_aimbot_input_angles(source_view_angles);
    return false;
  }

  if (aimbot_should_auto_scope(localplayer, weapon, scope_candidate)) {
    user_cmd->buttons |= IN_ATTACK2;
    store_aimbot_input_angles(source_view_angles);
    return false;
  }

  if (best_candidate.entity == nullptr) {
    store_aimbot_input_angles(source_view_angles);
    return false;
  }

  if (best_candidate.player != nullptr) {
    set_aimbot_preference(best_candidate.player);
  }
  user_cmd->buttons &= ~IN_RELOAD;

  if (aimbot_should_auto_rev(localplayer, weapon, best_candidate)) {
    user_cmd->buttons |= IN_ATTACK2;
    store_aimbot_input_angles(source_view_angles);
    return false;
  }

  if (!weapon->can_primary_attack() || !aimbot_scoped_only_ready(localplayer, weapon)) {
    store_aimbot_input_angles(source_view_angles);
    return false;
  }

  Vec3 target_view_angles = best_candidate.aim_angles - localplayer->get_punch_angles();
  user_cmd->view_angles = aimbot_apply_mode_angles(
    source_view_angles,
    target_view_angles,
    aimbot_last_input_angles,
    aimbot_last_input_angles_valid);
  const bool projectile_solution = best_candidate.projectile_direct || best_candidate.projectile_splash;
  const Vec3 projectile_base_angles = projectile_solution
    ? target_view_angles
    : user_cmd->view_angles;
  const Vec3 projectile_view_angles = projectile_solution
    ? aimbot_apply_projectile_random_compensation(localplayer, weapon, user_cmd, projectile_base_angles)
    : user_cmd->view_angles;
  const bool projectile_ready = aimbot_projectile_solution_ready(
    localplayer,
    weapon,
    user_cmd,
    best_candidate,
    projectile_view_angles);
  const bool hitscan_solution = !aimbot_is_projectile_weapon(weapon) && !aimbot_is_melee_weapon(weapon);
  const bool hitscan_ready = !hitscan_solution || hitscan_aim_trace_candidate(localplayer, best_candidate, user_cmd->view_angles);

  const bool headshot_ready = aimbot_wait_for_headshot_ready(localplayer, weapon);
  if (!headshot_ready) {
    user_cmd->buttons &= ~IN_ATTACK;
  }

  const bool attack_ready = localplayer->can_shoot(best_candidate.entity) &&
    projectile_ready &&
    hitscan_ready &&
    headshot_ready &&
    !(user_cmd->buttons & IN_ATTACK2);
  if (config.aimbot.auto_shoot && attack_ready) {
    user_cmd->buttons |= IN_ATTACK;
    g_aimbot_requested_shot = true;
  }

  if (attack_ready && (user_cmd->buttons & IN_ATTACK) && projectile_solution) {
    user_cmd->view_angles = projectile_view_angles;
  }

  aimbot_apply_visible_view(user_cmd);
  store_aimbot_input_angles(source_view_angles);
  return config.aimbot.aim_mode == Aim::AimMode::PSILENT;
}

bool aimbot_requested_shot() {
  return g_aimbot_requested_shot;
}
