/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/aimbot/aim_utils.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef AIM_UTILS_HPP
#define AIM_UTILS_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "aimbot_debug.hpp"
#include "aimbot.hpp"
#include "core/entity_cache.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/math/math.hpp"
#include "core/shared/sigs.hpp"
#include "libsigscan/libsigscan.h"
#include "features/menu/config.hpp"
#include "features/automation/nographics/nographics.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/aim_hitboxes.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

struct aimbot_candidate {
  Entity* entity = nullptr;
  Player* player = nullptr;
  int bone = 0;
  int hitbox = -1;
  int studio_hitbox = -1;
  Vec3 aim_position{};
  Vec3 approach_position{};
  Vec3 predicted_origin{};
  bool predicted_origin_valid = false;

  Vec3 melee_swing_start{};
  int melee_swing_tick = 0;
  Vec3 aim_angles{};
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  int health = 0;
  float simulation_time = 0.0f;

  float backtrack_timing_error = 0.0f;
  float backtrack_capture_gap = 0.0f;
  bool backtrack_on_shot = false;
  int tick_count = 0;
  Vec3 command_angles{};
  Vec3 backtrack_mins{};
  Vec3 backtrack_maxs{};
  Vec3 backtrack_hitbox_mins{};
  Vec3 backtrack_hitbox_maxs{};
  matrix_3x4 backtrack_bone{};
  bool spread_compensated = false;
  bool backtrack = false;
  bool backtrack_hitbox_valid = false;
  int pellet_index = -1;
  int pellet_count = 0;
  float spread = 0.0f;
  aimbot_debug_reason debug_reason = aimbot_debug_reason::none;
  aimbot_reject_debug reject_debug{};
  bool pose_timing_valid = false;
  int pose_target_tick = 0;
  int pose_command_tick = 0;
  float pose_lead_seconds = 0.0f;
  Vec3 pose_offset{};
  bool visible = false;
  bool preferred = false;
};

struct aimbot_point {
  bool valid = false;
  int bone = 0;
  int hitbox = -1;
  int studio_hitbox = -1;
  int priority = INT_MAX;
  Vec3 position{};
  Vec3 angles{};
  float fov = FLT_MAX;
};

inline static float aimbot_scoped_begin_time = 0.0f;
constexpr int aimbot_max_bones = 128;
constexpr int aimbot_bone_mask = 0x7FF00;
inline thread_local aimbot_reject_reason aimbot_bone_failure = aimbot_reject_reason::none;

struct aimbot_current_pose {
  Player* player = nullptr;
  const model_t* model = nullptr;
  float simulation_time = 0.0f;
  Vec3 network_origin{};
  Vec3 render_origin{};
  Vec3 velocity{};
  Vec3 eye_angles{};
  int sequence = -1;
  float cycle = 0.0f;
  std::uint64_t pose_parameter_hash = 0;
  int bone_count = 0;
  int generation = 0;
  int render_frame = 0;
  float render_realtime = 0.0f;
  float setup_time = NAN;
  bool valid = false;
  aimbot_pose_debug_info debug{};
  std::array<matrix_3x4, aimbot_max_bones> bones{};
};

inline std::array<aimbot_current_pose, 65> aimbot_current_poses{};
inline int aimbot_pose_generation = 0;
inline float aimbot_last_render_setup_time = NAN;
inline aimbot_pose_debug_info aimbot_bone_cache_debug{};

inline void aimbot_note_render_clock() {
  if (global_vars != nullptr && std::isfinite(global_vars->curtime)) {
    aimbot_last_render_setup_time = global_vars->curtime;
  }
}

inline aimbot_reject_reason aimbot_last_bone_failure() {
  return aimbot_bone_failure;
}

inline bool aimbot_vec3_is_finite(const Vec3& value);
inline float aimbot_distance_squared(const Vec3& left, const Vec3& right);
inline void aimbot_capture_latest_network_pose(Player* target,
                                                bool animation_already_updated);

inline std::uint64_t aimbot_hash_bytes(std::uint64_t hash, const void* data, std::size_t size) {
  if (data == nullptr) {
    return hash;
  }
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ULL;
  }
  return hash;
}

inline std::uint64_t aimbot_pose_parameter_hash(Player* target) {
  if (target == nullptr) {
    return 0;
  }

  static const int offset = tf2_netvars::find_offset("DT_BaseAnimating", {"m_flPoseParameter"});
  if (offset <= 0) {
    return 0;
  }

  constexpr std::size_t pose_parameter_count = 24;
  constexpr std::uint64_t fnv_offset = 1469598103934665603ULL;
  return aimbot_hash_bytes(fnv_offset,
    reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(target) + static_cast<std::uintptr_t>(offset)),
    sizeof(float) * pose_parameter_count);
}

inline int aimbot_sequence(Player* target) {
  static const int offset = tf2_netvars::find_offset("DT_BaseAnimating", {"m_nSequence"});
  if (target == nullptr || offset <= 0) {
    return -1;
  }
  return *reinterpret_cast<const int*>(reinterpret_cast<std::uintptr_t>(target) + static_cast<std::uintptr_t>(offset));
}

inline float aimbot_cycle(Player* target) {
  static const int offset = tf2_netvars::find_offset("DT_BaseAnimating", {"m_flCycle"});
  if (target == nullptr || offset <= 0) {
    return 0.0f;
  }
  return *reinterpret_cast<const float*>(reinterpret_cast<std::uintptr_t>(target) + static_cast<std::uintptr_t>(offset));
}

inline bool aimbot_current_pose_signature_matches(const aimbot_current_pose& pose,
                                                  Player* target,
                                                  const model_t* model,
                                                  float simulation_time,
                                                  const Vec3& network_origin,
                                                  const Vec3& render_origin,
                                                  const Vec3& velocity,
                                                  const Vec3& eye_angles,
                                                  int sequence,
                                                  float cycle,
                                                  std::uint64_t pose_parameter_hash,
                                                  float setup_time) {
  return pose.valid && pose.player == target && pose.model == model &&
    global_vars != nullptr && pose.render_frame == global_vars->framecount &&
    pose.simulation_time == simulation_time &&
    std::memcmp(&pose.network_origin, &network_origin, sizeof(Vec3)) == 0 &&
    std::memcmp(&pose.render_origin, &render_origin, sizeof(Vec3)) == 0 &&
    std::memcmp(&pose.velocity, &velocity, sizeof(Vec3)) == 0 &&
    std::memcmp(&pose.eye_angles, &eye_angles, sizeof(Vec3)) == 0 &&
    pose.sequence == sequence && pose.cycle == cycle &&
    pose.pose_parameter_hash == pose_parameter_hash &&
    pose.setup_time == setup_time;
}

inline bool aimbot_bones_are_finite(const matrix_3x4* bone_to_world, int bone_count) {
  if (bone_to_world == nullptr || bone_count <= 0 || bone_count > aimbot_max_bones) {
    return false;
  }

  for (int bone = 0; bone < bone_count; ++bone) {
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 4; ++column) {
        if (!std::isfinite(bone_to_world[bone].mat[row][column])) {
          return false;
        }
      }
    }
  }

  return true;
}

inline bool aimbot_copy_cached_bones(Player* target, matrix_3x4* bone_to_world, int* bone_count_out = nullptr) {
  if (target == nullptr || bone_to_world == nullptr) {
    return false;
  }

  return target->copy_cached_bones(bone_to_world, aimbot_max_bones, bone_count_out);
}

inline std::uint64_t aimbot_model_bone_counter() {
  static const auto* counter = []() -> const std::uint64_t* {
    const auto* instruction = reinterpret_cast<const std::uint8_t*>(
      sigscan_module("client.so", sigs::base_animating_invalidate_bone_cache));
    if (instruction == nullptr) {
      return nullptr;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, instruction + 3, sizeof(displacement));
    return reinterpret_cast<const std::uint64_t*>(instruction + 7 + displacement);
  }();
  return counter != nullptr ? *counter : 0;
}

inline bool aimbot_invalidate_bone_cache(Player* target) {
  if (target == nullptr) {
    return false;
  }

  using invalidate_bone_cache_fn = std::uintptr_t (*)(void*);
  static const auto invalidate_bone_cache = reinterpret_cast<invalidate_bone_cache_fn>(
    sigscan_module("client.so", sigs::base_animating_invalidate_bone_cache));
  if (invalidate_bone_cache == nullptr) {
    return false;
  }

  invalidate_bone_cache(target);
  return true;
}

inline bool aimbot_update_engine_bone_cache(Player* target,
                                            const matrix_3x4* bone_to_world,
                                            int bone_count,
                                            float setup_time) {
  aimbot_bone_cache_debug.cache_updated = false;
  aimbot_bone_cache_debug.cache_time = setup_time;
  aimbot_bone_cache_debug.cache_handle = 0;
  if (target == nullptr || bone_to_world == nullptr || bone_count <= 0 ||
      bone_count > aimbot_max_bones || !std::isfinite(setup_time)) {
    return false;
  }

  using get_bone_cache_fn = void* (*)(std::uintptr_t);
  using update_bone_cache_fn = void (*)(void*, const matrix_3x4*, int, float);
  static const auto get_bone_cache = reinterpret_cast<get_bone_cache_fn>(
    sigscan_module("client.so", sigs::studio_get_bone_cache));
  static const auto update_bone_cache = reinterpret_cast<update_bone_cache_fn>(
    sigscan_module("client.so", sigs::studio_bone_cache_update_bones));
  aimbot_bone_cache_debug.getter_ready = get_bone_cache != nullptr;
  aimbot_bone_cache_debug.updater_ready = update_bone_cache != nullptr;
  if (get_bone_cache == nullptr || update_bone_cache == nullptr) {
    return false;
  }

  constexpr std::uintptr_t bone_cache_handle_offset = 0xB98;
  const std::uintptr_t handle = *reinterpret_cast<const std::uintptr_t*>(
    reinterpret_cast<std::uintptr_t>(target) + bone_cache_handle_offset);
  aimbot_bone_cache_debug.cache_handle = handle;
  if (handle == 0) {
    return false;
  }

  void* cache = get_bone_cache(handle);
  if (cache == nullptr) {
    return false;
  }

  update_bone_cache(cache, bone_to_world, bone_count, setup_time);
  aimbot_bone_cache_debug.cache_updated = true;
  return true;
}

inline bool aimbot_ensure_studio_header(Player* target) {
  if (target == nullptr) {
    return false;
  }

  using lock_studio_hdr_fn = void (*)(void*);
  static const auto lock_studio_hdr = reinterpret_cast<lock_studio_hdr_fn>(
    sigscan_module("client.so", sigs::base_animating_lock_studio_hdr));
  if (lock_studio_hdr == nullptr) {
    return false;
  }

  lock_studio_hdr(target);
  constexpr std::uintptr_t studio_hdr_offset = 0xBE8;
  return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(target) + studio_hdr_offset) != nullptr;
}

inline bool aimbot_sequence_is_valid(Player* target) {
  if (target == nullptr) {
    return false;
  }

  constexpr std::uintptr_t sequence_offset = 0xB00;
  return *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(target) + sequence_offset) != -1;
}

inline bool aimbot_update_client_side_animation(Player* target) {
  if (target == nullptr) {
    return false;
  }

  using update_client_side_animation_fn = void (*)(void*);
  constexpr std::size_t update_client_side_animation_index = 256;
  void** vtable = *reinterpret_cast<void***>(target);
  if (vtable == nullptr || vtable[update_client_side_animation_index] == nullptr) {
    return false;
  }

  reinterpret_cast<update_client_side_animation_fn>(vtable[update_client_side_animation_index])(target);
  return true;
}

struct aimbot_quaternion {
  float x;
  float y;
  float z;
  float w;
};

struct aimbot_bone_bit_list {
  std::uint32_t bits[4]{};
};

inline void aimbot_make_angle_matrix(const Vec3& angles, const Vec3& origin, matrix_3x4& output) {
  constexpr float degrees_to_radians = 0.01745329251994329577f;
  const float pitch = angles.x * degrees_to_radians;
  const float yaw = angles.y * degrees_to_radians;
  const float roll = angles.z * degrees_to_radians;
  const float sp = std::sin(pitch);
  const float cp = std::cos(pitch);
  const float sy = std::sin(yaw);
  const float cy = std::cos(yaw);
  const float sr = std::sin(roll);
  const float cr = std::cos(roll);

  output.mat[0][0] = cp * cy;
  output.mat[1][0] = cp * sy;
  output.mat[2][0] = -sp;
  output.mat[0][1] = (sr * sp * cy) - (cr * sy);
  output.mat[1][1] = (sr * sp * sy) + (cr * cy);
  output.mat[2][1] = sr * cp;
  output.mat[0][2] = (cr * sp * cy) + (sr * sy);
  output.mat[1][2] = (cr * sp * sy) - (sr * cy);
  output.mat[2][2] = cr * cp;
  output.mat[0][3] = origin.x;
  output.mat[1][3] = origin.y;
  output.mat[2][3] = origin.z;
}

