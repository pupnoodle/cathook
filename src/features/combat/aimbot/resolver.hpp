#ifndef AIMBOT_RESOLVER_HPP
#define AIMBOT_RESOLVER_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include "aim_utils.hpp"
#include "core/entity_cache.hpp"
#include "core/shared/sigs.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/net_channel.hpp"
#include "libsigscan/libsigscan.h"

namespace resolver
{

constexpr int max_entities = 65;
constexpr int max_records = 16;
constexpr int max_yaw_candidates = 24;
constexpr int max_pitch_candidates = 5;
constexpr int max_pose_parameters = 24;

constexpr std::uintptr_t anim_state_gait_yaw_offset = 100;
constexpr std::uintptr_t anim_state_eye_yaw_offset = 140;
constexpr std::uintptr_t anim_state_eye_yaw_offset_ctf = 60;
constexpr std::uintptr_t anim_state_eye_pitch_offset = 144;

struct resolver_settings {
  bool auto_brute = false;
  float yaw_step_degrees = 90.0f;
  float brute_latency_scale = 1.5f;
  float brute_grace_seconds = 0.1f;
  bool sniper_dot_pitch = true;
  bool pitch_fold_recovery = true;
  bool minwalk_breaker = true;
};

inline resolver_settings settings{};
inline std::unordered_map<int, float> g_userid_yaw_offsets{};

struct anim_state_snapshot {
  bool valid = false;
  float eye_yaw = 0.0f;
  float eye_pitch = 0.0f;
  float raw_eye_pitch = 0.0f;
  float gait_yaw = 0.0f;
  float velocity_yaw = 0.0f;
  float speed_2d = 0.0f;
  bool moving = false;
};

struct resolver_record {
  bool valid = false;
  float sim_time = 0.0f;
  Vec3 origin{};
  Vec3 velocity{};
  float eye_yaw = 0.0f;
  float gait_yaw = 0.0f;
  float velocity_yaw = 0.0f;
  bool moving = false;
};

struct player_history {
  int ent_index = 0;
  int record_count = 0;
  std::array<resolver_record, max_records> records{};
};

struct yaw_candidate {
  bool valid = false;
  float yaw = 0.0f;
  float penalty = 0.0f;
};

struct pitch_candidate {
  bool valid = false;
  float pitch = 0.0f;
  float penalty = 0.0f;
};

struct yaw_candidate_list {
  int count = 0;
  std::array<yaw_candidate, max_yaw_candidates> values{};
};

struct pitch_candidate_list {
  int count = 0;
  std::array<pitch_candidate, max_pitch_candidates> values{};
};

enum class resolver_mode {
  unknown,
  moving,
  standing,
  jitter,
  spin,
  fakewalk,
  sideways_fake
};

struct resolver_debug_info {
  bool active = false;
  int yaw_candidates = 0;
  int misses = 0;
  int hits = 0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  resolver_mode mode = resolver_mode::unknown;
};

struct pending_shot {
  bool active = false;
  float time = 0.0f;
  float expire_time = 0.0f;
  float sim_time = 0.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;
  int hitbox = -1;
  bool backtrack = false;
};

struct player_resolver_state {
  int ent_index = 0;
  int brute_yaw_index = 0;
  int brute_pitch_index = 0;
  int misses = 0;
  int hits = 0;
  int yaw_candidates = 0;
  float selected_yaw = 0.0f;
  float selected_pitch = 0.0f;
  float yaw_offset = 0.0f;
  bool inverse_pitch = false;
  bool minwalk = false;
  bool brute_armed = false;
  float brute_deadline = 0.0f;
  int user_id = 0;
  resolver_mode mode = resolver_mode::unknown;
  pending_shot shot{};
};

struct hitscan_pose_guard {
  Player* player = nullptr;
  Vec3 original_angles{};
  Vec3 resolved_angles{};
  bool active = false;

  hitscan_pose_guard() = default;
  hitscan_pose_guard(const hitscan_pose_guard&) = delete;
  hitscan_pose_guard& operator=(const hitscan_pose_guard&) = delete;

  ~hitscan_pose_guard() {
    restore();
  }

