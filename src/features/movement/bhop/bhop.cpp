/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/movement/bhop/bhop.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/movement/bhop/bhop.hpp"
#include "features/automation/anti_cheat_compat/anti_cheat_compat.hpp"
#include "features/menu/config.hpp"
#include "core/math/math.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include <algorithm>
#include <cmath>

namespace
{

int g_moonwalk_applied_command = -1;
int g_fast_stop_command = -1;

struct break_jump_state {
  bool last_jump = false;
  int ticks_since_grounded = -1;
};

break_jump_state g_break_jump_state{};

struct auto_jump_state {
  bool jump = false;
  bool grounded = false;
  bool attempted = false;
};

auto_jump_state g_auto_jump_state{};
bool g_edge_jump_was_grounded = false;
bool g_reverse_jump_last_attack2 = false;

struct edgebug_solution {
  bool valid = false;
  bool duck = false;
  int ticks = 0;
  float yaw_delta = 0.0f;
  float forwardmove = 0.0f;
  float sidemove = 0.0f;
};

edgebug_solution g_edgebug_solution{};

struct movement_state_guard {
  Player* player = nullptr;
  Vec3 origin{};
  Vec3 abs_origin{};
  Vec3 velocity{};
  Vec3 base_velocity{};
  Vec3 view_offset{};
  int flags = 0;
  int ground_entity = 0;
  int buttons = 0;
  int last_buttons = 0;
  bool ducked = false;
  bool ducking = false;
  bool in_duck_jump = false;
  float duck_time = 0.0f;
  float duck_jump_time = 0.0f;
  float fall_velocity = 0.0f;
  int tickbase = 0;
  user_cmd* current_command = nullptr;
  int move_type = 0;
  int water_level = 0;
  Player* previous_host = nullptr;
  float curtime = 0.0f;
  float frametime = 0.0f;
  int tickcount = 0;
  bool prediction_in_prediction = false;
  bool prediction_first_time_predicted = false;

  explicit movement_state_guard(Player* value)
    : player(value), origin(value->get_origin()), abs_origin(value->get_abs_origin()),
      velocity(value->get_velocity()), base_velocity(value->get_base_velocity()),
      view_offset(value->get_view_offset()), flags(value->get_flags()),
      ground_entity(value->get_ground_entity_handle()), buttons(value->get_buttons()),
      last_buttons(value->get_last_buttons()), ducked(value->get_ducked()),
      ducking(value->get_ducking_state()), in_duck_jump(value->get_in_duck_jump()),
      duck_time(value->get_duck_time()), duck_jump_time(value->get_duck_jump_time()),
      fall_velocity(value->get_fall_velocity()), tickbase(value->get_tickbase()),
      current_command(value->get_current_cmd()), move_type(value->get_move_type()),
      water_level(value->get_water_level()),
      previous_host(move_helper != nullptr ? move_helper->get_host() : nullptr),
      curtime(global_vars->curtime),
      frametime(global_vars->frametime), tickcount(global_vars->tickcount),
      prediction_in_prediction(prediction->in_prediction),
      prediction_first_time_predicted(prediction->first_time_predicted) {}

  void restore()
  {
    if (player == nullptr) {
      return;
    }

    player->set_origin(origin);
    player->set_abs_origin(abs_origin);
    player->set_velocity(velocity);
    player->set_base_velocity(base_velocity);
    player->set_view_offset(view_offset);
    player->set_flags(flags);
    player->set_ground_entity_handle(ground_entity);
    player->set_buttons(buttons);
    player->set_last_buttons(last_buttons);
    player->set_ducked(ducked);
    player->set_ducking_state(ducking);
    player->set_in_duck_jump(in_duck_jump);
    player->set_duck_time(duck_time);
    player->set_duck_jump_time(duck_jump_time);
    player->set_fall_velocity(fall_velocity);
    player->set_tickbase(tickbase);
    player->set_current_cmd(current_command);
    player->set_move_type(move_type);
    player->set_water_level(water_level);
    move_helper->set_host(previous_host);
    global_vars->curtime = curtime;
    global_vars->frametime = frametime;
    global_vars->tickcount = tickcount;
    prediction->in_prediction = prediction_in_prediction;
    prediction->first_time_predicted = prediction_first_time_predicted;
  }

