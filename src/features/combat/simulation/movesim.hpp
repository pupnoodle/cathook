#ifndef MOVESIM_HPP
#define MOVESIM_HPP
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "core/math/math.hpp"
#include "core/types.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/game_movement.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/move_helper.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"
#include "games/tf2/sdk/netvars.hpp"

namespace movesim {

enum class surface_mode { ground, air, swim };

struct move_record {
  Vec3 direction{};
  float sim_time = 0.0f;
  surface_mode mode = surface_mode::ground;
  Vec3 velocity{};
  Vec3 origin{};
};

struct snapshot_state {
  Vec3 origin{};
  Vec3 abs_origin{};
  Vec3 velocity{};
  Vec3 base_velocity{};
  Vec3 view_offset{};
  int flags = 0;
  int ground_entity_handle = 0;
  int buttons = 0;
  int last_buttons = 0;
  int tickbase = 0;
  user_cmd* current_cmd = nullptr;
  bool ducked = false;
  bool ducking = false;
  bool in_duck_jump = false;
  float duck_time = 0.0f;
  float duck_jump_time = 0.0f;
  float fall_velocity = 0.0f;
  float curtime = 0.0f;
  float frametime = 0.0f;
  int tickcount = 0;
  bool in_prediction = false;
  bool first_time_predicted = false;
};

struct storage {
  Player* player = nullptr;
  bool valid = false;
  bool failed = false;
  bool predict_networked = false;
  float sim_time = 0.0f;
  float predicted_delta = 0.0f;
  Vec3 predicted_origin{};
  Vec3 terminal_velocity{};
  std::vector<Vec3> path{};

