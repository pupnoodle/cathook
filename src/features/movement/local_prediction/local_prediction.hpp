/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/movement/local_prediction/local_prediction.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef LOCAL_PREDICTION_HPP
#define LOCAL_PREDICTION_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "MD5/MD5.hpp"

#include "core/random_seed.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/game_movement.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/move_helper.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

#include "core/math/math.hpp"
#include "features/combat/aimbot/projectile/projectile_types.hpp"
#include "features/combat/aimbot/projectile/projectile_live_data.hpp"
#include "features/combat/aimbot/projectile/projectile_trace.hpp"
#include "features/combat/aimbot/proj_aim/proj_aim_budget.hpp"
#include "features/menu/config.hpp"

enum class LocalPredictionRunMode {
  MOVEMENT_ONLY,
  FULL_COMMAND
};

struct LocalPredictionTick {
  user_cmd cmd{};
};

struct LocalPredictionTickResult {
  user_cmd cmd{};
  Vec3 origin{};
  Vec3 velocity{};
  Vec3 wish_velocity{};
  Vec3 jump_velocity{};
  float step_height = 0.0f;
  bool grounded = false;
};

struct LocalPredictionRequest {
  std::vector<LocalPredictionTick> ticks{};
  LocalPredictionRunMode run_mode = LocalPredictionRunMode::MOVEMENT_ONLY;
};

struct LocalPredictionResult {
  bool success = false;
  bool restored = false;
  int simulated_ticks = 0;
  std::vector<LocalPredictionTickResult> ticks{};
};

struct LocalPredictionDebugState {
  bool last_run_ok = false;
  bool last_restore_ok = false;
  int last_simulated_ticks = 0;
  int last_failure_stage = 0;
};

inline static LocalPredictionDebugState local_prediction_debug;

struct LocalPredictionPlayerSnapshot {
  Vec3 origin{};
  Vec3 abs_origin{};
  Vec3 velocity{};
  Vec3 base_velocity{};
  Vec3 view_offset{};
  int flags = 0;
  int ground_entity_handle = 0;
  int tickbase = 0;
  user_cmd* current_cmd = nullptr;
  bool ducked = false;
  bool ducking = false;
  bool in_duck_jump = false;
  float duck_time = 0.0f;
  float duck_jump_time = 0.0f;
  float fall_velocity = 0.0f;
};

struct LocalPredictionGlobalSnapshot {
  float curtime = 0.0f;
  float frametime = 0.0f;
  int tickcount = 0;
  bool first_time_predicted = false;
  bool in_prediction = false;
  int random_seed_value = -1;
};

struct LocalPredictionSnapshot {
  LocalPredictionPlayerSnapshot player{};
  LocalPredictionGlobalSnapshot globals{};
};

inline bool local_prediction_capture(Player* localplayer, LocalPredictionSnapshot* snapshot) {
  if (localplayer == nullptr || snapshot == nullptr || global_vars == nullptr || prediction == nullptr) return false;

  snapshot->player.origin = localplayer->get_origin();
  snapshot->player.abs_origin = localplayer->get_abs_origin();
  snapshot->player.velocity = localplayer->get_velocity();
  snapshot->player.base_velocity = localplayer->get_base_velocity();
  snapshot->player.view_offset = localplayer->get_view_offset();
  snapshot->player.flags = localplayer->get_flags();
  snapshot->player.ground_entity_handle = localplayer->get_ground_entity_handle();
  snapshot->player.tickbase = localplayer->get_tickbase();
  snapshot->player.current_cmd = localplayer->get_current_cmd();
  snapshot->player.ducked = localplayer->get_ducked();
  snapshot->player.ducking = localplayer->get_ducking_state();
  snapshot->player.in_duck_jump = localplayer->get_in_duck_jump();
  snapshot->player.duck_time = localplayer->get_duck_time();
  snapshot->player.duck_jump_time = localplayer->get_duck_jump_time();
  snapshot->player.fall_velocity = localplayer->get_fall_velocity();

  snapshot->globals.curtime = global_vars->curtime;
  snapshot->globals.frametime = global_vars->frametime;
  snapshot->globals.tickcount = global_vars->tickcount;
  snapshot->globals.first_time_predicted = prediction->first_time_predicted;
  snapshot->globals.in_prediction = prediction->in_prediction;
  snapshot->globals.random_seed_value = random_seed != nullptr ? static_cast<int>(*random_seed) : -1;
  return true;
}

inline bool local_prediction_restore(Player* localplayer, const LocalPredictionSnapshot& snapshot) {
  if (localplayer == nullptr || global_vars == nullptr || prediction == nullptr) return false;

  localplayer->set_origin(snapshot.player.origin);
  localplayer->set_abs_origin(snapshot.player.abs_origin);
  localplayer->set_velocity(snapshot.player.velocity);
  localplayer->set_base_velocity(snapshot.player.base_velocity);
  localplayer->set_view_offset(snapshot.player.view_offset);
  localplayer->set_flags(snapshot.player.flags);
  localplayer->set_ground_entity_handle(snapshot.player.ground_entity_handle);
  localplayer->set_tickbase(snapshot.player.tickbase);
  localplayer->set_current_cmd(snapshot.player.current_cmd);
  localplayer->set_ducked(snapshot.player.ducked);
  localplayer->set_ducking_state(snapshot.player.ducking);
  localplayer->set_in_duck_jump(snapshot.player.in_duck_jump);
  localplayer->set_duck_time(snapshot.player.duck_time);
  localplayer->set_duck_jump_time(snapshot.player.duck_jump_time);
  localplayer->set_fall_velocity(snapshot.player.fall_velocity);

  global_vars->curtime = snapshot.globals.curtime;
  global_vars->frametime = snapshot.globals.frametime;
  global_vars->tickcount = snapshot.globals.tickcount;
  prediction->first_time_predicted = snapshot.globals.first_time_predicted;
  prediction->in_prediction = snapshot.globals.in_prediction;
  if (random_seed != nullptr) {
    *random_seed = snapshot.globals.random_seed_value;
  }
  return true;
}

inline bool local_prediction_run(Player* localplayer, const LocalPredictionRequest& request, LocalPredictionResult* result) {
  static bool local_prediction_active = false;
  local_prediction_debug = {};
  if (result != nullptr) {
    *result = {};
  }
  if (localplayer == nullptr || prediction == nullptr || game_movement == nullptr || move_helper == nullptr || global_vars == nullptr) {
    local_prediction_debug.last_failure_stage = 1;
    return false;
  }
  if (request.ticks.empty() || local_prediction_active) {
    local_prediction_debug.last_failure_stage = 2;
    return false;
  }

  LocalPredictionSnapshot snapshot{};
  if (!local_prediction_capture(localplayer, &snapshot)) {
    local_prediction_debug.last_failure_stage = 3;
    return false;
  }

  local_prediction_active = true;

  for (size_t tick_index = 0; tick_index < request.ticks.size(); ++tick_index) {
    const LocalPredictionTick& step = request.ticks[tick_index];
    user_cmd cmd = step.cmd;
    if (cmd.command_number == 0) {
      cmd.command_number = snapshot.globals.tickcount + static_cast<int>(tick_index) + 1;
    }
    if (cmd.tick_count <= 0) {
      cmd.tick_count = snapshot.globals.tickcount + static_cast<int>(tick_index) + 1;
    }
    cmd.has_been_predicted = false;

    localplayer->set_tickbase(snapshot.player.tickbase + static_cast<int>(tick_index));
    localplayer->set_current_cmd(&cmd);
    if (random_seed != nullptr) {
      *random_seed = MD5_PseudoRandom(static_cast<unsigned int>(cmd.command_number)) & INT_MAX;
    }

    global_vars->curtime = localplayer->get_tickbase() * TICK_INTERVAL;
    global_vars->frametime = prediction->engine_paused ? 0.0f : TICK_INTERVAL;
    global_vars->tickcount = localplayer->get_tickbase();
    prediction->first_time_predicted = false;
    prediction->in_prediction = true;
    prediction->set_local_view_angles(cmd.view_angles);

    move_helper->set_host(localplayer);
    prediction->run_command(localplayer, &cmd, move_helper);
    move_helper->set_host(nullptr);

    if (result != nullptr) {
      LocalPredictionTickResult tick_result{};
      tick_result.cmd = cmd;
      tick_result.origin = localplayer->get_origin();
      tick_result.velocity = localplayer->get_velocity();
      tick_result.grounded = localplayer->is_on_ground();
      result->ticks.push_back(tick_result);
      result->simulated_ticks = static_cast<int>(result->ticks.size());
    }
  }

  local_prediction_debug.last_simulated_ticks = static_cast<int>(request.ticks.size());
  local_prediction_debug.last_restore_ok = local_prediction_restore(localplayer, snapshot);
  local_prediction_active = false;
  if (result != nullptr) {
    result->success = local_prediction_debug.last_restore_ok;
    result->restored = local_prediction_debug.last_restore_ok;
  }
  local_prediction_debug.last_run_ok = local_prediction_debug.last_restore_ok;
  return local_prediction_debug.last_restore_ok;
}

struct LocalPredictionEntityHistorySample {
  Vec3 origin{};
  Vec3 velocity{};
  float sim_time = 0.0f;
  float curtime = 0.0f;
  int flags = 0;
  bool ducking = false;
  bool on_ground = false;
};

struct LocalPredictionEntityHistory {
  int ent_index = 0;
  int sample_count = 0;
  std::array<LocalPredictionEntityHistorySample, 48> samples{};
};

inline static std::array<LocalPredictionEntityHistory, 2049> local_prediction_entity_history{};

inline void local_prediction_record_entity(Entity* entity) {
  if (entity == nullptr || global_vars == nullptr) return;
  int ent_index = entity->get_index();
  if (ent_index <= 0 || ent_index >= static_cast<int>(local_prediction_entity_history.size())) return;

  LocalPredictionEntityHistory& history = local_prediction_entity_history[ent_index];
  LocalPredictionEntityHistorySample sample{};
  sample.origin = entity->get_origin();
  sample.sim_time = entity->get_simulation_time();
  sample.curtime = global_vars->curtime;
  if (entity->get_class_id() == class_id::PLAYER) {
    Player* player = static_cast<Player*>(entity);
    sample.velocity = player->get_velocity();
    sample.flags = player->get_flags();
    sample.ducking = player->is_ducking();
    sample.on_ground = player->is_on_ground();
  }

  if (history.sample_count > 0) {
    const LocalPredictionEntityHistorySample& last = history.samples[0];
    if (last.sim_time == sample.sim_time && distance_squared_2d(last.origin, sample.origin) < 0.0001f &&
        std::fabs(last.origin.z - sample.origin.z) < 0.0001f) {
      return;
    }
  }

  history.ent_index = ent_index;
  int shift_count = std::min(history.sample_count, static_cast<int>(history.samples.size()) - 1);
  for (int index = shift_count; index > 0; --index) {
    history.samples[index] = history.samples[index - 1];
  }
  history.samples[0] = sample;
  history.sample_count = std::min(history.sample_count + 1, static_cast<int>(history.samples.size()));
}

inline Vec3 local_prediction_estimate_entity_velocity(Entity* entity) {
  if (entity == nullptr) return Vec3{};
  int ent_index = entity->get_index();
  if (ent_index <= 0 || ent_index >= static_cast<int>(local_prediction_entity_history.size())) return Vec3{};

  const LocalPredictionEntityHistory& history = local_prediction_entity_history[ent_index];
  if (history.sample_count < 2) return Vec3{};

  const LocalPredictionEntityHistorySample& newest = history.samples[0];
  int reference_index = std::min(history.sample_count - 1, 3);
  const LocalPredictionEntityHistorySample& oldest = history.samples[reference_index];
  float delta_time = newest.sim_time - oldest.sim_time;
  if (delta_time <= 0.0001f) {
    delta_time = newest.curtime - oldest.curtime;
  }
  if (delta_time <= 0.0001f) return Vec3{};

  Vec3 velocity{};
  velocity.x = (newest.origin.x - oldest.origin.x) / delta_time;
  velocity.y = (newest.origin.y - oldest.origin.y) / delta_time;
  velocity.z = (newest.origin.z - oldest.origin.z) / delta_time;
  return velocity;
}

struct LocalPredictionEntityPath {
  bool valid = false;
  bool used_movement_sim = false;
  bool used_game_engine_movement = false;
  bool used_strafe_prediction = false;
  float start_time = 0.0f;
  float time_step = TICK_INTERVAL;
  float average_yaw = 0.0f;
  float strafe_confidence = 0.0f;
  Vec3 final_velocity{};
  std::vector<Vec3> positions{};
};

inline float local_prediction_velocity_2d_length(const Vec3& velocity) {
  return std::sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y));
}

inline float local_prediction_normalize_yaw(float yaw) {
  return std::remainder(yaw, 360.0f);
}

inline float local_prediction_vector_yaw(const Vec3& velocity) {
  return std::atan2(velocity.y, velocity.x) * radpi;
}

inline float local_prediction_estimate_sim_delta(Entity* entity) {
  if (entity == nullptr) {
    return TICK_INTERVAL;
  }

  int ent_index = entity->get_index();
  if (ent_index <= 0 || ent_index >= static_cast<int>(local_prediction_entity_history.size())) {
    return TICK_INTERVAL;
  }

  const LocalPredictionEntityHistory& history = local_prediction_entity_history[ent_index];
  float sim_delta_sum = 0.0f;
  int sim_delta_count = 0;
  for (int index = 1; index < history.sample_count; ++index) {
    float delta = history.samples[index - 1].sim_time - history.samples[index].sim_time;
    if (delta > 0.0001f) {
      sim_delta_sum += delta;
      ++sim_delta_count;
    }
  }

  if (sim_delta_count <= 0) {
    return TICK_INTERVAL;
  }

  return std::clamp(sim_delta_sum / static_cast<float>(sim_delta_count), static_cast<float>(TICK_INTERVAL), 0.25f);
}

inline int local_prediction_time_to_ticks(float time) {
  const float tick_interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : static_cast<float>(TICK_INTERVAL);
  return static_cast<int>(0.5f + (time / std::max(tick_interval, 0.0001f)));
}

inline float local_prediction_ticks_to_time(int ticks) {
  const float tick_interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : static_cast<float>(TICK_INTERVAL);
  return static_cast<float>(ticks) * tick_interval;
}