  ~movement_state_guard()
  {
    restore();
  }
};

[[nodiscard]] float normalize_2d_yaw(float yaw)
{
  return azimuth_to_signed(yaw);
}

[[nodiscard]] float vector_length_2d(const Vec3& vector)
{
  return std::sqrt((vector.x * vector.x) + (vector.y * vector.y));
}

void auto_jump(user_cmd* user_cmd, Player* localplayer)
{
  if (!config.misc.movement.bhop || user_cmd == nullptr || localplayer == nullptr) {
    return;
  }

  const bool last_jump = g_auto_jump_state.jump;
  const bool last_grounded = g_auto_jump_state.grounded;

  const bool current_jump = g_auto_jump_state.jump = (user_cmd->buttons & IN_JUMP) != 0;
  const bool current_grounded = g_auto_jump_state.grounded = localplayer->is_on_ground();

  if (current_jump && last_jump && (current_grounded ? !localplayer->is_ducking() : true)) {
    if (!(current_grounded && !last_grounded)) {
      user_cmd->buttons &= ~IN_JUMP;
    }

    if ((user_cmd->buttons & IN_JUMP) == 0 && current_grounded && !g_auto_jump_state.attempted) {
      user_cmd->buttons |= IN_JUMP;
    }
  }

  g_auto_jump_state.attempted = (user_cmd->buttons & IN_JUMP) != 0;

  anti_cheat_compat::on_auto_jump(user_cmd, localplayer);
}

void edge_jump(user_cmd* user_cmd, Player* localplayer)
{
  if (user_cmd == nullptr || localplayer == nullptr || !config.misc.movement.edge_jump) {
    g_edge_jump_was_grounded = localplayer != nullptr && localplayer->is_on_ground();
    return;
  }

  const bool grounded = localplayer->is_on_ground();
  const bool has_movement = (user_cmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)) != 0;
  if (g_edge_jump_was_grounded && !grounded && has_movement && (user_cmd->buttons & IN_JUMP) == 0) {
    user_cmd->buttons |= IN_JUMP;
  }
  g_edge_jump_was_grounded = grounded;
}

void jumpbug(user_cmd* user_cmd, Player* localplayer)
{
  if (!config.misc.movement.jumpbug || user_cmd == nullptr || localplayer == nullptr || engine_trace == nullptr ||
      localplayer->is_on_ground() || (user_cmd->buttons & IN_DUCK) == 0 || localplayer->get_velocity().z > -650.0f) {
    return;
  }

  const Vec3 origin = localplayer->get_origin();
  Vec3 mins = localplayer->get_collideable_mins();
  Vec3 maxs = localplayer->get_collideable_maxs();
  const float unduck_height = 20.0f;
  const float trace_distance = unduck_height + 2.0f;
  Vec3 end = origin;
  end.z -= trace_distance;

  trace_t trace{};
  Vec3 start = origin;
  Vec3 trace_end = end;
  engine_trace->trace_hull(&start, &trace_end, &mins, &maxs, MASK_PLAYERSOLID, &trace);
  if (trace.fraction >= 1.0f || trace.fraction * trace_distance < unduck_height) {
    return;
  }

  user_cmd->buttons &= ~IN_DUCK;
  user_cmd->buttons |= IN_JUMP;
}

void duck_jump(user_cmd* user_cmd, Player* localplayer)
{
  if (!config.misc.movement.duck_jump || user_cmd == nullptr || localplayer == nullptr ||
      !localplayer->is_on_ground() || (user_cmd->buttons & IN_JUMP) == 0 ||
      (localplayer->get_flags() & FL_INWATER) != 0) {
    return;
  }

  user_cmd->buttons |= IN_DUCK;
}