  MoveData move_data{};
  user_cmd dummy_cmd{};
  snapshot_state snapshot{};
  float average_yaw = 0.0f;
  float predicted_sim_time = 0.0f;
  bool direct_move = true;
  bool bunny_hop = false;
  bool local_sim = false;
  bool predict_requested = false;
  bool drain_charge_enabled = false;
  bool restored = false;
  int frozen_ticks = 0;
};

struct init_options {
  bool strafe_prediction = true;
  bool hitchance_gate = false;
  float hitchance_minimum = 0.0f;
  bool predict_networked = false;
  bool drain_charge = false;
  bool inject_jump = false;
  const user_cmd* local_command = nullptr;
};

inline void push_record(int entindex, const move_record& record);
inline void clear_records(int entindex);
inline void clear_all();
inline const std::vector<move_record>& history(int entindex);
inline bool initialize(Player* player, storage& state, const init_options& options);
inline bool run_tick(storage& state);
inline void restore(storage& state);

namespace detail {

inline constexpr std::size_t max_records = 66;
inline constexpr int strafe_samples = 33;
inline constexpr float gravity_value = 800.0f;
inline constexpr float charge_speed_reference = 520.0f;
inline constexpr float ground_straight_fuzzy = 100.0f;
inline constexpr float air_straight_fuzzy = 0.0f;
inline constexpr int ground_max_changes = 0;
inline constexpr int air_max_changes = 2;
inline constexpr int air_max_change_time = 16;
inline constexpr int minimum_strafes = 4;
inline constexpr float accept_yaw_per_tick = 0.36f;
inline constexpr float hitchance_deviation = 0.5f;
inline constexpr float stuck_min_speed = 10.0f;
inline constexpr float stuck_max_step = 0.05f;
inline constexpr int stuck_tick_limit = 3;

inline std::unordered_map<int, std::vector<move_record>>& record_map() {
  static std::unordered_map<int, std::vector<move_record>> records{};
  return records;
}

inline const std::vector<move_record>& record_view(int entindex) {
  static const std::vector<move_record> empty{};
  const auto& records = record_map();
  const auto found = records.find(entindex);
  return found != records.end() ? found->second : empty;
}
inline float tick_interval() {
  if (global_vars != nullptr && std::isfinite(global_vars->interval_per_tick) &&
      global_vars->interval_per_tick > 0.0001f) {
    return global_vars->interval_per_tick;
  }
  return 0.015f;
}

inline int time_to_ticks(float seconds) {
  return static_cast<int>(0.5f + seconds / tick_interval());
}

inline float round_to_ticks(float seconds) {
  return static_cast<float>(time_to_ticks(seconds)) * tick_interval();
}

inline float dot(const Vec3& left, const Vec3& right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline float length_squared(const Vec3& value) {
  return value.x * value.x + value.y * value.y + value.z * value.z;
}

inline float length(const Vec3& value) {
  return std::sqrt(length_squared(value));
}

inline float length_2d(const Vec3& value) {
  return std::sqrt(value.x * value.x + value.y * value.y);
}

inline Vec3 normalized_2d(const Vec3& value) {
  const float magnitude = length_2d(value);
  if (!(magnitude > 0.0001f)) {
    return Vec3{};
  }
  return Vec3{value.x / magnitude, value.y / magnitude, 0.0f};
}

inline float sign(float value) {
  return value > 0.0f ? 1.0f : (value < 0.0f ? -1.0f : 0.0f);
}

inline float normalize_angle(float angle) {
  angle = std::fmod(angle, 360.0f);
  if (angle > 180.0f) {
    angle -= 360.0f;
  }
  if (angle <= -180.0f) {
    angle += 360.0f;
  }
  return angle;
}

inline float direction_yaw(const Vec3& direction) {
  if (length_squared(direction) <= 0.0001f) {
    return 0.0f;
  }
  return normalize_angle(std::atan2(direction.y, direction.x) * radpi);
}

inline float remap_clamped(float value, float in_min, float in_max, float out_min,
                           float out_max) {
  if (in_max - in_min <= 0.0001f) {
    return out_max >= out_min ? out_min : out_max;
  }
  const float fraction = std::clamp((value - in_min) / (in_max - in_min), 0.0f, 1.0f);
  return out_min + (out_max - out_min) * fraction;
}

inline float air_friction_scale(float velocity_xy, float turn, float velocity_z,
                                float low = 50.0f, float high = 150.0f) {
  if (!(velocity_z > 0.0f) || velocity_z > 250.0f) {
    return 1.0f;
  }
  Convar* accelerate =
    convar_system != nullptr ? convar_system->find_var("sv_airaccelerate") : nullptr;
  const float scale = accelerate != nullptr ? std::max(accelerate->get_float(), 1.0f) : 10.0f;
  low *= scale;
  high *= scale;
  return remap_clamped(std::fabs(velocity_xy * turn), low, high, 1.0f, 0.25f);
}

inline surface_mode surface_mode_of(Player* player) {
  if (player->get_water_level() > 1) {
    return surface_mode::swim;
  }
  if ((player->get_flags() & FL_ONGROUND) != 0) {
    return surface_mode::ground;
  }
  return surface_mode::air;
}

inline float charge_meter_value(Player* player) {
  static const int offset = tf2_netvars::find_offset("DT_TFPlayer", {"m_flChargeMeter"});
  if (offset <= 0 || player == nullptr) {
    return 0.0f;
  }
  return *reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(player) +
                                         static_cast<uintptr_t>(offset));
}

inline void set_charge_meter_value(Player* player, float value) {
  static const int offset = tf2_netvars::find_offset("DT_TFPlayer", {"m_flChargeMeter"});
  if (offset <= 0 || player == nullptr) {
    return;
  }
  *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(player) +
                            static_cast<uintptr_t>(offset)) = value;
}

inline void remove_shield_charge(Player* player) {
  if (player == nullptr) {
    return;
  }
  const auto shared = reinterpret_cast<uintptr_t>(player->get_shared());
  static const int cond_offset =
    tf2_netvars::find_offset("DT_TFPlayerShared", {"m_nPlayerCond"});
  constexpr uint32_t shield_bit = 1u << static_cast<uint32_t>(TF_COND_SHIELD_CHARGE);
  *reinterpret_cast<uint32_t*>(shared + 0x128) &= ~shield_bit;
  if (cond_offset > 0) {
    *reinterpret_cast<uint32_t*>(shared + static_cast<uintptr_t>(cond_offset)) &= ~shield_bit;
  }
}

inline float max_speed_of(Player* player) {
  const float speed = player->get_max_speed();
  return std::isfinite(speed) && speed > 1.0f ? speed : 400.0f;
}

inline Player* player_from_index(int entindex) {
  if (entity_list == nullptr || entindex <= 0) {
    return nullptr;
  }
  Entity* entity = entity_list->entity_from_index(static_cast<unsigned int>(entindex));
  if (entity == nullptr || entity->get_class_id() != class_id::PLAYER) {
    return nullptr;
  }
  return static_cast<Player*>(entity);
}

struct yaw_pair_state {
  int changes = 0;
  int start_ticks = 0;
  int last_sign = 0;
  bool last_zero = false;
  bool started = false;
};

inline bool yaw_pair_delta(const move_record& recent, const move_record& older,
                           float max_speed, yaw_pair_state& state, float& out_yaw) {
  const int ticks = std::max(time_to_ticks(recent.sim_time - older.sim_time), 1);
  float yaw =
    normalize_angle(direction_yaw(recent.direction) - direction_yaw(older.direction));
  const bool airborne = recent.mode == surface_mode::air;
  if (!airborne && max_speed > 0.0f) {
    yaw *= std::clamp(length_2d(recent.velocity) / max_speed, 0.0f, 1.0f);
  } else if (airborne) {
    yaw /= air_friction_scale(length_2d(recent.velocity), yaw,
                              recent.velocity.z + gravity_value * tick_interval());
  }

  const bool ground = recent.mode != surface_mode::air;
  const float fuzzy = ground ? ground_straight_fuzzy : air_straight_fuzzy;
  const int max_changes = ground ? ground_max_changes : air_max_changes;
  const int max_change_time = ground ? 0 : air_max_change_time;
  if (std::fabs(yaw) > 45.0f) {
    return false;
  }

  const int current_sign = yaw > 0.0f ? 1 : (yaw < 0.0f ? -1 : state.last_sign);
  const bool current_zero = yaw == 0.0f;
  const bool changed = current_sign != state.last_sign || (current_zero && state.last_zero);
  const bool straight =
    std::fabs(yaw) * length_2d(recent.velocity) * static_cast<float>(ticks) < fuzzy;

  if (!state.started) {
    state.started = true;
    state.changes = 0;
    state.start_ticks = time_to_ticks(recent.sim_time);
    if (straight && ++state.changes > max_changes) {
      return false;
    }
  } else {
    if ((changed || straight) && ++state.changes > max_changes) {
      return false;
    }
    if (state.changes > 0 &&
        state.start_ticks - time_to_ticks(older.sim_time) > max_change_time) {
      return false;
    }
  }

  state.last_sign = current_sign;
  state.last_zero = current_zero;
  out_yaw = yaw;
  return true;
}

inline void reconstruct_wish_move(storage& state, const Vec3& direction) {
  if (length_squared(direction) <= 0.0001f) {
    return;
  }
  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(state.move_data.m_vecViewAngles, &forward, &right, &up);
  state.move_data.m_flForwardMove = dot(direction, forward);
  state.move_data.m_flSideMove = dot(direction, right);
  state.move_data.m_flUpMove = dot(direction, up);
  state.dummy_cmd.forwardmove = state.move_data.m_flForwardMove;
  state.dummy_cmd.sidemove = state.move_data.m_flSideMove;
  state.dummy_cmd.upmove = state.move_data.m_flUpMove;
}

inline void setup_move_data(storage& state) {
  Player* player = state.player;
  MoveData& move_data = state.move_data;
  move_data.m_bFirstRunOfFunctions = false;
  move_data.m_bGameCodeMovedPlayer = false;
  move_data.m_nPlayerHandle = player->get_ref_handle();

  move_data.SetAbsOrigin(player->get_origin());
  move_data.m_vecVelocity = player->get_velocity();
  const float max_speed = max_speed_of(player);
  move_data.m_flMaxSpeed = max_speed;
  move_data.m_flClientMaxSpeed = max_speed;

  const bool charging = player->in_cond(TF_COND_SHIELD_CHARGE);
  Vec3 view_angles{};
  if (state.local_sim) {
    view_angles = state.dummy_cmd.view_angles;
  } else if (charging) {
    view_angles = player->get_eye_angles();
  } else {
    view_angles = Vec3{0.0f, direction_yaw(move_data.m_vecVelocity), 0.0f};
  }
  move_data.m_vecViewAngles = view_angles;
  move_data.m_vecAngles = view_angles;
  move_data.m_vecOldAngles = view_angles;
  state.dummy_cmd.view_angles = view_angles;

  move_data.m_nButtons = state.dummy_cmd.buttons;
  move_data.m_nOldButtons = state.dummy_cmd.buttons;
  move_data.m_nImpulseCommand = 0;
  move_data.m_flForwardMove = 0.0f;
  move_data.m_flSideMove = 0.0f;
  move_data.m_flUpMove = 0.0f;

  Vec3 direction{};
  if (charging) {
    Vec3 forward{};
    angle_vectors(state.local_sim ? state.dummy_cmd.view_angles : player->get_eye_angles(),
                  &forward, nullptr, nullptr);
    direction = forward * charge_speed_reference;
  } else {
    const std::vector<move_record>& records = record_view(player->get_index());
    if (!records.empty()) {
      direction = records.back().direction;
    }
  }

  Entity* constraint_entity =
    entity_list != nullptr
      ? entity_list->entity_from_handle(player->get_constraint_entity_handle())
      : nullptr;
  move_data.m_vecConstraintCenter = constraint_entity != nullptr
                                      ? constraint_entity->get_abs_origin()
                                      : player->get_constraint_center();
  move_data.m_flConstraintRadius = player->get_constraint_radius();
  move_data.m_flConstraintWidth = player->get_constraint_width();
  move_data.m_flConstraintSpeedFactor = player->get_constraint_speed_factor();

  reconstruct_wish_move(state, direction);
}

inline void capture_snapshot(storage& state) {
  Player* player = state.player;
  snapshot_state& snap = state.snapshot;
  snap.origin = player->get_origin();
  snap.abs_origin = player->get_abs_origin();
  snap.velocity = player->get_velocity();
  snap.base_velocity = player->get_base_velocity();
  snap.view_offset = player->get_view_offset();
  snap.flags = player->get_flags();
  snap.ground_entity_handle = player->get_ground_entity_handle();
  snap.buttons = player->get_buttons();
  snap.last_buttons = player->get_last_buttons();
  snap.tickbase = player->get_tickbase();
  snap.current_cmd = player->get_current_cmd();
  snap.ducked = player->get_ducked();
  snap.ducking = player->get_ducking_state();
  snap.in_duck_jump = player->get_in_duck_jump();
  snap.duck_time = player->get_duck_time();
  snap.duck_jump_time = player->get_duck_jump_time();
  snap.fall_velocity = player->get_fall_velocity();
  if (global_vars != nullptr) {
    snap.curtime = global_vars->curtime;
    snap.frametime = global_vars->frametime;
    snap.tickcount = global_vars->tickcount;
  }
  if (prediction != nullptr) {
    snap.in_prediction = prediction->in_prediction;
    snap.first_time_predicted = prediction->first_time_predicted;
  }
}

inline void normalize_remote_velocity(storage& state) {
  Player* player = state.player;
  const std::vector<move_record>& records = record_view(player->get_index());
  if (records.empty()) {
    return;
  }
  const std::size_t sample_count = std::min<std::size_t>(records.size(), 5);
  Vec3 average{};
  for (std::size_t index = records.size() - sample_count; index < records.size(); ++index) {
    average += records[index].velocity;
  }
  average = average * (1.0f / static_cast<float>(sample_count));
  average.z = 0.0f;
  const float magnitude = length_2d(average);
  const float limit = max_speed_of(player) * 1.25f + 100.0f;
  if (std::isfinite(magnitude) && magnitude > 1.0f && magnitude <= limit) {
    Vec3 velocity = player->get_velocity();
    velocity.x = average.x;
    velocity.y = average.y;
    player->set_velocity(velocity);
  }
}

inline float predicted_delta_from_history(int entindex) {
  const std::vector<move_record>& records = record_view(entindex);
  float gaps[5]{};
  int count = 0;
  for (std::size_t index = records.size(); index > 1 && count < 5; --index) {
    const float gap = records[index - 1].sim_time - records[index - 2].sim_time;
    if (std::isfinite(gap) && gap > 0.0f) {
      gaps[count++] = gap;
    }
  }
  if (count == 0) {
    return tick_interval();
  }
  float total = 0.0f;
  for (int index = 0; index < count; ++index) {
    total += gaps[index];
  }
  return total / static_cast<float>(count);
}

inline void synthesize_record(Player* player, storage& state) {
  move_record record{};
  Vec3 direction{};
  const bool charging = player->in_cond(TF_COND_SHIELD_CHARGE);
  if (state.local_sim) {
    user_cmd* command = player->current_command();
    if (command == nullptr) {
      command = &state.dummy_cmd;
    }
    Vec3 forward{};
    Vec3 right{};
    angle_vectors(command->view_angles, &forward, &right, nullptr);
    direction = forward * command->forwardmove + right * command->sidemove;
    if (length_squared(direction) <= 0.0001f) {
      direction = normalized_2d(player->get_velocity()) * charge_speed_reference;
    }
  } else if (charging) {
    Vec3 forward{};
    angle_vectors(player->get_eye_angles(), &forward, nullptr, nullptr);
    direction = forward * charge_speed_reference;
  } else {
    direction = normalized_2d(player->get_velocity()) * charge_speed_reference;
  }
  record.direction = direction;
  record.sim_time = player->get_simulation_time();
  record.mode = surface_mode_of(player);
  record.velocity = player->get_velocity();
  record.origin = player->get_origin();
  push_record(player->get_index(), record);
}

}