inline float local_prediction_interp_time() {
  static Convar* cl_interp = nullptr;
  static Convar* cl_interp_ratio = nullptr;
  static Convar* cl_updaterate = nullptr;
  if (convar_system != nullptr) {
    if (cl_interp == nullptr) {
      cl_interp = convar_system->find_var("cl_interp");
    }
    if (cl_interp_ratio == nullptr) {
      cl_interp_ratio = convar_system->find_var("cl_interp_ratio");
    }
    if (cl_updaterate == nullptr) {
      cl_updaterate = convar_system->find_var("cl_updaterate");
    }
  }

  const float interp = cl_interp != nullptr ? cl_interp->get_float() : 0.0f;
  const float ratio = cl_interp_ratio != nullptr ? cl_interp_ratio->get_float() : 1.0f;
  const float update_rate = cl_updaterate != nullptr ? cl_updaterate->get_float() : 66.0f;
  return std::max(interp, ratio / std::max(update_rate, 1.0f));
}

inline float local_prediction_net_latency_flow(int flow) {
  if (client_state == nullptr || client_state->m_NetChannel == nullptr) {
    return 0.0f;
  }

  void** vtable = *reinterpret_cast<void***>(client_state->m_NetChannel);
  if (vtable == nullptr) {
    return 0.0f;
  }

  auto get_latency_fn = reinterpret_cast<float (*)(void*, int)>(vtable[9]);
  if (get_latency_fn == nullptr) {
    return 0.0f;
  }

  const float latency = get_latency_fn(client_state->m_NetChannel, flow);
  return std::isfinite(latency) ? std::clamp(latency, 0.0f, 1.0f) : 0.0f;
}

inline float local_prediction_outgoing_latency() {
  constexpr int flow_outgoing = 0;
  return local_prediction_net_latency_flow(flow_outgoing);
}

inline float local_prediction_incoming_latency() {
  constexpr int flow_incoming = 1;
  return local_prediction_net_latency_flow(flow_incoming);
}

inline float local_prediction_net_latency() {
  return std::clamp(local_prediction_outgoing_latency() + local_prediction_incoming_latency(), 0.0f, 1.0f);
}

inline float local_prediction_estimate_entity_lag(Entity* entity) {
  if (entity == nullptr || global_vars == nullptr) {
    return 0.0f;
  }

  const int ent_index = entity->get_index();
  if (ent_index <= 0 || ent_index >= static_cast<int>(local_prediction_entity_history.size())) {
    return 0.0f;
  }

  const LocalPredictionEntityHistory& history = local_prediction_entity_history[ent_index];
  if (history.sample_count <= 0) {
    return 0.0f;
  }

  return std::clamp(global_vars->curtime - history.samples[0].sim_time, 0.0f, 0.6f);
}

inline int local_prediction_network_lead_ticks(Entity* entity) {
  const float lead_time = local_prediction_outgoing_latency() +
    local_prediction_interp_time() +
    (local_prediction_estimate_entity_lag(entity) * 0.5f);
  return std::clamp(
    static_cast<int>(local_prediction_time_to_ticks(lead_time)),
    0,
    std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420) / 2);
}

inline float local_prediction_estimate_average_yaw_step(Player* player) {
  if (player == nullptr) {
    return 0.0f;
  }

  int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= static_cast<int>(local_prediction_entity_history.size())) {
    return 0.0f;
  }

  const LocalPredictionEntityHistory& history = local_prediction_entity_history[ent_index];
  float yaw_sum = 0.0f;
  int yaw_count = 0;

  for (int index = 1; index < history.sample_count; ++index) {
    const LocalPredictionEntityHistorySample& newer = history.samples[index - 1];
    const LocalPredictionEntityHistorySample& older = history.samples[index];
    if (newer.on_ground != older.on_ground) {
      continue;
    }

    const float newer_speed = local_prediction_velocity_2d_length(newer.velocity);
    const float older_speed = local_prediction_velocity_2d_length(older.velocity);
    if (newer_speed < 10.0f || older_speed < 10.0f) {
      continue;
    }

    float sim_delta = newer.sim_time - older.sim_time;
    if (sim_delta <= 0.0001f) {
      sim_delta = newer.curtime - older.curtime;
    }
    if (sim_delta <= 0.0001f) {
      continue;
    }

    const int tick_delta = std::max(1, static_cast<int>(std::round(sim_delta / TICK_INTERVAL)));
    const float newer_yaw = local_prediction_vector_yaw(newer.velocity);
    const float older_yaw = local_prediction_vector_yaw(older.velocity);
    const float yaw_delta = local_prediction_normalize_yaw(newer_yaw - older_yaw) / static_cast<float>(tick_delta);
    if (std::fabs(yaw_delta) > 45.0f) {
      continue;
    }

    yaw_sum += yaw_delta;
    ++yaw_count;
  }

  if (yaw_count < 2) {
    return 0.0f;
  }

  return yaw_sum / static_cast<float>(yaw_count);
}

struct local_prediction_strafe_estimate {
  float yaw_step = 0.0f;
  float confidence = 0.0f;
  bool valid = false;
};

inline float local_prediction_dot_2d(const Vec3& left, const Vec3& right) {
  return (left.x * right.x) + (left.y * right.y);
}

inline Vec3 local_prediction_yaw_to_forward(float yaw) {
  const float radians = yaw * pideg;
  return Vec3{std::cos(radians), std::sin(radians), 0.0f};
}

inline Vec3 local_prediction_rotate_2d(const Vec3& value, float yaw_delta) {
  const float radians = yaw_delta * pideg;
  const float sine = std::sin(radians);
  const float cosine = std::cos(radians);
  return Vec3{
    (value.x * cosine) - (value.y * sine),
    (value.x * sine) + (value.y * cosine),
    value.z
  };
}

inline local_prediction_strafe_estimate local_prediction_estimate_strafe(Player* player) {
  local_prediction_strafe_estimate estimate{};
  if (player == nullptr || !config.aimbot.projectile_strafe_prediction) {
    return estimate;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= static_cast<int>(local_prediction_entity_history.size())) {
    return estimate;
  }

  const LocalPredictionEntityHistory& history = local_prediction_entity_history[ent_index];
  if (history.sample_count < 5) {
    return estimate;
  }

  const bool current_air = !history.samples[0].on_ground;
  const float current_speed = local_prediction_velocity_2d_length(history.samples[0].velocity);
  const int wanted_samples = current_air ? 10 : 8;
  const float straight_fuzzy_value = current_air ? 56.0f : 84.0f;
  const int max_sign_changes = current_air ? 3 : 1;

  float yaw_sum = 0.0f;
  int yaw_ticks = 0;
  int valid_pairs = 0;
  int sign_changes = 0;
  int last_sign = 0;

  const int sample_limit = std::min(history.sample_count, wanted_samples);
  for (int index = 1; index < sample_limit; ++index) {
    const auto& newer = history.samples[index - 1];
    const auto& older = history.samples[index];
    if (newer.on_ground != older.on_ground) {
      continue;
    }

    const float newer_speed = local_prediction_velocity_2d_length(newer.velocity);
    const float older_speed = local_prediction_velocity_2d_length(older.velocity);
    if (newer_speed < 40.0f || older_speed < 40.0f) {
      continue;
    }

    float delta_time = newer.sim_time - older.sim_time;
    if (delta_time <= 0.0001f) {
      delta_time = newer.curtime - older.curtime;
    }
    if (delta_time <= 0.0001f) {
      continue;
    }

    const int tick_delta = std::max(1, static_cast<int>(std::round(delta_time / TICK_INTERVAL)));
    const float yaw_delta = local_prediction_normalize_yaw(
      local_prediction_vector_yaw(newer.velocity) - local_prediction_vector_yaw(older.velocity));
    const float yaw_per_tick = yaw_delta / static_cast<float>(tick_delta);
    if (std::fabs(yaw_per_tick) > 45.0f) {
      continue;
    }

    const bool straight = std::fabs(yaw_delta) * newer_speed * static_cast<float>(tick_delta) < straight_fuzzy_value;
    if (straight) {
      continue;
    }

    const int sign = yaw_per_tick > 0.0f ? 1 : -1;
    if (last_sign != 0 && sign != last_sign) {
      ++sign_changes;
      if (sign_changes > max_sign_changes) {
        break;
      }
    }

    last_sign = sign;
    yaw_sum += yaw_per_tick * static_cast<float>(tick_delta);
    yaw_ticks += tick_delta;
    ++valid_pairs;
  }

  if (valid_pairs < 3 || yaw_ticks <= 0) {
    return estimate;
  }

  estimate.yaw_step = yaw_sum / static_cast<float>(yaw_ticks);
  estimate.confidence = std::clamp((static_cast<float>(valid_pairs) / static_cast<float>(std::max(3, sample_limit - 1))) * 100.0f, 0.0f, 100.0f);
  float required_confidence = std::clamp(config.aimbot.projectile_strafe_confidence, 0.0f, 100.0f);
  if (current_air) {
    required_confidence = std::max(required_confidence, 58.0f);
  }
  if (current_speed > 700.0f) {
    required_confidence = std::max(required_confidence, current_air ? 70.0f : 70.0f);
  }
  if (current_speed > 1100.0f) {
    required_confidence = std::max(required_confidence, current_air ? 80.0f : 88.0f);
  }

  if (current_air && current_speed > 650.0f) {
    const float confidence_scale = std::clamp(estimate.confidence / 100.0f, 0.35f, 0.85f);
    estimate.yaw_step *= confidence_scale;
  }

  estimate.valid = std::fabs(estimate.yaw_step) >= 0.12f &&
    estimate.confidence >= required_confidence;
  return estimate;
}

inline float local_prediction_estimated_max_speed(Player* player, const Vec3& velocity) {
  if (player == nullptr) {
    return 300.0f;
  }

  constexpr float max_prediction_speed = 3500.0f;
  const float observed_speed = local_prediction_velocity_2d_length(velocity);
  const float netvar_max_speed = player->get_max_speed();
  if (netvar_max_speed > 1.0f && netvar_max_speed < 1000.0f) {
    return std::clamp(std::max(netvar_max_speed, observed_speed), 1.0f, max_prediction_speed);
  }

  float base_speed = 300.0f;
  switch (player->get_tf_class()) {
  case tf_class::SCOUT:
    base_speed = 400.0f;
    break;
  case tf_class::SOLDIER:
    base_speed = 240.0f;
    break;
  case tf_class::DEMOMAN:
    base_speed = 280.0f;
    break;
  case tf_class::MEDIC:
  case tf_class::SPY:
    base_speed = 320.0f;
    break;
  case tf_class::HEAVYWEAPONS:
    base_speed = 230.0f;
    break;
  default:
    break;
  }

  return std::clamp(std::max(base_speed, observed_speed), 1.0f, max_prediction_speed);
}

inline void local_prediction_fill_move_from_velocity(MoveData* move, const Vec3& velocity, float view_yaw, float max_speed) {
  if (move == nullptr) {
    return;
  }

  const Vec3 forward = local_prediction_yaw_to_forward(view_yaw);
  const Vec3 right{-forward.y, forward.x, 0.0f};
  Vec3 wish_velocity{velocity.x, velocity.y, 0.0f};
  const float wish_speed = local_prediction_velocity_2d_length(wish_velocity);
  if (wish_speed > max_speed && wish_speed > 0.0001f) {
    wish_velocity = wish_velocity * (max_speed / wish_speed);
  }

  move->m_flForwardMove = std::clamp(local_prediction_dot_2d(wish_velocity, forward), -max_speed, max_speed);
  move->m_flSideMove = std::clamp(local_prediction_dot_2d(wish_velocity, right), -max_speed, max_speed);
  move->m_flUpMove = 0.0f;
}

inline int local_prediction_configured_path_ticks() {
  return std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420);
}

inline int local_prediction_path_tick_count(float horizon_seconds) {
  return std::clamp(
    static_cast<int>(std::ceil(horizon_seconds / TICK_INTERVAL)),
    1,
    local_prediction_configured_path_ticks());
}