void break_jump(user_cmd* user_cmd, Player* localplayer)
{
  if (user_cmd == nullptr || localplayer == nullptr || !config.misc.movement.break_jump) {
    g_break_jump_state = {};
    return;
  }

  const bool last_jump = g_break_jump_state.last_jump;
  const bool current_jump = (user_cmd->buttons & IN_JUMP) != 0;
  g_break_jump_state.last_jump = current_jump;

  if (localplayer->is_on_ground()) {
    g_break_jump_state.ticks_since_grounded = -1;
  }
  ++g_break_jump_state.ticks_since_grounded;

  const int ticks = g_break_jump_state.ticks_since_grounded;
  if (ticks > 1 || (ticks == 0 &&
      (last_jump || !current_jump || localplayer->is_ducking()))) {
    return;
  }

  user_cmd->buttons |= IN_DUCK;
}

void auto_reverse_jump(user_cmd* user_cmd, Player* localplayer)
{
  if (user_cmd == nullptr || localplayer == nullptr || !config.misc.movement.auto_reverse_jump) {
    g_reverse_jump_last_attack2 = false;
    return;
  }

  const bool current_attack2 = (user_cmd->buttons & IN_ATTACK2) != 0;
  const bool pressed_attack2 = current_attack2 && !g_reverse_jump_last_attack2;
  g_reverse_jump_last_attack2 = current_attack2;

  if (!pressed_attack2 || !localplayer->is_on_ground() || localplayer->is_ducking()) {
    return;
  }

  Weapon* weapon = localplayer->get_weapon();
  if (weapon == nullptr || !weapon->is_minigun()) {
    return;
  }

  user_cmd->buttons |= IN_JUMP;
}

void reset_auto_edgebug()
{
  g_edgebug_solution = {};
}

void apply_auto_edgebug_command(user_cmd* user_cmd, const edgebug_solution& solution)
{
  if (user_cmd == nullptr || !solution.valid) {
    return;
  }

  if (solution.duck) {
    user_cmd->buttons |= IN_DUCK;
  } else {
    user_cmd->buttons &= ~IN_DUCK;
  }

  user_cmd->forwardmove = solution.forwardmove;
  user_cmd->sidemove = solution.sidemove;
  if (solution.yaw_delta != 0.0f) {
    user_cmd->view_angles.y = normalize_2d_yaw(user_cmd->view_angles.y + solution.yaw_delta);
  }
}