inline void push_record(int entindex, const move_record& record) {
  std::vector<move_record>& records = detail::record_map()[entindex];
  records.push_back(record);
  if (records.size() > detail::max_records) {
    records.erase(records.begin());
  }
}

inline void clear_records(int entindex) {
  detail::record_map().erase(entindex);
}

inline void clear_all() {
  detail::record_map().clear();
}

inline const std::vector<move_record>& history(int entindex) {
  return detail::record_view(entindex);
}

inline bool average_yaw(int entindex, float sample_window_seconds, float* out_yaw_per_tick) {
  if (out_yaw_per_tick != nullptr) {
    *out_yaw_per_tick = 0.0f;
  }
  const std::vector<move_record>& records = detail::record_view(entindex);
  if (records.size() < 2) {
    return false;
  }

  const float newest_time = records.back().sim_time;
  const float cutoff = newest_time - std::max(sample_window_seconds, detail::tick_interval());
  std::size_t begin = records.size();
  while (begin > 0 && records[begin - 1].sim_time >= cutoff) {
    --begin;
  }
  if (records.size() - begin < 2) {
    return false;
  }

  Player* player = detail::player_from_index(entindex);
  const float max_speed = player != nullptr ? detail::max_speed_of(player) : 400.0f;

  detail::yaw_pair_state state{};
  float accumulated = 0.0f;
  int ticks = 0;
  int valid_pairs = 0;
  bool ground_window = true;

  for (std::size_t index = records.size(); index > begin + 1; --index) {
    const move_record& recent = records[index - 1];
    const move_record& older = records[index - 2];
    if (recent.mode != older.mode) {
      continue;
    }
    ground_window = recent.mode != surface_mode::air;
    float yaw = 0.0f;
    if (!detail::yaw_pair_delta(recent, older, max_speed, state, yaw)) {
      break;
    }
    accumulated += yaw;
    ticks += std::max(detail::time_to_ticks(recent.sim_time - older.sim_time), 1);
    ++valid_pairs;
  }

  if (valid_pairs < detail::minimum_strafes) {
    return false;
  }

  float minimum_ticks = static_cast<float>(detail::strafe_samples / 2);
  if (ground_window) {
    Vec3 local_origin{};
    Player* local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
    if (local != nullptr) {
      local_origin = local->get_origin();
    }
    minimum_ticks = detail::remap_clamped(
      distance_3d(local_origin, records.back().origin), 0.0f, 1000.0f, 17.0f, 34.0f);
  }

  const float yaw_per_tick =
    accumulated / std::max(static_cast<float>(ticks), minimum_ticks);
  if (std::fabs(yaw_per_tick) < detail::accept_yaw_per_tick) {
    return false;
  }
  if (out_yaw_per_tick != nullptr) {
    *out_yaw_per_tick = yaw_per_tick;
  }
  return true;
}