inline bool aimbot_reconstruct_bones(Player* target,
                                     matrix_3x4* bone_to_world,
                                     int bone_count,
                                     float current_time,
                                     int pose_frame,
                                     const Vec3& network_origin,
                                     bool clear_ik_targets) {
  if (target == nullptr || bone_to_world == nullptr || bone_count <= 0 ||
      bone_count > aimbot_max_bones || !std::isfinite(current_time)) {
    return false;
  }

  using render_state_fn = const Vec3& (*)(void*);
  using get_skeleton_fn = void (*)(void*, void*, Vec3*, aimbot_quaternion*, int, float);
  using build_transformations_fn = void (*)(void*, void*, Vec3*, aimbot_quaternion*,
                                             const matrix_3x4&, int, aimbot_bone_bit_list*);
  using bone_accessor_fn = void (*)(void*, int);
  using ik_stage_fn = void (*)(void*, Vec3*, aimbot_quaternion*, void*, aimbot_bone_bit_list*);
  using ik_lock_fn = void (*)(void*, float);
  using ik_constructor_fn = void (*)(void*);
  using ik_init_fn = void (*)(void*, void*, const Vec3*, const Vec3*, float, int, int);
  using ik_clear_targets_fn = void (*)(void*);
  using post_transform_fn = void (*)(void*, void*);
  using attachment_helper_fn = bool (*)(void*, void*);

  static const auto enable_bone_accessor = reinterpret_cast<bone_accessor_fn>(
    sigscan_module("client.so", sigs::base_animating_bone_accessor_enable));
  static const auto disable_bone_accessor = reinterpret_cast<bone_accessor_fn>(
    sigscan_module("client.so", sigs::base_animating_bone_accessor_disable));
  static const auto setup_bones_attachment_helper = reinterpret_cast<attachment_helper_fn>(
    sigscan_module("client.so", sigs::base_animating_setup_bones_attachment_helper));
  static const auto ik_update_targets = reinterpret_cast<ik_stage_fn>(
    sigscan_module("client.so", sigs::ik_context_update_targets));
  static const auto ik_solve_dependencies = reinterpret_cast<ik_stage_fn>(
    sigscan_module("client.so", sigs::ik_context_solve_dependencies));
  static const auto ik_init = reinterpret_cast<ik_init_fn>(
    sigscan_module("client.so", sigs::ik_context_init));
  static const auto ik_clear_targets = reinterpret_cast<ik_clear_targets_fn>(
    sigscan_module("client.so", sigs::ik_context_clear_targets));
  if (enable_bone_accessor == nullptr || disable_bone_accessor == nullptr ||
      setup_bones_attachment_helper == nullptr || ik_update_targets == nullptr ||
      ik_solve_dependencies == nullptr || ik_init == nullptr || ik_clear_targets == nullptr) {
    return false;
  }

  void** vtable = *reinterpret_cast<void***>(target);
  constexpr std::size_t render_angles_index = 47;
  constexpr std::size_t get_skeleton_index = 241;
  constexpr std::size_t build_transformations_index = 227;
  constexpr std::size_t update_ik_locks_index = 229;
  constexpr std::size_t calculate_ik_locks_index = 230;
  constexpr std::size_t post_transform_index = 235;
  if (vtable == nullptr || vtable[render_angles_index] == nullptr ||
      vtable[get_skeleton_index] == nullptr || vtable[build_transformations_index] == nullptr ||
      vtable[update_ik_locks_index] == nullptr || vtable[calculate_ik_locks_index] == nullptr ||
      vtable[post_transform_index] == nullptr) {
    return false;
  }

  const auto get_render_angles = reinterpret_cast<render_state_fn>(vtable[render_angles_index]);
  const auto get_skeleton = reinterpret_cast<get_skeleton_fn>(vtable[get_skeleton_index]);
  const auto build_transformations = reinterpret_cast<build_transformations_fn>(vtable[build_transformations_index]);
  const auto update_ik_locks = reinterpret_cast<ik_lock_fn>(vtable[update_ik_locks_index]);
  const auto calculate_ik_locks = reinterpret_cast<ik_lock_fn>(vtable[calculate_ik_locks_index]);
  const auto post_transform = reinterpret_cast<post_transform_fn>(vtable[post_transform_index]);

  constexpr std::uintptr_t studio_hdr_offset = 0xBE8;
  void* studio_header = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(target) + studio_hdr_offset);
  if (studio_header == nullptr) {
    return false;
  }

  const Vec3 root_origin = network_origin;
  const Vec3 ik_angles = get_render_angles(target);
  if (!aimbot_vec3_is_finite(root_origin) || !aimbot_vec3_is_finite(ik_angles)) {
    return false;
  }

  Vec3 root_angles = ik_angles;
  root_angles.x = 0.0f;
  root_angles.z = 0.0f;

  matrix_3x4 root_transform{};
  aimbot_make_angle_matrix(root_angles, root_origin, root_transform);
  Vec3 positions[aimbot_max_bones]{};
  aimbot_quaternion rotations[aimbot_max_bones]{};
  aimbot_bone_bit_list computed{};

  constexpr std::uintptr_t previous_bone_mask_offset = 0x830;
  constexpr std::uintptr_t accumulated_bone_mask_offset = 0x834;
  constexpr std::uintptr_t readable_bone_mask_offset = 0x848;
  constexpr std::uintptr_t writable_bone_mask_offset = 0x84C;
  constexpr std::uintptr_t last_bone_setup_time_offset = 0xBA0;
  constexpr std::uintptr_t model_bone_counter_offset = 0x820;
  auto* const target_bytes = reinterpret_cast<std::uint8_t*>(target);
  const std::uint64_t model_bone_counter = aimbot_model_bone_counter();
  if (model_bone_counter == 0) {
    return false;
  }
  const std::uint32_t previous_accumulated_mask =
    *reinterpret_cast<std::uint32_t*>(target_bytes + accumulated_bone_mask_offset);
  *reinterpret_cast<std::uint64_t*>(target_bytes + readable_bone_mask_offset) = 0;
  *reinterpret_cast<float*>(target_bytes + last_bone_setup_time_offset) = current_time;
  *reinterpret_cast<std::uint32_t*>(target_bytes + previous_bone_mask_offset) = previous_accumulated_mask;
  *reinterpret_cast<std::uint32_t*>(target_bytes + accumulated_bone_mask_offset) = 0;
  *reinterpret_cast<std::uint32_t*>(target_bytes + accumulated_bone_mask_offset) |= aimbot_bone_mask;
  const std::uint32_t available_mask = previous_accumulated_mask | aimbot_bone_mask;
  *reinterpret_cast<std::uint32_t*>(target_bytes + readable_bone_mask_offset) = available_mask;
  *reinterpret_cast<std::uint32_t*>(target_bytes + writable_bone_mask_offset) = available_mask;
  *reinterpret_cast<std::uint64_t*>(target_bytes + model_bone_counter_offset) = model_bone_counter;

  enable_bone_accessor(target, 8);
  constexpr std::uintptr_t entity_flags_offset = 0x460;
  constexpr std::uint32_t setting_up_bones_flag = 1u << 3;
  auto* const entity_flags = reinterpret_cast<std::uint32_t*>(target_bytes + entity_flags_offset);
  const std::uint32_t saved_entity_flags = *entity_flags;
  *entity_flags = saved_entity_flags | setting_up_bones_flag;
  constexpr std::uintptr_t ik_context_offset = 0x7F0;
  constexpr std::uintptr_t bone_accessor_offset = 0x840;

  void* const ik_context = *reinterpret_cast<void**>(target_bytes + ik_context_offset);
  if (ik_context != nullptr && clear_ik_targets) {
    ik_clear_targets(ik_context);
  }
  if (ik_context != nullptr) {
    ik_init(ik_context, studio_header, &ik_angles, &root_origin,
      current_time, pose_frame, available_mask);
  }

  get_skeleton(target, studio_header, positions, rotations, available_mask, current_time);
  void* const bone_array = *reinterpret_cast<void**>(target_bytes + bone_accessor_offset);
  if (bone_array == nullptr) {
    *entity_flags = saved_entity_flags;
    disable_bone_accessor(target, 8);
    return false;
  }
  if (ik_context != nullptr) {
    update_ik_locks(target, current_time);
    ik_update_targets(ik_context, positions, rotations, bone_array, &computed);
    calculate_ik_locks(target, current_time);
    ik_solve_dependencies(ik_context, positions, rotations, bone_array, &computed);
  }
  build_transformations(target, studio_header, positions, rotations, root_transform, available_mask, &computed);
  *entity_flags = saved_entity_flags;
  disable_bone_accessor(target, 8);

  post_transform(target, studio_header);

  if (!setup_bones_attachment_helper(target, studio_header)) {
    return false;
  }

  int reconstructed_count = 0;
  if (!target->copy_cached_bones(bone_to_world, aimbot_max_bones, &reconstructed_count) ||
      reconstructed_count < bone_count || !aimbot_bones_are_finite(bone_to_world, bone_count)) {
    return false;
  }
  return true;
}

class aimbot_bone_access_guard {
public:
  aimbot_bone_access_guard() {
    static const auto constructor = reinterpret_cast<constructor_fn>(
      sigscan_module("client.so", sigs::base_animating_auto_allow_bone_access));
    static const auto destructor = reinterpret_cast<destructor_fn>(
      sigscan_module("client.so", sigs::base_animating_auto_allow_bone_access_on_delete));
    if (constructor == nullptr || destructor == nullptr) {
      return;
    }

    destructor_ = destructor;

    constructor(this, true, false);
    active_ = true;
  }

  ~aimbot_bone_access_guard() {
    if (active_) {
      destructor_(this);
    }
  }

  aimbot_bone_access_guard(const aimbot_bone_access_guard&) = delete;
  aimbot_bone_access_guard& operator=(const aimbot_bone_access_guard&) = delete;

  bool active() const {
    return active_;
  }

private:
  using constructor_fn = void (*)(void*, bool, bool);
  using destructor_fn = void (*)(void*);

  destructor_fn destructor_ = nullptr;
  bool active_ = false;
};

inline bool aimbot_setup_bones_at_time(Player* target,
  matrix_3x4* bone_to_world,
  float setup_time,
  int pose_frame,
  const Vec3& network_origin,
  bool clear_ik_targets,
  bool = false,
  int* bone_count_out = nullptr) {
  aimbot_bone_failure = aimbot_reject_reason::none;
  aimbot_bone_cache_debug = {};
  aimbot_bone_cache_debug.target_index = target != nullptr ? target->get_index() : -1;
  aimbot_bone_cache_debug.current_frame = global_vars != nullptr ? global_vars->framecount : 0;
  if (bone_count_out != nullptr) {
    *bone_count_out = 0;
  }
  if (target == nullptr || bone_to_world == nullptr || model_info == nullptr) {
    aimbot_bone_failure = aimbot_reject_reason::invalid;
    return false;
  }

  const model_t* model = target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  if (hdr == nullptr || hdr->num_bones <= 0 || hdr->num_bones > aimbot_max_bones) {
    aimbot_bone_failure = aimbot_reject_reason::no_studio_model;
    return false;
  }

  if (!std::isfinite(setup_time)) {
    aimbot_bone_failure = aimbot_reject_reason::bone_reconstruction;
    return false;
  }

  if (!aimbot_ensure_studio_header(target)) {
    aimbot_bone_failure = aimbot_reject_reason::bone_studio_header;
    return false;
  }

  if (!aimbot_sequence_is_valid(target)) {
    aimbot_bone_failure = aimbot_reject_reason::bone_sequence;
    return false;
  }

  const int setup_bone_count = std::min(hdr->num_bones, aimbot_max_bones);
  if (setup_bone_count <= 0) {
    aimbot_bone_failure = aimbot_reject_reason::bone_cache;
    return false;
  }

  if (!aimbot_reconstruct_bones(target, bone_to_world, setup_bone_count, setup_time,
      pose_frame, network_origin, clear_ik_targets) ||
      !aimbot_bones_are_finite(bone_to_world, setup_bone_count)) {
    aimbot_bone_failure = aimbot_reject_reason::bone_reconstruction;
    return false;
  }
  if (bone_count_out != nullptr) {
    *bone_count_out = setup_bone_count;
  }
  return true;
}

inline void aimbot_capture_latest_network_pose(Player* target,
                                                bool animation_already_updated = false) {
  if (target == nullptr) {
    return;
  }

  const int index = target->get_index();
  if (index <= 0 || index >= static_cast<int>(aimbot_current_poses.size())) {
    return;
  }

  aimbot_current_pose& pose = aimbot_current_poses[static_cast<std::size_t>(index)];
  if (target->is_dormant() || !target->is_alive()) {
    pose = {};
    return;
  }

  const model_t* model = target->get_model();
  const float simulation_time = target->get_simulation_time();
  const Vec3 network_origin = target->get_origin();
  const Vec3 render_origin = target->get_render_origin();
  const Vec3 velocity = target->get_velocity();
  const Vec3 eye_angles = target->get_eye_angles();
  const int sequence = aimbot_sequence(target);
  const float cycle = aimbot_cycle(target);
  const std::uint64_t pose_parameter_hash = aimbot_pose_parameter_hash(target);
  const float setup_time = simulation_time;
  if (model == nullptr || !std::isfinite(simulation_time) || simulation_time <= 0.0f ||
      !aimbot_vec3_is_finite(network_origin) || !aimbot_vec3_is_finite(render_origin) ||
      !aimbot_vec3_is_finite(velocity) || !aimbot_vec3_is_finite(eye_angles) ||
      !std::isfinite(cycle)) {
    pose = {};
    return;
  }

  if (aimbot_current_pose_signature_matches(pose, target, model, simulation_time,
      network_origin, render_origin, velocity, eye_angles, sequence, cycle,
      pose_parameter_hash, setup_time)) {

    pose.render_frame = global_vars != nullptr ? global_vars->framecount : pose.render_frame;
    pose.render_realtime = global_vars != nullptr ? global_vars->realtime : pose.render_realtime;
    pose.debug.signature_reused = true;
    pose.debug.current_frame = pose.render_frame;
    return;
  }

  const bool same_identity = pose.player == target && pose.model == model;
  const float sample_gap = same_identity ? simulation_time - pose.simulation_time : FLT_MAX;
  const float origin_delta_sq = same_identity
    ? aimbot_distance_squared(pose.network_origin, network_origin)
    : FLT_MAX;
  const float tick_interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : static_cast<float>(TICK_INTERVAL);
  const bool clear_ik_targets = !same_identity || sample_gap <= 0.0f ||
    sample_gap > tick_interval * 3.5f || origin_delta_sq > (64.0f * 64.0f);

  pose.valid = false;
  pose.player = target;
  pose.model = model;
  pose.simulation_time = simulation_time;
  pose.network_origin = network_origin;
  pose.render_origin = render_origin;
  pose.velocity = velocity;
  pose.eye_angles = eye_angles;
  pose.sequence = sequence;
  pose.cycle = cycle;
  pose.pose_parameter_hash = pose_parameter_hash;
  pose.bone_count = 0;
  pose.generation = ++aimbot_pose_generation;
  pose.debug = {};
  pose.debug.target_index = index;
  pose.debug.target_handle = target->get_ref_handle();
  pose.debug.generation = pose.generation;
  pose.debug.pose_frame = global_vars != nullptr ? global_vars->framecount : pose.generation;
  pose.debug.current_frame = pose.debug.pose_frame;
  pose.debug.simulation_time = simulation_time;
  pose.debug.setup_time = setup_time;
  pose.debug.simulation_age = global_vars != nullptr && std::isfinite(global_vars->curtime)
    ? global_vars->curtime - simulation_time
    : NAN;

  const int pose_frame = global_vars != nullptr ? global_vars->framecount : pose.generation;
  if (!aimbot_setup_bones_at_time(target, pose.bones.data(), setup_time,
      pose_frame, network_origin, clear_ik_targets, animation_already_updated,
      &pose.bone_count)) {
    pose.debug = aimbot_bone_cache_debug;
    pose.debug.target_index = index;
    pose.debug.target_handle = target->get_ref_handle();
    pose.debug.generation = pose.generation;
    pose.debug.pose_frame = pose_frame;
    pose.debug.current_frame = global_vars != nullptr ? global_vars->framecount : pose_frame;
    pose.debug.simulation_time = simulation_time;
    pose.debug.setup_time = setup_time;
    pose.debug.simulation_age = global_vars != nullptr && std::isfinite(global_vars->curtime)
      ? global_vars->curtime - simulation_time
      : NAN;
    pose.debug.failure = aimbot_last_bone_failure();
    return;
  }

  pose.velocity = target->get_velocity();
  pose.eye_angles = target->get_eye_angles();
  pose.sequence = aimbot_sequence(target);
  pose.cycle = aimbot_cycle(target);
  pose.pose_parameter_hash = aimbot_pose_parameter_hash(target);
  pose.render_frame = global_vars != nullptr ? global_vars->framecount : 0;
  pose.render_realtime = global_vars != nullptr ? global_vars->realtime : 0.0f;
  pose.setup_time = setup_time;
  pose.valid = pose.bone_count > 0;
  pose.debug = aimbot_bone_cache_debug;
  pose.debug.valid = pose.valid;
  pose.debug.target_index = index;
  pose.debug.target_handle = target->get_ref_handle();
  pose.debug.bone_count = pose.bone_count;
  pose.debug.generation = pose.generation;
  pose.debug.pose_frame = pose.render_frame;
  pose.debug.current_frame = global_vars != nullptr ? global_vars->framecount : pose.render_frame;
  pose.debug.simulation_time = simulation_time;
  pose.debug.setup_time = setup_time;
  pose.debug.simulation_age = global_vars != nullptr && std::isfinite(global_vars->curtime)
    ? global_vars->curtime - simulation_time
    : NAN;
}