  void restore() {
    if (!active || player == nullptr) {
      return;
    }

    player->set_eye_angles(original_angles);
    aimbot_invalidate_bone_cache(player);
    active = false;
  }
};

inline std::array<player_history, max_entities> g_history{};
inline std::array<player_resolver_state, max_entities> g_resolver_state{};

[[nodiscard]] inline float normalize_yaw(float yaw)
{
  return std::remainder(yaw, 360.0f);
}

[[nodiscard]] inline float yaw_delta(float left, float right)
{
  return normalize_yaw(left - right);
}

[[nodiscard]] inline float clamp_pitch(float pitch)
{
  return std::clamp(pitch, -89.0f, 89.0f);
}

[[nodiscard]] inline bool finite_angle(float value)
{
  return std::isfinite(value) && value >= -720.0f && value <= 720.0f;
}

[[nodiscard]] inline float vector_yaw(const Vec3& value)
{
  return normalize_yaw(std::atan2(value.y, value.x) * radpi);
}

[[nodiscard]] inline float speed_2d(const Vec3& value)
{
  return std::sqrt((value.x * value.x) + (value.y * value.y));
}

constexpr int flow_outgoing = 0;

[[nodiscard]] inline float outgoing_latency_seconds()
{
  net_channel* channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  return channel != nullptr ? std::clamp(channel->get_latency(flow_outgoing), 0.0f, 10.0f) : 0.0f;
}

inline void apply_yaw_offset(player_resolver_state* state, float* yaw)
{
  if (state == nullptr || yaw == nullptr || !settings.auto_brute) {
    return;
  }
  *yaw = normalize_yaw(*yaw + state->yaw_offset);
}

inline void apply_pitch_flags(const player_resolver_state* state, float* pitch)
{
  if (state == nullptr || pitch == nullptr) {
    return;
  }
  if (state->inverse_pitch) {
    *pitch = clamp_pitch(-*pitch);
  }
}

[[nodiscard]] inline bool minwalk_active(Player* player)
{
  if (!settings.minwalk_breaker || player == nullptr || !player->is_alive() || !player->is_on_ground()) {
    return false;
  }

  const Vec3 velocity = player->get_velocity();
  const float horizontal_speed_sqr = (velocity.x * velocity.x) + (velocity.y * velocity.y);
  return std::isfinite(horizontal_speed_sqr) && horizontal_speed_sqr < 2.0f;
}

inline void sync_user_identity(Player* player, player_resolver_state* state)
{
  if (player == nullptr || state == nullptr || engine == nullptr) {
    return;
  }

  player_info pinfo{};
  if (!engine->get_player_info(player->get_index(), &pinfo) || pinfo.user_id <= 0) {
    return;
  }

  if (state->user_id != 0 && state->user_id != pinfo.user_id) {
    g_userid_yaw_offsets[state->user_id] = state->yaw_offset;
    const auto restored = g_userid_yaw_offsets.find(pinfo.user_id);
    state->yaw_offset = restored != g_userid_yaw_offsets.end() ? restored->second : 0.0f;
    state->brute_yaw_index = 0;
    state->brute_pitch_index = 0;
    state->misses = 0;
    state->hits = 0;
    state->inverse_pitch = false;
    state->brute_armed = false;
    state->shot = {};
  }
  state->user_id = pinfo.user_id;
}

[[nodiscard]] inline int player_anim_state_offset()
{
  static int offset = -1;
  if (offset >= 0) {
    return offset;
  }

  offset = 0;
  const auto* match = reinterpret_cast<const std::uint8_t*>(
    sigscan_module("client.so", sigs::ctf_player_anim_state_store));
  if (match == nullptr) {
    return offset;
  }

  std::int32_t store_offset = 0;
  std::memcpy(&store_offset, match + 13, sizeof(store_offset));
  if (store_offset >= 0x400 && store_offset <= 0x8000) {
    offset = store_offset;
  }

  return offset;
}

[[nodiscard]] inline void* player_anim_state(Player* player)
{
  const int offset = player_anim_state_offset();
  if (player == nullptr || offset <= 0) {
    return nullptr;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player);
  return *reinterpret_cast<void**>(base + static_cast<std::uintptr_t>(offset));
}

[[nodiscard]] inline bool read_anim_state_snapshot(Player* player, anim_state_snapshot* snapshot)
{
  if (snapshot == nullptr) {
    return false;
  }

  *snapshot = {};
  if (player == nullptr) {
    return false;
  }

  void* raw_state = player_anim_state(player);
  if (raw_state == nullptr) {
    return false;
  }

  const auto state = reinterpret_cast<std::uintptr_t>(raw_state);
  const auto owner_multi = *reinterpret_cast<void**>(state + 48);
  const auto owner_ctf = *reinterpret_cast<void**>(state + 304);
  if (owner_multi != player && owner_ctf != player) {
    return false;
  }

  Vec3 eye_angles = player->get_eye_angles();
  float eye_yaw = *reinterpret_cast<float*>(state + anim_state_eye_yaw_offset);
  const float ctf_eye_yaw = *reinterpret_cast<float*>(state + 60);
  if (!finite_angle(eye_yaw) && finite_angle(ctf_eye_yaw)) {
    eye_yaw = ctf_eye_yaw;
  }
  if (!finite_angle(eye_yaw)) {
    eye_yaw = eye_angles.y;
  }

  float eye_pitch = *reinterpret_cast<float*>(state + anim_state_eye_pitch_offset);
  if (!finite_angle(eye_pitch)) {
    eye_pitch = eye_angles.x;
  }

  float gait_yaw = *reinterpret_cast<float*>(state + anim_state_gait_yaw_offset);
  if (!finite_angle(gait_yaw)) {
    gait_yaw = eye_yaw;
  }

  const Vec3 velocity = player->get_velocity();
  const float movement_speed = speed_2d(velocity);

  snapshot->valid = true;
  snapshot->eye_yaw = normalize_yaw(eye_yaw);
  snapshot->eye_pitch = clamp_pitch(eye_pitch);
  snapshot->raw_eye_pitch = finite_angle(eye_angles.x) ? eye_angles.x : eye_pitch;
  snapshot->gait_yaw = normalize_yaw(gait_yaw);
  snapshot->velocity_yaw = movement_speed > 1.0f ? vector_yaw(velocity) : snapshot->gait_yaw;
  snapshot->speed_2d = movement_speed;
  snapshot->moving = movement_speed > 18.0f;
  return true;
}

[[nodiscard]] inline player_history* history_for_player(Player* player)
{
  if (player == nullptr) {
    return nullptr;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return nullptr;
  }

  return &g_history[ent_index];
}

[[nodiscard]] inline player_resolver_state* state_for_player(Player* player)
{
  if (player == nullptr) {
    return nullptr;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return nullptr;
  }

  player_resolver_state& state = g_resolver_state[ent_index];
  state.ent_index = ent_index;
  return &state;
}

[[nodiscard]] inline const player_resolver_state* state_for_player_const(Player* player)
{
  if (player == nullptr) {
    return nullptr;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return nullptr;
  }

  return &g_resolver_state[ent_index];
}

inline void clear_player(Player* player)
{
  if (player == nullptr) {
    return;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return;
  }

  player_resolver_state& state = g_resolver_state[ent_index];
  if (state.user_id > 0) {
    g_userid_yaw_offsets[state.user_id] = state.yaw_offset;
  }
  const bool keep_offsets = state.user_id > 0;
  const float saved_yaw_offset = state.yaw_offset;
  const int saved_user_id = state.user_id;
  g_history[ent_index] = {};
  g_resolver_state[ent_index] = {};
  if (keep_offsets) {
    g_resolver_state[ent_index].yaw_offset = saved_yaw_offset;
    g_resolver_state[ent_index].user_id = saved_user_id;
  }
}

inline void record_player(Player* player)
{
  if (!config.aimbot.resolver || player == nullptr || player->is_dormant() || !player->is_alive()) {
    clear_player(player);
    return;
  }

  player_resolver_state* state = state_for_player(player);
  if (state != nullptr) {
    sync_user_identity(player, state);
    state->minwalk = minwalk_active(player);
  }

  player_history* history = history_for_player(player);
  if (history == nullptr) {
    return;
  }

  anim_state_snapshot snapshot{};
  if (!read_anim_state_snapshot(player, &snapshot)) {
    return;
  }

  resolver_record record{};
  record.valid = true;
  record.sim_time = player->get_simulation_time();
  record.origin = player->get_origin();
  record.velocity = player->get_velocity();
  record.eye_yaw = snapshot.eye_yaw;
  record.gait_yaw = snapshot.gait_yaw;
  record.velocity_yaw = snapshot.velocity_yaw;
  record.moving = snapshot.moving;

  if (!std::isfinite(record.sim_time) || record.sim_time <= 0.0f) {
    return;
  }

  if (history->record_count > 0 && history->records[0].valid &&
      record.sim_time + 0.0001f < history->records[0].sim_time) {
    clear_player(player);
    history = history_for_player(player);
    if (history == nullptr) {
      return;
    }
  }

  history->ent_index = player->get_index();
  if (history->record_count > 0) {
    const resolver_record& last = history->records[0];
    if (last.valid &&
        std::fabs(last.sim_time - record.sim_time) <= 0.0001f &&
        std::fabs(yaw_delta(last.eye_yaw, record.eye_yaw)) <= 0.01f) {
      return;
    }
  }

  const int shift_count = std::min(history->record_count, max_records - 1);
  for (int index = shift_count; index > 0; --index) {
    history->records[index] = history->records[index - 1];
  }

  history->records[0] = record;
  history->record_count = std::min(history->record_count + 1, max_records);
}

inline void clear()
{
  g_history = {};
  g_resolver_state = {};
  g_userid_yaw_offsets.clear();
}

inline void add_yaw(yaw_candidate_list* list, float yaw, float penalty)
{
  if (list == nullptr || list->count >= max_yaw_candidates || !finite_angle(yaw)) {
    return;
  }

  const float normalized = normalize_yaw(yaw);
  for (int index = 0; index < list->count; ++index) {
    if (std::fabs(yaw_delta(list->values[index].yaw, normalized)) < 2.0f) {
      list->values[index].penalty = std::min(list->values[index].penalty, penalty);
      return;
    }
  }

  yaw_candidate& candidate = list->values[list->count];
  candidate.valid = true;
  candidate.yaw = normalized;
  candidate.penalty = penalty;
  ++list->count;
}

inline void add_pitch(pitch_candidate_list* list, float pitch, float penalty)
{
  if (list == nullptr || list->count >= max_pitch_candidates || !finite_angle(pitch)) {
    return;
  }

  const float clamped = clamp_pitch(pitch);
  for (int index = 0; index < list->count; ++index) {
    if (std::fabs(list->values[index].pitch - clamped) < 1.0f) {
      list->values[index].penalty = std::min(list->values[index].penalty, penalty);
      return;
    }
  }

  pitch_candidate& candidate = list->values[list->count];
  candidate.valid = true;
  candidate.pitch = clamped;
  candidate.penalty = penalty;
  ++list->count;
}

[[nodiscard]] inline float yaw_to_local(Player* localplayer, Player* player)
{
  if (localplayer == nullptr || player == nullptr) {
    return 0.0f;
  }

  return aimbot_calculate_angles_to_position(player->get_origin(), localplayer->get_origin()).y;
}

inline Vec3 owned_sniper_dot_origin(Player* player)
{
  if (player == nullptr) {
    return {};
  }

  for (Entity* entity : entity_cache_entities(class_id::SNIPER_DOT)) {
    if (entity == nullptr || entity->is_dormant() || entity->get_owner_entity() != player) {
      continue;
    }

    const Vec3 dot_origin = entity->get_origin();
    if (aimbot_vec3_is_finite(dot_origin)) {
      return dot_origin;
    }
  }
  return {};
}

[[nodiscard]] inline resolver_mode detect_mode(Player* player, const anim_state_snapshot& snapshot)
{
  const player_history* history = history_for_player(player);
  const bool fakewalk = snapshot.speed_2d > 2.0f && snapshot.speed_2d <= 18.0f;

  if (history != nullptr && history->record_count >= 3) {
    const resolver_record& newest = history->records[0];
    const resolver_record& previous = history->records[1];
    const resolver_record& older = history->records[2];
    if (newest.valid && previous.valid && older.valid) {
      const float step_a = yaw_delta(newest.eye_yaw, previous.eye_yaw);
      const float step_b = yaw_delta(previous.eye_yaw, older.eye_yaw);
      const float abs_step_a = std::fabs(step_a);
      const float abs_step_b = std::fabs(step_b);
      if (abs_step_a > 140.0f && abs_step_b > 140.0f && (step_a > 0.0f) != (step_b > 0.0f)) {
        return resolver_mode::sideways_fake;
      }

      if (std::fabs(step_a) > 25.0f && std::fabs(step_b) > 25.0f) {
        if ((step_a > 0.0f) != (step_b > 0.0f)) {
          return resolver_mode::jitter;
        }

        if (std::fabs(std::fabs(step_a) - std::fabs(step_b)) < 35.0f) {
          return resolver_mode::spin;
        }
      }
    }
  }

  if (fakewalk) {
    return resolver_mode::fakewalk;
  }

  return snapshot.moving ? resolver_mode::moving : resolver_mode::standing;
}

inline void add_mode_yaws(Player* localplayer,
  Player* player,
  yaw_candidate_list* list,
  const anim_state_snapshot& snapshot,
  resolver_mode mode,
  const player_resolver_state* state)
{
  if (list == nullptr) {
    return;
  }

  const float base_yaw = snapshot.valid ? snapshot.eye_yaw : player->get_eye_angles().y;
  const float gait_yaw = snapshot.valid ? snapshot.gait_yaw : base_yaw;
  const float local_yaw = yaw_to_local(localplayer, player);
  const float movement_yaw = snapshot.moving ? snapshot.velocity_yaw : gait_yaw;
  const int brute_index = state != nullptr ? state->brute_yaw_index : 0;
  constexpr std::array<float, 9> brute_offsets = {
    0.0f,
    180.0f,
    -58.0f,
    58.0f,
    -90.0f,
    90.0f,
    -120.0f,
    120.0f,
    -180.0f
  };

  const float brute_offset = brute_offsets[static_cast<std::size_t>(std::abs(brute_index) % static_cast<int>(brute_offsets.size()))];
  const float brute_base = mode == resolver_mode::moving ? movement_yaw : base_yaw;
  add_yaw(list, brute_base + brute_offset, -4.0f);

  switch (mode) {
  case resolver_mode::moving:
    add_yaw(list, snapshot.velocity_yaw, -3.0f);
    add_yaw(list, gait_yaw, -2.0f);
    break;
  case resolver_mode::fakewalk:
    add_yaw(list, gait_yaw, -3.0f);
    add_yaw(list, local_yaw + 180.0f, -1.5f);
    add_yaw(list, base_yaw + 180.0f, -1.0f);
    break;
  case resolver_mode::jitter:
    if (const player_history* history = history_for_player(player); history != nullptr && history->record_count >= 2) {
      const float jitter_delta = std::clamp(std::fabs(yaw_delta(history->records[0].eye_yaw, history->records[1].eye_yaw)), 35.0f, 180.0f);
      add_yaw(list, base_yaw + jitter_delta, -3.0f);
      add_yaw(list, base_yaw - jitter_delta, -3.0f);
      add_yaw(list, history->records[1].eye_yaw, -2.0f);
    }
    break;
  case resolver_mode::spin:
    if (const player_history* history = history_for_player(player); history != nullptr && history->record_count >= 2) {
      const float spin_step = yaw_delta(history->records[0].eye_yaw, history->records[1].eye_yaw);
      add_yaw(list, base_yaw + spin_step, -2.5f);
      add_yaw(list, base_yaw + (spin_step * 2.0f), -2.0f);
    }
    break;
  case resolver_mode::sideways_fake:
    add_yaw(list, base_yaw + 90.0f, -5.0f);
    add_yaw(list, base_yaw - 90.0f, -5.0f);
    add_yaw(list, gait_yaw, -2.5f);
    if (const player_history* history = history_for_player(player); history != nullptr && history->record_count >= 2) {
      add_yaw(list, history->records[1].eye_yaw + 90.0f, -2.0f);
      add_yaw(list, history->records[1].eye_yaw - 90.0f, -2.0f);
    }
    break;
  case resolver_mode::standing:
  case resolver_mode::unknown:
    add_yaw(list, base_yaw, -3.0f);
    add_yaw(list, base_yaw + 180.0f, -1.5f);
    add_yaw(list, gait_yaw, -1.0f);
    break;
  }
}

inline void add_history_yaws(Player* player, yaw_candidate_list* list)
{
  const player_history* history = history_for_player(player);
  if (history == nullptr || list == nullptr || history->record_count <= 0) {
    return;
  }

  const resolver_record& latest = history->records[0];
  if (latest.valid) {
    add_yaw(list, latest.eye_yaw, 2.0f);
    add_yaw(list, latest.gait_yaw, 3.0f);
    if (latest.moving) {
      add_yaw(list, latest.velocity_yaw, 1.0f);
    }
  }

  for (int index = 1; index < history->record_count; ++index) {
    const resolver_record& record = history->records[index];
    if (!record.valid) {
      continue;
    }

    if (record.moving) {
      add_yaw(list, record.velocity_yaw, 4.0f);
      add_yaw(list, record.gait_yaw, 5.0f);
      break;
    }
  }

  if (history->record_count >= 2 && history->records[0].valid && history->records[1].valid) {
    const float yaw_step = yaw_delta(history->records[0].eye_yaw, history->records[1].eye_yaw);
    if (std::fabs(yaw_step) <= 90.0f) {
      add_yaw(list, history->records[0].eye_yaw + yaw_step, 6.0f);
      add_yaw(list, history->records[0].gait_yaw + yaw_step, 7.0f);
    }
  }
}

[[nodiscard]] inline yaw_candidate_list build_yaw_candidates(Player* localplayer,
  Player* player,
  const anim_state_snapshot& snapshot,
  resolver_mode mode,
  const player_resolver_state* state)
{
  yaw_candidate_list list{};
  const Vec3 eye_angles = player != nullptr ? player->get_eye_angles() : Vec3{};
  const float eye_yaw = finite_angle(eye_angles.y) ? eye_angles.y : snapshot.eye_yaw;
  const float base_yaw = snapshot.valid ? snapshot.eye_yaw : eye_yaw;
  const float gait_yaw = snapshot.valid ? snapshot.gait_yaw : base_yaw;
  const float local_yaw = yaw_to_local(localplayer, player);

  add_mode_yaws(localplayer, player, &list, snapshot, mode, state);

  add_yaw(&list, base_yaw, 0.0f);
  add_yaw(&list, gait_yaw, snapshot.moving ? 0.5f : 2.5f);
  if (mode == resolver_mode::sideways_fake) {
    add_yaw(&list, base_yaw + 90.0f, -5.0f);
    add_yaw(&list, base_yaw - 90.0f, -5.0f);
    add_yaw(&list, gait_yaw + 90.0f, -1.0f);
    add_yaw(&list, gait_yaw - 90.0f, -1.0f);
  }
  if (snapshot.valid && snapshot.moving) {
    add_yaw(&list, snapshot.velocity_yaw, 0.25f);
  }

  add_history_yaws(player, &list);

  add_yaw(&list, local_yaw, 8.0f);
  add_yaw(&list, local_yaw + 180.0f, 8.5f);
  add_yaw(&list, local_yaw + 90.0f, 9.0f);
  add_yaw(&list, local_yaw - 90.0f, 9.0f);

  constexpr std::array<float, 8> desync_offsets = {
    -58.0f,
    58.0f,
    -90.0f,
    90.0f,
    -120.0f,
    120.0f,
    180.0f,
    -180.0f
  };

  for (const float offset : desync_offsets) {
    add_yaw(&list, base_yaw + offset, 10.0f + (std::fabs(offset) * 0.01f));
  }

  for (const float offset : desync_offsets) {
    add_yaw(&list, gait_yaw + offset, 11.0f + (std::fabs(offset) * 0.01f));
  }

  return list;
}

[[nodiscard]] inline pitch_candidate_list build_pitch_candidates(Player* player,
  const anim_state_snapshot& snapshot,
  const player_resolver_state* state)
{
  pitch_candidate_list list{};
  const Vec3 eye_angles = player != nullptr ? player->get_eye_angles() : Vec3{};
  const float raw_pitch = finite_angle(eye_angles.x) ? eye_angles.x : snapshot.raw_eye_pitch;
  const float base_pitch = snapshot.valid ? snapshot.eye_pitch : clamp_pitch(raw_pitch);
  constexpr std::array<float, 5> brute_pitches = {
    0.0f,
    -89.0f,
    89.0f,
    -45.0f,
    45.0f
  };
  const int brute_index = state != nullptr ? state->brute_pitch_index : 0;

  add_pitch(&list, base_pitch, 0.0f);
  add_pitch(&list, brute_pitches[static_cast<std::size_t>(std::abs(brute_index) % static_cast<int>(brute_pitches.size()))], -1.0f);

  if (settings.sniper_dot_pitch) {
    const Vec3 dot_origin = owned_sniper_dot_origin(player);
    const Vec3 eye_origin = player != nullptr ? player->get_origin() + player->get_view_offset() : Vec3{};
    if (aimbot_vec3_is_finite(dot_origin) && aimbot_vec3_is_finite(eye_origin)) {
      add_pitch(&list, aimbot_calculate_angles_to_position(eye_origin, dot_origin).x, -3.0f);
    }
  }

  if (finite_angle(raw_pitch) && std::fabs(raw_pitch) >= 89.0f && settings.pitch_fold_recovery) {
    const bool pitch_down = raw_pitch > 0.0f;
    if (settings.sniper_dot_pitch) {
      const Vec3 dot_origin = owned_sniper_dot_origin(player);
      const Vec3 eye_origin = player->get_origin() + player->get_view_offset();
      if (aimbot_vec3_is_finite(dot_origin) && aimbot_vec3_is_finite(eye_origin)) {
        add_pitch(&list, aimbot_calculate_angles_to_position(eye_origin, dot_origin).x, 0.5f);
      }
    }
    add_pitch(&list, pitch_down ? 89.0f : -89.0f, 1.0f);
    add_pitch(&list, 0.0f, 1.5f);
    add_pitch(&list, pitch_down ? -89.0f : 89.0f, 2.0f);
    add_pitch(&list, -base_pitch, 2.5f);
  }

  if (list.count <= 0) {
    add_pitch(&list, 0.0f, 4.0f);
  }

  return list;
}

[[nodiscard]] inline int candidate_exposure_count(Player* localplayer,
  Player* player,
  std::uint32_t hitbox_mask,
  bool require_visibility,
  unsigned int trace_mask)
{
  if (!require_visibility || localplayer == nullptr || player == nullptr || model_info == nullptr) {
    return 0;
  }

  const model_t* model = player->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(player->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr) {
    return 0;
  }

  matrix_3x4 bone_to_world[128]{};
  int bone_count = 0;
  if (!player->copy_cached_bones(bone_to_world, 128, &bone_count)) {
    return 0;
  }

  int exposed = 0;
  for (int hitbox_id = aim_hitbox_head; hitbox_id <= aim_hitbox_right_foot; ++hitbox_id) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask) || hitbox_id >= hitbox_set->num_hitboxes) {
      continue;
    }

    studio_box* hitbox = hitbox_set->hitbox(hitbox_id);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= bone_count) {
      continue;
    }

    const Vec3 center = aimbot_transform_point((hitbox->bbmin + hitbox->bbmax) * 0.5f, bone_to_world[hitbox->bone]);
    if (aimbot_vec3_is_finite(center) && aimbot_trace_visible_to_position(localplayer, player, center, trace_mask)) {
      ++exposed;
    }
  }

  return exposed;
}