inline float strafe_hitchance(int entindex, int group_size, float minimum_confidence) {
  (void)minimum_confidence;
  const std::vector<move_record>& records = detail::record_view(entindex);
  const int group = std::max(group_size, 1);
  if (records.size() < 3) {
    return 1.0f;
  }

  Player* player = detail::player_from_index(entindex);
  const float max_speed = player != nullptr ? detail::max_speed_of(player) : 400.0f;

  std::vector<float> scaled_yaws;
  scaled_yaws.reserve(records.size());
  float overall_total = 0.0f;
  for (std::size_t index = records.size(); index > 1; --index) {
    const move_record& recent = records[index - 1];
    const move_record& older = records[index - 2];
    const int ticks = std::max(detail::time_to_ticks(recent.sim_time - older.sim_time), 1);
    const float yaw = detail::normalize_angle(detail::direction_yaw(recent.direction) -
                                              detail::direction_yaw(older.direction));
    float scaled = yaw / static_cast<float>(ticks);
    if (recent.mode != surface_mode::air && max_speed > 0.0f) {
      scaled *= std::clamp(detail::length_2d(recent.velocity) / max_speed, 0.0f, 1.0f);
    } else if (recent.mode == surface_mode::air) {
      scaled /= detail::air_friction_scale(
        detail::length_2d(recent.velocity), scaled * static_cast<float>(ticks),
        recent.velocity.z + detail::gravity_value * detail::tick_interval());
    }
    overall_total += scaled;
    scaled_yaws.push_back(scaled);
  }

  const int pair_count = static_cast<int>(scaled_yaws.size());
  if (pair_count < 2) {
    return 1.0f;
  }
  const float overall_mean = overall_total / static_cast<float>(pair_count);

  int total_groups = 0;
  int violations = 0;
  float group_total = 0.0f;
  int group_count = 0;
  for (int index = 0; index < pair_count; ++index) {
    group_total += scaled_yaws[static_cast<std::size_t>(index)];
    ++group_count;
    if ((index + 1) % group == 0 || index == pair_count - 1) {
      const float group_mean = group_total / static_cast<float>(group_count);
      ++total_groups;
      if (std::fabs(group_mean - overall_mean) > detail::hitchance_deviation) {
        ++violations;
      }
      group_total = 0.0f;
      group_count = 0;
    }
  }

  const float confidence =
    total_groups > 0
      ? 1.0f - static_cast<float>(violations) / static_cast<float>(total_groups)
      : 1.0f;
  return std::clamp(confidence, 0.0f, 1.0f);
}