bool simulate_edgebug_candidate(
  Player* localplayer,
  const user_cmd& source_command,
  bool duck,
  float yaw_delta,
  edgebug_solution& solution,
  movement_state_guard& state)
{
  if (localplayer == nullptr || prediction == nullptr || game_movement == nullptr || move_helper == nullptr ||
      global_vars == nullptr) {
    return false;
  }

  state.restore();
  prediction->in_prediction = true;
  prediction->first_time_predicted = false;
  move_helper->set_host(localplayer);

  const float interval = tick_interval();
  static Convar* sv_gravity = nullptr;
  if (sv_gravity == nullptr && convar_system != nullptr) {
    sv_gravity = convar_system->find_var("sv_gravity");
  }
  const float gravity = sv_gravity != nullptr ? std::max(0.0f, sv_gravity->get_float()) : 800.0f;
  const float fall_per_tick = -gravity * interval;
  const float original_vertical_velocity = localplayer->get_velocity().z;
  const float start_time = global_vars->curtime;
  const float max_yaw_delta = std::fabs(yaw_delta);

  user_cmd simulated_command = source_command;
  simulated_command.buttons = duck ? (source_command.buttons | IN_DUCK) : (source_command.buttons & ~IN_DUCK);
  simulated_command.command_number = source_command.command_number;
  simulated_command.tick_count = source_command.tick_count;
  if (yaw_delta == 0.0f) {
    simulated_command.forwardmove = source_command.forwardmove;
    simulated_command.sidemove = source_command.sidemove;
  } else {
    if (simulated_command.forwardmove == 0.0f && simulated_command.sidemove == 0.0f) {
      simulated_command.sidemove = 450.0f;
    }
    simulated_command.forwardmove = std::clamp(simulated_command.forwardmove, -450.0f, 450.0f);
    simulated_command.sidemove = std::clamp(simulated_command.sidemove, -450.0f, 450.0f);
  }

  localplayer->set_current_cmd(&simulated_command);
  for (int tick = 1; tick <= 96; ++tick) {
    const Vec3 previous_velocity = localplayer->get_velocity();
    const bool was_grounded = localplayer->is_on_ground();
    if (previous_velocity.z >= 0.0f || was_grounded) {
      break;
    }

    const float tick_yaw_delta = yaw_delta == 0.0f
      ? 0.0f
      : std::clamp(yaw_delta * static_cast<float>(tick), -max_yaw_delta, max_yaw_delta);
    simulated_command.view_angles.y = normalize_2d_yaw(source_command.view_angles.y + tick_yaw_delta);
    simulated_command.command_number = source_command.command_number + tick;
    simulated_command.tick_count = source_command.tick_count + tick;
    localplayer->set_current_cmd(&simulated_command);
    global_vars->curtime = start_time + static_cast<float>(tick) * interval;
    global_vars->frametime = prediction->engine_paused ? 0.0f : interval;
    global_vars->tickcount = simulated_command.tick_count;
    prediction->set_local_view_angles(simulated_command.view_angles);

    MoveData move{};
    prediction->setup_move(localplayer, &simulated_command, move_helper, &move);
    if (!game_movement->process_movement(localplayer, &move)) {
      break;
    }
    prediction->finish_move(localplayer, &simulated_command, &move);
    localplayer->set_velocity(move.m_vecVelocity);
    localplayer->set_origin(move.GetAbsOrigin());
    localplayer->set_abs_origin(move.GetAbsOrigin());

    const Vec3 current_velocity = localplayer->get_velocity();
    const bool landed = localplayer->is_on_ground();
    const float expected_velocity = previous_velocity.z + fall_per_tick;
    const bool edge_collision = previous_velocity.z > original_vertical_velocity + 0.01f &&
      std::fabs(current_velocity.z - expected_velocity) <= 2.5f;
    if (!landed && edge_collision) {
      solution = {
        .valid = true,
        .duck = duck,
        .ticks = tick,
        .yaw_delta = yaw_delta,
        .forwardmove = simulated_command.forwardmove,
        .sidemove = simulated_command.sidemove
      };
      localplayer->set_current_cmd(state.current_command);
      return true;
    }
  }

  localplayer->set_current_cmd(state.current_command);
  return false;
}

bool find_auto_edgebug_solution(user_cmd* user_cmd, Player* localplayer, edgebug_solution& solution)
{
  if (user_cmd == nullptr || localplayer == nullptr || prediction == nullptr || game_movement == nullptr ||
      move_helper == nullptr || global_vars == nullptr) {
    return false;
  }

  movement_state_guard state{localplayer};
  const auto mode = config.misc.movement.auto_edgebug;
  const bool can_strafe = mode == Misc::Movement::auto_edgebug_mode::STRAFE ||
    mode == Misc::Movement::auto_edgebug_mode::STRAFE_SILENT;
  const std::array<float, 3> yaw_samples = can_strafe
    ? std::array<float, 3>{-45.0f, 0.0f, 45.0f}
    : std::array<float, 3>{0.0f, 0.0f, 0.0f};

  for (const float yaw_delta : yaw_samples) {
    if (simulate_edgebug_candidate(localplayer, *user_cmd, true, yaw_delta, solution, state)) {
      return true;
    }
    if (simulate_edgebug_candidate(localplayer, *user_cmd, false, yaw_delta, solution, state)) {
      return true;
    }
  }

  return false;
}

void auto_strafe_legit(user_cmd* user_cmd)
{
  if (user_cmd == nullptr || user_cmd->mouse_dx == 0) {
    return;
  }

  static Convar* cl_sidespeed = nullptr;
  if (cl_sidespeed == nullptr && convar_system != nullptr) {
    cl_sidespeed = convar_system->find_var("cl_sidespeed");
  }

  const float side_speed = cl_sidespeed != nullptr ? cl_sidespeed->get_float() : 450.0f;

  user_cmd->forwardmove = 0.0f;
  user_cmd->sidemove = user_cmd->mouse_dx > 0 ? side_speed : -side_speed;
}