inline aimbot_pose_debug_info aimbot_get_pose_debug(Player* target) {
  aimbot_pose_debug_info result{};
  result.current_frame = global_vars != nullptr ? global_vars->framecount : 0;
  if (target == nullptr) {
    return result;
  }

  const int index = target->get_index();
  result.target_index = index;
  result.target_handle = target->get_ref_handle();
  if (index <= 0 || index >= static_cast<int>(aimbot_current_poses.size())) {
    return result;
  }

  const aimbot_current_pose& pose = aimbot_current_poses[static_cast<std::size_t>(index)];
  if (pose.player == target) {
    result = pose.debug;
    result.target_index = index;
    result.target_handle = target->get_ref_handle();
    result.current_frame = global_vars != nullptr ? global_vars->framecount : result.current_frame;
  }
  return result;
}

inline aimbot_pose_debug_info aimbot_get_pose_debug_index(int index) {
  if (index <= 0 || index >= static_cast<int>(aimbot_current_poses.size())) {
    return {};
  }
  return aimbot_current_poses[static_cast<std::size_t>(index)].debug;
}

inline void aimbot_clear_network_pose(Player* target) {
  if (target == nullptr) {
    return;
  }
  const int index = target->get_index();
  if (index > 0 && index < static_cast<int>(aimbot_current_poses.size())) {
    aimbot_current_poses[static_cast<std::size_t>(index)] = {};
  }
}

inline bool aimbot_copy_network_pose_bones(Player* target,
                                           matrix_3x4* bone_to_world,
                                           int* bone_count_out = nullptr) {
  if (bone_count_out != nullptr) {
    *bone_count_out = 0;
  }
  if (target == nullptr || bone_to_world == nullptr) {
    return false;
  }

  const int index = target->get_index();
  if (index <= 0 || index >= static_cast<int>(aimbot_current_poses.size())) {
    return false;
  }

  const aimbot_current_pose& pose = aimbot_current_poses[static_cast<std::size_t>(index)];
  if (!pose.valid || pose.player != target || pose.bone_count <= 0 ||
      pose.bone_count > aimbot_max_bones || target->is_dormant() || !target->is_alive()) {
    return false;
  }

  std::memcpy(bone_to_world, pose.bones.data(),
    sizeof(matrix_3x4) * static_cast<std::size_t>(pose.bone_count));
  if (!aimbot_bones_are_finite(bone_to_world, pose.bone_count)) {
    if (bone_count_out != nullptr) {
      *bone_count_out = 0;
    }
    return false;
  }

  if (bone_count_out != nullptr) {
    *bone_count_out = pose.bone_count;
  }
  return true;
}

inline int aimbot_current_pose_generation(Player* target) {
  if (target == nullptr) {
    return 0;
  }
  const int index = target->get_index();
  if (index <= 0 || index >= static_cast<int>(aimbot_current_poses.size())) {
    return 0;
  }
  const aimbot_current_pose& pose = aimbot_current_poses[static_cast<std::size_t>(index)];
  return pose.player == target ? pose.generation : 0;
}

inline bool aimbot_get_bones(Player* target, matrix_3x4* bone_to_world, int* bone_count_out = nullptr) {
  if (target == nullptr || bone_to_world == nullptr) {
    aimbot_bone_failure = aimbot_reject_reason::invalid;
    return false;
  }

  aimbot_capture_latest_network_pose(target, false);
  const bool copied = aimbot_copy_network_pose_bones(target, bone_to_world, bone_count_out);
  aimbot_bone_failure = copied ? aimbot_reject_reason::none : aimbot_reject_reason::bone_reconstruction;
  return copied;
}

inline bool aimbot_get_hitbox_center(Player* target,
  int studio_hitbox_id,
  Vec3* center_out,
  int* bone_out = nullptr) {
  if (center_out == nullptr || target == nullptr || model_info == nullptr) {
    return false;
  }

  const model_t* model = target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr || studio_hitbox_id < 0 || studio_hitbox_id >= hitbox_set->num_hitboxes) {
    return false;
  }

  studio_box* hitbox = hitbox_set->hitbox(studio_hitbox_id);
  if (hitbox == nullptr || hitbox->bone < 0) {
    return false;
  }

  matrix_3x4 bone_to_world[aimbot_max_bones]{};
  int bone_count = 0;
  if (!aimbot_get_bones(target, bone_to_world, &bone_count) || hitbox->bone >= bone_count) {
    return false;
  }

  const Vec3 local_center = (hitbox->bbmin + hitbox->bbmax) * 0.5f;
  const matrix_3x4& matrix = bone_to_world[hitbox->bone];
  *center_out = Vec3{
    (local_center.x * matrix.mat[0][0]) + (local_center.y * matrix.mat[0][1]) + (local_center.z * matrix.mat[0][2]) + matrix.mat[0][3],
    (local_center.x * matrix.mat[1][0]) + (local_center.y * matrix.mat[1][1]) + (local_center.z * matrix.mat[1][2]) + matrix.mat[1][3],
    (local_center.x * matrix.mat[2][0]) + (local_center.y * matrix.mat[2][1]) + (local_center.z * matrix.mat[2][2]) + matrix.mat[2][3]
  };
  if (!aimbot_vec3_is_finite(*center_out)) {
    return false;
  }

  if (bone_out != nullptr) {
    *bone_out = hitbox->bone;
  }
  return true;
}

inline bool aimbot_get_bone_position(Player* target, int bone, Vec3* position_out) {
  if (target == nullptr || position_out == nullptr || bone < 0) {
    return false;
  }

  matrix_3x4 bone_to_world[aimbot_max_bones]{};
  int bone_count = 0;
  if (!aimbot_get_bones(target, bone_to_world, &bone_count) || bone >= bone_count) {
    return false;
  }

  *position_out = Vec3{
    bone_to_world[bone].mat[0][3],
    bone_to_world[bone].mat[1][3],
    bone_to_world[bone].mat[2][3]
  };
  return aimbot_vec3_is_finite(*position_out);
}

inline bool aimbot_copy_studio_hitboxes(Player* target,
  studio_hitbox_set** hitbox_set_out,
  matrix_3x4* bone_to_world,
  int* bone_count_out = nullptr) {
  if (target == nullptr || hitbox_set_out == nullptr || bone_to_world == nullptr || model_info == nullptr) {
    return false;
  }

  const model_t* model = target->get_model();
  if (model == nullptr) {
    return false;
  }

  studio_hdr* hdr = model_info->get_studio_model(model);
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr) {
    return false;
  }

  if (!aimbot_get_bones(target, bone_to_world, bone_count_out)) {
    return false;
  }

  *hitbox_set_out = hitbox_set;
  return true;
}