inline void sort_yaw_candidates(yaw_candidate_list* list)
{
  if (list == nullptr || list->count <= 1) {
    return;
  }

  std::sort(list->values.begin(), list->values.begin() + list->count,
    [](const yaw_candidate& left, const yaw_candidate& right) {
      return left.penalty < right.penalty;
    });
}

inline void sort_pitch_candidates(pitch_candidate_list* list)
{
  if (list == nullptr || list->count <= 1) {
    return;
  }

  std::sort(list->values.begin(), list->values.begin() + list->count,
    [](const pitch_candidate& left, const pitch_candidate& right) {
      return left.penalty < right.penalty;
    });
}

[[nodiscard]] inline bool begin_hitscan_pose(Player* localplayer,
                                             Player* player,
                                             hitscan_pose_guard* guard)
{
  if (guard == nullptr || player == nullptr || !config.aimbot.resolver ||
      player->is_dormant() || !player->is_alive()) {
    return false;
  }

  const Vec3 original_angles = player->get_eye_angles();
  if (!aimbot_vec3_is_finite(original_angles)) {
    return false;
  }

  anim_state_snapshot snapshot{};
  if (!read_anim_state_snapshot(player, &snapshot)) {
    return false;
  }

  player_resolver_state* state = state_for_player(player);
  const resolver_mode mode = detect_mode(player, snapshot);
  if (state != nullptr) {
    state->mode = mode;
  }

  player_resolver_state candidate_state{};
  if (state != nullptr) {
    candidate_state = *state;
    candidate_state.brute_yaw_index = 0;
    candidate_state.brute_pitch_index = 0;
  }

  yaw_candidate_list yaw_candidates = build_yaw_candidates(
    localplayer, player, snapshot, mode, state != nullptr ? &candidate_state : nullptr);
  pitch_candidate_list pitch_candidates = build_pitch_candidates(
    player, snapshot, state != nullptr ? &candidate_state : nullptr);
  sort_yaw_candidates(&yaw_candidates);
  sort_pitch_candidates(&pitch_candidates);
  if (yaw_candidates.count <= 0 || pitch_candidates.count <= 0) {
    return false;
  }

  const int configured_yaws = std::clamp(
    config.aimbot.resolver_max_yaws, 1, max_yaw_candidates);
  const int yaw_count = std::min(yaw_candidates.count, configured_yaws);
  const int yaw_index = state != nullptr
    ? std::abs(state->brute_yaw_index) % yaw_count
    : 0;
  const int pitch_index = state != nullptr
    ? std::abs(state->brute_pitch_index) % pitch_candidates.count
    : 0;

  float applied_yaw = yaw_candidates.values[yaw_index].yaw;
  float applied_pitch = pitch_candidates.values[pitch_index].pitch;
  if (!finite_angle(applied_yaw) || !finite_angle(applied_pitch)) {
    return false;
  }

  apply_yaw_offset(state, &applied_yaw);
  apply_pitch_flags(state, &applied_pitch);

  if (state != nullptr) {
    state->yaw_candidates = yaw_count;
    state->selected_yaw = applied_yaw;
    state->selected_pitch = applied_pitch;
  }

  guard->player = player;
  guard->original_angles = original_angles;
  guard->resolved_angles = Vec3{applied_pitch, applied_yaw, original_angles.z};
  guard->active = true;
  player->set_eye_angles(guard->resolved_angles);

  if (!aimbot_update_client_side_animation(player)) {
    guard->restore();
    return false;
  }

  return true;
}