inline bool local_prediction_simulate_player_path(Player* player,
  float horizon_seconds,
  LocalPredictionEntityPath* path_out,
  int lead_ticks = 0,
  int max_sim_ticks = INT_MAX) {
  if (player == nullptr || path_out == nullptr || prediction == nullptr || game_movement == nullptr || move_helper == nullptr || global_vars == nullptr) {
    return false;
  }

  if (entity_list == nullptr || player != entity_list->get_localplayer()) {
    return false;
  }

  if (horizon_seconds <= 0.0f || max_sim_ticks <= 0) {
    return false;
  }

  static bool movement_sim_active = false;
  if (movement_sim_active) {
    return false;
  }

  LocalPredictionSnapshot snapshot{};
  if (!local_prediction_capture(player, &snapshot)) {
    return false;
  }

  Vec3 initial_velocity = snapshot.player.velocity;

  movement_sim_active = true;
  *path_out = {};
  path_out->time_step = TICK_INTERVAL;
  path_out->start_time = local_prediction_ticks_to_time(lead_ticks);
  path_out->used_movement_sim = true;
  path_out->used_game_engine_movement = true;
  const int requested_tick_count = local_prediction_path_tick_count(horizon_seconds);
  int tick_count = std::min(requested_tick_count, max_sim_ticks);
  tick_count = std::clamp(tick_count, 1, requested_tick_count);
  path_out->positions.reserve(static_cast<size_t>(tick_count) + 1);
  if (lead_ticks <= 0) {
    path_out->positions.push_back(snapshot.player.origin);
  }

  MoveData move{};
  move.m_bFirstRunOfFunctions = false;
  move.m_bGameCodeMovedPlayer = false;
  move.m_nPlayerHandle = player->get_index();
  move.SetAbsOrigin(snapshot.player.origin);
  move.m_vecVelocity = initial_velocity;

  const float observed_speed = local_prediction_velocity_2d_length(initial_velocity);
  const float max_speed = local_prediction_estimated_max_speed(player, initial_velocity);
  move.m_flMaxSpeed = max_speed;
  move.m_flClientMaxSpeed = max_speed;

  float view_yaw = observed_speed > 1.0f ? local_prediction_vector_yaw(initial_velocity) : 0.0f;
  const local_prediction_strafe_estimate strafe_estimate = local_prediction_estimate_strafe(player);
  const float yaw_step = strafe_estimate.valid ? strafe_estimate.yaw_step : local_prediction_estimate_average_yaw_step(player);
  path_out->average_yaw = yaw_step;
  path_out->strafe_confidence = strafe_estimate.confidence;
  path_out->used_strafe_prediction = strafe_estimate.valid;

  move.m_vecViewAngles = {0.0f, view_yaw, 0.0f};
  move.m_vecAbsViewAngles = move.m_vecViewAngles;
  move.m_vecAngles = move.m_vecViewAngles;
  move.m_vecOldAngles = move.m_vecViewAngles;
  move.m_nButtons = snapshot.player.ducked ? IN_DUCK : 0;
  move.m_nOldButtons = move.m_nButtons;
  local_prediction_fill_move_from_velocity(&move, initial_velocity, view_yaw, max_speed);

  user_cmd dummy_cmd{};
  dummy_cmd.command_number = snapshot.globals.tickcount + 1;
  dummy_cmd.tick_count = snapshot.globals.tickcount + 1;
  dummy_cmd.buttons = move.m_nButtons;

  move_helper->set_host(player);
  player->set_current_cmd(&dummy_cmd);

  bool movement_ok = true;
  for (int tick = 0; tick < tick_count + lead_ticks; ++tick) {
    global_vars->curtime = (snapshot.player.tickbase + tick) * TICK_INTERVAL;
    global_vars->frametime = prediction->engine_paused ? 0.0f : TICK_INTERVAL;
    global_vars->tickcount = snapshot.globals.tickcount + tick;
    prediction->first_time_predicted = false;
    prediction->in_prediction = true;

    if (std::fabs(yaw_step) > 0.01f) {
      view_yaw = local_prediction_normalize_yaw(view_yaw + yaw_step);
      move.m_vecVelocity = local_prediction_rotate_2d(move.m_vecVelocity, yaw_step);
      local_prediction_fill_move_from_velocity(&move, move.m_vecVelocity, view_yaw, max_speed);
    } else if (!player->is_on_ground() && observed_speed <= 1.0f) {
      move.m_flForwardMove = 0.0f;
      move.m_flSideMove = 0.0f;
    }

    move.m_vecViewAngles = {0.0f, view_yaw, 0.0f};
    move.m_vecAbsViewAngles = move.m_vecViewAngles;
    move.m_vecAngles = move.m_vecViewAngles;
    move.m_vecOldAngles = move.m_vecViewAngles;
    dummy_cmd.view_angles = move.m_vecViewAngles;
    dummy_cmd.buttons = move.m_nButtons;

    if (!game_movement->process_movement(player, &move)) {
      movement_ok = false;
      break;
    }

    player->set_origin(move.GetAbsOrigin());
    player->set_abs_origin(move.GetAbsOrigin());
    player->set_velocity(move.m_vecVelocity);
    if (tick + 1 >= lead_ticks) {
      path_out->positions.push_back(move.GetAbsOrigin());
    }
  }

  path_out->final_velocity = move.m_vecVelocity;
  path_out->valid = movement_ok && path_out->positions.size() > 1;

  move_helper->set_host(nullptr);
  local_prediction_restore(player, snapshot);
  movement_sim_active = false;
  return path_out->valid;
}

inline float local_prediction_projectile_gravity_scale();

inline float local_prediction_dot_3d(const Vec3& left, const Vec3& right) {
  return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
}

inline Vec3 local_prediction_clip_velocity(const Vec3& velocity, const Vec3& normal, float overbounce = 1.0f) {
  const float backoff = local_prediction_dot_3d(velocity, normal) * overbounce;
  Vec3 clipped = velocity - (normal * backoff);
  if (std::fabs(clipped.x) < 0.001f) clipped.x = 0.0f;
  if (std::fabs(clipped.y) < 0.001f) clipped.y = 0.0f;
  if (std::fabs(clipped.z) < 0.001f) clipped.z = 0.0f;
  return clipped;
}

struct local_prediction_lightweight_state {
  Vec3 origin{};
  Vec3 velocity{};
  Vec3 mins{};
  Vec3 maxs{};
  bool grounded = false;
  bool swimming = false;
  bool has_move_input = false;
};

inline bool local_prediction_trace_hull(const Vec3& start,
  const Vec3& end,
  const Vec3& mins,
  const Vec3& maxs,
  Entity* skip_entity,
  trace_t* trace_out) {
  if (engine_trace == nullptr || trace_out == nullptr) {
    return false;
  }

  ray_t ray = engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end), const_cast<Vec3*>(&mins), const_cast<Vec3*>(&maxs));
  trace_filter filter{};
  engine_trace->init_trace_filter(&filter, skip_entity);
  engine_trace->trace_ray(&ray, MASK_PLAYERSOLID, &filter, trace_out);
  return trace_out->fraction < 1.0f || trace_out->start_solid || trace_out->all_solid;
}

inline bool local_prediction_find_ground(local_prediction_lightweight_state* state, Entity* skip_entity) {
  if (state == nullptr) {
    return false;
  }

  // do not snap a rising target back to the floor; jumping/rocket-jumping/airblasted players have
  // upward z velocity and treating them as grounded zeros that velocity, ruining airborne prediction
  constexpr float upward_velocity_epsilon = 16.0f;
  if (state->velocity.z > upward_velocity_epsilon) {
    return false;
  }

  // shorten the probe when the target is clearly mid-air; only snap when actually settling on a surface
  const float ground_probe = state->grounded ? 18.0f : 4.0f;
  const Vec3 end = state->origin - Vec3{0.0f, 0.0f, ground_probe};
  trace_t trace{};
  if (!local_prediction_trace_hull(state->origin, end, state->mins, state->maxs, skip_entity, &trace)) {
    return false;
  }

  if (trace.start_solid || trace.all_solid || trace.plane.normal.z < 0.65f) {
    return false;
  }

  if (trace.fraction >= 1.0f) {
    return false;
  }

  state->origin = trace.endpos;
  state->velocity.z = 0.0f;
  return true;
}

inline Vec3 local_prediction_client_mv_horizontal_wish_dir(const local_prediction_lightweight_state* state, float move_yaw_deg) {
  if (state == nullptr) {
    return Vec3{1.0f, 0.0f, 0.0f};
  }

  const float speed_2d = local_prediction_velocity_2d_length(state->velocity);
  if (speed_2d > 8.0f) {
    return Vec3{state->velocity.x / speed_2d, state->velocity.y / speed_2d, 0.0f};
  }

  return local_prediction_yaw_to_forward(move_yaw_deg);
}

inline float local_prediction_client_mv_sv_gravity() {
  static Convar* sv_gravity = nullptr;
  if (sv_gravity == nullptr && convar_system != nullptr) {
    sv_gravity = convar_system->find_var("sv_gravity");
  }
  const float g = sv_gravity != nullptr ? sv_gravity->get_float() : 800.0f;
  return std::clamp(g, 100.0f, 2000.0f);
}

inline float local_prediction_client_mv_sv_friction() {
  static Convar* v = nullptr;
  if (v == nullptr && convar_system != nullptr) {
    v = convar_system->find_var("sv_friction");
  }
  return v != nullptr ? std::clamp(v->get_float(), 0.0f, 10.0f) : 1.0f;
}

inline float local_prediction_client_mv_sv_stopspeed() {
  static Convar* v = nullptr;
  if (v == nullptr && convar_system != nullptr) {
    v = convar_system->find_var("sv_stopspeed");
  }
  return v != nullptr ? std::clamp(v->get_float(), 1.0f, 1000.0f) : 100.0f;
}

inline float local_prediction_client_mv_sv_airaccelerate() {
  static Convar* v = nullptr;
  if (v == nullptr && convar_system != nullptr) {
    v = convar_system->find_var("sv_airaccelerate");
  }
  return v != nullptr ? std::clamp(v->get_float(), 0.0f, 150.0f) : 10.0f;
}

// i dont know tf2 says this in the source :thumbsup: white heart white heart
inline float local_prediction_client_mv_get_air_speed_cap() {
  return 30.0f;
}

inline float local_prediction_air_max_yaw_rate_per_tick(float horizontal_speed, float max_speed, float dt) {
  if (horizontal_speed <= 1.0f || dt <= 0.0f) {
    return 30.0f;
  }
  const float airaccel = local_prediction_client_mv_sv_airaccelerate();
  const float air_cap = local_prediction_client_mv_get_air_speed_cap();
  const float wish_for_accel = std::max(max_speed, 1.0f);
  const float accel_speed = airaccel * wish_for_accel * dt;
  const float max_perp = std::min(accel_speed, air_cap);
  if (max_perp <= 0.0f) {
    return 0.0f;
  }
  const float radians = std::atan2(max_perp, horizontal_speed);
  return radians / pideg;
}

inline void local_prediction_client_mv_apply_friction(local_prediction_lightweight_state* state, float dt) {
  if (state == nullptr || !state->grounded || state->swimming || dt <= 0.0f) {
    return;
  }

  const float speed = local_prediction_velocity_2d_length(state->velocity);
  if (speed < 0.1f) {
    return;
  }

  constexpr float surface_friction = 1.0f;
  const float friction = local_prediction_client_mv_sv_friction() * surface_friction;
  const float stop_speed = local_prediction_client_mv_sv_stopspeed();
  const float control = speed < stop_speed ? stop_speed : speed;
  const float new_speed = speed - (dt * control * friction);
  if (new_speed <= 0.0f) {
    state->velocity.x = 0.0f;
    state->velocity.y = 0.0f;
    return;
  }

  const float scale = new_speed / speed;
  state->velocity.x *= scale;
  state->velocity.y *= scale;
}

inline float local_prediction_client_mv_sv_accelerate() {
  static Convar* v = nullptr;
  if (v == nullptr && convar_system != nullptr) {
    v = convar_system->find_var("sv_accelerate");
  }
  return v != nullptr ? std::clamp(v->get_float(), 0.0f, 50.0f) : 10.0f;
}

inline void local_prediction_client_mv_accelerate_along_wish(
  local_prediction_lightweight_state* state,
  float dt,
  float max_speed,
  float accel_scale,
  const Vec3& wish_dir,
  float wish_speed_cap = 0.0f) {
  if (state == nullptr || dt <= 0.0f || accel_scale <= 0.0f) {
    return;
  }

  const float wish_speed = max_speed;
  const float wish_speed_for_add = wish_speed_cap > 0.0f
    ? std::min(wish_speed, wish_speed_cap)
    : wish_speed;
  const float current_speed = local_prediction_dot_2d(state->velocity, wish_dir);
  const float add_speed = wish_speed_for_add - current_speed;
  if (add_speed <= 0.0f) {
    return;
  }

  float accel_speed = accel_scale * wish_speed * dt;
  if (accel_speed > add_speed) {
    accel_speed = add_speed;
  }

  state->velocity.x += wish_dir.x * accel_speed;
  state->velocity.y += wish_dir.y * accel_speed;

  if (wish_speed_cap > 0.0f) {
    return;
  }
  const float new_2d = local_prediction_velocity_2d_length(state->velocity);
  const float cap = max_speed * 1.15f;
  if (new_2d > cap && new_2d > 0.001f) {
    const float scale = cap / new_2d;
    state->velocity.x *= scale;
    state->velocity.y *= scale;
  }
}

