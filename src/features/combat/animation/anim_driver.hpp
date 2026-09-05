#ifndef ANIM_DRIVER_HPP
#define ANIM_DRIVER_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "core/entity_cache.hpp"
#include "core/types.hpp"
#include "features/combat/aimbot/aim_utils.hpp"
#include "features/combat/aimbot/resolver.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "features/combat/simulation/movesim.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace animation
{

constexpr int max_entities = backtrack::max_entities;
struct animation_settings {
  bool enabled = true;
  bool drive_remote_anims = true;
  bool feed_movesim = true;
};

inline animation_settings settings{};
inline bool g_driving = false;

struct anim_driver_pose {
  Player* player = nullptr;
  unsigned int handle = 0;
  const model_t* model = nullptr;
  bool valid = false;
  bool dormant = false;
  float sim_time = 0.0f;
  int sequence = -1;
  float cycle = 0.0f;
  Vec3 eye_angles{};
  Vec3 origin{};
  Vec3 velocity{};
  int bone_count = 0;
  std::array<matrix_3x4, backtrack::max_bones> bones{};
};

inline std::array<anim_driver_pose, max_entities> g_poses{};
inline std::array<float, max_entities> g_last_driven_simtime{};
inline std::array<Vec3, max_entities> g_last_driven_origin{};

[[nodiscard]] inline int index_of(Player* player)
{
  if (player == nullptr) {
    return -1;
  }

  const int index = player->get_index();
  return index > 0 && index < max_entities ? index : -1;
}

inline void reset_player(Player* player)
{
  const int index = index_of(player);
  if (index <= 0) {
    return;
  }

  aimbot_anim_detail::reset(player);
  g_poses[static_cast<std::size_t>(index)].valid = false;
  g_poses[static_cast<std::size_t>(index)].dormant = true;
  g_last_driven_simtime[static_cast<std::size_t>(index)] = 0.0f;
  backtrack::mark_stale(player);
}

inline void clear()
{
  std::fill_n(aimbot_anim_detail::slot_player, aimbot_anim_detail::anim_slot_count, nullptr);
  std::fill_n(aimbot_anim_detail::slot_simtime, aimbot_anim_detail::anim_slot_count, 0.0f);
  g_poses = {};
  g_last_driven_simtime = {};
  g_last_driven_origin = {};
}

[[nodiscard]] inline const anim_driver_pose* latest_pose(Player* player)
{
  const int index = index_of(player);
  if (index <= 0) {
    return nullptr;
  }

  const anim_driver_pose& pose = g_poses[static_cast<std::size_t>(index)];
  return pose.valid && pose.player == player && !pose.dormant &&
    pose.handle == player->get_ref_handle() && pose.model == player->get_model() &&
    pose.sim_time == player->get_simulation_time() ? &pose : nullptr;
}

inline bool copy_pose_bones(Player* player, matrix_3x4* bones, int max_bones, int* bone_count_out)
{
  if (bone_count_out != nullptr) {
    *bone_count_out = 0;
  }

  const anim_driver_pose* pose = latest_pose(player);
  if (pose == nullptr || bones == nullptr || max_bones <= 0 ||
      pose->bone_count <= 0 || pose->bone_count > max_bones ||
      pose->bone_count > backtrack::max_bones) {
    return false;
  }

  for (int bone_index = 0; bone_index < pose->bone_count; ++bone_index) {
    bones[bone_index] = pose->bones[static_cast<std::size_t>(bone_index)];
  }
  if (!aimbot_bones_are_finite(bones, pose->bone_count)) {
    if (bone_count_out != nullptr) {
      *bone_count_out = 0;
    }
    return false;
  }

  if (bone_count_out != nullptr) {
    *bone_count_out = pose->bone_count;
  }
  return true;
}

namespace detail {

[[nodiscard]] inline float tick_interval()
{
  return global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : static_cast<float>(TICK_INTERVAL);
}

[[nodiscard]] inline int time_to_ticks(float seconds)
{
  return static_cast<int>(0.5f + (seconds / std::max(tick_interval(), 0.0001f)));
}

[[nodiscard]] inline bool enemy_of_local(Player* player, Player* localplayer)
{
  if (player == nullptr || localplayer == nullptr || player == localplayer) {
    return false;
  }

  return !(player->get_team() == localplayer->get_team() ||
           player->is_friend() ||
           player->is_ignored());
}

inline void feed_movesim(Player* player)
{
  if (!settings.feed_movesim || player == nullptr || global_vars == nullptr) {
    return;
  }

  const int index = index_of(player);
  if (index <= 0) {
    return;
  }

  const Vec3 velocity = player->get_velocity();
  const float horizontal_speed = std::sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y));
  movesim::move_record record{};
  record.direction = horizontal_speed > 0.0001f
    ? Vec3{velocity.x / horizontal_speed, velocity.y / horizontal_speed, 0.0f}
    : Vec3{};
  record.sim_time = player->get_simulation_time();
  record.velocity = velocity;
  record.origin = player->get_origin();
  record.mode = player->get_water_level() > 1
    ? movesim::surface_mode::swim
    : ((player->get_flags() & FL_ONGROUND) != 0
      ? movesim::surface_mode::ground
      : movesim::surface_mode::air);
  movesim::push_record(index, record);
}