inline bool initialize(Player* player, storage& state, const init_options& options) {
  if (state.player != nullptr && state.valid && !state.restored) {
    restore(state);
  }
  state = storage{};

  if (player == nullptr || !player->is_alive() || global_vars == nullptr ||
      prediction == nullptr || game_movement == nullptr || move_helper == nullptr ||
      entity_list == nullptr) {
    return false;
  }
  if (player->get_move_type() != MOVETYPE_WALK) {
    return false;
  }

  const bool local =
    engine != nullptr && player->get_index() == engine->get_localplayer_index();

  state.player = player;
  state.valid = true;
  state.local_sim = local;
  state.predict_requested = options.predict_networked;
  state.drain_charge_enabled = options.drain_charge;
  const int jump_buttons =
    options.local_command != nullptr ? options.local_command->buttons : player->get_buttons();
  state.bunny_hop = options.inject_jump && local && (jump_buttons & IN_JUMP) != 0;

  detail::capture_snapshot(state);

  move_helper->set_host(player);

  user_cmd command{};
  if (local && options.local_command != nullptr) {
    command = *options.local_command;
  } else {
    command.buttons = player->get_buttons();
    command.view_angles = player->get_eye_angles();
  }
  command.impulse = 0;
  command.weapon_select = 0;
  command.weapon_subtype = 0;
  command.has_been_predicted = false;
  state.dummy_cmd = command;
  player->set_current_cmd(&state.dummy_cmd);

  if (history(player->get_index()).empty()) {
    detail::synthesize_record(player, state);
  }

  if (!local) {
    detail::normalize_remote_velocity(state);
    player->set_base_velocity(Vec3{});
    if ((player->get_flags() & FL_ONGROUND) != 0 && !player->is_dormant()) {
      Vec3 velocity = player->get_velocity();
      velocity.z = std::min(velocity.z, 0.0f);
      player->set_velocity(velocity);
    } else {
      player->set_ground_entity_handle(0);
    }
  }

  player->set_ducked(player->is_ducking() && !player->is_dormant());
  player->set_flags(player->get_flags() & ~static_cast<int>(FL_DUCKING));
  player->set_duck_time(0.0f);
  player->set_duck_jump_time(0.0f);
  player->set_ducking_state(false);
  player->set_in_duck_jump(false);

  detail::setup_move_data(state);

  state.sim_time = player->get_simulation_time();
  state.predicted_delta = detail::predicted_delta_from_history(player->get_index());
  state.predicted_sim_time = state.sim_time + state.predicted_delta;
  state.predicted_origin = state.move_data.GetAbsOrigin();
  state.direct_move = player->is_on_ground() || player->get_water_level() > 1;
  state.terminal_velocity = state.move_data.m_vecVelocity;
  state.path.reserve(static_cast<std::size_t>(401));
  state.path.push_back(state.move_data.GetAbsOrigin());

  if (options.strafe_prediction) {
    float yaw_per_tick = 0.0f;
    if (average_yaw(player->get_index(),
                    static_cast<float>(detail::strafe_samples) * detail::tick_interval(),
                    &yaw_per_tick)) {
      state.average_yaw = yaw_per_tick;
    }
    if (options.hitchance_gate &&
        detail::length_2d(state.move_data.m_vecVelocity) > 1.0f) {
      const float confidence =
        strafe_hitchance(player->get_index(), detail::strafe_samples,
                         options.hitchance_minimum);
      if (confidence < options.hitchance_minimum) {
        state.failed = true;
        return false;
      }
    }
    if (state.average_yaw == 0.0f && !state.direct_move) {
      state.move_data.m_flForwardMove = 0.0f;
      state.move_data.m_flSideMove = 0.0f;
      state.dummy_cmd.forwardmove = 0.0f;
      state.dummy_cmd.sidemove = 0.0f;
    }
  }

  return true;
}