inline void local_prediction_player_path_tick(local_prediction_lightweight_state* state,
  Entity* skip_entity,
  float dt,
  float yaw_step,
  bool with_hull_trace,
  float max_speed,
  float* move_yaw_deg) {
  if (state == nullptr || skip_entity == nullptr || dt <= 0.0f || move_yaw_deg == nullptr) {
    return;
  }

  if (state->grounded && !state->swimming && state->velocity.z > 16.0f) {
    state->grounded = false;
  }

  if (!state->grounded && !state->swimming) {
    const float h_speed = local_prediction_velocity_2d_length(state->velocity);
    const float max_air_yaw = local_prediction_air_max_yaw_rate_per_tick(h_speed, max_speed, dt);
    if (max_air_yaw > 0.0f) {
      if (yaw_step > max_air_yaw) yaw_step = max_air_yaw;
      if (yaw_step < -max_air_yaw) yaw_step = -max_air_yaw;
    }
  }

  local_prediction_client_mv_apply_friction(state, dt);

  if (std::fabs(yaw_step) > 0.01f && local_prediction_velocity_2d_length(state->velocity) > 20.0f) {
    state->velocity = local_prediction_rotate_2d(state->velocity, yaw_step);
  }

  *move_yaw_deg = local_prediction_normalize_yaw(*move_yaw_deg + yaw_step);

  const Vec3 wish_dir = local_prediction_client_mv_horizontal_wish_dir(state, *move_yaw_deg);

  if (state->has_move_input && state->grounded && !state->swimming) {
    local_prediction_client_mv_accelerate_along_wish(
      state,
      dt,
      max_speed,
      local_prediction_client_mv_sv_accelerate(),
      wish_dir,
      0.0f);
  } else if (state->has_move_input && !state->grounded && !state->swimming) {
    local_prediction_client_mv_accelerate_along_wish(
      state,
      dt,
      max_speed,
      local_prediction_client_mv_sv_airaccelerate(),
      wish_dir,
      local_prediction_client_mv_get_air_speed_cap());
  }

  const float gravity = state->swimming ? 0.0f : local_prediction_client_mv_sv_gravity();
  if (!state->grounded && !state->swimming) {
    state->velocity.z -= 0.5f * gravity * dt;  // StartGravity
  }

  Vec3 end = state->origin;
  if (state->grounded || state->swimming) {
    end += Vec3{state->velocity.x * dt, state->velocity.y * dt, 0.0f};
  } else {
    end += Vec3{
      state->velocity.x * dt,
      state->velocity.y * dt,
      state->velocity.z * dt
    };
  }

  if (!with_hull_trace) {
    state->origin = end;
    if (!state->grounded && !state->swimming) {
      state->velocity.z -= 0.5f * gravity * dt;  // FinishGravity
    }
    return;
  }

  const Vec3 saved_origin = state->origin;
  const Vec3 saved_velocity = state->velocity;
  const bool was_grounded = state->grounded;

  trace_t move_trace{};
  const bool any_block = local_prediction_trace_hull(state->origin, end, state->mins, state->maxs, skip_entity, &move_trace);

  if (any_block) {
    if (!move_trace.start_solid && !move_trace.all_solid) {
      state->origin = move_trace.endpos;
    }

    const float remaining = std::clamp(1.0f - move_trace.fraction, 0.0f, 1.0f);
    if (move_trace.plane.normal.z >= 0.65f) {
      state->grounded = true;
      state->velocity.z = 0.0f;
    } else {
      state->velocity = local_prediction_clip_velocity(state->velocity, move_trace.plane.normal, 1.0f);
      if (remaining > 0.001f) {
        const Vec3 slide_end = state->origin + (state->velocity * (dt * remaining));
        trace_t slide_trace{};
        if (!local_prediction_trace_hull(state->origin, slide_end, state->mins, state->maxs, skip_entity, &slide_trace)) {
          state->origin = slide_end;
        } else if (!slide_trace.start_solid && !slide_trace.all_solid) {
          state->origin = slide_trace.endpos;
        }
      }
      state->grounded = false;
    }
  } else {
    state->origin = end;
  }

  // step-up (Source CGameMovement::StepMove style): only fires when we were grounded coming into
  // the tick and the regular move was significantly blocked (saves 3 hull traces per tick when not
  // needed). Use whichever (slide vs step-up) gets us further horizontally.
  if (with_hull_trace && was_grounded && !state->swimming && any_block && move_trace.fraction < 0.85f) {
    constexpr float step_size = 18.0f;  // TF2 m_flStepSize
    constexpr float step_down_extra = 4.0f;
    trace_t up_trace{};
    const Vec3 up_end = saved_origin + Vec3{0.0f, 0.0f, step_size};
    local_prediction_trace_hull(saved_origin, up_end, state->mins, state->maxs, skip_entity, &up_trace);
    const Vec3 stepped = (up_trace.start_solid || up_trace.all_solid) ? saved_origin : up_trace.endpos;

    const Vec3 horiz_step = Vec3{saved_velocity.x * dt, saved_velocity.y * dt, 0.0f};
    const Vec3 forward_end = stepped + horiz_step;
    trace_t fwd_trace{};
    local_prediction_trace_hull(stepped, forward_end, state->mins, state->maxs, skip_entity, &fwd_trace);
    const Vec3 after_forward = (fwd_trace.start_solid || fwd_trace.all_solid) ? stepped : fwd_trace.endpos;

    trace_t down_trace{};
    const Vec3 down_end = after_forward - Vec3{0.0f, 0.0f, step_size + step_down_extra};
    if (local_prediction_trace_hull(after_forward, down_end, state->mins, state->maxs, skip_entity, &down_trace)) {
      if (!down_trace.start_solid && !down_trace.all_solid && down_trace.plane.normal.z >= 0.65f) {
        const float step_dx = down_trace.endpos.x - saved_origin.x;
        const float step_dy = down_trace.endpos.y - saved_origin.y;
        const float step_dist_2d_sq = (step_dx * step_dx) + (step_dy * step_dy);
        const float slide_dx = state->origin.x - saved_origin.x;
        const float slide_dy = state->origin.y - saved_origin.y;
        const float slide_dist_2d_sq = (slide_dx * slide_dx) + (slide_dy * slide_dy);
        if (step_dist_2d_sq > slide_dist_2d_sq + 1.0f) {
          state->origin = down_trace.endpos;
          state->velocity.x = saved_velocity.x;
          state->velocity.y = saved_velocity.y;
          state->velocity.z = 0.0f;
          state->grounded = true;
        }
      }
    }
  }

  // FinishGravity: second half of gravity, applied after the move so post-collision vz=0 stays sane
  if (!state->grounded && !state->swimming) {
    state->velocity.z -= 0.5f * gravity * dt;
  }

  if (!state->swimming) {
    state->grounded = local_prediction_find_ground(state, skip_entity);
  }
}

inline Vec3 local_prediction_player_path_seed_velocity(Player* player) {
  if (player == nullptr) {
    return Vec3{};
  }

  const Vec3 current_velocity = player->get_velocity();
  const float current_speed = local_prediction_velocity_2d_length(current_velocity);
  const bool current_stationary =
    player->is_on_ground() &&
    current_speed < 8.0f &&
    std::fabs(current_velocity.z) < 8.0f;
  if (current_stationary) {
    return Vec3{};
  }

  if (current_speed > 1.0f || std::fabs(current_velocity.z) > 1.0f) {
    return current_velocity;
  }

  return local_prediction_estimate_entity_velocity(player);
}

inline LocalPredictionEntityPath local_prediction_build_player_path_client_sim(Player* player,
  Entity* skip_entity,
  int lead_ticks,
  int step_count,
  int hull_trace_stride) {
  LocalPredictionEntityPath path{};
  if (player == nullptr || skip_entity == nullptr || engine_trace == nullptr || step_count < 1) {
    return path;
  }

  Vec3 origin = player->get_origin();
  Vec3 velocity = local_prediction_player_path_seed_velocity(player);
  bool target_grounded = false;
  bool target_swimming = false;
  if (std::sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y) + (velocity.z * velocity.z)) <= 0.001f) {
    target_grounded = player->is_on_ground();
    target_swimming = player->get_water_level() > 1;
  } else {
    target_grounded = player->is_on_ground();
    target_swimming = player->get_water_level() > 1;
  }

  const int lead_clamped = std::max(0, lead_ticks);
  const float lead_time = local_prediction_ticks_to_time(lead_clamped);
  path.time_step = TICK_INTERVAL;
  path.start_time = lead_time;
  path.positions.reserve(static_cast<size_t>(step_count) + 1);

  local_prediction_lightweight_state state{};
  state.origin = origin;
  state.velocity = velocity;
  state.mins = player->get_player_mins(player->is_ducking());
  state.maxs = player->get_player_maxs(player->is_ducking());
  state.grounded = target_grounded || target_swimming;
  state.swimming = target_swimming;
  state.has_move_input = local_prediction_velocity_2d_length(velocity) > 8.0f;

// chinese z bump
  if (state.grounded) {
    constexpr float dist_epsilon_3 = 0.09375f;  // 0.03125f * 3
    state.origin.z += dist_epsilon_3;
  }

  if (engine_trace != nullptr) {
    const float seed_speed_2d = local_prediction_velocity_2d_length(state.velocity);
    if (seed_speed_2d > 50.0f) {
      const Vec3 probe_end = state.origin + (state.velocity * (static_cast<float>(TICK_INTERVAL) * 1.25f));
      trace_t probe_trace{};
      if (local_prediction_trace_hull(state.origin, probe_end, state.mins, state.maxs, skip_entity, &probe_trace) &&
          !probe_trace.start_solid && !probe_trace.all_solid &&
          probe_trace.fraction < 1.0f && probe_trace.plane.normal.z < 0.7f) {
        state.velocity = local_prediction_clip_velocity(state.velocity, probe_trace.plane.normal, 1.0f);
      }
    }
  }

  const local_prediction_strafe_estimate strafe_estimate = local_prediction_estimate_strafe(player);
  const float yaw_step = strafe_estimate.valid ? strafe_estimate.yaw_step : local_prediction_estimate_average_yaw_step(player);
  path.average_yaw = yaw_step;
  path.strafe_confidence = strafe_estimate.confidence;
  path.used_strafe_prediction = std::fabs(yaw_step) > 0.01f;
  path.used_movement_sim = true;
  path.used_game_engine_movement = false;

  const float max_speed_seed = local_prediction_estimated_max_speed(player, velocity);
  const float speed_2d = local_prediction_velocity_2d_length(state.velocity);
  if (speed_2d > max_speed_seed && speed_2d > 0.001f) {
    const float speed_scale = max_speed_seed / speed_2d;
    state.velocity.x *= speed_scale;
    state.velocity.y *= speed_scale;
  }

  float move_yaw = local_prediction_normalize_yaw(
    local_prediction_velocity_2d_length(velocity) > 8.0f ? local_prediction_vector_yaw(velocity) : player->get_eye_angles().y);

  const int stride = std::max(1, hull_trace_stride);
  int hull_step_counter = 0;
  for (int lead = 0; lead < lead_clamped; ++lead) {
    const bool hull_trace = (stride <= 1) || ((hull_step_counter % stride) == 0);
    const float max_speed_tick = local_prediction_estimated_max_speed(player, state.velocity);
    local_prediction_player_path_tick(
      &state,
      skip_entity,
      static_cast<float>(TICK_INTERVAL),
      yaw_step,
      hull_trace,
      max_speed_tick,
      &move_yaw);
    ++hull_step_counter;
  }
  path.positions.push_back(state.origin);

  for (int step = 1; step <= step_count; ++step) {
    const bool hull_trace = (stride <= 1) || ((hull_step_counter % stride) == 0);
    const float max_speed_tick = local_prediction_estimated_max_speed(player, state.velocity);
    local_prediction_player_path_tick(
      &state,
      skip_entity,
      static_cast<float>(TICK_INTERVAL),
      yaw_step,
      hull_trace,
      max_speed_tick,
      &move_yaw);
    ++hull_step_counter;
    path.positions.push_back(state.origin);
  }

  path.final_velocity = state.velocity;
  path.valid = path.positions.size() > 1;
  return path;
}

inline void local_prediction_extend_player_path_lightweight(Player* player,
  LocalPredictionEntityPath* path,
  int requested_tick_count) {
  if (player == nullptr || path == nullptr || !path->valid || path->positions.empty()) {
    return;
  }

  const int existing_tick_count = static_cast<int>(path->positions.size()) - 1;
  if (existing_tick_count >= requested_tick_count) {
    return;
  }

  local_prediction_lightweight_state state{};
  state.origin = path->positions.back();
  state.velocity = path->final_velocity;
  state.mins = player->get_player_mins(player->is_ducking());
  state.maxs = player->get_player_maxs(player->is_ducking());
  state.swimming = player->get_water_level() > 1;
  state.grounded = state.swimming || (std::fabs(state.velocity.z) <= 1.0f && player->is_on_ground());
  state.has_move_input = local_prediction_velocity_2d_length(state.velocity) > 8.0f;

  path->positions.reserve(static_cast<size_t>(requested_tick_count) + 1);
  const int trace_stride = std::max(1, config.aimbot.projectile_trace_interval);
  float move_yaw = local_prediction_normalize_yaw(
    local_prediction_velocity_2d_length(state.velocity) > 8.0f
      ? local_prediction_vector_yaw(state.velocity)
      : player->get_eye_angles().y);
  for (int step = existing_tick_count + 1; step <= requested_tick_count; ++step) {
    const int steps_since_extend = step - (existing_tick_count + 1);
    const bool hull_trace = (trace_stride <= 1) || ((steps_since_extend % trace_stride) == 0);
    const float max_speed_tick = local_prediction_estimated_max_speed(player, state.velocity);
    local_prediction_player_path_tick(
      &state,
      player,
      static_cast<float>(TICK_INTERVAL),
      path->average_yaw,
      hull_trace,
      max_speed_tick,
      &move_yaw);
    path->positions.push_back(state.origin);
  }

  path->final_velocity = state.velocity;
  path->valid = path->positions.size() > 1;
}

inline LocalPredictionEntityPath local_prediction_predict_entity_path(Entity* entity,
  float horizon_seconds,
  bool use_movement_sim = true,
  bool apply_network_lead = false) {
  LocalPredictionEntityPath path{};
  if (entity == nullptr || horizon_seconds <= 0.0f) {
    return path;
  }

  const int lead_ticks = apply_network_lead ? local_prediction_network_lead_ticks(entity) : 0;
  const float lead_time = local_prediction_ticks_to_time(lead_ticks);
  const int step_count = local_prediction_path_tick_count(horizon_seconds);

  Player* const entity_as_player = entity->get_class_id() == class_id::PLAYER ? static_cast<Player*>(entity) : nullptr;

  const bool can_use_engine_movement_sim =
    use_movement_sim &&
    entity_as_player != nullptr &&
    entity_list != nullptr &&
    entity_as_player == entity_list->get_localplayer() &&
    prediction != nullptr &&
    game_movement != nullptr &&
    move_helper != nullptr &&
    global_vars != nullptr;
  if (can_use_engine_movement_sim) {
    if (local_prediction_simulate_player_path(entity_as_player, horizon_seconds, &path, lead_ticks)) {
      local_prediction_extend_player_path_lightweight(entity_as_player, &path, step_count);
      return path;
    }
  }

  if (entity_as_player != nullptr && engine_trace != nullptr) {
    return local_prediction_build_player_path_client_sim(
      entity_as_player,
      entity,
      lead_ticks,
      step_count,
      std::max(1, config.aimbot.projectile_trace_interval));
  }

  Vec3 origin = entity->get_origin();
  Vec3 velocity = local_prediction_estimate_entity_velocity(entity);
  path.time_step = TICK_INTERVAL;
  path.start_time = lead_time;
  path.positions.reserve(static_cast<size_t>(step_count) + 1);

  origin += velocity * lead_time;
  path.positions.push_back(origin);
  for (int step = 1; step <= step_count; ++step) {
    const float time = std::min(horizon_seconds, static_cast<float>(step) * static_cast<float>(TICK_INTERVAL));
    path.positions.push_back(origin + (velocity * time));
  }
  path.final_velocity = velocity;
  path.valid = path.positions.size() > 1;
  return path;
}

inline Vec3 local_prediction_predict_entity_origin(Entity* entity, float horizon_seconds) {
  if (entity == nullptr) return Vec3{};
  LocalPredictionEntityPath path = local_prediction_predict_entity_path(entity, horizon_seconds);
  if (!path.valid || path.positions.empty()) {
    return entity->get_origin();
  }

  size_t index = std::min(
    path.positions.size() - 1,
    static_cast<size_t>(std::ceil(horizon_seconds / std::max(path.time_step, 0.0001f))));
  return path.positions[index];
}

inline float local_prediction_vector_length(Vec3 value) {
  return std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
}

inline Vec3 local_prediction_normalize(Vec3 value) {
  float length = local_prediction_vector_length(value);
  if (length <= 0.0001f) return Vec3{};
  return value * (1.0f / length);
}

inline Vec3 local_prediction_direction_to_angles(const Vec3& direction) {
  float yaw_hyp = std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
  return Vec3{
    -std::atan2(direction.z, yaw_hyp) * radpi,
    std::atan2(direction.y, direction.x) * radpi,
    0.0f
  };
}