inline bool aimbot_vec3_is_finite(const Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline Vec3 aimbot_normalize_vector(const Vec3& value) {
  const float length = std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
  return length > 0.0001f ? value * (1.0f / length) : Vec3{};
}

inline Vec3 aimbot_direction_to_angles(const Vec3& direction) {
  const float planar_length = std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
  return Vec3{
    std::atan2(-direction.z, planar_length) * radpi,
    std::atan2(direction.y, direction.x) * radpi,
    0.0f
  };
}

inline void reset_aimbot_scope_timing() {
  aimbot_scoped_begin_time = 0.0f;
}

inline bool aimbot_sniper_scope_active(Player* localplayer) {
  if (localplayer == nullptr) {
    return false;
  }

  if (localplayer->is_scoped()) {
    return true;
  }

  const int fov = localplayer->get_fov();
  const int default_fov = localplayer->get_default_fov();
  if (fov > 0 && default_fov > 0 && fov < default_fov) {
    return true;
  }

  if (config.visuals.override_fov || config.visuals.removals.zoom) {
    return false;
  }

  const float view_fov = client != nullptr ? client->get_fov() : 0.0f;
  return std::isfinite(view_fov) && view_fov > 1.0f &&
    default_fov > 0 && view_fov < static_cast<float>(default_fov) - 0.5f;
}

inline bool aimbot_sniper_scope_confirmed(Player* localplayer) {
  return aimbot_sniper_scope_active(localplayer);
}

inline void update_aimbot_scope_timing(Player* localplayer) {
  if (!aimbot_sniper_scope_confirmed(localplayer)) {
    reset_aimbot_scope_timing();
    return;
  }

  if (aimbot_scoped_begin_time > 0.0f) {
    return;
  }

  aimbot_scoped_begin_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
}

inline float aimbot_tracked_scoped_time(Player* localplayer) {
  if (!aimbot_sniper_scope_confirmed(localplayer) || aimbot_scoped_begin_time <= 0.0f) {
    return 0.0f;
  }

  const float current_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
  return std::max(current_time - aimbot_scoped_begin_time, 0.0f);
}

inline bool aimbot_sniper_scope_time_ready(Player* localplayer) {
  if (!aimbot_sniper_scope_confirmed(localplayer)) {
    return false;
  }

  constexpr float sniper_headshot_scope_delay = 0.45f;

  return aimbot_tracked_scoped_time(localplayer) >= sniper_headshot_scope_delay;
}

inline bool aimbot_body_aim_lethal(Player* localplayer, Weapon* weapon, Player* target);

inline float aimbot_distance_squared(const Vec3& left, const Vec3& right) {
  const Vec3 delta = left - right;
  return (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z);
}

inline Vec3 aimbot_transform_point(const Vec3& point, const matrix_3x4& matrix) {
  return Vec3{
    (point.x * matrix.mat[0][0]) + (point.y * matrix.mat[0][1]) + (point.z * matrix.mat[0][2]) + matrix.mat[0][3],
    (point.x * matrix.mat[1][0]) + (point.y * matrix.mat[1][1]) + (point.z * matrix.mat[1][2]) + matrix.mat[1][3],
    (point.x * matrix.mat[2][0]) + (point.y * matrix.mat[2][1]) + (point.z * matrix.mat[2][2]) + matrix.mat[2][3]
  };
}

inline bool aimbot_translate_bones(matrix_3x4* bone_to_world,
                                   int bone_count,
                                   const Vec3& offset) {
  if (bone_to_world == nullptr || bone_count <= 0 || bone_count > aimbot_max_bones ||
      !aimbot_vec3_is_finite(offset)) {
    return false;
  }

  for (int bone = 0; bone < bone_count; ++bone) {
    bone_to_world[bone].mat[0][3] += offset.x;
    bone_to_world[bone].mat[1][3] += offset.y;
    bone_to_world[bone].mat[2][3] += offset.z;
  }
  return aimbot_bones_are_finite(bone_to_world, bone_count);
}

inline Vec3 aimbot_inverse_transform_point(const Vec3& point, const matrix_3x4& matrix) {
  const Vec3 delta{
    point.x - matrix.mat[0][3],
    point.y - matrix.mat[1][3],
    point.z - matrix.mat[2][3]
  };

  return Vec3{
    (delta.x * matrix.mat[0][0]) + (delta.y * matrix.mat[1][0]) + (delta.z * matrix.mat[2][0]),
    (delta.x * matrix.mat[0][1]) + (delta.y * matrix.mat[1][1]) + (delta.z * matrix.mat[2][1]),
    (delta.x * matrix.mat[0][2]) + (delta.y * matrix.mat[1][2]) + (delta.z * matrix.mat[2][2])
  };
}

inline Vec3 aimbot_clamp_to_hitbox(const Vec3& point, const studio_box& hitbox) {
  return Vec3{
    std::clamp(point.x, hitbox.bbmin.x, hitbox.bbmax.x),
    std::clamp(point.y, hitbox.bbmin.y, hitbox.bbmax.y),
    std::clamp(point.z, hitbox.bbmin.z, hitbox.bbmax.z)
  };
}

inline bool aimbot_add_local_hitbox_point(Vec3* points, int* point_count, int max_points, const Vec3& point) {
  if (points == nullptr || point_count == nullptr || *point_count >= max_points || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  for (int index = 0; index < *point_count; ++index) {
    if (aimbot_distance_squared(points[index], point) < 0.25f) {
      return false;
    }
  }

  points[*point_count] = point;
  ++(*point_count);
  return true;
}

inline float aimbot_effective_multipoint_scale() {
  const float configured = config.aimbot.multipoint_scale;
  if (!std::isfinite(configured)) {
    return 0.0f;
  }

  return std::clamp(configured, 0.0f, 100.0f) / 100.0f;
}

inline float aimbot_effective_bone_size_subtract() {
  const float configured = config.aimbot.bone_size_subtract;
  if (!std::isfinite(configured)) {
    return 0.0f;
  }

  return std::clamp(configured, 0.0f, 12.0f);
}

inline float aimbot_effective_bone_size_min_scale() {
  const float configured = config.aimbot.bone_size_min_scale;
  if (!std::isfinite(configured)) {
    return 0.4f;
  }

  return std::clamp(configured, 0.05f, 1.0f);
}

inline float aimbot_multipoint_scale_for_hitbox(int base_hitbox) {
  if (base_hitbox == aim_hitbox_head) {
    return 0.72f;
  }

  return aimbot_effective_multipoint_scale();
}

inline bool aimbot_local_point_inside_hitbox(const Vec3& point, const studio_box& hitbox) {
  if (!aimbot_vec3_is_finite(point) || !aimbot_vec3_is_finite(hitbox.bbmin) ||
      !aimbot_vec3_is_finite(hitbox.bbmax)) {
    return false;
  }

  constexpr float epsilon = 0.01f;
  const Vec3 center = (hitbox.bbmin + hitbox.bbmax) * 0.5f;
  const Vec3 half = (hitbox.bbmax - hitbox.bbmin) * 0.5f;
  const Vec3 delta = point - center;
  if (delta.x < -half.x - epsilon || delta.x > half.x + epsilon ||
      delta.y < -half.y - epsilon || delta.y > half.y + epsilon ||
      delta.z < -half.z - epsilon || delta.z > half.z + epsilon) {
    return false;
  }

  if (!std::isfinite(hitbox.radius) || hitbox.radius <= 0.0f) {
    return true;
  }

  const float half_values[3]{half.x, half.y, half.z};
  int axis = 0;
  if (half_values[1] > half_values[axis]) axis = 1;
  if (half_values[2] > half_values[axis]) axis = 2;

  const int first_radial_axis = axis == 0 ? 1 : 0;
  const int second_radial_axis = axis == 2 ? 1 : 2;
  const float radial_limit = std::min({
    hitbox.radius,
    half_values[first_radial_axis],
    half_values[second_radial_axis]});
  if (radial_limit <= 0.0f) {
    return true;
  }

  const float axial_half = std::max(0.0f, half_values[axis] - radial_limit);
  const float delta_values[3]{delta.x, delta.y, delta.z};
  const float axial = std::fabs(delta_values[axis]);
  const float radial_first = delta_values[first_radial_axis];
  const float radial_second = delta_values[second_radial_axis];
  const float outside_axial = std::max(0.0f, axial - axial_half);
  const float distance_squared = outside_axial * outside_axial +
    radial_first * radial_first + radial_second * radial_second;
  return distance_squared <= (radial_limit * radial_limit) + epsilon;
}

inline int aimbot_build_local_hitbox_points(const studio_box& hitbox,
  const matrix_3x4&,
  const Vec3&,
  Vec3* points,
  int max_points,
  bool include_multipoint,
  int base_hitbox = -1) {
  int point_count = 0;
  const Vec3 center = (hitbox.bbmin + hitbox.bbmax) * 0.5f;
  aimbot_add_local_hitbox_point(points, &point_count, max_points, center);

  if (!include_multipoint) {
    return point_count;
  }

  const float scale = aimbot_multipoint_scale_for_hitbox(base_hitbox);
  if (scale <= 0.0f) {
    return point_count;
  }

  const Vec3 half = (hitbox.bbmax - hitbox.bbmin) * 0.5f;
  if (!aimbot_vec3_is_finite(half) || half.x <= 0.0f || half.y <= 0.0f || half.z <= 0.0f) {
    return point_count;
  }

  const float subtract = aimbot_effective_bone_size_subtract();
  const float minimum_scale = aimbot_effective_bone_size_min_scale();
  const Vec3 safe_half{
    std::min(half.x, std::max(half.x - subtract, half.x * minimum_scale)),
    std::min(half.y, std::max(half.y - subtract, half.y * minimum_scale)),
    std::min(half.z, std::max(half.z - subtract, half.z * minimum_scale))
  };

  const Vec3 sample_half = safe_half * scale;
  const Vec3 axis_points[6]{
    {center.x - sample_half.x, center.y, center.z},
    {center.x + sample_half.x, center.y, center.z},
    {center.x, center.y - sample_half.y, center.z},
    {center.x, center.y + sample_half.y, center.z},
    {center.x, center.y, center.z - sample_half.z},
    {center.x, center.y, center.z + sample_half.z}
  };
  for (const Vec3& point : axis_points) {
    if (aimbot_local_point_inside_hitbox(point, hitbox)) {
      aimbot_add_local_hitbox_point(points, &point_count, max_points, point);
    }
  }

  const float signs[2]{-1.0f, 1.0f};
  for (const float x_sign : signs) {
    for (const float y_sign : signs) {
      for (const float z_sign : signs) {
        const Vec3 point = center + Vec3{
          sample_half.x * x_sign,
          sample_half.y * y_sign,
          sample_half.z * z_sign};
        if (aimbot_local_point_inside_hitbox(point, hitbox)) {
          aimbot_add_local_hitbox_point(points, &point_count, max_points, point);
        }
      }
    }
  }

  const Vec3 edge_points[12]{
    {center.x - sample_half.x, center.y - sample_half.y, center.z},
    {center.x - sample_half.x, center.y + sample_half.y, center.z},
    {center.x + sample_half.x, center.y - sample_half.y, center.z},
    {center.x + sample_half.x, center.y + sample_half.y, center.z},
    {center.x - sample_half.x, center.y, center.z - sample_half.z},
    {center.x - sample_half.x, center.y, center.z + sample_half.z},
    {center.x + sample_half.x, center.y, center.z - sample_half.z},
    {center.x + sample_half.x, center.y, center.z + sample_half.z},
    {center.x, center.y - sample_half.y, center.z - sample_half.z},
    {center.x, center.y - sample_half.y, center.z + sample_half.z},
    {center.x, center.y + sample_half.y, center.z - sample_half.z},
    {center.x, center.y + sample_half.y, center.z + sample_half.z}
  };
  for (const Vec3& point : edge_points) {
    if (!aimbot_local_point_inside_hitbox(point, hitbox)) {
      continue;
    }
    aimbot_add_local_hitbox_point(
      points,
      &point_count,
      max_points,
      point);
  }

  return point_count;
}

inline unsigned int aimbot_visibility_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline unsigned int aimbot_hitscan_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline bool is_player_visible(Player* localplayer, Player* entity, int bone) {
  if (localplayer == nullptr || entity == nullptr || engine_trace == nullptr) return false;

  Vec3 start_pos = localplayer->get_shoot_pos();
  Vec3 target_pos{};
  if (!aimbot_get_bone_position(entity, bone, &target_pos)) {
    return false;
  }

  struct ray_t ray = engine_trace->init_ray(&start_pos, &target_pos);
  struct trace_filter filter;
  engine_trace->init_hitscan_trace_filter(&filter, localplayer, entity);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, aimbot_visibility_trace_mask(), &filter, &trace_world);
  return trace_world.entity == entity || (!trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f);
}

inline bool aimbot_trace_visible_to_position(Player* localplayer,
  Entity* target,
  const Vec3& target_pos,
  unsigned int trace_mask = aimbot_visibility_trace_mask()) {
  if (localplayer == nullptr || target == nullptr || engine_trace == nullptr || !aimbot_vec3_is_finite(target_pos)) return false;

  Vec3 start_pos = localplayer->get_shoot_pos();
  Vec3 end_pos = target_pos;

  struct ray_t ray = engine_trace->init_ray(&start_pos, &end_pos);
  struct trace_filter filter;
  engine_trace->init_hitscan_trace_filter(&filter, localplayer, target);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, trace_mask, &filter, &trace_world);
  return trace_world.entity == target || (!trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f);
}

inline Vec3 aimbot_calculate_angles_to_position(const Vec3& start, const Vec3& target) {
  Vec3 diff{
    target.x - start.x,
    target.y - start.y,
    target.z - start.z
  };
  float yaw_hyp_sq = (diff.x * diff.x) + (diff.y * diff.y);
  if (yaw_hyp_sq <= 1e-12f) {
    return Vec3{
      diff.z > 0.0f ? -89.0f : (diff.z < 0.0f ? 89.0f : 0.0f),
      0.0f,
      0.0f
    };
  }
  float yaw_hyp = std::sqrt(yaw_hyp_sq);
  return Vec3{
    -std::atan2(diff.z, yaw_hyp) * radpi,
    std::atan2(diff.y, diff.x) * radpi,
    0.0f
  };
}

inline Vec3 aimbot_normalize_angle_delta(const Vec3& target_angles, const Vec3& source_angles) {
  float x_diff = target_angles.x - source_angles.x;
  float y_diff = target_angles.y - source_angles.y;

  return Vec3{
    std::clamp(std::remainder(x_diff, 360.0f), -89.0f, 89.0f),
    std::clamp(std::remainder(y_diff, 360.0f), -180.0f, 180.0f),
    0.0f
  };
}

inline Vec3 aimbot_clamp_angles(Vec3 angles) {
  angles.x = std::clamp(angles.x, -89.0f, 89.0f);
  angles.y = std::remainder(angles.y, 360.0f);
  angles.z = 0.0f;
  return angles;
}

inline float aimbot_calculate_fov(const Vec3& target_angles, const Vec3& source_angles) {
  Vec3 delta = aimbot_normalize_angle_delta(target_angles, source_angles);
  return std::hypot(delta.x, delta.y);
}

inline bool aimbot_sniper_headshot_ready(Player* localplayer, Weapon* weapon);

inline bool aimbot_headshot_ready_for_priority(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr || !weapon->is_headshot_weapon()) {
    return false;
  }

  if (weapon->is_sniper_rifle()) {
    return aimbot_sniper_headshot_ready(localplayer, weapon);
  }

  if (weapon->can_fire_critical_shot(true)) {
    return true;
  }

  switch (weapon->get_weapon_id()) {
  case TF_WEAPON_REVOLVER:
    return attribute_manager == nullptr ||
      attribute_manager->attrib_hook_value(0, "set_weapon_mode", weapon->to_entity()) != 1 ||
      weapon->can_ambassador_headshot();
  default:
    return false;
  }
}

inline int aimbot_default_bone(Player* localplayer, Player* target, Weapon* weapon) {
  if (localplayer == nullptr || target == nullptr) return 0;

  int bone = target->get_tf_class() == tf_class::ENGINEER ? 5 : 2;
  if (aimbot_headshot_ready_for_priority(localplayer, weapon)) {
    bone = target->get_head_bone();
  }

  return bone;
}

inline bool aimbot_model_name_is(Player* target, const char* name) {
  if (target == nullptr || name == nullptr || model_info == nullptr) {
    return false;
  }

  const model_t* model = target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  if (hdr == nullptr) {
    return false;
  }

  const std::size_t name_length = std::strlen(name);
  if (name_length >= sizeof(hdr->name)) {
    return false;
  }

  return std::strncmp(hdr->name, name, name_length) == 0 && hdr->name[name_length] == '\0';
}

inline bool aimbot_model_name_is_any(Player* target, const char* first, const char* second, const char* third = nullptr) {
  return aimbot_model_name_is(target, first) ||
    aimbot_model_name_is(target, second) ||
    (third != nullptr && aimbot_model_name_is(target, third));
}