[[nodiscard]] inline float resolver_point_score(const aimbot_point& point, float angle_penalty, int exposed_hitboxes)
{
  return (static_cast<float>(point.priority) * 4096.0f) + point.fov + angle_penalty - (static_cast<float>(exposed_hitboxes) * 3.0f);
}

[[nodiscard]] inline const char* mode_name(resolver_mode mode)
{
  switch (mode) {
  case resolver_mode::moving:
    return "moving";
  case resolver_mode::standing:
    return "standing";
  case resolver_mode::jitter:
    return "jitter";
  case resolver_mode::spin:
    return "spin";
  case resolver_mode::fakewalk:
    return "fakewalk";
  case resolver_mode::sideways_fake:
    return "sideways";
  case resolver_mode::unknown:
    break;
  }

  return "unknown";
}

[[nodiscard]] inline resolver_debug_info debug_for_player(Player* player)
{
  resolver_debug_info info{};
  const player_resolver_state* state = state_for_player_const(player);
  if (state == nullptr || state->ent_index <= 0) {
    return info;
  }

  info.active = true;
  info.yaw_candidates = state->yaw_candidates;
  info.misses = state->misses;
  info.hits = state->hits;
  info.yaw = state->selected_yaw;
  info.pitch = state->selected_pitch;
  info.mode = state->mode;
  return info;
}