void auto_strafe_directional(user_cmd* user_cmd, Player* localplayer)
{
  if (user_cmd == nullptr || localplayer == nullptr) {
    return;
  }

  if ((user_cmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)) == 0) {
    return;
  }

  const float forward_move = user_cmd->forwardmove;
  const float side_move = user_cmd->sidemove;

  Vec3 forward{};
  Vec3 right{};
  angle_vectors(user_cmd->view_angles, &forward, &right, nullptr);
  forward.z = 0.0f;
  right.z = 0.0f;

  const float forward_length = vector_length_2d(forward);
  const float right_length = vector_length_2d(right);
  if (forward_length <= 0.001f || right_length <= 0.001f) {
    return;
  }

  forward.x /= forward_length;
  forward.y /= forward_length;
  right.x /= right_length;
  right.y /= right_length;

  Vec3 wish_direction{
    (forward.x * forward_move) + (right.x * side_move),
    (forward.y * forward_move) + (right.y * side_move),
    0.0f
  };

  if (vector_length_2d(wish_direction) <= 0.001f) {
    return;
  }

  const Vec3 velocity = localplayer->get_velocity();
  if (vector_length_2d(velocity) <= 1.0f) {
    return;
  }

  const float direction_delta = normalize_2d_yaw(vector_yaw(wish_direction) - vector_yaw(velocity));
  if (std::fabs(direction_delta) > config.misc.movement.auto_strafe_max_delta) {
    return;
  }

  const float turn_scale = 0.9f + (0.1f * config.misc.movement.auto_strafe_turn_scale);
  const float rotation = ((direction_delta > 0.0f ? -90.0f : 90.0f) + (direction_delta * turn_scale)) * pideg;
  const float cos_rotation = std::cos(rotation);
  const float sin_rotation = std::sin(rotation);

  user_cmd->forwardmove = (cos_rotation * forward_move) - (sin_rotation * side_move);
  user_cmd->sidemove = (sin_rotation * forward_move) + (cos_rotation * side_move);
}

void auto_strafe(user_cmd* user_cmd, Player* localplayer)
{
  if (user_cmd == nullptr || localplayer == nullptr) {
    return;
  }

  if (config.misc.movement.auto_strafe == Misc::Movement::auto_strafe_mode::OFF) {
    return;
  }

  if (localplayer->is_on_ground() || (localplayer->get_flags() & FL_INWATER) != 0 ||
      localplayer->get_move_type() != MOVETYPE_WALK) {
    return;
  }

  switch (config.misc.movement.auto_strafe) {
  case Misc::Movement::auto_strafe_mode::LEGIT:
    auto_strafe_legit(user_cmd);
    break;
  case Misc::Movement::auto_strafe_mode::DIRECTIONAL:
    auto_strafe_directional(user_cmd, localplayer);
    break;
  case Misc::Movement::auto_strafe_mode::OFF:
  default:
    break;
  }
}