inline Vec3 local_prediction_angles_to_direction(const Vec3& angles) {
  float pitch = angles.x * pideg;
  float yaw = angles.y * pideg;
  float cp = std::cos(pitch);
  return Vec3{
    cp * std::cos(yaw),
    cp * std::sin(yaw),
    -std::sin(pitch)
  };
}

inline bool local_prediction_flip_projectile_offset_y() {
  static Convar* cl_flipviewmodels = nullptr;
  if (cl_flipviewmodels == nullptr && convar_system != nullptr) {
    cl_flipviewmodels = convar_system->find_var("cl_flipviewmodels");
  }

  return cl_flipviewmodels != nullptr && cl_flipviewmodels->get_int() != 0;
}

inline float local_prediction_projectile_gravity_scale() {
  static Convar* sv_gravity = nullptr;
  if (sv_gravity == nullptr && convar_system != nullptr) {
    sv_gravity = convar_system->find_var("sv_gravity");
  }

  const float gravity = sv_gravity != nullptr ? sv_gravity->get_float() : 800.0f;
  return gravity / 800.0f;
}

inline Vec3 local_prediction_projectile_offset_for_weapon(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr) {
    return Vec3{};
  }

  const bool ducking = localplayer->is_ducking();
  switch (weapon->get_def_id()) {
  case Soldier_m_TheCowMangler5000:
  case Soldier_s_TheRighteousBison:
    return Vec3{23.5f, 8.0f, ducking ? 8.0f : -3.0f};
  case Engi_m_ThePomson6000:
    return Vec3{23.5f, 8.0f, (ducking ? 8.0f : -3.0f) - 13.0f};
  case Pyro_m_DragonsFury:
    return Vec3{3.0f, 7.0f, -9.0f};
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheDirectHit:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
    return Vec3{23.5f, attribute_manager != nullptr && static_cast<int>(attribute_manager->attrib_hook_value(0.0f, "centerfire_projectile", weapon->to_entity())) == 1 ? 0.0f : 12.0f, ducking ? 8.0f : -3.0f};
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
    return Vec3{16.0f, 8.0f, -6.0f};
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheManmelter:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
    return Vec3{23.5f, 12.0f, ducking ? 8.0f : -3.0f};
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Engi_m_TheRescueRanger:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    return Vec3{23.5f, 8.0f, -3.0f};
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
    return Vec3{16.0f, 6.0f, -8.0f};
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return Vec3{16.0f, 8.0f, -6.0f};
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return Vec3{16.0f, 8.0f, -6.0f};
  default:
    return Vec3{};
  }
}

inline bool local_prediction_vec3_is_zero(const Vec3& value) {
  return value.x == 0.0f && value.y == 0.0f && value.z == 0.0f;
}

inline LocalPredictionLaunchState local_prediction_build_launch_state(Player* localplayer, user_cmd* user_cmd) {
  LocalPredictionLaunchState state{};
  if (localplayer == nullptr || user_cmd == nullptr) return state;
  state.origin = localplayer->get_shoot_pos();
  state.view_angles = user_cmd->view_angles;
  state.direction = local_prediction_normalize(local_prediction_angles_to_direction(state.view_angles));
  state.inherited_velocity = localplayer->get_velocity();
  return state;
}

inline LocalPredictionLaunchState local_prediction_build_launch_state(Player* localplayer, Weapon* weapon, user_cmd* user_cmd) {
  LocalPredictionLaunchState state = local_prediction_build_launch_state(localplayer, user_cmd);
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr) {
    return state;
  }

  const Vec3 offset = local_prediction_projectile_offset_for_weapon(localplayer, weapon);
  if (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f) {
    return state;
  }

  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(state.view_angles, &forward, &right, &up);
  state.origin += (forward * offset.x) + (right * offset.y) + (up * offset.z);
  return state;
}

inline LocalPredictionProjectileTrace local_prediction_simulate_projectile(const LocalPredictionLaunchState& launch_state,
  const LocalPredictionProjectileParameters& params) {
  LocalPredictionProjectileTrace trace{};
  if (params.speed <= 0.0f || params.time_step <= 0.0f || params.max_time <= 0.0f) return trace;

  Vec3 position = launch_state.origin;
  Vec3 velocity{
    launch_state.direction.x * params.speed + launch_state.inherited_velocity.x,
    launch_state.direction.y * params.speed + launch_state.inherited_velocity.y,
    launch_state.direction.z * params.speed + launch_state.inherited_velocity.z
  };

  int step_count = std::max(1, static_cast<int>(std::ceil(params.max_time / params.time_step)));
  trace.steps.reserve(step_count + 1);
  for (int step_index = 0; step_index <= step_count; ++step_index) {
    float time = std::min(params.max_time, step_index * params.time_step);
    LocalPredictionProjectileStep step{};
    step.time = time;
    step.position = position;
    step.velocity = velocity;
    trace.steps.push_back(step);

    if (time >= params.max_time) break;
    position.x += velocity.x * params.time_step;
    position.y += velocity.y * params.time_step;
    position.z += velocity.z * params.time_step;
    velocity.z -= params.gravity * params.time_step;
  }

  trace.valid = !trace.steps.empty();
  return trace;
}

inline LocalPredictionProjectileParameters local_prediction_projectile_parameters_for_weapon(Weapon* weapon) {
  LocalPredictionProjectileParameters params{};
  if (weapon == nullptr) return params;

  const float gravity_scale = local_prediction_projectile_gravity_scale();
  const auto projectile_speed = [weapon](float base_speed) {
    return projectile_speed_attr(weapon, projectile_weapon_data_speed_or(weapon, base_speed));
  };
  const auto projectile_range_speed = [weapon](float base_speed) {
    return projectile_range_speed_attr(weapon, projectile_weapon_data_speed_or(weapon, base_speed));
  };
  const auto attribute_value = [weapon](float base_value, const char* attribute_name) {
    return projectile_attr_float(weapon, base_value, attribute_name);
  };

  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
    params.speed = projectile_speed(1100.0f);
    params.gravity = 0.0f;
    params.max_time = 6.0f;
    break;
  case Soldier_m_TheDirectHit:
    params.speed = projectile_speed(3000.0f);
    params.gravity = 0.0f;
    params.max_time = 4.0f;
    break;
  case Soldier_s_TheRighteousBison:
    params.speed = projectile_speed(1200.0f);
    params.gravity = 0.0f;
    params.max_time = 5.0f;
    break;
  case Engi_m_ThePomson6000:
    params.speed = projectile_speed(1200.0f);
    params.gravity = 0.0f;
    params.max_time = 5.0f;
    break;
  case Pyro_m_DragonsFury:
    {
      static Convar* tf_fireball_speed = nullptr;
      static Convar* tf_fireball_distance = nullptr;
      static Convar* tf_fireball_max_lifetime = nullptr;
      if (convar_system != nullptr) {
        if (tf_fireball_speed == nullptr) {
          tf_fireball_speed = convar_system->find_var("tf_fireball_speed");
        }
        if (tf_fireball_distance == nullptr) {
          tf_fireball_distance = convar_system->find_var("tf_fireball_distance");
        }
        if (tf_fireball_max_lifetime == nullptr) {
          tf_fireball_max_lifetime = convar_system->find_var("tf_fireball_max_lifetime");
        }
      }

      const float speed = tf_fireball_speed != nullptr ? tf_fireball_speed->get_float() : 3000.0f;
      const float distance = tf_fireball_distance != nullptr ? tf_fireball_distance->get_float() : 1200.0f;
      const float max_lifetime = tf_fireball_max_lifetime != nullptr ? tf_fireball_max_lifetime->get_float() : 0.8f;
      params.speed = projectile_speed(speed);
      params.max_time = std::min(distance / std::max(params.speed, 1.0f), max_lifetime);
    }
    params.gravity = 0.0f;
    break;
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Engi_m_TheRescueRanger:
    params.speed = projectile_speed(2400.0f);
    params.gravity = 0.2f * gravity_scale * 800.0f;
    params.max_time = 4.0f;
    break;
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    {
      const float charge_begin_time = weapon->get_charge_begin_time();
      const float held_time = charge_begin_time > 0.0f && global_vars != nullptr
        ? std::clamp(global_vars->curtime - charge_begin_time, 0.0f, 1.0f)
        : 0.0f;
      const float charge = std::clamp(held_time, 0.0f, 1.0f);
      params.speed = 1800.0f + ((2600.0f - 1800.0f) * charge);
      params.gravity = (0.5f + ((0.1f - 0.5f) * charge)) * gravity_scale * 800.0f;
    }
    params.max_time = 4.0f;
    break;
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
    params.speed = weapon->get_def_id() == Pyro_s_TheScorchShot ? 2000.0f : projectile_speed(2000.0f);
    params.gravity = (weapon->get_def_id() == Pyro_s_TheScorchShot ? 0.3f : 0.3f) * gravity_scale * 800.0f;
    params.max_time = 1.8f;
    break;
  case Pyro_s_TheManmelter:
    params.speed = projectile_speed(3000.0f);
    params.gravity = 0.45f * gravity_scale * 800.0f;
    params.max_time = 1.8f;
    break;
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
    params.speed = projectile_range_speed(1200.0f);
    params.gravity = gravity_scale * 800.0f;
    if (weapon->get_def_id() == Demoman_m_TheLooseCannon) {
      const float mortar_time = attribute_value(0.0f, "grenade_launcher_mortar_mode");
      if (mortar_time > 0.0f) {
        const float detonate_time = weapon->get_detonate_time();
        const float remaining_time = detonate_time > 0.0f && global_vars != nullptr
          ? detonate_time - global_vars->curtime
          : mortar_time;
        params.max_time = std::clamp(remaining_time, static_cast<float>(TICK_INTERVAL), mortar_time);
      } else {
        params.max_time = 2.0f;
      }
    } else {
      params.max_time = weapon->get_def_id() == Demoman_m_TheIronBomber ? 1.4f : 2.0f;
    }
    break;
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
    params.speed = projectile_speed(1000.0f);
    params.gravity = gravity_scale * 800.0f;
    params.max_time = 2.2f;
    break;
  case Pyro_s_GasPasser:
    params.speed = projectile_speed(2000.0f);
    params.gravity = gravity_scale * 800.0f;
    params.max_time = 2.2f;
    break;
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    params.speed = projectile_speed(3000.0f);
    params.gravity = gravity_scale * 800.0f;
    params.max_time = 2.2f;
    break;
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
    params.speed = projectile_speed(1000.0f);
    params.gravity = 0.3f * gravity_scale * 800.0f;
    params.max_time = 2.0f;
    break;
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    {
      const float charge_rate = attribute_manager != nullptr
        ? attribute_manager->attrib_hook_value(4.0f, "stickybomb_charge_rate", weapon->to_entity())
        : 4.0f;
      const float charge_begin_time = weapon->get_charge_begin_time();
      const float held_time = charge_begin_time > 0.0f && global_vars != nullptr
        ? std::clamp(global_vars->curtime - charge_begin_time, 0.0f, charge_rate)
        : 0.0f;
      const float charge = charge_rate > 0.0f ? std::clamp(held_time / charge_rate, 0.0f, 1.0f) : 0.0f;
      params.speed = projectile_range_speed(900.0f + ((2400.0f - 900.0f) * charge));
      params.gravity = gravity_scale * 800.0f;
      const int def = weapon->get_def_id();
      if (def == Demoman_s_TheScottishResistance) {
        params.max_time = 2.45f;
      } else if (def == Demoman_s_TheQuickiebombLauncher) {
        params.max_time = 1.7f;
      } else {
        params.max_time = 2.0f;
      }
    }
    break;
  default:
    break;
  }

  return params;
}

inline bool projectile_sim_is_grenade_like_weapon(Weapon* weapon) {
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
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return true;
  default:
    return false;
  }
}

inline bool projectile_sim_is_rocket_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
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
    return true;
  default:
    return false;
  }
}

inline bool projectile_sim_uses_pipe_fire_setup(Weapon* weapon) {
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
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return true;
  default:
    return false;
  }
}

inline projectile_sim_velocity_mode projectile_sim_velocity_mode_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return projectile_sim_velocity_mode::forward;
  }

  switch (weapon->get_def_id()) {
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return projectile_sim_velocity_mode::cleaver;
  default:
    return projectile_sim_is_grenade_like_weapon(weapon)
      ? projectile_sim_velocity_mode::pipe_lift
      : projectile_sim_velocity_mode::forward;
  }
}

inline float projectile_sim_drag_for_weapon(Weapon* weapon, float speed) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  const auto remap_clamped = [](float value, float in_min, float in_max, float out_min, float out_max) {
    const float clamped = std::clamp(value, in_min, in_max);
    return out_min + ((clamped - in_min) / std::max(in_max - in_min, 0.0001f)) * (out_max - out_min);
  };

  switch (weapon->get_def_id()) {
  case Demoman_m_TheLochnLoad:
    return remap_clamped(speed, 1504.0f, 3500.0f, 0.070f, 0.085f);
  case Demoman_m_TheLooseCannon:
    return remap_clamped(speed, 1454.0f, 3500.0f, 0.385f, 0.530f);
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
    return remap_clamped(speed, 1217.0f, 3500.0f, 0.100f, 0.200f);
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return remap_clamped(speed, 922.0f, 2400.0f, 0.085f, 0.190f);
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
    return 0.057f;
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return 0.310f;
  default:
    return 0.0f;
  }
}

inline Vec3 projectile_sim_hull_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return Vec3{};
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
    return Vec3{1.0f, 1.0f, 1.0f};
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
    return Vec3{6.0f, 6.0f, 6.0f};
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return Vec3{3.0f, 3.0f, 3.0f};
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return Vec3{1.0f, 1.0f, 10.0f};
  case Soldier_s_TheRighteousBison:
  case Engi_m_ThePomson6000:
  case Pyro_m_DragonsFury:
    return Vec3{1.0f, 1.0f, 1.0f};
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
    return Vec3{3.0f, 3.0f, 3.0f};
  case Engi_m_TheRescueRanger:
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    return Vec3{1.0f, 1.0f, 1.0f};
  default:
    return Vec3{};
  }
}