inline int aimbot_studio_hitbox_to_base(Player* target, int hitbox_id) {
  if (aimbot_model_name_is(target, "models/bots/engineer/bot_engineer.mdl")) {
    switch (hitbox_id) {
    case 0: return aim_hitbox_head;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 5: return aim_hitbox_left_upper_arm;
    case 6: return aim_hitbox_left_forearm;
    case 7: return aim_hitbox_left_hand;
    case 8: return aim_hitbox_right_upper_arm;
    case 9: return aim_hitbox_right_forearm;
    case 10: return aim_hitbox_right_hand;
    case 11: return aim_hitbox_left_thigh;
    case 12: return aim_hitbox_left_calf;
    case 13: return aim_hitbox_left_foot;
    case 14: return aim_hitbox_right_thigh;
    case 15: return aim_hitbox_right_calf;
    case 16: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is_any(target, "models/vsh/player/saxton_hale.mdl", "models/vsh/player/hell_hale.mdl", "models/vsh/player/santa_hale.mdl")) {
    switch (hitbox_id) {
    case 0: return aim_hitbox_head;
    case 1:
    case 14: return aim_hitbox_pelvis;
    case 15: return aim_hitbox_spine_0;
    case 16: return aim_hitbox_spine_1;
    case 17: return aim_hitbox_spine_2;
    case 18: return aim_hitbox_spine_3;
    case 12: return aim_hitbox_left_upper_arm;
    case 10: return aim_hitbox_left_forearm;
    case 8: return aim_hitbox_left_hand;
    case 13: return aim_hitbox_right_upper_arm;
    case 11: return aim_hitbox_right_forearm;
    case 9: return aim_hitbox_right_hand;
    case 6: return aim_hitbox_left_thigh;
    case 4: return aim_hitbox_left_calf;
    case 2: return aim_hitbox_left_foot;
    case 7: return aim_hitbox_right_thigh;
    case 5: return aim_hitbox_right_calf;
    case 3: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is_any(target, "models/player/scout_infected.mdl", "models/player/soldier_infected.mdl", "models/player/sniper_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 9: return aim_hitbox_left_upper_arm;
    case 11: return aim_hitbox_left_forearm;
    case 19: return aim_hitbox_left_hand;
    case 8:
    case 10: return aim_hitbox_right_upper_arm;
    case 12: return aim_hitbox_right_forearm;
    case 20: return aim_hitbox_right_hand;
    case 13: return aim_hitbox_left_thigh;
    case 15: return aim_hitbox_left_calf;
    case 17: return aim_hitbox_left_foot;
    case 14: return aim_hitbox_right_thigh;
    case 16: return aim_hitbox_right_calf;
    case 18: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/pyro_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 8: return aim_hitbox_left_upper_arm;
    case 9: return aim_hitbox_left_forearm;
    case 10: return aim_hitbox_left_hand;
    case 11:
    case 12: return aim_hitbox_right_upper_arm;
    case 13: return aim_hitbox_right_forearm;
    case 14: return aim_hitbox_right_hand;
    case 15: return aim_hitbox_left_thigh;
    case 16: return aim_hitbox_left_calf;
    case 17: return aim_hitbox_left_foot;
    case 19: return aim_hitbox_right_thigh;
    case 20: return aim_hitbox_right_calf;
    case 21: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/demo_infected.mdl")) {
    switch (hitbox_id) {
    case 16: return aim_hitbox_head;
    case 0:
    case 15: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 5:
    case 6: return aim_hitbox_left_upper_arm;
    case 13: return aim_hitbox_left_forearm;
    case 17: return aim_hitbox_left_hand;
    case 7:
    case 8: return aim_hitbox_right_upper_arm;
    case 14: return aim_hitbox_right_forearm;
    case 18: return aim_hitbox_right_hand;
    case 9: return aim_hitbox_left_thigh;
    case 10: return aim_hitbox_left_calf;
    case 19: return aim_hitbox_left_foot;
    case 11: return aim_hitbox_right_thigh;
    case 12: return aim_hitbox_right_calf;
    case 20: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/heavy_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 9: return aim_hitbox_left_upper_arm;
    case 11: return aim_hitbox_left_forearm;
    case 17: return aim_hitbox_left_hand;
    case 8:
    case 10: return aim_hitbox_right_upper_arm;
    case 12: return aim_hitbox_right_forearm;
    case 18: return aim_hitbox_right_hand;
    case 13: return aim_hitbox_left_thigh;
    case 15: return aim_hitbox_left_calf;
    case 19: return aim_hitbox_left_foot;
    case 14: return aim_hitbox_right_thigh;
    case 16: return aim_hitbox_right_calf;
    case 20: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/engineer_infected.mdl")) {
    switch (hitbox_id) {
    case 8: return aim_hitbox_head;
    case 0:
    case 7: return aim_hitbox_pelvis;
    case 3: return aim_hitbox_spine_0;
    case 4: return aim_hitbox_spine_1;
    case 5: return aim_hitbox_spine_2;
    case 6: return aim_hitbox_spine_3;
    case 11:
    case 12: return aim_hitbox_left_upper_arm;
    case 13: return aim_hitbox_left_forearm;
    case 20: return aim_hitbox_left_hand;
    case 14:
    case 15: return aim_hitbox_right_upper_arm;
    case 16: return aim_hitbox_right_forearm;
    case 19: return aim_hitbox_right_hand;
    case 9: return aim_hitbox_left_thigh;
    case 10: return aim_hitbox_left_calf;
    case 17: return aim_hitbox_left_foot;
    case 1: return aim_hitbox_right_thigh;
    case 2: return aim_hitbox_right_calf;
    case 18: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/medic_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 9: return aim_hitbox_left_upper_arm;
    case 11: return aim_hitbox_left_forearm;
    case 17: return aim_hitbox_left_hand;
    case 8:
    case 10: return aim_hitbox_right_upper_arm;
    case 12: return aim_hitbox_right_forearm;
    case 18: return aim_hitbox_right_hand;
    case 13: return aim_hitbox_left_thigh;
    case 15: return aim_hitbox_left_calf;
    case 22: return aim_hitbox_left_foot;
    case 14: return aim_hitbox_right_thigh;
    case 16: return aim_hitbox_right_calf;
    case 23: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/spy_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 9: return aim_hitbox_left_upper_arm;
    case 11: return aim_hitbox_left_forearm;
    case 13: return aim_hitbox_left_hand;
    case 8:
    case 10: return aim_hitbox_right_upper_arm;
    case 12: return aim_hitbox_right_forearm;
    case 14: return aim_hitbox_right_hand;
    case 15: return aim_hitbox_left_thigh;
    case 17: return aim_hitbox_left_calf;
    case 19: return aim_hitbox_left_foot;
    case 16: return aim_hitbox_right_thigh;
    case 18: return aim_hitbox_right_calf;
    case 20: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  return hitbox_id;
}

inline int aimbot_base_hitbox_to_studio(Player* target, int hitbox_id) {
  if (aimbot_model_name_is(target, "models/bots/engineer/bot_engineer.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 0;
    case aim_hitbox_pelvis:
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 5;
    case aim_hitbox_left_forearm: return 6;
    case aim_hitbox_left_hand: return 7;
    case aim_hitbox_right_upper_arm: return 8;
    case aim_hitbox_right_forearm: return 9;
    case aim_hitbox_right_hand: return 10;
    case aim_hitbox_left_thigh: return 11;
    case aim_hitbox_left_calf: return 12;
    case aim_hitbox_left_foot: return 13;
    case aim_hitbox_right_thigh: return 14;
    case aim_hitbox_right_calf: return 15;
    case aim_hitbox_right_foot: return 16;
    default: return -1;
    }
  }

  if (aimbot_model_name_is_any(target, "models/vsh/player/saxton_hale.mdl", "models/vsh/player/hell_hale.mdl", "models/vsh/player/santa_hale.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 0;
    case aim_hitbox_pelvis: return 14;
    case aim_hitbox_spine_0: return 15;
    case aim_hitbox_spine_1: return 16;
    case aim_hitbox_spine_2: return 17;
    case aim_hitbox_spine_3: return 18;
    case aim_hitbox_left_upper_arm: return 12;
    case aim_hitbox_left_forearm: return 10;
    case aim_hitbox_left_hand: return 8;
    case aim_hitbox_right_upper_arm: return 13;
    case aim_hitbox_right_forearm: return 11;
    case aim_hitbox_right_hand: return 9;
    case aim_hitbox_left_thigh: return 6;
    case aim_hitbox_left_calf: return 4;
    case aim_hitbox_left_foot: return 2;
    case aim_hitbox_right_thigh: return 7;
    case aim_hitbox_right_calf: return 5;
    case aim_hitbox_right_foot: return 3;
    default: return -1;
    }
  }

  if (aimbot_model_name_is_any(target, "models/player/scout_infected.mdl", "models/player/soldier_infected.mdl", "models/player/sniper_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 9;
    case aim_hitbox_left_forearm: return 11;
    case aim_hitbox_left_hand: return 19;
    case aim_hitbox_right_upper_arm: return 10;
    case aim_hitbox_right_forearm: return 12;
    case aim_hitbox_right_hand: return 20;
    case aim_hitbox_left_thigh: return 13;
    case aim_hitbox_left_calf: return 15;
    case aim_hitbox_left_foot: return 17;
    case aim_hitbox_right_thigh: return 14;
    case aim_hitbox_right_calf: return 16;
    case aim_hitbox_right_foot: return 18;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/pyro_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 8;
    case aim_hitbox_left_forearm: return 9;
    case aim_hitbox_left_hand: return 10;
    case aim_hitbox_right_upper_arm: return 12;
    case aim_hitbox_right_forearm: return 13;
    case aim_hitbox_right_hand: return 14;
    case aim_hitbox_left_thigh: return 15;
    case aim_hitbox_left_calf: return 16;
    case aim_hitbox_left_foot: return 17;
    case aim_hitbox_right_thigh: return 19;
    case aim_hitbox_right_calf: return 20;
    case aim_hitbox_right_foot: return 21;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/demo_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 16;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 6;
    case aim_hitbox_left_forearm: return 13;
    case aim_hitbox_left_hand: return 17;
    case aim_hitbox_right_upper_arm: return 8;
    case aim_hitbox_right_forearm: return 14;
    case aim_hitbox_right_hand: return 18;
    case aim_hitbox_left_thigh: return 9;
    case aim_hitbox_left_calf: return 10;
    case aim_hitbox_left_foot: return 19;
    case aim_hitbox_right_thigh: return 11;
    case aim_hitbox_right_calf: return 12;
    case aim_hitbox_right_foot: return 20;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/heavy_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 9;
    case aim_hitbox_left_forearm: return 11;
    case aim_hitbox_left_hand: return 17;
    case aim_hitbox_right_upper_arm: return 10;
    case aim_hitbox_right_forearm: return 12;
    case aim_hitbox_right_hand: return 18;
    case aim_hitbox_left_thigh: return 13;
    case aim_hitbox_left_calf: return 15;
    case aim_hitbox_left_foot: return 19;
    case aim_hitbox_right_thigh: return 14;
    case aim_hitbox_right_calf: return 16;
    case aim_hitbox_right_foot: return 20;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/engineer_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 8;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 3;
    case aim_hitbox_spine_1: return 4;
    case aim_hitbox_spine_2: return 5;
    case aim_hitbox_spine_3: return 6;
    case aim_hitbox_left_upper_arm: return 12;
    case aim_hitbox_left_forearm: return 13;
    case aim_hitbox_left_hand: return 20;
    case aim_hitbox_right_upper_arm: return 15;
    case aim_hitbox_right_forearm: return 16;
    case aim_hitbox_right_hand: return 19;
    case aim_hitbox_left_thigh: return 9;
    case aim_hitbox_left_calf: return 10;
    case aim_hitbox_left_foot: return 17;
    case aim_hitbox_right_thigh: return 1;
    case aim_hitbox_right_calf: return 2;
    case aim_hitbox_right_foot: return 18;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/medic_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 9;
    case aim_hitbox_left_forearm: return 11;
    case aim_hitbox_left_hand: return 17;
    case aim_hitbox_right_upper_arm: return 10;
    case aim_hitbox_right_forearm: return 12;
    case aim_hitbox_right_hand: return 18;
    case aim_hitbox_left_thigh: return 13;
    case aim_hitbox_left_calf: return 15;
    case aim_hitbox_left_foot: return 22;
    case aim_hitbox_right_thigh: return 14;
    case aim_hitbox_right_calf: return 16;
    case aim_hitbox_right_foot: return 23;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/spy_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 9;
    case aim_hitbox_left_forearm: return 11;
    case aim_hitbox_left_hand: return 13;
    case aim_hitbox_right_upper_arm: return 10;
    case aim_hitbox_right_forearm: return 12;
    case aim_hitbox_right_hand: return 14;
    case aim_hitbox_left_thigh: return 15;
    case aim_hitbox_left_calf: return 17;
    case aim_hitbox_left_foot: return 19;
    case aim_hitbox_right_thigh: return 16;
    case aim_hitbox_right_calf: return 18;
    case aim_hitbox_right_foot: return 20;
    default: return -1;
    }
  }

  return hitbox_id;
}

inline bool aimbot_hitbox_matches_mask(int hitbox_id, uint32_t hitbox_mask) {
  switch (hitbox_id) {
  case aim_hitbox_head:
    return (hitbox_mask & aim_hitbox_mask_head) != 0;
  case aim_hitbox_pelvis:
    return (hitbox_mask & aim_hitbox_mask_pelvis) != 0;
  case aim_hitbox_spine_0:
  case aim_hitbox_spine_1:
  case aim_hitbox_spine_2:
  case aim_hitbox_spine_3:
    return (hitbox_mask & aim_hitbox_mask_body) != 0;
  case aim_hitbox_left_upper_arm:
  case aim_hitbox_left_forearm:
  case aim_hitbox_left_hand:
  case aim_hitbox_right_upper_arm:
  case aim_hitbox_right_forearm:
  case aim_hitbox_right_hand:
    return (hitbox_mask & aim_hitbox_mask_arms) != 0;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    return (hitbox_mask & aim_hitbox_mask_legs) != 0;
  default:
    return false;
  }
}

inline int aimbot_hitbox_priority(Player* localplayer, Player* target, Weapon* weapon, int hitbox_id) {
  bool prefer_head = false;
  if (localplayer != nullptr && target != nullptr) {
    const uint32_t modifiers = config.aimbot.hitscan_modifiers;
    const bool user_wants_head =
      weapon != nullptr &&
      weapon->is_headshot_weapon() &&
      (modifiers & Aim::hitscan_mod_wait_for_headshot) != 0 &&
      (!weapon->is_sniper_rifle() || aimbot_sniper_scope_active(localplayer));
    prefer_head = user_wants_head || aimbot_headshot_ready_for_priority(localplayer, weapon);
  }

  switch (hitbox_id) {
  case aim_hitbox_head:
    return prefer_head ? 0 : 1;
  case aim_hitbox_spine_0:
  case aim_hitbox_spine_1:
  case aim_hitbox_spine_2:
  case aim_hitbox_spine_3:
    return prefer_head ? 1 : 0;
  case aim_hitbox_pelvis:
    return 2;
  case aim_hitbox_left_upper_arm:
  case aim_hitbox_left_forearm:
  case aim_hitbox_left_hand:
  case aim_hitbox_right_upper_arm:
  case aim_hitbox_right_forearm:
  case aim_hitbox_right_hand:
    return 3;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    return 4;
  default:
    return INT_MAX;
  }
}

inline int aimbot_fallback_hitbox_for_mask(Player* localplayer, Player* target, Weapon* weapon, uint32_t hitbox_mask) {
  constexpr int fallback_hitboxes[] = {
    aim_hitbox_head,
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh
  };

  int best_hitbox = -1;
  int best_priority = INT_MAX;
  for (const int hitbox_id : fallback_hitboxes) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
      continue;
    }

    const int priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox_id);
    if (priority < best_priority) {
      best_hitbox = hitbox_id;
      best_priority = priority;
    }
  }

  return best_hitbox;
}

inline aimbot_point aimbot_find_best_point(Player* localplayer,
  Player* target,
  Weapon* weapon,
  const Vec3& original_view_angles,
  uint32_t hitbox_mask,
  bool require_visibility = true,
  unsigned int trace_mask = aimbot_visibility_trace_mask()) {
  aimbot_point best_point{};
  if (localplayer == nullptr || target == nullptr) {
    return best_point;
  }

  if (hitbox_mask == aim_hitbox_mask_none) {
    return best_point;
  }

  studio_hitbox_set* hitbox_set = nullptr;
  matrix_3x4 bone_to_world[aimbot_max_bones]{};
  int bone_count = 0;
  if (aimbot_copy_studio_hitboxes(target, &hitbox_set, bone_to_world, &bone_count)) {
    const Vec3 shoot_pos = localplayer->get_shoot_pos();
    for (int studio_hitbox_id = 0; studio_hitbox_id < hitbox_set->num_hitboxes; ++studio_hitbox_id) {
      studio_box* hitbox = hitbox_set->hitbox(studio_hitbox_id);
      if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= bone_count) {
        continue;
      }

      const int hitbox_id = aimbot_studio_hitbox_to_base(target, studio_hitbox_id);
      if (hitbox_id < 0 || !aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
        continue;
      }

      constexpr int max_local_points = 21;
      Vec3 local_points[max_local_points]{};
      const int point_count = aimbot_build_local_hitbox_points(
        *hitbox,
        bone_to_world[hitbox->bone],
        shoot_pos,
        local_points,
        max_local_points,
        require_visibility,
        hitbox_id);

      for (int point_index = 0; point_index < point_count; ++point_index) {
        const Vec3 hitbox_position = aimbot_transform_point(local_points[point_index], bone_to_world[hitbox->bone]);
        if (!aimbot_vec3_is_finite(hitbox_position)) {
          continue;
        }

        if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, hitbox_position, trace_mask)) {
          continue;
        }

        aimbot_point point{};
        point.valid = true;
        point.bone = hitbox->bone;
        point.hitbox = hitbox_id;
        point.studio_hitbox = studio_hitbox_id;
        point.priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox_id);
        point.position = hitbox_position;
        point.angles = aimbot_calculate_angles_to_position(shoot_pos, hitbox_position);
        point.fov = aimbot_calculate_fov(point.angles, original_view_angles);

        if (!best_point.valid ||
            point.priority < best_point.priority ||
            (point.priority == best_point.priority && point.fov < best_point.fov)) {
          best_point = point;
        }
      }
    }
  }

  if (best_point.valid) {
    return best_point;
  }

  for (int hitbox_id = aim_hitbox_head; hitbox_id <= aim_hitbox_right_foot; ++hitbox_id) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
      continue;
    }

    Vec3 hitbox_position{};
    int hitbox_bone = 0;
    const int studio_hitbox_id = aimbot_base_hitbox_to_studio(target, hitbox_id);
    if (studio_hitbox_id < 0 ||
        !aimbot_get_hitbox_center(target, studio_hitbox_id, &hitbox_position, &hitbox_bone)) {
      continue;
    }

    if (!aimbot_vec3_is_finite(hitbox_position)) {
      continue;
    }

    if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, hitbox_position, trace_mask)) {
      continue;
    }

    aimbot_point point{};
    point.valid = true;
    point.bone = hitbox_bone;
    point.hitbox = hitbox_id;
    point.studio_hitbox = studio_hitbox_id;
    point.priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox_id);
    point.position = hitbox_position;
    point.angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), hitbox_position);
    point.fov = aimbot_calculate_fov(point.angles, original_view_angles);

    if (!best_point.valid ||
        point.priority < best_point.priority ||
        (point.priority == best_point.priority && point.fov < best_point.fov)) {
      best_point = point;
    }
  }

  if (best_point.valid) {
    return best_point;
  }

  const int fallback_hitbox = aimbot_fallback_hitbox_for_mask(localplayer, target, weapon, hitbox_mask);
  if (fallback_hitbox < 0) {
    return {};
  }

  int fallback_bone = 0;
  Vec3 fallback_position{};
  const int fallback_studio_hitbox = aimbot_base_hitbox_to_studio(target, fallback_hitbox);
  if (fallback_studio_hitbox < 0 ||
      !aimbot_get_hitbox_center(target, fallback_studio_hitbox, &fallback_position, &fallback_bone) ||
      !aimbot_vec3_is_finite(fallback_position)) {
    fallback_bone = fallback_hitbox == aim_hitbox_head
      ? target->get_head_bone()
      : aimbot_default_bone(localplayer, target, weapon);
    if (!aimbot_get_bone_position(target, fallback_bone, &fallback_position)) {
      return {};
    }
  }
  if (!aimbot_vec3_is_finite(fallback_position)) {
    return {};
  }

  if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, fallback_position, trace_mask)) {
    return {};
  }

  best_point.valid = true;
  best_point.bone = fallback_bone;
  best_point.hitbox = fallback_hitbox;
  best_point.studio_hitbox = fallback_studio_hitbox;
  best_point.priority = aimbot_hitbox_priority(localplayer, target, weapon, fallback_hitbox);
  best_point.position = fallback_position;
  best_point.angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), fallback_position);
  best_point.fov = aimbot_calculate_fov(best_point.angles, original_view_angles);
  return best_point;
}