void fast_stop(user_cmd* user_cmd, Player* localplayer)
{
  if (user_cmd == nullptr || localplayer == nullptr || !config.misc.movement.fast_stop ||
      !localplayer->is_on_ground() || (localplayer->get_flags() & FL_INWATER) != 0 ||
      localplayer->get_move_type() != MOVETYPE_WALK ||
      std::fabs(user_cmd->forwardmove) > 0.0f || std::fabs(user_cmd->sidemove) > 0.0f) {
    return;
  }

  Vec3 velocity = localplayer->get_velocity();
  velocity.z = 0.0f;
  const float speed = vector_length_2d(velocity);
  if (!std::isfinite(speed) || speed < 0.1f) {
    return;
  }

  static Convar* sv_friction = nullptr;
  static Convar* sv_stopspeed = nullptr;
  if (convar_system != nullptr) {
    if (sv_friction == nullptr) {
      sv_friction = convar_system->find_var("sv_friction");
    }
    if (sv_stopspeed == nullptr) {
      sv_stopspeed = convar_system->find_var("sv_stopspeed");
    }
  }

  const float friction = sv_friction != nullptr ? std::max(0.0f, sv_friction->get_float()) : 4.0f;
  const float stop_speed = sv_stopspeed != nullptr ? std::max(0.0f, sv_stopspeed->get_float()) : 100.0f;
  const float interval = tick_interval();
  const float control = std::max(speed, stop_speed);
  const float drop = control * friction * interval;
  const float friction_speed = std::max(speed - drop, 0.0f);
  if (friction_speed < 10.0f) {
    return;
  }

  const float view_yaw = std::isfinite(user_cmd->view_angles.y) ? user_cmd->view_angles.y : 0.0f;
  const float delta = normalize_2d_yaw(view_yaw - vector_yaw(velocity)) * pideg;
  user_cmd->forwardmove = -std::cos(delta) * friction_speed;
  user_cmd->sidemove = -std::sin(delta) * friction_speed;
  g_fast_stop_command = user_cmd->command_number;
}

void fast_accelerate(user_cmd* user_cmd, Player* localplayer)
{
  if (user_cmd == nullptr || localplayer == nullptr || !localplayer->is_on_ground() ||
      (localplayer->get_flags() & FL_INWATER) != 0 ||
      (user_cmd->buttons & (IN_ATTACK | IN_ATTACK2 | IN_ATTACK3)) != 0 ||
      client_state == nullptr || client_state->chokedcommands != 0) {
    return;
  }

  if (!config.misc.movement.fast_accelerate || localplayer->is_ducking() ||
      (user_cmd->buttons & IN_DUCK) != 0) {
    return;
  }

  const float speed = vector_length_2d(localplayer->get_velocity());
  const float max_speed = std::min(localplayer->get_max_speed() * 0.9f, 520.0f) - 10.0f;
  if (speed >= max_speed || (user_cmd->forwardmove == 0.0f && user_cmd->sidemove == 0.0f)) {
    return;
  }

  const float move_length = std::hypot(user_cmd->forwardmove, user_cmd->sidemove);
  if (move_length <= 1.0f) {
    return;
  }

  const float reverse_yaw = std::atan2(-user_cmd->sidemove, -user_cmd->forwardmove) * radpi;
  user_cmd->forwardmove = -move_length;
  user_cmd->sidemove = 0.0f;
  user_cmd->view_angles.y = normalize_2d_yaw(user_cmd->view_angles.y - reverse_yaw);
  user_cmd->view_angles.z = 270.0f;
}

bool moonwalk(user_cmd* user_cmd, Player* localplayer)
{
  if (user_cmd == nullptr || localplayer == nullptr) {
    return false;
  }

  if (!config.misc.movement.moonwalk) {
    return false;
  }

  if (!localplayer->is_ducking() || !localplayer->is_on_ground()) {
    return false;
  }

  const Vec3 velocity = localplayer->get_velocity();
  const float speed_2d = vector_length_2d(velocity);
  const float max_speed_threshold = std::min(localplayer->get_max_speed() * 0.9f, 520.0f) - 10.0f;
  if (speed_2d >= max_speed_threshold) {
    return false;
  }

  const bool has_input = user_cmd->forwardmove != 0.0f || user_cmd->sidemove != 0.0f;
  if (!has_input) {
    return false;
  }

  const bool has_button_input = (user_cmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)) != 0;
  if (!has_button_input && !config.misc.movement.moonwalk_navbot_compat) {
    return false;
  }

  if (client_state == nullptr || client_state->chokedcommands != 0) {
    return false;
  }

  if (config.misc.movement.moonwalk_forward) {
    user_cmd->forwardmove *= -1.0f;
    user_cmd->sidemove *= -1.0f;
    user_cmd->view_angles.x = 91.0f;
  }

  const float move_fwd = user_cmd->forwardmove;
  const float move_side = user_cmd->sidemove;
  const float move_length = std::hypot(move_fwd, move_side);
  const float reverse_yaw = std::atan2(-move_side, -move_fwd) * radpi;

  const float player_max_speed = localplayer->get_max_speed();
  const float boost_length = player_max_speed > 1.0f ? std::max(move_length, player_max_speed) : move_length;

  user_cmd->forwardmove = -boost_length;
  user_cmd->sidemove = 0.0f;
  user_cmd->view_angles.y = normalize_2d_yaw(user_cmd->view_angles.y - reverse_yaw);
  user_cmd->view_angles.z = 270.0f;

  return true;
}

}