inline projectile_sim_profile projectile_sim_profile_for_weapon(Player* localplayer, Weapon* weapon) {
  projectile_sim_profile profile{};
  if (localplayer == nullptr || weapon == nullptr) {
    return profile;
  }

  profile.params = local_prediction_projectile_parameters_for_weapon(weapon);
  if (profile.params.speed <= 0.0f) {
    return profile;
  }

  if (localplayer->in_cond(TF_COND_RUNE_PRECISION) &&
      (projectile_sim_is_rocket_weapon(weapon) || projectile_sim_uses_pipe_fire_setup(weapon))) {
    profile.params.speed = std::max(profile.params.speed, 3000.0f);
  }

  if (projectile_sim_is_rocket_weapon(weapon) && attribute_manager != nullptr) {
    const int rocket_specialist = static_cast<int>(attribute_manager->attrib_hook_value(
      0.0f,
      "rocket_specialist",
      static_cast<Entity*>(localplayer)));
    if (rocket_specialist > 0) {
      const float specialist_scale = 1.15f + ((std::clamp(static_cast<float>(rocket_specialist), 1.0f, 4.0f) - 1.0f) / 3.0f) * (1.6f - 1.15f);
      profile.params.speed = std::min(profile.params.speed * specialist_scale, 3000.0f);
    }
  }

  const float configured_horizon = static_cast<float>(std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420)) *
    static_cast<float>(TICK_INTERVAL);
  profile.params.max_time = std::min(profile.params.max_time, configured_horizon);
  profile.params.time_step = std::max(profile.params.time_step, static_cast<float>(TICK_INTERVAL));
  profile.offset = local_prediction_projectile_offset_for_weapon(localplayer, weapon);
  if (local_prediction_flip_projectile_offset_y()) {
    profile.offset.y *= -1.0f;
  }
  profile.hull = projectile_sim_hull_for_weapon(weapon);
  if (local_prediction_vec3_is_zero(profile.hull)) {
    profile.hull = Vec3{2.0f, 2.0f, 2.0f};
  }
  profile.hull_trace = profile.hull.x > 0.0f || profile.hull.y > 0.0f || profile.hull.z > 0.0f;
  profile.lifetime = profile.params.max_time;
  profile.initial_lift = projectile_sim_is_grenade_like_weapon(weapon) ? 200.0f : 0.0f;
  profile.drag = projectile_sim_drag_for_weapon(weapon, profile.params.speed);
  profile.trace_mask = MASK_SOLID;
  profile.inherit_velocity = false;
  profile.velocity_mode = projectile_sim_velocity_mode_for_weapon(weapon);
  profile.fire_setup_mode = projectile_sim_uses_pipe_fire_setup(weapon)
    ? projectile_sim_fire_setup_mode::pipe_style
    : projectile_sim_fire_setup_mode::traced_forward;
  profile.spawn_trace_mode = profile.fire_setup_mode == projectile_sim_fire_setup_mode::pipe_style
    ? projectile_sim_spawn_trace_mode::hull
    : projectile_sim_spawn_trace_mode::line;
  profile.spawn_trace_mins = profile.fire_setup_mode == projectile_sim_fire_setup_mode::pipe_style
    ? Vec3{-8.0f, -8.0f, -8.0f}
    : Vec3{};
  profile.spawn_trace_maxs = profile.fire_setup_mode == projectile_sim_fire_setup_mode::pipe_style
    ? Vec3{8.0f, 8.0f, 8.0f}
    : Vec3{};
  profile.valid = true;
  return profile;
}

inline projectile_sim_launch projectile_sim_build_launch_from_angles(Player* localplayer,
  Weapon* weapon,
  const Vec3& angles,
  const projectile_sim_profile& profile) {
  projectile_sim_launch launch{};
  if (localplayer == nullptr || weapon == nullptr || !profile.valid || engine_trace == nullptr) {
    return launch;
  }

  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  launch.angles = angles;
  launch.inherited_velocity = profile.inherit_velocity ? localplayer->get_velocity() : Vec3{};

  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(angles, &forward, &right, &up);
  Vec3 muzzle_pos = shoot_pos;
  if (profile.offset.x != 0.0f || profile.offset.y != 0.0f || profile.offset.z != 0.0f) {
    muzzle_pos += (forward * profile.offset.x) + (right * profile.offset.y) + (up * profile.offset.z);
  }

  Vec3 launch_direction{};
  if (profile.fire_setup_mode == projectile_sim_fire_setup_mode::traced_forward) {
    Vec3 end_pos = shoot_pos + (forward * 2000.0f);
    trace_t trace{};
    const bool traced = projectile_trace_ray(
      shoot_pos,
      end_pos,
      nullptr,
      nullptr,
      projectile_trace_contract::fire_setup,
      localplayer->to_entity(),
      static_cast<int>(localplayer->get_team()),
      &trace);
    if (traced && trace.fraction > 0.1f && trace.fraction < 1.0f && !trace.start_solid) {
      end_pos = trace.endpos;
    }

    launch_direction = local_prediction_normalize(end_pos - muzzle_pos);
  } else {
    launch_direction = local_prediction_normalize(local_prediction_angles_to_direction(angles));
  }

  if (profile.spawn_trace_mode != projectile_sim_spawn_trace_mode::none) {
    trace_t spawn_trace{};
    bool spawn_traced = false;
    if (profile.spawn_trace_mode == projectile_sim_spawn_trace_mode::hull) {
      spawn_traced = projectile_trace_ray(
        shoot_pos,
        muzzle_pos,
        &profile.spawn_trace_mins,
        &profile.spawn_trace_maxs,
        projectile_trace_contract::spawn,
        localplayer->to_entity(),
        -1,
        &spawn_trace);
    } else {
      spawn_traced = projectile_trace_ray(
        shoot_pos,
        muzzle_pos,
        nullptr,
        nullptr,
        projectile_trace_contract::spawn,
        localplayer->to_entity(),
        -1,
        &spawn_trace);
    }

    if (!spawn_traced) {
      return {};
    }
    if (spawn_trace.start_solid || spawn_trace.all_solid) {
      return {};
    }
    muzzle_pos = spawn_trace.endpos;
  }

  launch.direction = launch_direction;
  launch.origin = muzzle_pos;
  launch.valid = !local_prediction_vec3_is_zero(launch.direction);
  return launch;
}

inline projectile_sim_launch projectile_sim_build_launch(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const projectile_sim_profile& profile) {
  if (user_cmd == nullptr) {
    return {};
  }

  return projectile_sim_build_launch_from_angles(localplayer, weapon, user_cmd->view_angles, profile);
}

inline Vec3 projectile_sim_initial_velocity(const projectile_sim_launch& launch, const projectile_sim_profile& profile) {
  Vec3 velocity{};
  if (profile.velocity_mode == projectile_sim_velocity_mode::pipe_lift) {
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    angle_vectors(launch.angles, &forward, &right, &up);
    velocity = (forward * profile.params.speed) + (up * profile.initial_lift);
  } else if (profile.velocity_mode == projectile_sim_velocity_mode::cleaver) {
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    angle_vectors(launch.angles, &forward, &right, &up);
    velocity = local_prediction_normalize((forward * 10.0f) + up) * profile.params.speed;
  } else {
    velocity = Vec3{
      launch.direction.x * profile.params.speed,
      launch.direction.y * profile.params.speed,
      launch.direction.z * profile.params.speed
    };
  }
  velocity += launch.inherited_velocity;
  return velocity;
}

inline Vec3 projectile_sim_apply_drag(const Vec3& velocity, const projectile_sim_profile& profile, float dt) {
  if (profile.drag <= 0.0f || dt <= 0.0f) {
    return velocity;
  }

  const float drag_scale = std::clamp(1.0f - (profile.drag * dt), 0.0f, 1.0f);
  return velocity * drag_scale;
}

inline bool projectile_sim_trace_step(const Vec3& start,
  const Vec3& end,
  const projectile_sim_profile& profile,
  Entity* skip_entity,
  Entity* target_entity,
  projectile_sim_trace_mode trace_mode,
  trace_t* trace_out) {
  if (engine_trace == nullptr || trace_mode == projectile_sim_trace_mode::none || trace_out == nullptr) {
    return false;
  }

  Vec3 mins = profile.hull * -1.0f;
  Vec3 maxs = profile.hull;
  if (trace_mode == projectile_sim_trace_mode::world) {
    if (!proj_aim_budget_try_trace_call()) {
      return false;
    }
    ray_t ray = profile.hull_trace
      ? engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end), &mins, &maxs)
      : engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end));
    trace_filter filter{};
    engine_trace->init_world_trace_filter(&filter);
    engine_trace->trace_ray(&ray, MASK_SOLID_BRUSHONLY, &filter, trace_out);
  } else if (trace_mode == projectile_sim_trace_mode::blocking_non_player) {
    if (!projectile_trace_ray(
        start,
        end,
        profile.hull_trace ? &mins : nullptr,
        profile.hull_trace ? &maxs : nullptr,
        projectile_trace_contract::world_block,
        skip_entity,
        -1,
        trace_out)) {
      return false;
    }
  } else {
    if (!proj_aim_budget_try_trace_call()) {
      return false;
    }
    ray_t ray = profile.hull_trace
      ? engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end), &mins, &maxs)
      : engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end));
    trace_filter filter{};
    engine_trace->init_trace_filter(&filter, skip_entity);
    engine_trace->trace_ray(&ray, profile.trace_mask, &filter, trace_out);
  }
  if (trace_out->fraction >= 0.97f && !trace_out->start_solid && !trace_out->all_solid) {
    return false;
  }

  if (trace_mode == projectile_sim_trace_mode::world || trace_mode == projectile_sim_trace_mode::blocking_non_player) {
    return true;
  }

  return true;
}

inline bool projectile_simulation::init(const projectile_sim_launch& launch_in,
  const projectile_sim_profile& profile_in,
  Entity* skip_entity_in,
  Entity* target_entity_in,
  projectile_sim_trace_mode trace_mode_in) {
  *this = {};
  if (!profile_in.valid || !launch_in.valid || profile_in.params.speed <= 0.0f || profile_in.params.time_step <= 0.0f || profile_in.lifetime <= 0.0f) {
    return false;
  }

  profile = profile_in;
  launch = launch_in;
  skip_entity = skip_entity_in;
  target_entity = target_entity_in;
  trace_mode = trace_mode_in;
  position = launch.origin;
  velocity = projectile_sim_initial_velocity(launch, profile);
  max_ticks = std::clamp(
    static_cast<int>(std::ceil(profile.lifetime / profile.params.time_step)),
    1,
    std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420));

  result.steps.reserve(static_cast<size_t>(max_ticks) + 1);
  result.steps.emplace_back(projectile_sim_step{
    .time = 0.0f,
    .position = position,
    .velocity = velocity
  });
  initialized = true;
  result.valid = true;
  return true;
}

inline bool projectile_simulation::step() {
  if (!initialized || finished) {
    return false;
  }

  if (tick >= max_ticks || time >= profile.lifetime) {
    finished = true;
    return false;
  }

  const float dt = std::min(profile.params.time_step, profile.lifetime - time);
  const Vec3 next_position{
    position.x + (velocity.x * dt),
    position.y + (velocity.y * dt),
    position.z + (velocity.z * dt) - (0.5f * profile.params.gravity * dt * dt)
  };

  trace_t trace{};
  const bool hit = profile.collide_world &&
    projectile_sim_trace_step(position, next_position, profile, skip_entity, target_entity, trace_mode, &trace);

  if (hit) {
    const float trace_fraction = std::clamp(trace.fraction, 0.0f, 1.0f);
    const float hit_time = time + (dt * trace_fraction);
    const bool hit_target = target_entity != nullptr && trace.entity == target_entity;

    result.steps.emplace_back(projectile_sim_step{
      .time = hit_time,
      .position = trace.endpos,
      .velocity = velocity,
      .trace = trace,
      .hit = true,
      .hit_target = hit_target
    });

    result.hit = true;
    result.hit_target = hit_target;
    result.hit_time = hit_time;
    result.hit_position = trace.endpos;
    result.hit_normal = trace.plane.normal;
    result.hit_entity = trace.entity;
    position = trace.endpos;
    time = hit_time;
    finished = true;
    return false;
  }

  position = next_position;
  velocity.z -= profile.params.gravity * dt;
  velocity = projectile_sim_apply_drag(velocity, profile, dt);
  time += dt;
  ++tick;

  result.steps.emplace_back(projectile_sim_step{
    .time = time,
    .position = position,
    .velocity = velocity
  });
  return true;
}

inline projectile_sim_result projectile_simulation::run() {
  while (step()) {
  }

  return result;
}

inline projectile_sim_result projectile_sim_run(const projectile_sim_launch& launch,
  const projectile_sim_profile& profile,
  Entity* skip_entity = nullptr,
  Entity* target_entity = nullptr,
  projectile_sim_trace_mode trace_mode = projectile_sim_trace_mode::none) {
  const bool traced_sim = trace_mode != projectile_sim_trace_mode::none && profile.collide_world;
  if (traced_sim && !proj_aim_budget_try_sim_call()) {
    return {};
  }

  projectile_simulation sim{};
  if (!sim.init(launch, profile, skip_entity, target_entity, trace_mode)) {
    return {};
  }

  return sim.run();
}

inline Vec3 projectile_sim_direction_for_time(const projectile_sim_launch& launch,
  const projectile_sim_profile& profile,
  const Vec3& target_position,
  float travel_time) {
  if (!profile.valid || profile.params.speed <= 0.0f || travel_time <= 0.0001f) {
    return Vec3{};
  }

  Vec3 needed_velocity = (target_position - launch.origin) * (1.0f / travel_time);
  needed_velocity -= launch.inherited_velocity;
  needed_velocity.z -= profile.initial_lift;
  needed_velocity.z += 0.5f * profile.params.gravity * travel_time;

  return local_prediction_normalize(needed_velocity);
}

inline Vec3 projectile_sim_position_at_time(const projectile_sim_launch& launch,
  const projectile_sim_profile& profile,
  float travel_time) {
  Vec3 velocity = projectile_sim_initial_velocity(launch, profile);
  return Vec3{
    launch.origin.x + (velocity.x * travel_time),
    launch.origin.y + (velocity.y * travel_time),
    launch.origin.z + (velocity.z * travel_time) - (0.5f * profile.params.gravity * travel_time * travel_time)
  };
}