inline void update_pending_shots()
{
  if (global_vars == nullptr) {
    return;
  }

  for (player_resolver_state& state : g_resolver_state) {
    const bool expired_by_deadline = state.brute_armed && global_vars->curtime >= state.brute_deadline;
    if (!state.shot.active && !expired_by_deadline) {
      continue;
    }
    if (!expired_by_deadline && global_vars->curtime < state.shot.expire_time) {
      continue;
    }

    state.shot.active = false;
    state.brute_armed = false;
    state.misses = std::min(state.misses + 1, 64);
    if (settings.auto_brute && settings.yaw_step_degrees > 0.0f) {
      state.yaw_offset = normalize_yaw(state.yaw_offset + settings.yaw_step_degrees);
    }
    const int max_yaws = std::clamp(config.aimbot.resolver_max_yaws, 1, max_yaw_candidates);
    state.brute_yaw_index = (state.brute_yaw_index + 1) % max_yaws;
    if ((state.misses % 2) == 0) {
      state.brute_pitch_index = (state.brute_pitch_index + 1) % max_pitch_candidates;
      state.inverse_pitch = !state.inverse_pitch;
    }
  }
}

inline void note_shot(Player* player, int hitbox, float sim_time, bool backtrack)
{
  if (!config.aimbot.resolver || player == nullptr || global_vars == nullptr) {
    return;
  }

  player_resolver_state* state = state_for_player(player);
  if (state == nullptr) {
    return;
  }

  pending_shot shot{};
  shot.active = true;
  shot.time = global_vars->curtime;
  const float deadline = std::max(outgoing_latency_seconds() * settings.brute_latency_scale +
    settings.brute_grace_seconds, backtrack ? 0.55f : 0.35f);
  shot.expire_time = global_vars->curtime + deadline;
  shot.sim_time = sim_time;
  shot.yaw = state->selected_yaw;
  shot.pitch = state->selected_pitch;
  shot.hitbox = hitbox;
  shot.backtrack = backtrack;
  state->shot = shot;
  state->brute_armed = true;
  state->brute_deadline = shot.expire_time;
}