inline bool aimbot_segment_aabb_enter_fraction(const Vec3& start,
  const Vec3& end,
  const Vec3& mins,
  const Vec3& maxs,
  float* enter_fraction_out = nullptr) {
  Vec3 delta = end - start;
  float enter = 0.0f;
  float exit = 1.0f;

  const auto clip_axis = [&](float start_axis, float delta_axis, float min_axis, float max_axis) -> bool {
    if (std::fabs(delta_axis) <= 0.0001f) {
      return start_axis >= min_axis && start_axis <= max_axis;
    }

    float inv_delta = 1.0f / delta_axis;
    float t1 = (min_axis - start_axis) * inv_delta;
    float t2 = (max_axis - start_axis) * inv_delta;
    if (t1 > t2) {
      std::swap(t1, t2);
    }

    enter = std::max(enter, t1);
    exit = std::min(exit, t2);
    return enter <= exit;
  };

  const bool intersects =
    clip_axis(start.x, delta.x, mins.x, maxs.x) &&
    clip_axis(start.y, delta.y, mins.y, maxs.y) &&
    clip_axis(start.z, delta.z, mins.z, maxs.z);
  if (intersects && enter_fraction_out != nullptr) {
    *enter_fraction_out = std::clamp(enter, 0.0f, 1.0f);
  }

  return intersects;
}

inline bool aimbot_segment_intersects_aabb(const Vec3& start,
  const Vec3& end,
  const Vec3& mins,
  const Vec3& maxs) {
  return aimbot_segment_aabb_enter_fraction(start, end, mins, maxs);
}

inline bool aimbot_is_repair_wrench(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Engi_t_Wrench:
  case Engi_t_WrenchR:
  case Engi_t_TheGunslinger:
  case Engi_t_TheSouthernHospitality:
  case Engi_t_GoldenWrench:
  case Engi_t_TheJag:
  case Engi_t_TheEurekaEffect:
  case Engi_t_FestiveWrench:
  case Engi_t_SilverBotkillerWrenchMkI:
  case Engi_t_GoldBotkillerWrenchMkI:
  case Engi_t_RustBotkillerWrenchMkI:
  case Engi_t_BloodBotkillerWrenchMkI:
  case Engi_t_CarbonadoBotkillerWrenchMkI:
  case Engi_t_DiamondBotkillerWrenchMkI:
  case Engi_t_SilverBotkillerWrenchMkII:
  case Engi_t_GoldBotkillerWrenchMkII:
    return true;
  default:
    return false;
  }
}

inline bool aimbot_is_sword_melee(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Demoman_t_TheEyelander:
  case Demoman_t_TheScotsmansSkullcutter:
  case Demoman_t_HorselessHeadlessHorsemannsHeadtaker:
  case Demoman_t_TheClaidheamhMor:
  case Demoman_t_TheHalfZatoichi:
  case Demoman_t_ThePersianPersuader:
  case Demoman_t_NessiesNineIron:
  case Demoman_t_FestiveEyelander:
    return true;
  default:
    return false;
  }
}

inline float aimbot_get_base_melee_range(Player* localplayer, Weapon* weapon) {
  if (localplayer != nullptr && localplayer->in_cond(TF_COND_SHIELD_CHARGE)) {
    return 128.0f;
  }

  return aimbot_is_sword_melee(weapon) ? 72.0f : 48.0f;
}

struct aimbot_melee_swing_geometry {
  float range = 0.0f;
  Vec3 hull_mins{};
  Vec3 hull_maxs{};
};

inline aimbot_melee_swing_geometry aimbot_get_melee_swing_geometry(Player* localplayer, Weapon* weapon) {
  aimbot_melee_swing_geometry geometry{};
  if (weapon == nullptr) {
    return geometry;
  }

  float melee_range = aimbot_get_base_melee_range(localplayer, weapon);
  float melee_hull = 18.0f;

  if (attribute_manager != nullptr) {
    melee_hull = attribute_manager->attrib_hook_value(melee_hull, "melee_bounds_multiplier", weapon->to_entity());
    melee_range = attribute_manager->attrib_hook_value(melee_range, "melee_range_multiplier", weapon->to_entity());
  }

  if (localplayer != nullptr && localplayer->get_model_scale() > 1.0f) {
    const float model_scale = localplayer->get_model_scale();
    melee_range *= model_scale;
    melee_hull *= model_scale;
  }

  if (!std::isfinite(melee_range) || melee_range <= 0.0f ||
      !std::isfinite(melee_hull) || melee_hull < 0.0f) {
    return geometry;
  }

  geometry.range = melee_range;
  geometry.hull_mins = Vec3{-melee_hull, -melee_hull, -melee_hull};
  geometry.hull_maxs = Vec3{melee_hull, melee_hull, melee_hull};
  return geometry;
}

inline float aimbot_get_melee_range(Player* localplayer, Weapon* weapon) {
  return aimbot_get_melee_swing_geometry(localplayer, weapon).range;
}

inline float aimbot_get_melee_hull(Player* localplayer, Weapon* weapon) {
  const aimbot_melee_swing_geometry geometry = aimbot_get_melee_swing_geometry(localplayer, weapon);
  return std::max(
    std::fabs(geometry.hull_mins.x),
    std::max(std::fabs(geometry.hull_mins.y), std::fabs(geometry.hull_mins.z)));
}

inline bool aimbot_trace_melee_swing(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  const Vec3& start,
  const Vec3& aim_angles,
  trace_t* trace_out = nullptr,
  float* hit_distance_out = nullptr) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || engine_trace == nullptr) {
    return false;
  }

  if (trace_out != nullptr) {
    *trace_out = {};
  }
  if (hit_distance_out != nullptr) {
    *hit_distance_out = FLT_MAX;
  }

  const aimbot_melee_swing_geometry geometry = aimbot_get_melee_swing_geometry(localplayer, weapon);
  if (geometry.range <= 0.0f) {
    return false;
  }

  Vec3 forward{};
  angle_vectors(aim_angles, &forward, nullptr, nullptr);
  if (!aimbot_vec3_is_finite(start) || !aimbot_vec3_is_finite(forward)) {
    return false;
  }

  const Vec3 end = start + (forward * geometry.range);
  const auto run_trace = [&](const Vec3* hull_mins, const Vec3* hull_maxs, trace_t* out) {
    Vec3 trace_start = start;
    Vec3 trace_end = end;
    ray_t ray = hull_mins != nullptr && hull_maxs != nullptr
      ? engine_trace->init_ray(&trace_start, &trace_end, const_cast<Vec3*>(hull_mins), const_cast<Vec3*>(hull_maxs))
      : engine_trace->init_ray(&trace_start, &trace_end);
    trace_filter filter{};
    engine_trace->init_melee_trace_filter(&filter, localplayer, target);
    engine_trace->trace_ray(&ray, MASK_SOLID, &filter, out);
    return true;
  };

  trace_t line_trace{};
  if (!run_trace(nullptr, nullptr, &line_trace)) {
    return false;
  }
  if (!line_trace.all_solid && !line_trace.start_solid && line_trace.entity == target) {
    if (trace_out != nullptr) {
      *trace_out = line_trace;
    }
    if (hit_distance_out != nullptr) {
      *hit_distance_out = std::clamp(line_trace.fraction, 0.0f, 1.0f) * geometry.range;
    }
    return true;
  }

  if (line_trace.all_solid || line_trace.start_solid || line_trace.fraction < 1.0f) {
    return false;
  }

  trace_t hull_trace{};
  if (!run_trace(&geometry.hull_mins, &geometry.hull_maxs, &hull_trace)) {
    return false;
  }
  if (hull_trace.all_solid || hull_trace.start_solid || hull_trace.entity != target) {
    return false;
  }

  if (trace_out != nullptr) {
    *trace_out = hull_trace;
  }
  if (hit_distance_out != nullptr) {
    *hit_distance_out = std::clamp(hull_trace.fraction, 0.0f, 1.0f) * geometry.range;
  }
  return true;
}

inline bool aimbot_is_friendlyfire_enabled() {
  static Convar* friendlyfire = convar_system->find_var("mp_friendlyfire");
  return friendlyfire != nullptr && friendlyfire->get_int() != 0;
}

inline bool aimbot_aim_at_enabled(uint32_t flag) {
  return (config.aimbot.aim_at & flag) != 0;
}

inline uint32_t aimbot_target_flag_for_entity(Entity* entity) {
  if (entity == nullptr) {
    return 0;
  }

  switch (entity->get_class_id()) {
  case class_id::SENTRY: return Aim::aim_at_sentries;
  case class_id::DISPENSER:
  case class_id::OBJECT_CART_DISPENSER: return Aim::aim_at_dispensers;
  case class_id::TELEPORTER: return Aim::aim_at_teleporters;
  default: break;
  }

  if (std::find(entity_cache_npcs().begin(), entity_cache_npcs().end(), entity) != entity_cache_npcs().end()) {
    return Aim::aim_at_npcs;
  }
  if (entity->is_network_class("CTFPumpkinBomb")) {
    return Aim::aim_at_bombs;
  }
  if (entity->get_class_id() == class_id::PILL_OR_STICKY) {
    return Aim::aim_at_stickies;
  }
  return 0;
}

inline bool aimbot_has_mvm_bot_model(Player* player) {
  if (player == nullptr) {
    return false;
  }

  const char* model_name = player->get_model_name();
  return strstr(model_name, "models/bots/") != nullptr;
}