inline bool projectile_sim_calc_angle_to_point(const Vec3& from,
  const Vec3& to,
  const projectile_sim_profile& profile,
  bool high_arc,
  Vec3* angle_out,
  float* time_out) {
  if (angle_out == nullptr || time_out == nullptr || !profile.valid || profile.params.speed <= 1.0f) {
    return false;
  }

  const Vec3 delta = to - from;
  const float dx = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));
  const float dy = delta.z;
  if (dx <= 0.001f) {
    return false;
  }

  Vec3 angle{0.0f, std::atan2(delta.y, delta.x) * radpi, 0.0f};
  float flight_time = 0.0f;
  float speed = profile.params.speed;
  float vertical_lift = profile.initial_lift;
  if (profile.velocity_mode == projectile_sim_velocity_mode::cleaver) {
    constexpr float cleaver_scale = 0.9950371902f;
    constexpr float cleaver_lift_scale = 0.0995037190f;
    speed = profile.params.speed * cleaver_scale;
    vertical_lift = profile.params.speed * cleaver_lift_scale;
  }

  if (profile.params.gravity > 0.0f) {
    const float gravity = profile.params.gravity;
    float work_speed = speed;
    float work_lift = vertical_lift;
    float pitch = 0.0f;
    float horizontal_velocity = 1.0f;
    flight_time = 0.1f;
    constexpr int max_drag_iterations = 6;
    for (int drag_iteration = 0; drag_iteration < max_drag_iterations; ++drag_iteration) {
      const float speed2 = work_speed * work_speed;
      const float lift2 = work_lift * work_lift;
      const float dx2 = dx * dx;

      const float a = (dy * lift2) + (dx * work_speed * work_lift) + (0.5f * gravity * dx2);
      const float b = (-2.0f * dy * work_speed * work_lift) - (dx * (speed2 - lift2));
      const float c = (dy * speed2) - (dx * work_speed * work_lift) + (0.5f * gravity * dx2);
      const float root = (b * b) - (4.0f * a * c);
      if (root < 0.0f || std::fabs(a) <= 0.0001f) {
        return false;
      }

      const float z = (-b + (high_arc ? std::sqrt(root) : -std::sqrt(root))) / (2.0f * a);
      pitch = std::atan(z);
      horizontal_velocity = (work_speed * std::cos(pitch)) - (work_lift * std::sin(pitch));
      if (horizontal_velocity <= 1.0f) {
        return false;
      }

      flight_time = dx / horizontal_velocity;
      if (profile.drag <= 0.0f) {
        break;
      }

      const float drag_step = std::clamp(profile.drag * flight_time, 0.0f, 0.92f);
      const float prev_speed = work_speed;
      work_speed = std::max(work_speed * (1.0f - drag_step), 200.0f);
      work_lift = std::max(work_lift * (1.0f - drag_step), 0.0f);
      if (std::fabs(work_speed - prev_speed) < 0.35f) {
        break;
      }
    }

    angle.x = -pitch * radpi;
  } else {
    angle = local_prediction_direction_to_angles(local_prediction_normalize(delta));
    flight_time = local_prediction_vector_length(delta) / speed;
  }

  if (!std::isfinite(flight_time) || flight_time <= 0.0f) {
    return false;
  }

  *angle_out = angle;
  *time_out = flight_time;
  return true;
}

inline bool projectile_sim_solve_launch_to_point(Player* localplayer,
  Weapon* weapon,
  const projectile_sim_profile& profile,
  const Vec3& target_position,
  bool high_arc,
  projectile_sim_launch* launch_out,
  float* flight_time_out) {
  if (localplayer == nullptr || weapon == nullptr || launch_out == nullptr || flight_time_out == nullptr || !profile.valid) {
    return false;
  }

  Vec3 shoot_angles{};
  float flight_time = 0.0f;
  if (!projectile_sim_calc_angle_to_point(localplayer->get_shoot_pos(), target_position, profile, high_arc, &shoot_angles, &flight_time)) {
    return false;
  }

  projectile_sim_launch launch{};
  for (int pass = 0; pass < 5; ++pass) {
    launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);

    Vec3 adjusted_target = target_position;
    if (profile.inherit_velocity) {
      adjusted_target -= launch.inherited_velocity * flight_time;
    }

    Vec3 desired_angles{};
    if (!projectile_sim_calc_angle_to_point(launch.origin, adjusted_target, profile, high_arc, &desired_angles, &flight_time)) {
      return false;
    }

    shoot_angles = desired_angles;
  }

  launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);
  Vec3 adjusted_target = target_position;
  if (profile.inherit_velocity) {
    adjusted_target -= launch.inherited_velocity * flight_time;
  }

  if (!projectile_sim_calc_angle_to_point(launch.origin, adjusted_target, profile, high_arc, &shoot_angles, &flight_time)) {
    return false;
  }

  launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);
  *launch_out = launch;
  *flight_time_out = flight_time;
  return true;
}

inline bool projectile_sim_solve_launch_for_time(Player* localplayer,
  Weapon* weapon,
  const projectile_sim_profile& profile,
  const Vec3& target_position,
  float travel_time,
  projectile_sim_launch* launch_out) {
  if (localplayer == nullptr || weapon == nullptr || launch_out == nullptr || !profile.valid || travel_time <= 0.0001f) {
    return false;
  }

  Vec3 shoot_angles = local_prediction_direction_to_angles(local_prediction_normalize(target_position - localplayer->get_shoot_pos()));
  projectile_sim_launch launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);

  for (int pass = 0; pass < 5; ++pass) {
    Vec3 direction = projectile_sim_direction_for_time(launch, profile, target_position, travel_time);
    if (local_prediction_vec3_is_zero(direction)) {
      return false;
    }

    shoot_angles = local_prediction_direction_to_angles(direction);
    launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);
  }

  Vec3 direction = projectile_sim_direction_for_time(launch, profile, target_position, travel_time);
  if (local_prediction_vec3_is_zero(direction)) {
    return false;
  }

  launch.angles = local_prediction_direction_to_angles(direction);
  launch.direction = direction;
  *launch_out = launch;
  return true;
}

inline Vec3 local_prediction_path_position_at_time(const LocalPredictionEntityPath& path, float time) {
  if (!path.valid || path.positions.empty()) {
    return Vec3{};
  }

  const float local_time = time - path.start_time;
  if (local_time <= 0.0f || path.positions.size() == 1) {
    return path.positions.front();
  }

  const float step = std::max(path.time_step, 0.0001f);
  const float exact_index = local_time / step;
  const size_t lower_index = std::min(
    static_cast<size_t>(std::floor(exact_index)),
    path.positions.size() - 1);
  const size_t upper_index = std::min(lower_index + 1, path.positions.size() - 1);
  const float fraction = std::clamp(exact_index - static_cast<float>(lower_index), 0.0f, 1.0f);

  return path.positions[lower_index] + ((path.positions[upper_index] - path.positions[lower_index]) * fraction);
}

inline Vec3 projectile_sim_position_at_trace_time(const projectile_sim_result& sim_result, float time) {
  if (!sim_result.valid || sim_result.steps.empty()) {
    return Vec3{};
  }

  if (time <= sim_result.steps.front().time || sim_result.steps.size() == 1) {
    return sim_result.steps.front().position;
  }

  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const projectile_sim_step& previous = sim_result.steps[index - 1];
    const projectile_sim_step& current = sim_result.steps[index];
    if (time > current.time) {
      continue;
    }

    const float duration = std::max(current.time - previous.time, 0.0001f);
    const float fraction = std::clamp((time - previous.time) / duration, 0.0f, 1.0f);
    return previous.position + ((current.position - previous.position) * fraction);
  }

  return sim_result.steps.back().position;
}

inline float projectile_sim_direct_tolerance(const projectile_sim_profile& profile) {
  const float hull_2d = std::sqrt((profile.hull.x * profile.hull.x) + (profile.hull.y * profile.hull.y));
  const float hull_size = std::max(hull_2d, std::max(profile.hull.z, 6.0f));
  return std::clamp(hull_size + 10.0f, 16.0f, 48.0f);
}

inline bool projectile_sim_closest_approach_to_linear_target(const projectile_sim_result& sim_result,
  const Vec3& target_origin,
  const Vec3& target_velocity,
  float* time_out,
  float* miss_out) {
  if (!sim_result.valid || sim_result.steps.empty() || time_out == nullptr || miss_out == nullptr) {
    return false;
  }

  float best_time = sim_result.steps.front().time;
  Vec3 target_position = target_origin + (target_velocity * best_time);
  float best_miss = distance_3d(sim_result.steps.front().position, target_position);

  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const projectile_sim_step& previous = sim_result.steps[index - 1];
    const projectile_sim_step& current = sim_result.steps[index];
    const float duration = current.time - previous.time;
    if (duration <= 0.0001f) {
      continue;
    }

    const Vec3 projectile_delta = current.position - previous.position;
    const Vec3 target_start = target_origin + (target_velocity * previous.time);
    const Vec3 target_end = target_origin + (target_velocity * current.time);
    const Vec3 target_delta = target_end - target_start;
    const Vec3 relative_start = previous.position - target_start;
    const Vec3 relative_delta = projectile_delta - target_delta;
    const float relative_delta_sq = local_prediction_dot_3d(relative_delta, relative_delta);
    float segment_t = 0.0f;
    if (relative_delta_sq > 0.0001f) {
      segment_t = std::clamp(-local_prediction_dot_3d(relative_start, relative_delta) / relative_delta_sq, 0.0f, 1.0f);
    }

    const Vec3 relative_position = relative_start + (relative_delta * segment_t);
    const float miss = local_prediction_vector_length(relative_position);
    if (miss < best_miss) {
      best_miss = miss;
      best_time = previous.time + (duration * segment_t);
    }
  }

  *time_out = best_time;
  *miss_out = best_miss;
  return std::isfinite(best_time) && std::isfinite(best_miss);
}

inline bool projectile_sim_closest_approach_to_path(const projectile_sim_result& sim_result,
  const LocalPredictionEntityPath& target_path,
  const Vec3& target_offset,
  float* time_out,
  float* miss_out) {
  if (!sim_result.valid || sim_result.steps.empty() || !target_path.valid || target_path.positions.empty() ||
      time_out == nullptr || miss_out == nullptr) {
    return false;
  }

  const float min_time = std::max(sim_result.steps.front().time, target_path.start_time);
  if (min_time > sim_result.steps.back().time) {
    return false;
  }

  float best_time = min_time;
  Vec3 target_position = local_prediction_path_position_at_time(target_path, best_time) + target_offset;
  float best_miss = distance_3d(projectile_sim_position_at_trace_time(sim_result, best_time), target_position);

  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const projectile_sim_step& previous = sim_result.steps[index - 1];
    const projectile_sim_step& current = sim_result.steps[index];
    if (current.time < min_time) {
      continue;
    }

    const float segment_start_time = std::max(previous.time, min_time);
    const float segment_end_time = current.time;
    const float duration = segment_end_time - segment_start_time;
    if (duration <= 0.0001f) {
      continue;
    }

    const Vec3 projectile_start = projectile_sim_position_at_trace_time(sim_result, segment_start_time);
    const Vec3 projectile_end = current.position;
    const Vec3 projectile_delta = projectile_end - projectile_start;
    const Vec3 target_start = local_prediction_path_position_at_time(target_path, segment_start_time) + target_offset;
    const Vec3 target_end = local_prediction_path_position_at_time(target_path, segment_end_time) + target_offset;
    const Vec3 target_delta = target_end - target_start;
    const Vec3 relative_start = projectile_start - target_start;
    const Vec3 relative_delta = projectile_delta - target_delta;
    const float relative_delta_sq = local_prediction_dot_3d(relative_delta, relative_delta);
    float segment_t = 0.0f;
    if (relative_delta_sq > 0.0001f) {
      segment_t = std::clamp(-local_prediction_dot_3d(relative_start, relative_delta) / relative_delta_sq, 0.0f, 1.0f);
    }

    const Vec3 relative_position = relative_start + (relative_delta * segment_t);
    const float miss = local_prediction_vector_length(relative_position);
    if (miss < best_miss) {
      best_miss = miss;
      best_time = segment_start_time + (duration * segment_t);
    }
  }

  *time_out = best_time;
  *miss_out = best_miss;
  return std::isfinite(best_time) && std::isfinite(best_miss);
}

inline bool projectile_sim_closest_approach_to_static_point(const projectile_sim_result& sim_result,
  const Vec3& target_origin,
  float* time_out,
  float* miss_out) {
  if (!sim_result.valid || sim_result.steps.empty() || time_out == nullptr || miss_out == nullptr) {
    return false;
  }

  float best_time = sim_result.steps.front().time;
  float best_miss = distance_3d(sim_result.steps.front().position, target_origin);
  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const projectile_sim_step& previous = sim_result.steps[index - 1];
    const projectile_sim_step& current = sim_result.steps[index];
    const Vec3 segment = current.position - previous.position;
    const float segment_length_sq = local_prediction_dot_3d(segment, segment);
    float segment_t = 0.0f;
    if (segment_length_sq > 0.0001f) {
      segment_t = std::clamp(local_prediction_dot_3d(target_origin - previous.position, segment) / segment_length_sq, 0.0f, 1.0f);
    }

    const Vec3 closest_position = previous.position + (segment * segment_t);
    const float miss = distance_3d(closest_position, target_origin);
    if (miss < best_miss) {
      best_miss = miss;
      best_time = previous.time + ((current.time - previous.time) * segment_t);
    }
  }

  *time_out = best_time;
  *miss_out = best_miss;
  return std::isfinite(best_time) && std::isfinite(best_miss);
}

inline LocalPredictionProjectileTrace local_prediction_trace_from_projectile_sim(const projectile_sim_result& sim_result) {
  LocalPredictionProjectileTrace trace{};
  if (!sim_result.valid || sim_result.steps.empty()) {
    return trace;
  }

  trace.steps.reserve(sim_result.steps.size());
  for (const projectile_sim_step& sim_step : sim_result.steps) {
    trace.steps.push_back({
      .time = sim_step.time,
      .position = sim_step.position,
      .velocity = sim_step.velocity
    });
  }
  trace.valid = !trace.steps.empty();
  return trace;
}

inline int local_prediction_projectile_arc_branch_count(bool has_gravity) {
  if (!has_gravity) {
    return 1;
  }

  return 2;
}

inline bool local_prediction_projectile_arc_high_branch(bool has_gravity, int branch_index) {
  return has_gravity && branch_index == 1;
}