inline bool run_tick(storage& state) {
  if (state.player == nullptr || !state.valid || state.failed || state.restored ||
      game_movement == nullptr || global_vars == nullptr) {
    return false;
  }

  Player* player = state.player;
  MoveData& move_data = state.move_data;
  const float interval = detail::tick_interval();

  if (prediction != nullptr) {
    prediction->in_prediction = true;
    prediction->first_time_predicted = false;
  }
  const bool paused = prediction != nullptr && prediction->engine_paused;
  global_vars->frametime = paused ? 0.0f : interval;

  if (state.drain_charge_enabled && player->in_cond(TF_COND_SHIELD_CHARGE)) {
    Convar* drain_var = convar_system != nullptr
                          ? convar_system->find_var("tf_demoman_charge_drain_time")
                          : nullptr;
    float drain_time = drain_var != nullptr ? drain_var->get_float() : 1.5f;
    if (!(drain_time > 0.01f)) {
      drain_time = 1.5f;
    }
    if (attribute_manager != nullptr) {
      drain_time = attribute_manager->attrib_hook_value(drain_time, "mod_charge_time",
                                                        player->to_entity());
    }
    if (drain_time > 0.01f) {
      detail::set_charge_meter_value(
        player, detail::charge_meter_value(player) - interval * 100.0f / drain_time);
      if (detail::charge_meter_value(player) <= 0.0f) {
        detail::remove_shield_charge(player);
        const float refreshed = detail::max_speed_of(player);
        move_data.m_flMaxSpeed = refreshed;
        move_data.m_flClientMaxSpeed = refreshed;
      }
    }
  }

  float correction = 0.0f;
  if (state.average_yaw != 0.0f) {
    if (!state.direct_move && !player->in_cond(TF_COND_SHIELD_CHARGE)) {
      correction = 90.0f * detail::sign(state.average_yaw);
    }
    move_data.m_vecViewAngles.y += state.average_yaw + correction;
  }

  const bool swimming = player->get_water_level() > 1;
  const float saved_client_max = move_data.m_flClientMaxSpeed;
  if (player->get_ducked() && player->is_on_ground() && !swimming) {
    move_data.m_flClientMaxSpeed /= 3.0f;
  }

  if (state.bunny_hop && player->is_on_ground() && !player->get_ducked()) {
    move_data.m_nOldButtons &= ~IN_JUMP;
    move_data.m_nButtons |= IN_JUMP;
  }

  const Vec3 pre_origin = move_data.GetAbsOrigin();
  game_movement->process_movement(player, &move_data);
  move_data.m_flClientMaxSpeed = saved_client_max;
  const Vec3 post_origin = move_data.GetAbsOrigin();

  if (correction != 0.0f) {
    move_data.m_vecViewAngles.y -= correction;
  }

  state.sim_time = detail::round_to_ticks(state.sim_time + interval);
  if (state.predict_requested &&
      detail::time_to_ticks(state.sim_time) >=
        detail::time_to_ticks(state.predicted_sim_time)) {
    state.predict_networked = true;
  }
  state.predicted_origin = post_origin;
  if (state.predict_networked) {
    state.predicted_sim_time += state.predicted_delta;
  }

  const bool was_direct = state.direct_move;
  state.direct_move = player->is_on_ground() || swimming;
  if (!was_direct && state.direct_move) {
    if (state.average_yaw != 0.0f) {
      state.average_yaw *= -1.0f;
    } else if (move_data.m_flForwardMove == 0.0f && move_data.m_flSideMove == 0.0f &&
               detail::length_2d(move_data.m_vecVelocity) >
                 move_data.m_flMaxSpeed * 0.015f) {
      detail::reconstruct_wish_move(
        state,
        detail::normalized_2d(move_data.m_vecVelocity) * detail::charge_speed_reference);
    }
  }

  bool ok = std::isfinite(post_origin.x) && std::isfinite(post_origin.y) &&
            std::isfinite(post_origin.z);
  if (ok && !paused) {
    const float moved = detail::length(post_origin - pre_origin);
    const float speed = detail::length(move_data.m_vecVelocity);
    if (speed > detail::stuck_min_speed && moved < detail::stuck_max_step) {
      ++state.frozen_ticks;
    } else {
      state.frozen_ticks = 0;
    }
    if (state.frozen_ticks >= detail::stuck_tick_limit) {
      state.failed = true;
      ok = false;
    }
  }

  state.terminal_velocity = move_data.m_vecVelocity;
  state.path.push_back(post_origin);
  return ok && !state.failed;
}