void bhop(user_cmd* user_cmd)
{
  if (user_cmd == nullptr || entity_list == nullptr) {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  g_fast_stop_command = -1;
  auto_jump(user_cmd, localplayer);
  edge_jump(user_cmd, localplayer);
  jumpbug(user_cmd, localplayer);
  duck_jump(user_cmd, localplayer);
  break_jump(user_cmd, localplayer);
  auto_reverse_jump(user_cmd, localplayer);
  fast_stop(user_cmd, localplayer);
  auto_strafe(user_cmd, localplayer);
}

void movement_post_prediction(user_cmd* user_cmd)
{
  if (user_cmd == nullptr || entity_list == nullptr) {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  if (g_fast_stop_command != user_cmd->command_number) {
    fast_accelerate(user_cmd, localplayer);
  }
}

bool auto_edgebug_create_move(user_cmd* user_cmd)
{
  const auto mode = config.misc.movement.auto_edgebug;
  if (mode == Misc::Movement::auto_edgebug_mode::OFF || user_cmd == nullptr || entity_list == nullptr) {
    reset_auto_edgebug();
    return false;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive() || localplayer->get_move_type() != MOVETYPE_WALK ||
      localplayer->get_water_level() > 1 || prediction == nullptr || game_movement == nullptr ||
      move_helper == nullptr || global_vars == nullptr) {
    reset_auto_edgebug();
    return false;
  }

  if (localplayer->is_on_ground() || (user_cmd->buttons & (IN_ATTACK | IN_ATTACK2 | IN_ATTACK3)) != 0) {
    reset_auto_edgebug();
    return false;
  }

  if (g_edgebug_solution.valid && g_edgebug_solution.ticks > 0) {
    apply_auto_edgebug_command(user_cmd, g_edgebug_solution);
    --g_edgebug_solution.ticks;
    const bool silent = mode == Misc::Movement::auto_edgebug_mode::STRAFE_SILENT;
    if (g_edgebug_solution.ticks == 0) {
      g_edgebug_solution = {};
    }
    return silent;
  }

  reset_auto_edgebug();
  if (localplayer->get_velocity().z >= 0.0f) {
    return false;
  }

  edgebug_solution solution{};
  if (!find_auto_edgebug_solution(user_cmd, localplayer, solution)) {
    return false;
  }

  g_edgebug_solution = solution;
  apply_auto_edgebug_command(user_cmd, g_edgebug_solution);
  --g_edgebug_solution.ticks;
  const bool silent = mode == Misc::Movement::auto_edgebug_mode::STRAFE_SILENT;
  if (g_edgebug_solution.ticks == 0) {
    g_edgebug_solution = {};
  }
  return silent;
}

bool moonwalk_create_move(user_cmd* user_cmd)
{
  if (user_cmd == nullptr || entity_list == nullptr) {
    return false;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return false;
  }

  const bool applied = moonwalk(user_cmd, localplayer);
  if (applied) {
    g_moonwalk_applied_command = user_cmd->command_number;
  }

  return applied;
}

bool moonwalk_applied_to_command(int command_number)
{
  return command_number > 0 && g_moonwalk_applied_command == command_number;
}

void reset_movement_session_state()
{
  g_moonwalk_applied_command = -1;
  g_fast_stop_command = -1;
  g_break_jump_state = {};
  g_edgebug_solution = {};
  g_auto_jump_state = {};
  g_edge_jump_was_grounded = false;
  g_reverse_jump_last_attack2 = false;
}