inline float local_prediction_projectile_arc_score_bias(const projectile_sim_profile& profile,
  bool high_arc,
  float flight_time) {
  if (!high_arc) {
    return 0.0f;
  }

  // gentle preference for low-arc shots; must stay well below intercept_error_cap so high-arc
  // intercepts are still accepted when the low-arc solver fails (long-range huntsman / grenades).
  // intercept_error_cap is roughly max(320, speed*time_step*20), so cap the bias at ~25% of the
  // expected speed-based component plus a small extra cost proportional to flight time.
  const float speed_component = std::clamp(profile.params.speed * profile.params.time_step * 20.0f, 320.0f, 1200.0f);
  const float base_cost = std::min(speed_component * 0.18f, 90.0f);
  const float time_cost = std::min(
    std::clamp(flight_time, 0.0f, profile.params.max_time) * profile.params.speed * 0.04f,
    60.0f);
  return base_cost + time_cost;
}

inline LocalPredictionInterceptResult local_prediction_find_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  const LocalPredictionEntityPath& target_path,
  const Vec3& target_offset,
  user_cmd* user_cmd,
  float horizon_seconds = 1.5f) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr || !target_path.valid || target_path.positions.empty()) return result;

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f) return result;
  profile.params.max_time = std::min(profile.params.max_time, horizon_seconds);
  profile.lifetime = profile.params.max_time;

  float best_error = FLT_MAX;
  float best_time = 0.0f;
  float best_distance = FLT_MAX;
  projectile_sim_launch best_launch{};
  size_t best_path_index = 0;

  const size_t path_step_count = std::min(
    target_path.positions.size() - 1,
    static_cast<size_t>(std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420)));
  size_t path_stride = std::max<size_t>(1, (path_step_count + 47) / 48);
  if (proj_aim_budget().active) {
    path_stride *= static_cast<size_t>(std::clamp(proj_aim_budget().intercept_path_stride_mul, 1, 8));
  }

  const auto score_path_index = [&](size_t path_index) {
    const float target_time = target_path.start_time + (static_cast<float>(path_index) * target_path.time_step);
    if (target_time <= 0.0001f) {
      return;
    }
    if (target_time > profile.params.max_time) {
      return;
    }
    const Vec3 predicted_target = target_path.positions[path_index] + target_offset;

    const Vec3 to_target = predicted_target - localplayer->get_shoot_pos();
    const float straight_distance = local_prediction_vector_length(to_target);
    if (straight_distance <= 1.0f) {
      return;
    }

    const int arc_branch_count = local_prediction_projectile_arc_branch_count(profile.params.gravity > 0.0f);
    for (int arc_branch = 0; arc_branch < arc_branch_count; ++arc_branch) {
      const bool high_arc = local_prediction_projectile_arc_high_branch(profile.params.gravity > 0.0f, arc_branch);
      projectile_sim_launch candidate_launch{};
      float flight_time = 0.0f;
      if (!projectile_sim_solve_launch_to_point(
          localplayer,
          weapon,
          profile,
          predicted_target,
          high_arc,
          &candidate_launch,
          &flight_time)) {
        continue;
      }

      if (flight_time > profile.params.max_time) {
        continue;
      }

      const float spatial_error = distance_3d(projectile_sim_position_at_time(candidate_launch, profile, flight_time), predicted_target);
      const float time_error = std::fabs(flight_time - target_time);
      const float arc_bias = local_prediction_projectile_arc_score_bias(profile, high_arc, flight_time);
      const float score_error = spatial_error + (time_error * profile.params.speed) + arc_bias;
      if (score_error < best_error) {
        best_error = score_error;
        best_time = flight_time;
        best_distance = straight_distance;
        best_launch = candidate_launch;
        best_path_index = path_index;
      }
    }
  };

  for (size_t path_index = 0; path_index <= path_step_count; path_index += path_stride) {
    score_path_index(path_index);

    if (path_step_count - path_index < path_stride) {
      break;
    }
  }

  if (path_stride > 1) {
    const size_t back = std::min(path_stride * 5, best_path_index);
    const size_t start_refine = best_path_index > back ? best_path_index - back : 0;
    const size_t end_refine = std::min(path_step_count, best_path_index + (path_stride * 5));
    for (size_t path_index = start_refine; path_index <= end_refine; ++path_index) {
      if ((path_index % path_stride) == 0 && path_index != best_path_index) {
        continue;
      }
      score_path_index(path_index);
    }

    const size_t fine_back = std::min<size_t>(2, best_path_index);
    const size_t fine_start = best_path_index > fine_back ? best_path_index - fine_back : 0;
    const size_t fine_end = std::min(path_step_count, best_path_index + 2);
    for (size_t path_index = fine_start; path_index <= fine_end; ++path_index) {
      if (path_index == best_path_index) {
        continue;
      }
      score_path_index(path_index);
    }
  }

  const float intercept_error_cap =
    std::max(320.0f, profile.params.speed * profile.params.time_step * 20.0f) +
    (proj_aim_budget().active ? proj_aim_budget().intercept_error_cap_add : 0.0f);
  if (best_error > intercept_error_cap || local_prediction_vec3_is_zero(best_launch.direction)) {
    return {};
  }

  profile.lifetime = std::min(profile.lifetime, best_time + profile.params.time_step);
  const projectile_sim_result sim_result = projectile_sim_run(best_launch, profile);
  LocalPredictionProjectileTrace trace = local_prediction_trace_from_projectile_sim(sim_result);
  if (!sim_result.valid || !trace.valid || trace.steps.empty()) {
    return {};
  }
  float final_time = 0.0f;
  float final_miss = FLT_MAX;
  if (!projectile_sim_closest_approach_to_path(sim_result, target_path, target_offset, &final_time, &final_miss)) {
    return {};
  }

  const Vec3 final_base_origin = local_prediction_path_position_at_time(target_path, final_time);
  const Vec3 final_target = final_base_origin + target_offset;
  // final-miss tolerance: the projectile_sim_direct_tolerance is point-to-point. proj_aim_trace
  // does the precise hull-vs-path test downstream, so here we add the player-hull horizontal
  // radius (~24) so we don't reject an intercept that proj_aim_trace would happily accept.
  constexpr float player_hull_extra = 24.0f;
  if (final_miss > projectile_sim_direct_tolerance(profile) + player_hull_extra) {
    return {};
  }

  result.valid = true;
  result.has_target_base_origin = true;
  result.intercept_time = final_time;
  result.intercept_distance = best_distance;
  result.miss_distance = final_miss;
  result.aim_angles = best_launch.angles;
  result.target_origin = final_target;
  result.target_base_origin = final_base_origin;
  result.target_offset = target_offset;
  result.target_velocity = target_path.final_velocity;
  result.trace = trace;
  return result;
}

inline LocalPredictionInterceptResult local_prediction_find_static_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  const Vec3& target_origin,
  user_cmd* user_cmd,
  float horizon_seconds = 1.5f) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr) return result;

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f) return result;
  profile.params.max_time = std::min(profile.params.max_time, horizon_seconds);
  profile.lifetime = profile.params.max_time;

  float best_score = FLT_MAX;
  float best_time = 0.0f;
  float best_miss = FLT_MAX;
  projectile_sim_launch best_launch{};
  projectile_sim_result best_sim_result{};

  const int arc_branch_count = local_prediction_projectile_arc_branch_count(profile.params.gravity > 0.0f);
  for (int arc_branch = 0; arc_branch < arc_branch_count; ++arc_branch) {
    const bool high_arc = local_prediction_projectile_arc_high_branch(profile.params.gravity > 0.0f, arc_branch);
    projectile_sim_launch candidate_launch{};
    float flight_time = 0.0f;
    if (!projectile_sim_solve_launch_to_point(
        localplayer,
        weapon,
        profile,
        target_origin,
        high_arc,
        &candidate_launch,
        &flight_time)) {
      continue;
    }

    if (!candidate_launch.valid || flight_time > profile.params.max_time) {
      continue;
    }

    projectile_sim_profile sim_profile = profile;
    sim_profile.lifetime = std::min(sim_profile.lifetime, flight_time + sim_profile.params.time_step);
    const projectile_sim_result sim_result = projectile_sim_run(candidate_launch, sim_profile);
    if (!sim_result.valid || sim_result.steps.empty()) {
      continue;
    }

    float final_time = 0.0f;
    float final_miss = FLT_MAX;
    if (!projectile_sim_closest_approach_to_static_point(sim_result, target_origin, &final_time, &final_miss)) {
      continue;
    }

    const float arc_bias = local_prediction_projectile_arc_score_bias(profile, high_arc, flight_time);
    const float score = final_miss + (std::fabs(final_time - flight_time) * profile.params.speed) + arc_bias;
    if (score < best_score) {
      best_score = score;
      best_time = final_time;
      best_miss = final_miss;
      best_launch = candidate_launch;
      best_sim_result = sim_result;
    }
  }

  if (!best_launch.valid || best_miss > projectile_sim_direct_tolerance(profile)) {
    return {};
  }

  LocalPredictionProjectileTrace trace = local_prediction_trace_from_projectile_sim(best_sim_result);
  if (!trace.valid || trace.steps.empty()) {
    return {};
  }

  result.valid = true;
  result.has_target_base_origin = true;
  result.intercept_time = best_time;
  result.intercept_distance = distance_3d(localplayer->get_shoot_pos(), target_origin);
  result.miss_distance = best_miss;
  result.aim_angles = best_launch.angles;
  result.target_origin = target_origin;
  result.target_base_origin = target_origin;
  result.target_offset = Vec3{};
  result.target_velocity = Vec3{};
  result.trace = trace;
  return result;
}

inline LocalPredictionInterceptResult local_prediction_find_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  const Vec3& target_origin,
  const Vec3& target_velocity,
  user_cmd* user_cmd,
  float horizon_seconds = 1.5f) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr) return result;

  if (local_prediction_vector_length(target_velocity) <= 0.001f) {
    return local_prediction_find_static_projectile_intercept(
      localplayer,
      weapon,
      target_origin,
      user_cmd,
      horizon_seconds);
  }

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f) return result;
  profile.params.max_time = std::min(profile.params.max_time, horizon_seconds);
  profile.lifetime = profile.params.max_time;

  float best_error = FLT_MAX;
  float best_time = 0.0f;
  float best_distance = FLT_MAX;
  projectile_sim_launch best_launch{};

  const int step_count = std::clamp(
    static_cast<int>(std::ceil(profile.params.max_time / profile.params.time_step)),
    4,
    std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420));
  int step_stride = std::max(1, (step_count + 47) / 48);
  if (proj_aim_budget().active) {
    step_stride *= std::clamp(proj_aim_budget().intercept_path_stride_mul, 1, 8);
  }
  for (int step_index = 1; step_index <= step_count; step_index += step_stride) {
    const float target_time = std::min(profile.params.max_time, step_index * profile.params.time_step);
    Vec3 predicted_target = target_origin + (target_velocity * target_time);

    Vec3 to_target = predicted_target - localplayer->get_shoot_pos();
    float straight_distance = local_prediction_vector_length(to_target);
    if (straight_distance <= 1.0f) continue;

    const int arc_branch_count = local_prediction_projectile_arc_branch_count(profile.params.gravity > 0.0f);
    for (int arc_branch = 0; arc_branch < arc_branch_count; ++arc_branch) {
      const bool high_arc = local_prediction_projectile_arc_high_branch(profile.params.gravity > 0.0f, arc_branch);
      projectile_sim_launch candidate_launch{};
      float flight_time = 0.0f;
      if (!projectile_sim_solve_launch_to_point(
          localplayer,
          weapon,
          profile,
          predicted_target,
          high_arc,
          &candidate_launch,
          &flight_time)) {
        continue;
      }

      if (flight_time > profile.params.max_time) {
        continue;
      }

      const float spatial_error = distance_3d(projectile_sim_position_at_time(candidate_launch, profile, flight_time), predicted_target);
      const float time_error = std::fabs(flight_time - target_time);
      const float arc_bias = local_prediction_projectile_arc_score_bias(profile, high_arc, flight_time);
      const float score_error = spatial_error + (time_error * profile.params.speed) + arc_bias;
      if (score_error < best_error) {
        best_error = score_error;
        best_time = flight_time;
        best_distance = straight_distance;
        best_launch = candidate_launch;
      }
    }
  }

  const float intercept_error_cap =
    std::max(320.0f, profile.params.speed * profile.params.time_step * 20.0f) +
    (proj_aim_budget().active ? proj_aim_budget().intercept_error_cap_add : 0.0f);
  if (best_error > intercept_error_cap || local_prediction_vec3_is_zero(best_launch.direction)) {
    return {};
  }

  profile.lifetime = std::min(profile.lifetime, best_time + profile.params.time_step);
  const projectile_sim_result sim_result = projectile_sim_run(best_launch, profile);
  LocalPredictionProjectileTrace trace = local_prediction_trace_from_projectile_sim(sim_result);
  if (!sim_result.valid || !trace.valid || trace.steps.empty()) {
    return {};
  }
  float final_time = 0.0f;
  float final_miss = FLT_MAX;
  if (!projectile_sim_closest_approach_to_linear_target(sim_result, target_origin, target_velocity, &final_time, &final_miss)) {
    return {};
  }

  const Vec3 final_target = target_origin + (target_velocity * final_time);
  constexpr float player_hull_extra = 24.0f;
  if (final_miss > projectile_sim_direct_tolerance(profile) + player_hull_extra) {
    return {};
  }

  result.valid = true;
  result.has_target_base_origin = true;
  result.intercept_time = final_time;
  result.intercept_distance = best_distance;
  result.miss_distance = final_miss;
  result.aim_angles = best_launch.angles;
  result.target_origin = final_target;
  result.target_base_origin = final_target;
  result.target_offset = Vec3{};
  result.target_velocity = target_velocity;
  result.trace = trace;
  return result;
}

inline LocalPredictionInterceptResult local_prediction_find_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  user_cmd* user_cmd,
  float horizon_seconds = 1.5f) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || user_cmd == nullptr) return result;

  Vec3 target_velocity = local_prediction_estimate_entity_velocity(target);
  if (local_prediction_vector_length(target_velocity) <= 0.001f && target->get_class_id() == class_id::PLAYER) {
    target_velocity = static_cast<Player*>(target)->get_velocity();
  }

  return local_prediction_find_projectile_intercept(
    localplayer,
    weapon,
    target->get_origin(),
    target_velocity,
    user_cmd,
    horizon_seconds);
}

#endif