inline void on_local_weapon_fire(Player* shooter)
{
  if (shooter == nullptr || entity_list == nullptr || global_vars == nullptr) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (shooter != localplayer) {
    return;
  }

  for (player_resolver_state& state : g_resolver_state) {
    if (state.brute_armed && state.shot.active) {
      const float deadline = std::max(
        outgoing_latency_seconds() * settings.brute_latency_scale + settings.brute_grace_seconds,
        state.shot.backtrack ? 0.55f : 0.35f);
      state.brute_deadline = std::max(state.brute_deadline, global_vars->curtime + deadline);
      state.shot.expire_time = state.brute_deadline;
    }
  }
}

inline void note_player_hurt(Player* attacker, Player* victim)
{
  if (!config.aimbot.resolver || attacker == nullptr || victim == nullptr || entity_list == nullptr) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || attacker != localplayer) {
    return;
  }

  player_resolver_state* state = state_for_player(victim);
  if (state == nullptr) {
    return;
  }

  state->shot.active = false;
  state->brute_armed = false;
  state->hits = std::min(state->hits + 1, 64);
  state->misses = std::max(state->misses - 1, 0);
}

[[nodiscard]] inline bool resolved_eye_angles(Player* player, Vec3* angles)
{
  if (player == nullptr || angles == nullptr || player->is_dormant() || !player->is_alive()) {
    return false;
  }

  const Vec3 original_angles = player->get_eye_angles();
  if (!config.aimbot.resolver || !aimbot_vec3_is_finite(original_angles)) {
    *angles = original_angles;
    return aimbot_vec3_is_finite(original_angles);
  }

  anim_state_snapshot snapshot{};
  if (!read_anim_state_snapshot(player, &snapshot)) {
    *angles = original_angles;
    return false;
  }

  player_resolver_state* state = state_for_player(player);
  const resolver_mode mode = detect_mode(player, snapshot);
  if (state != nullptr) {
    state->mode = mode;
    state->minwalk = minwalk_active(player);
  }

  yaw_candidate_list yaw_candidates = build_yaw_candidates(nullptr, player, snapshot, mode, state);
  pitch_candidate_list pitch_candidates = build_pitch_candidates(player, snapshot, state);
  sort_yaw_candidates(&yaw_candidates);
  sort_pitch_candidates(&pitch_candidates);
  if (yaw_candidates.count <= 0 || pitch_candidates.count <= 0) {
    *angles = original_angles;
    return false;
  }

  float applied_yaw = yaw_candidates.values[0].yaw;
  float applied_pitch = pitch_candidates.values[0].pitch;
  apply_yaw_offset(state, &applied_yaw);
  apply_pitch_flags(state, &applied_pitch);
  if (!finite_angle(applied_yaw) || !finite_angle(applied_pitch)) {
    *angles = original_angles;
    return false;
  }

  if (state != nullptr) {
    state->selected_yaw = applied_yaw;
    state->selected_pitch = applied_pitch;
  }
  *angles = Vec3{applied_pitch, applied_yaw, 0.0f};
  return true;
}