inline bool aimbot_is_fake_player(Player* player) {
  if (player == nullptr || engine == nullptr) {
    return false;
  }

  player_info info{};
  return engine->get_player_info(player->get_index(), &info) && info.fakeplayer;
}

inline bool aimbot_should_target_player_type(Player* player) {
  if (aimbot_has_mvm_bot_model(player)) {
    return aimbot_aim_at_enabled(Aim::aim_at_mvm_robots);
  }

  if (aimbot_is_fake_player(player)) {
    return aimbot_aim_at_enabled(Aim::aim_at_enemies) ||
      aimbot_aim_at_enabled(Aim::aim_at_mvm_robots);
  }

  return aimbot_aim_at_enabled(Aim::aim_at_enemies);
}

inline bool aimbot_should_target_player_type(const entity_cache_player_entry& entry) {
  if (aimbot_has_mvm_bot_model(entry.player)) {
    return aimbot_aim_at_enabled(Aim::aim_at_mvm_robots);
  }

  if (entry.player_info_valid && entry.fakeplayer) {
    return aimbot_aim_at_enabled(Aim::aim_at_enemies) ||
      aimbot_aim_at_enabled(Aim::aim_at_mvm_robots);
  }

  if (!entry.player_info_valid) {
    return aimbot_should_target_player_type(entry.player);
  }

  return aimbot_aim_at_enabled(Aim::aim_at_enemies);
}

inline bool aimbot_ignore_enabled(uint32_t flag) {
  return (config.aimbot.ignore & flag) != 0;
}

inline bool aimbot_player_is_preferred(Player* player) {
  if (player == nullptr) {
    return false;
  }

  if (aimbot::has_preference(player)) {
    return true;
  }

  player_info info{};
  if (engine != nullptr && engine->get_player_info(player->get_index(), &info) &&
      info.friends_id != 0 && cathook::core::players::is_prioritized(static_cast<std::uint32_t>(info.friends_id))) {
    return true;
  }

  return (config.aimbot.hitscan_modifiers & Aim::hitscan_mod_prefer_medics) != 0 &&
    player->get_tf_class() == tf_class::MEDIC;
}

enum class aimbot_player_skip_reason {
  none,
  invalid,
  local,
  dormant,
  dead,
  invulnerable,
  ignored,
  friend_state,
  ipc_bot,
  cloaked,
  team,
  type
};

inline bool aimbot_should_extinguish_team(Player* localplayer, Player* player, Weapon* weapon) {
  return localplayer != nullptr && player != nullptr && weapon != nullptr &&
    player->get_team() == localplayer->get_team() &&
    !aimbot_is_friendlyfire_enabled() &&
    (config.aimbot.hitscan_modifiers & Aim::hitscan_mod_extinguish_team) != 0 &&
    player->in_cond(TF_COND_BURNING) && attribute_manager != nullptr &&
    attribute_manager->attrib_hook_value(0, "jarate_duration", weapon->to_entity()) > 0.0f;
}

inline aimbot_player_skip_reason aimbot_player_skip_reason_for(
  Player* localplayer, Player* player, Weapon* weapon = nullptr) {
  if (localplayer == nullptr || player == nullptr) return aimbot_player_skip_reason::invalid;
  if (player == localplayer) return aimbot_player_skip_reason::local;
  if (player->is_dormant()) return aimbot_player_skip_reason::dormant;
  if (!player->is_alive()) return aimbot_player_skip_reason::dead;
  if (aimbot_ignore_enabled(Aim::ignore_invulnerable) && player->is_invulnerable()) return aimbot_player_skip_reason::invulnerable;
  if (player->is_ignored()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_party) && player->is_party()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_friends) && player->is_friend() && !player->is_party()) return aimbot_player_skip_reason::friend_state;
  if (aimbot_ignore_enabled(Aim::ignore_unprioritized) && !aimbot_player_is_preferred(player)) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_cloaked) && player->is_cloaked()) {
    return aimbot_player_skip_reason::cloaked;
  }
  if (aimbot_ignore_enabled(Aim::ignore_invisible_players) &&
      player->get_invisibility() >= std::clamp(config.aimbot.ignore_invisible / 100.0f, 0.0f, 1.0f)) {
    return aimbot_player_skip_reason::ignored;
  }
  if (aimbot_ignore_enabled(Aim::ignore_dead_ringer) && player->is_dead_ringer_active()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_vaccinator) && player->is_vaccinator_resistant()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_disguised) && player->is_disguised()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_taunting) && player->is_taunting()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_sentry_busters) && player->is_sentry_buster()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_unsimulated) && global_vars != nullptr &&
      config.aimbot.ignore_unsimulated_ticks > 0 && global_vars->interval_per_tick > 0.0f &&
      global_vars->curtime - player->get_simulation_time() >
        global_vars->interval_per_tick * static_cast<float>(config.aimbot.ignore_unsimulated_ticks)) {
    return aimbot_player_skip_reason::ignored;
  }
  player_info pinfo{};
  if (aimbot_ignore_enabled(Aim::ignore_ipc_bots) &&
      engine != nullptr &&
      engine->get_player_info(player->get_index(), &pinfo) &&
      pinfo.friends_id != 0 &&
      pinfo.fakeplayer != true &&
      cat_ipc::client::is_local_ipc_friend(static_cast<std::uint32_t>(pinfo.friends_id))) {
    return aimbot_player_skip_reason::ipc_bot;
  }
  if (player->get_team() == localplayer->get_team() &&
      (!aimbot_is_friendlyfire_enabled() || aimbot_ignore_enabled(Aim::ignore_team)) &&
      !aimbot_should_extinguish_team(localplayer, player, weapon)) return aimbot_player_skip_reason::team;
  if (!aimbot_should_target_player_type(player)) return aimbot_player_skip_reason::type;
  return aimbot_player_skip_reason::none;
}

inline aimbot_player_skip_reason aimbot_player_skip_reason_for(
  Player* localplayer, const entity_cache_player_entry& entry, Weapon* weapon = nullptr) {
  Player* player = entry.player;
  if (localplayer == nullptr || player == nullptr) return aimbot_player_skip_reason::invalid;
  if (player == localplayer) return aimbot_player_skip_reason::local;
  if (entry.dormant) return aimbot_player_skip_reason::dormant;
  if (!entry.alive) return aimbot_player_skip_reason::dead;
  if (aimbot_ignore_enabled(Aim::ignore_invulnerable) && player->is_invulnerable()) return aimbot_player_skip_reason::invulnerable;
  if (entry.ignored) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_party) && player->is_party()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_friends) && entry.friendly && !player->is_party()) return aimbot_player_skip_reason::friend_state;
  if (aimbot_ignore_enabled(Aim::ignore_unprioritized) && !aimbot_player_is_preferred(player)) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_cloaked) && player->is_cloaked()) {
    return aimbot_player_skip_reason::cloaked;
  }
  if (aimbot_ignore_enabled(Aim::ignore_invisible_players) &&
      player->get_invisibility() >= std::clamp(config.aimbot.ignore_invisible / 100.0f, 0.0f, 1.0f)) {
    return aimbot_player_skip_reason::ignored;
  }
  if (aimbot_ignore_enabled(Aim::ignore_dead_ringer) && player->is_dead_ringer_active()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_vaccinator) && player->is_vaccinator_resistant()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_disguised) && player->is_disguised()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_taunting) && player->is_taunting()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_sentry_busters) && player->is_sentry_buster()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_unsimulated) && global_vars != nullptr &&
      config.aimbot.ignore_unsimulated_ticks > 0 &&
      global_vars->interval_per_tick > 0.0f &&
      global_vars->curtime - entry.simulation_time >
        global_vars->interval_per_tick * static_cast<float>(config.aimbot.ignore_unsimulated_ticks)) {
    return aimbot_player_skip_reason::ignored;
  }
  if (aimbot_ignore_enabled(Aim::ignore_ipc_bots)) {
    if (entry.player_info_valid && entry.friends_id != 0 && !entry.fakeplayer) {
      if (cat_ipc::client::is_local_ipc_friend(static_cast<std::uint32_t>(entry.friends_id))) {
        return aimbot_player_skip_reason::ipc_bot;
      }
    } else {
      player_info pinfo{};
      if (engine != nullptr &&
          engine->get_player_info(player->get_index(), &pinfo) &&
          pinfo.friends_id != 0 &&
          pinfo.fakeplayer != true &&
          cat_ipc::client::is_local_ipc_friend(static_cast<std::uint32_t>(pinfo.friends_id))) {
        return aimbot_player_skip_reason::ipc_bot;
      }
    }
  }
  if (entry.team == localplayer->get_team() &&
      (!aimbot_is_friendlyfire_enabled() || aimbot_ignore_enabled(Aim::ignore_team)) &&
      !aimbot_should_extinguish_team(localplayer, player, weapon)) return aimbot_player_skip_reason::team;
  if (!aimbot_should_target_player_type(entry)) return aimbot_player_skip_reason::type;
  return aimbot_player_skip_reason::none;
}

inline bool aimbot_should_skip_player(Player* localplayer, Player* player) {
  return aimbot_player_skip_reason_for(localplayer, player) != aimbot_player_skip_reason::none;
}

inline bool aimbot_entity_is_enemy_owned(Player* localplayer, Entity* entity) {
  if (localplayer == nullptr || entity == nullptr) {
    return false;
  }

  Entity* owner = entity->get_owner_entity();
  if (owner != nullptr) {
    if (owner == localplayer || owner->get_team() == localplayer->get_team()) {
      return false;
    }

    return true;
  }

  const tf_team target_team = entity->get_team();
  return target_team == tf_team::UNKNOWN ||
         target_team == tf_team::SPECTATOR ||
         target_team != localplayer->get_team();
}

inline bool aimbot_should_skip_non_player_target(Player* localplayer, Entity* entity) {
  if (localplayer == nullptr || entity == nullptr || entity == localplayer) {
    return true;
  }

  if (entity->is_dormant()) {
    return true;
  }

  if (entity->is_building()) {
    if (!aimbot_aim_at_enabled(aimbot_target_flag_for_entity(entity))) {
      return true;
    }

    auto* building = static_cast<Building*>(entity);
    return building->is_carried() ||
           building->get_health() <= 0 ||
           !aimbot_entity_is_enemy_owned(localplayer, entity);
  }

  const uint32_t target_flag = aimbot_target_flag_for_entity(entity);
  return target_flag == 0 || !aimbot_aim_at_enabled(target_flag) ||
    !aimbot_entity_is_enemy_owned(localplayer, entity);
}

inline Vec3 aimbot_entity_target_position(Entity* entity) {
  if (entity == nullptr) {
    return {};
  }

  const Vec3 mins = entity->get_collideable_mins();
  const Vec3 maxs = entity->get_collideable_maxs();
  const Vec3 center_offset = (mins + maxs) * 0.5f;
  return entity->get_collision_origin() + center_offset;
}

inline int aimbot_entity_health(Entity* entity) {
  if (entity == nullptr) {
    return 0;
  }

  if (entity->get_class_id() == class_id::PLAYER) {
    return static_cast<Player*>(entity)->get_health();
  }

  if (entity->is_building()) {
    return static_cast<Building*>(entity)->get_health();
  }

  return 1;
}

inline bool aimbot_entity_melee_reachable(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  const Vec3& aim_angles) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  return aimbot_trace_melee_swing(localplayer, weapon, target, localplayer->get_shoot_pos(), aim_angles);
}

inline aimbot_candidate aimbot_find_non_player_candidate(Player* localplayer,
  Weapon* weapon,
  Entity* entity,
  const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (weapon == nullptr || aimbot_should_skip_non_player_target(localplayer, entity)) {
    return candidate;
  }

  const Vec3 target_position = aimbot_entity_target_position(entity);
  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  const Vec3 aim_angles = aimbot_calculate_angles_to_position(shoot_pos, target_position);
  if (weapon->is_melee() && !aimbot_entity_melee_reachable(localplayer, weapon, entity, aim_angles)) {
    return candidate;
  }

  candidate.entity = entity;
  candidate.aim_position = target_position;
  candidate.aim_angles = aim_angles;
  candidate.fov = aimbot_calculate_fov(aim_angles, original_view_angles);
  candidate.distance = distance_3d(localplayer->get_origin(), entity->get_origin());
  candidate.health = aimbot_entity_health(entity);
  candidate.visible = aimbot_trace_visible_to_position(localplayer, entity, target_position);
  return candidate;
}

inline bool aimbot_candidate_visible_shootable(Player* localplayer, const aimbot_candidate& candidate) {
  return localplayer != nullptr &&
         candidate.entity != nullptr &&
         candidate.visible &&
         localplayer->can_shoot(candidate.entity);
}

inline bool aimbot_weapon_requires_scope(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  if (weapon->get_def_id() == Sniper_m_TheMachina) {
    return true;
  }

  return attribute_manager != nullptr &&
    attribute_manager->attrib_hook_value(0, "sniper_only_fire_zoomed", weapon->to_entity()) != 0;
}

inline bool aimbot_modifier_enabled(uint32_t flag) {
  return (config.aimbot.hitscan_modifiers & flag) != 0;
}

inline bool aimbot_sniper_charge_full(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  const float charge = weapon->get_charged_damage();

  return std::isfinite(charge) && charge >= 150.0f;
}

inline bool aimbot_sniper_headshot_ready(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr) {
    return false;
  }

  return !weapon->is_sniper_rifle() || aimbot_sniper_scope_time_ready(localplayer);
}

inline bool aimbot_wait_for_headshot_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_headshot) ||
      localplayer == nullptr || weapon == nullptr || candidate.player == nullptr ||
      !weapon->is_headshot_weapon()) {
    return true;
  }

  if (candidate.hitbox >= 0 && candidate.hitbox != aim_hitbox_head) {
    return candidate.player != nullptr &&
      aimbot_body_aim_lethal(localplayer, weapon, candidate.player);
  }

  switch (weapon->get_weapon_id()) {
  case TF_WEAPON_SNIPERRIFLE:
  case TF_WEAPON_SNIPERRIFLE_DECAP:
    return aimbot_sniper_scope_confirmed(localplayer) &&
      aimbot_sniper_headshot_ready(localplayer, weapon);
  case TF_WEAPON_SNIPERRIFLE_CLASSIC:
    return aimbot_sniper_headshot_ready(localplayer, weapon);
  case TF_WEAPON_REVOLVER:
    return attribute_manager == nullptr ||
      attribute_manager->attrib_hook_value(0, "set_weapon_mode", weapon->to_entity()) != 1 ||
      weapon->can_ambassador_headshot();
  default:
    return true;
  }
}