inline void drive_player(Player* player)
{
  if (player == nullptr || player->is_dormant() || !player->is_alive() || global_vars == nullptr) {
    return;
  }

  const int index = index_of(player);
  if (index <= 0) {
    return;
  }

  const float sim_time = player->get_simulation_time();
  const Vec3 origin = player->get_origin();
  if (!std::isfinite(sim_time) || sim_time <= 0.0f || !aimbot_vec3_is_finite(origin)) {
    return;
  }

  const std::size_t slot = static_cast<std::size_t>(index);
  float& last_driven = g_last_driven_simtime[slot];
  anim_driver_pose& pose = g_poses[slot];
  const bool same_identity = pose.player == player && pose.handle == player->get_ref_handle() &&
    pose.model == player->get_model();
  if (same_identity && pose.valid && sim_time == last_driven) {
    return;
  }
  if (!same_identity) {
    last_driven = 0.0f;
  }
  const Vec3& last_origin = g_last_driven_origin[slot];
  const Vec3 teleport_delta = origin - last_origin;
  const bool teleported =
    (teleport_delta.x * teleport_delta.x) + (teleport_delta.y * teleport_delta.y) +
    (teleport_delta.z * teleport_delta.z) > (64.0f * 64.0f);
  const bool rewound = sim_time < last_driven - detail::tick_interval();
  if (teleported || rewound || last_driven <= 0.0f) {
    last_driven = 0.0f;
    aimbot_anim_detail::reset(player);
  }

  Vec3 original_eye_angles = player->get_eye_angles();
  Vec3 driven_eye_angles = original_eye_angles;
  const bool have_resolved = resolver::resolved_eye_angles(player, &driven_eye_angles);
  if (have_resolved) {
    player->set_eye_angles(driven_eye_angles);
  }

  pose.valid = false;
  pose.player = player;
  pose.handle = player->get_ref_handle();
  pose.model = player->get_model();
  pose.dormant = false;
  pose.sim_time = sim_time;
  pose.eye_angles = have_resolved ? driven_eye_angles : original_eye_angles;
  pose.origin = origin;
  pose.velocity = player->get_velocity();
  pose.bone_count = 0;

  if (aimbot_update_client_side_animation(player)) {
    pose.valid = aimbot_setup_bones_at_time(player,
      pose.bones.data(),
      sim_time,
      global_vars->framecount,
      origin,
      teleported || rewound || last_driven <= 0.0f,
      true,
      &pose.bone_count);
    if (pose.valid) {
      pose.sequence = aimbot_sequence(player);
      pose.cycle = aimbot_cycle(player);
    }
  }

  player->set_eye_angles(original_eye_angles);
  aimbot_invalidate_bone_cache(player);

  last_driven = sim_time;
  g_last_driven_origin[slot] = origin;
}

}

inline void update_all()
{
  if (g_driving || !settings.enabled || !settings.drive_remote_anims) {
    return;
  }

  if (global_vars == nullptr || entity_list == nullptr || engine == nullptr || !engine->is_in_game()) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  g_driving = true;
  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    if (!detail::enemy_of_local(player, localplayer) || player->is_dormant() || !player->is_alive()) {
      continue;
    }

    detail::drive_player(player);
    detail::feed_movesim(player);
  }
  g_driving = false;
}

}

#endif