[[nodiscard]] inline bool setup_record_bones(Player* player, matrix_3x4* bones, int max_bones, float sim_time)
{
  if (!config.aimbot.resolver || player == nullptr || bones == nullptr || max_bones <= 0) {
    return false;
  }

  anim_state_snapshot snapshot{};
  if (!read_anim_state_snapshot(player, &snapshot)) {
    return false;
  }

  player_resolver_state* state = state_for_player(player);
  const resolver_mode mode = detect_mode(player, snapshot);
  if (state != nullptr) {
    state->mode = mode;
  }

  yaw_candidate_list yaw_candidates = build_yaw_candidates(nullptr, player, snapshot, mode, state);
  pitch_candidate_list pitch_candidates = build_pitch_candidates(player, snapshot, state);
  sort_yaw_candidates(&yaw_candidates);
  sort_pitch_candidates(&pitch_candidates);
  if (yaw_candidates.count <= 0 || pitch_candidates.count <= 0) {
    return false;
  }

  (void)sim_time;
  int bone_count = 0;
  const bool result = player->copy_cached_bones(bones, max_bones, &bone_count);

  if (state != nullptr) {
    state->yaw_candidates = yaw_candidates.count;
    state->selected_yaw = yaw_candidates.values[0].yaw;
    state->selected_pitch = pitch_candidates.values[0].pitch;
  }

  return result;
}