inline void restore(storage& state) {
  if (state.player == nullptr || !state.valid || state.restored) {
    return;
  }
  Player* player = state.player;
  const snapshot_state& snap = state.snapshot;
  player->set_origin(snap.origin);
  player->set_abs_origin(snap.abs_origin);
  player->set_velocity(snap.velocity);
  player->set_base_velocity(snap.base_velocity);
  player->set_view_offset(snap.view_offset);
  player->set_flags(snap.flags);
  player->set_ground_entity_handle(snap.ground_entity_handle);
  player->set_buttons(snap.buttons);
  player->set_last_buttons(snap.last_buttons);
  player->set_tickbase(snap.tickbase);
  player->set_current_cmd(snap.current_cmd);
  player->set_ducked(snap.ducked);
  player->set_ducking_state(snap.ducking);
  player->set_in_duck_jump(snap.in_duck_jump);
  player->set_duck_time(snap.duck_time);
  player->set_duck_jump_time(snap.duck_jump_time);
  player->set_fall_velocity(snap.fall_velocity);
  if (global_vars != nullptr) {
    global_vars->curtime = snap.curtime;
    global_vars->frametime = snap.frametime;
    global_vars->tickcount = snap.tickcount;
  }
  if (prediction != nullptr) {
    prediction->in_prediction = snap.in_prediction;
    prediction->first_time_predicted = snap.first_time_predicted;
  }
  move_helper->set_host(nullptr);
  state.restored = true;
}

}
#endif