inline float aimbot_sniper_estimated_charge(Weapon* weapon) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  const float charge = weapon->get_charged_damage();
  return std::isfinite(charge) ? std::max(charge, 50.0f) : 50.0f;
}

inline int aimbot_sniper_estimated_damage(Player* localplayer,
  Weapon* weapon,
  Player* target,
  bool headshot) {
  if (weapon == nullptr || target == nullptr) {
    return 0;
  }

  const float base_damage = aimbot_sniper_estimated_charge(weapon);
  float multiplier = headshot ? 3.0f : 1.0f;
  if (localplayer != nullptr && localplayer->is_crit_boosted()) {
    multiplier = 3.0f;
  } else if (target->in_cond(TF_COND_URINE) || target->in_cond(TF_COND_MARKEDFORDEATH)) {
    multiplier = 1.36f;
  }

  if (attribute_manager != nullptr) {
    multiplier = attribute_manager->attrib_hook_value(multiplier, "mult_dmg", weapon->to_entity());
    if (!headshot) {
      multiplier = attribute_manager->attrib_hook_value(multiplier, "bodyshot_damage_modify", weapon->to_entity());
    }
    if (aimbot_sniper_charge_full(weapon)) {
      multiplier = attribute_manager->attrib_hook_value(
        multiplier,
        "sniper_full_charge_damage_bonus",
        weapon->to_entity());
    }
  }

  return static_cast<int>(std::ceil(base_damage * multiplier));
}

inline bool aimbot_revolver_lethal_body(Player* localplayer, Weapon* weapon, Player* target) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  const Vec3 origin_delta = target->get_origin() - localplayer->get_origin();
  const float dist = std::sqrt(
    (origin_delta.x * origin_delta.x) +
    (origin_delta.y * origin_delta.y) +
    (origin_delta.z * origin_delta.z));
  const float ramp = std::clamp((900.0f - dist) / (900.0f - 90.0f), 0.0f, 1.0f);
  const float base_damage = std::lerp(21.0f, 60.0f, ramp);
  float multiplier = 1.0f;
  if (attribute_manager != nullptr) {
    multiplier = attribute_manager->attrib_hook_value(multiplier, "mult_dmg", weapon->to_entity());
  }

  const int dmg = static_cast<int>(std::ceil(base_damage * multiplier));
  return target->get_health() <= dmg;
}

inline bool aimbot_body_aim_lethal(Player* localplayer, Weapon* weapon, Player* target) {
  if (!aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal) ||
      localplayer == nullptr ||
      weapon == nullptr ||
      target == nullptr ||
      !weapon->is_headshot_weapon()) {
    return false;
  }

  if (weapon->is_sniper_rifle()) {
    const int bodyshot_damage = aimbot_sniper_estimated_damage(localplayer, weapon, target, false);
    return target->get_health() > 0 && target->get_health() <= bodyshot_damage;
  }

  if (weapon->get_weapon_id() == TF_WEAPON_REVOLVER &&
      attribute_manager != nullptr &&
      attribute_manager->attrib_hook_value(0, "set_weapon_mode", weapon->to_entity()) == 1) {
    return aimbot_revolver_lethal_body(localplayer, weapon, target);
  }

  return false;
}

inline bool aimbot_wait_for_charge_ready(Player* localplayer,
  Weapon* weapon,
  const aimbot_candidate& candidate) {
  if (!aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_charge) ||
      localplayer == nullptr ||
      weapon == nullptr ||
      candidate.entity == nullptr) {
    return true;
  }

  if (!weapon->is_sniper_rifle()) {
    return true;
  }

  const float charge = weapon->get_charged_damage();
  if (!localplayer->is_scoped() || aimbot_sniper_charge_full(weapon)) {
    return true;
  }

  if (candidate.player != nullptr) {
    const bool headshot_hitbox = candidate.hitbox == aim_hitbox_head &&
      (weapon->get_weapon_id() != TF_WEAPON_SNIPERRIFLE_CLASSIC || aimbot_sniper_charge_full(weapon));
    const int dmg = aimbot_sniper_estimated_damage(localplayer, weapon, candidate.player, headshot_hitbox);
    if (candidate.player->get_health() > 0 && candidate.player->get_health() <= dmg &&
        (!headshot_hitbox || aimbot_sniper_headshot_ready(localplayer, weapon) || localplayer->is_crit_boosted())) {
      return true;
    }
    return false;
  }

  const int damage = static_cast<int>(std::ceil(std::max(std::isfinite(charge) ? charge : 0.0f, 50.0f)));
  const int health = aimbot_entity_health(candidate.entity);
  return health <= 0 || health <= damage;
}

inline bool aimbot_is_projectile_weapon(Weapon* weapon) {
  if (weapon == nullptr) return false;
  if (weapon->is_flamethrower()) return true;

  const int weapon_id = weapon->get_weapon_id();
  if (weapon_id == TF_WEAPON_THROWABLE || weapon_id == TF_WEAPON_GRENADE_THROWABLE) {
    return true;
  }

  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheDirectHit:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
  case Soldier_s_TheRighteousBison:
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
  case Engi_m_TheRescueRanger:
  case Engi_m_ThePomson6000:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheManmelter:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
  case Pyro_m_DragonsFury:
  case Pyro_s_GasPasser:
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Sniper_s_TheSelfAwareBeautyMark:
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

      return weapon->get_projectile_type() > 1;
  }
}

inline bool aimbot_is_melee_weapon(Weapon* weapon) {
  return weapon != nullptr && weapon->is_melee();
}

inline float aimbot_effective_fov(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.fov * 0.2f : candidate.fov;
}

inline bool aimbot_fov_unlimited() {
  return config.aimbot.fov <= 0.0f;
}

inline float aimbot_fov_limit(float scale = 1.0f, float minimum = 0.0f, float extra = 0.0f) {
  if (aimbot_fov_unlimited()) {
    return FLT_MAX;
  }

  return std::max(config.aimbot.fov * scale, minimum) + extra;
}

inline bool aimbot_fov_exceeds_limit(float fov,
  float scale = 1.0f,
  float minimum = 0.0f,
  float extra = 0.0f) {

  if (aimbot_fov_unlimited()) {
    return false;
  }

  return !std::isfinite(fov) || fov > aimbot_fov_limit(scale, minimum, extra);
}

inline bool aimbot_fov_within_limit(float fov,
  float scale = 1.0f,
  float minimum = 0.0f,
  float extra = 0.0f) {
  return !aimbot_fov_exceeds_limit(fov, scale, minimum, extra);
}

inline float aimbot_effective_distance(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.distance * 0.35f : candidate.distance;
}

inline int aimbot_effective_smallest_health(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.health - 500 : candidate.health;
}

inline int aimbot_effective_largest_health(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.health + 500 : candidate.health;
}

inline bool aimbot_candidate_better(const aimbot_candidate& candidate, const aimbot_candidate& best) {
  if (candidate.entity == nullptr) return false;
  if (best.entity == nullptr) return true;

  switch (config.aimbot.target_type) {
  case Aim::TargetType::FOV:
    return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
  case Aim::TargetType::DISTANCE:
    if (aimbot_effective_distance(candidate) == aimbot_effective_distance(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_distance(candidate) < aimbot_effective_distance(best);
  case Aim::TargetType::LEAST_HEALTH:
    if (aimbot_effective_smallest_health(candidate) == aimbot_effective_smallest_health(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_smallest_health(candidate) < aimbot_effective_smallest_health(best);
  case Aim::TargetType::MOST_HEALTH:
    if (aimbot_effective_largest_health(candidate) == aimbot_effective_largest_health(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_largest_health(candidate) > aimbot_effective_largest_health(best);
  default:
    return false;
  }
}

inline float aimbot_candidate_target_speed(const aimbot_candidate& candidate) {
  if (candidate.player == nullptr) {
    return 0.0f;
  }

  const Vec3 target_velocity = candidate.player->get_velocity();
  return std::hypot(target_velocity.x, target_velocity.y) + (std::fabs(target_velocity.z) * 0.35f);
}

inline float aimbot_candidate_motion_scale(const aimbot_candidate& candidate) {
  const float target_speed = aimbot_candidate_target_speed(candidate);
  const float speed_ratio = std::clamp(target_speed / 320.0f, 0.0f, 1.75f);
  float motion_scale = 0.82f + (speed_ratio * 0.34f);

  return std::clamp(motion_scale, 0.75f, 1.65f);
}

inline bool aimbot_mode_uses_visible_steering() {
  return config.aimbot.aim_mode == Aim::AimMode::SMOOTH ||
         config.aimbot.aim_mode == Aim::AimMode::ASSISTIVE;
}

inline float aimbot_assist_strength(const Vec3& original_view_angles,
  const Vec3& target_view_angles,
  float motion_scale = 1.0f) {
  const float assist_strength = std::clamp(config.aimbot.assist_strength / 100.0f, 0.0f, 1.0f);
  if (assist_strength <= 0.0f) {
    return 0.0f;
  }

  const float aim_fov = std::max(config.aimbot.fov, 1.0f);
  const float fov_ratio = std::clamp(
    aimbot_calculate_fov(target_view_angles, original_view_angles) / aim_fov,
    0.0f,
    1.0f);
  const float close_ratio = 1.0f - fov_ratio;
  const float curve = 1.0f - (1.0f - 1.0f) * close_ratio;

  (void)motion_scale;
  return std::clamp(assist_strength * std::clamp(curve, 0.05f, 1.0f), 0.0f, 1.0f);
}

inline Vec3 aimbot_lerp_angles(const Vec3& source_angles, const Vec3& target_angles, float amount) {
  amount = std::clamp(amount, 0.0f, 1.0f);
  const Vec3 delta = aimbot_normalize_angle_delta(target_angles, source_angles);
  return aimbot_clamp_angles(Vec3{
    source_angles.x + (delta.x * amount),
    source_angles.y + (delta.y * amount),
    0.0f
  });
}

inline Vec3 aimbot_apply_smooth_angles(const Vec3& source_view_angles,
  const Vec3& target_view_angles,
  float motion_scale = 1.0f) {
  const float strength = aimbot_assist_strength(source_view_angles, target_view_angles, motion_scale);
  if (strength >= 1.0f) {
    return aimbot_clamp_angles(target_view_angles);
  }

  return aimbot_lerp_angles(source_view_angles, target_view_angles, strength);
}

inline Vec3 aimbot_apply_assistive_angles(const Vec3& source_view_angles,
  const Vec3& target_view_angles,
  const Vec3& last_input_angles,
  const bool has_last_input_angles,
  float motion_scale = 1.0f) {
  const float strength = aimbot_assist_strength(source_view_angles, target_view_angles, motion_scale);
  if (strength <= 0.0f) {
    return source_view_angles;
  }

  if (!has_last_input_angles) {
    return aimbot_clamp_angles(source_view_angles);
  }

  const Vec3 mouse_delta = aimbot_normalize_angle_delta(source_view_angles, last_input_angles);
  const Vec3 target_delta = aimbot_normalize_angle_delta(target_view_angles, last_input_angles);
  const float mouse_delta_length_sq = (mouse_delta.x * mouse_delta.x) + (mouse_delta.y * mouse_delta.y);
  const float target_delta_length_sq = (target_delta.x * target_delta.x) + (target_delta.y * target_delta.y);

  Vec3 limited_target_delta{};
  if (target_delta_length_sq > 0.000001f && mouse_delta_length_sq > 0.000001f) {
    const float target_delta_length = std::sqrt(target_delta_length_sq);
    const float limited_length = std::sqrt(std::min(mouse_delta_length_sq, target_delta_length_sq));
    limited_target_delta = Vec3{
      target_delta.x * (limited_length / target_delta_length),
      target_delta.y * (limited_length / target_delta_length),
      0.0f
    };
  }
  const Vec3 blended_delta{
    mouse_delta.x + ((limited_target_delta.x - mouse_delta.x) * strength),
    mouse_delta.y + ((limited_target_delta.y - mouse_delta.y) * strength),
    0.0f
  };

  return aimbot_clamp_angles(Vec3{
    source_view_angles.x - mouse_delta.x + blended_delta.x,
    source_view_angles.y - mouse_delta.y + blended_delta.y,
    0.0f
  });
}

inline Vec3 aimbot_apply_mode_angles(const Vec3& source_view_angles,
  const Vec3& target_view_angles,
  const Vec3& last_input_angles,
  const bool has_last_input_angles,
  const aimbot_candidate& candidate) {
  const float motion_scale = aimbot_candidate_motion_scale(candidate);
  switch (config.aimbot.aim_mode) {
  case Aim::AimMode::SMOOTH:
    return aimbot_apply_smooth_angles(source_view_angles, target_view_angles, motion_scale);
  case Aim::AimMode::ASSISTIVE:
    return aimbot_apply_assistive_angles(
      source_view_angles,
      target_view_angles,
      last_input_angles,
      has_last_input_angles,
      motion_scale);
  default:
    return target_view_angles;
  }
}

inline bool aimbot_should_auto_rev(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if ((!config.aimbot.auto_rev && !aimbot_modifier_enabled(Aim::hitscan_mod_auto_rev)) ||
      localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::HEAVYWEAPONS || !weapon->is_minigun()) return false;
  if (!localplayer->is_heavy_revved() && !weapon->can_secondary_attack()) return false;
  if (!localplayer->is_heavy_revved() && !localplayer->is_on_ground()) return false;
  if (!aimbot_candidate_visible_shootable(localplayer, candidate) && candidate.distance <= config.aimbot.auto_rev_threshold) return false;
  return true;
}

inline bool aimbot_should_auto_unrev(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_unrev || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::HEAVYWEAPONS || !weapon->is_minigun()) return false;
  if (!localplayer->is_heavy_revved()) return false;
  if (aimbot_candidate_visible_shootable(localplayer, candidate)) return false;
  return candidate.distance <= config.aimbot.auto_rev_threshold;
}
#endif