[[nodiscard]] inline aimbot_point find_point(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& bullet_view_angles,
  bool require_visibility,
  unsigned int trace_mask)
{
  if (!config.aimbot.resolver || localplayer == nullptr || weapon == nullptr || player == nullptr) {
    return {};
  }

  anim_state_snapshot snapshot{};
  if (!read_anim_state_snapshot(player, &snapshot)) {
    return {};
  }

  player_resolver_state* state = state_for_player(player);
  const resolver_mode mode = detect_mode(player, snapshot);
  if (state != nullptr) {
    state->mode = mode;
  }

  yaw_candidate_list yaw_candidates = build_yaw_candidates(localplayer, player, snapshot, mode, state);
  pitch_candidate_list pitch_candidates = build_pitch_candidates(player, snapshot, state);
  sort_yaw_candidates(&yaw_candidates);
  sort_pitch_candidates(&pitch_candidates);
  if (yaw_candidates.count <= 0 || pitch_candidates.count <= 0) {
    return {};
  }

  const std::uint32_t configured_mask = config.aimbot.hitscan_hitboxes & aim_hitbox_mask_all;
  const std::uint32_t hitbox_mask = configured_mask;
  if (state != nullptr) {
    state->yaw_candidates = yaw_candidates.count;
    state->selected_yaw = yaw_candidates.values[0].yaw;
    state->selected_pitch = pitch_candidates.values[0].pitch;
  }

  return aimbot_find_best_point(
    localplayer,
    player,
    weapon,
    bullet_view_angles,
    hitbox_mask,
    require_visibility,
    trace_mask);
}

}
#endif
