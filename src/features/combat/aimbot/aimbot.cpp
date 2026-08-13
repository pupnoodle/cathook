#include "aimbot.hpp"
#include "aim_state.hpp"
#include "aim_spread.hpp"
#include "aim_auto_shoot.hpp"
#include "aim_scope.hpp"
#include "aim_walk.hpp"
#include "aim_targeting.hpp"
#include "aim_utils.hpp"
#include "aimbot_debug.hpp"
#include "hitscan_aim.hpp"
#include "melee_aim.hpp"
#include "projectile_aim.hpp"
#include "resolver.hpp"
#include "core/entity_cache.hpp"
#include "core/logger.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"
#include <array>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace aimbot {
namespace {

struct aimbot_run_context;

struct fire_readiness {
  bool attack = false;
  bool headshot = true;
  bool charge = true;
  bool trace = true;
  bool settled = true;
  bool primary = true;

  bool ready() const {
    return attack && headshot && charge && trace && settled && primary;
  }
};

struct aimbot_run_context {
  user_cmd* cmd = nullptr;
  Player* local = nullptr;
  Weapon* weapon = nullptr;
  Vec3 source_angles{};
  Vec3 target_angles{};
  Vec3 applied_angles{};
  aimbot_candidate target{};
  aimbot_debug_state debug{};
  aim_spread::hitscan_fire_solution hitscan_fire{};
  aim_auto_shoot::result auto_shoot{};
  fire_readiness readiness{};
  bool hitscan = false;
  bool melee = false;
  bool projectile = false;
  bool psilent = false;

  bool manual_attack = false;

  aimbot_run_result finish(aimbot_debug_reason reason) {
    aimbot_state& state = current_state();
    debug.reason = reason;
    debug.requested_shot = state.requested_shot;
    aimbot_debug_set_state(debug);
    store_input_angles(cmd != nullptr ? cmd->view_angles : source_angles);
    return {
      .psilent_command = psilent,
      .requested_shot = state.requested_shot,
      .active_target = state.active_target
    };
  }
};

struct pending_shot_diagnostic {
  bool active = false;
  bool fire_event = false;
  bool backtrack = false;
  bool readiness_ready = false;
  bool attack_button = false;
  std::uint64_t id = 0;
  float issued_realtime = 0.0f;
  float fired_realtime = 0.0f;
  int target_index = -1;
  int hitbox = -1;
  int command_tick = 0;
  int weapon_def_id = -1;
  float simulation_time = 0.0f;
  int pose_generation = 0;
  Vec3 target_origin{};
  Vec3 aim_position{};
};

struct shot_log_state {
  std::unique_ptr<cathook::core::logger> file{};
  std::array<pending_shot_diagnostic, 16> pending{};
  std::uint64_t next_id = 1;
};

shot_log_state& get_shot_log_state() {
  static shot_log_state state{};
  return state;
}

void write_shot_log(const char* event, const pending_shot_diagnostic& shot, int damage = 0) {
  shot_log_state& state = get_shot_log_state();
  if (!state.file) {
    state.file = std::make_unique<cathook::core::logger>(
      cathook::core::log_directory() / "shots.log");
  }
  if (!state.file->is_open()) {
    return;
  }

  const float realtime = global_vars != nullptr ? global_vars->realtime : 0.0f;
  const float game_time = global_vars != nullptr ? global_vars->curtime : -1.0f;
#if defined(__linux__)

  const int process_id = static_cast<int>(::getpid());
#else

  const int process_id = -1;
#endif

  std::ostringstream line{};
  line << std::fixed << std::setprecision(3)
       << "pid=" << process_id
       << " event=" << (event != nullptr ? event : "unknown")
       << " shot=" << shot.id
       << " realtime=" << realtime
       << " game_time=" << game_time
       << " target=" << shot.target_index
       << " hitbox=" << shot.hitbox
       << " weapon_def=" << shot.weapon_def_id
       << " pose_time=" << shot.simulation_time
       << " pose_age_ms=" << ((game_time - shot.simulation_time) * 1000.0f)
       << " command_tick=" << shot.command_tick
       << " fire_event=" << (shot.fire_event ? 1 : 0)
       << " backtrack=" << (shot.backtrack ? 1 : 0)
       << " readiness=" << (shot.readiness_ready ? 1 : 0)
       << " attack_button=" << (shot.attack_button ? 1 : 0)
       << " pose_generation=" << shot.pose_generation
       << " damage=" << damage
       << " target_origin={" << shot.target_origin.x << ',' << shot.target_origin.y << ',' << shot.target_origin.z << "}"
       << " aim_position={" << shot.aim_position.x << ',' << shot.aim_position.y << ',' << shot.aim_position.z << "}";
  state.file->write(line.str());
}

pending_shot_diagnostic* allocate_shot_diagnostic() {
  shot_log_state& state = get_shot_log_state();
  pending_shot_diagnostic* oldest = &state.pending[0];
  for (pending_shot_diagnostic& shot : state.pending) {
    if (!shot.active) {
      return &shot;
    }
    if (shot.issued_realtime < oldest->issued_realtime) {
      oldest = &shot;
    }
  }
  return oldest;
}

void note_shot_diagnostic(const aimbot_run_context& ctx) {
  if (ctx.target.player == nullptr || ctx.weapon == nullptr || global_vars == nullptr) {
    return;
  }
  pending_shot_diagnostic* shot = allocate_shot_diagnostic();
  *shot = {};
  shot->active = true;
  shot->id = get_shot_log_state().next_id++;
  shot->issued_realtime = global_vars->realtime;
  shot->target_index = ctx.target.player->get_index();
  shot->hitbox = ctx.target.hitbox;
  shot->command_tick = ctx.cmd != nullptr ? ctx.cmd->tick_count : 0;
  shot->weapon_def_id = ctx.weapon->get_def_id();
  shot->simulation_time = ctx.target.simulation_time;
  shot->backtrack = ctx.target.backtrack;
  shot->readiness_ready = ctx.readiness.ready();
  shot->attack_button = ctx.cmd != nullptr && (ctx.cmd->buttons & IN_ATTACK) != 0;
  shot->pose_generation = aimbot_current_pose_generation(ctx.target.player);
  shot->target_origin = ctx.target.player->get_origin();
  shot->aim_position = ctx.target.aim_position;

}

bool weapon_allows_primary_fire(Player* localplayer, Weapon* weapon) {
  return aim_scope::fire_ready(localplayer, weapon);
}

bool should_hold_sniper_charge(const aimbot_run_context& ctx) {
  return config.aimbot.auto_shoot &&
    ctx.hitscan &&
    ctx.target.entity != nullptr &&
    ctx.local != nullptr &&
    ctx.weapon != nullptr &&
    ctx.weapon->is_sniper_rifle() &&
    aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_charge) &&
    aimbot_sniper_scope_confirmed(ctx.local) &&
    !ctx.readiness.charge;
}

bool melee_swing_active(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr || !aimbot_is_melee_weapon(weapon)) {
    return false;
  }

  constexpr float max_active_swing_time = 0.5f;
  const float current_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
  const float smack_time = weapon->get_smack_time();
  return smack_time > current_time && smack_time - current_time <= max_active_swing_time;
}

void apply_visible_view(user_cmd* cmd, bool force_visible = false) {
  if (cmd == nullptr || (config.aimbot.aim_mode == Aim::AimMode::PSILENT && !force_visible)) {
    return;
  }

  Vec3 angles = cmd->view_angles;
  if (prediction != nullptr) {
    prediction->set_local_view_angles(angles);
    prediction->set_view_angles(angles);
  }
  if (engine != nullptr) {
    engine->set_view_angles(angles);
  }
}

bool apply_scope_command(aimbot_run_context& ctx, const aim_scope::decision& command) {
  if (!aim_scope::apply(ctx.cmd, command)) {
    return false;
  }
  if (ctx.target.player != nullptr) {
    set_preference(ctx.target.player);
  }

  ctx.debug.scoped = aimbot_sniper_scope_active(ctx.local);
  ctx.debug.scoped_ready = false;
  return true;
}

void reset_all_state() {
  clear_target_state();
  reset_aimbot_scope_timing();
  aim_scope::reset_auto_scope();
}

void clear_invalid_state(Player* localplayer) {
  aimbot_state& state = current_state();
  if (!state.active_target) {
    state.target_player = nullptr;
    state.target_entity = nullptr;
  }

  if (active_target_player() != nullptr &&
      (!entity_cache_snapshot_contains_player(active_target_player()) ||
        aimbot_should_skip_player(localplayer, active_target_player()))) {
    state.target_player = nullptr;
    state.target_entity = nullptr;
    state.active_target = false;
  }

  if (state.preference.player != nullptr &&
      (!entity_cache_snapshot_contains_player(state.preference.player) ||
        aimbot_should_skip_player(localplayer, state.preference.player))) {
    clear_preference();
  }

  if (state.active_target && state.target_player == nullptr &&
      state.target_entity != nullptr &&
      state.target_entity->get_class_id() == class_id::PLAYER) {
    state.target_entity = nullptr;
    state.active_target = false;
  }
}

Vec3 candidate_command_angles(Player* localplayer, const aimbot_candidate& candidate, bool hitscan) {
  if (localplayer == nullptr) {
    return candidate.command_angles;
  }
  if (candidate.player != nullptr && aimbot_vec3_is_finite(candidate.command_angles)) {
    return candidate.command_angles;
  }
  if (hitscan) {
    return hitscan_aim_command_angles(localplayer, candidate.aim_angles);
  }
  return candidate.aim_angles - localplayer->get_punch_angles();
}

void populate_debug(aimbot_run_context& ctx) {
  aimbot_debug_state& debug = ctx.debug;
  const backtrack::backtrack_timing timing = backtrack::current_timing();
  debug.outgoing_latency = timing.outgoing_latency;
  debug.incoming_latency = timing.incoming_latency;
  debug.interpolation = backtrack::interpolation_time();
  debug.fake_interpolation = timing.fake_interp;
  debug.timing_correct = timing.correct;
  debug.lerp_ticks = timing.lerp_ticks;
  debug.candidates_total = aim_state::scan.candidates_total;
  debug.candidates_visible = aim_state::scan.candidates_visible;
  debug.candidates_rejected = aim_state::scan.candidates_rejected;
  debug.skipped_ignored = aim_state::scan.skipped_ignored;
  debug.skipped_friends = aim_state::scan.skipped_friends;
  debug.skipped_ipc = aim_state::scan.skipped_ipc;
  debug.skipped_cloaked = aim_state::scan.skipped_cloaked;
  debug.skipped_team = aim_state::scan.skipped_team;
  debug.skipped_invulnerable = aim_state::scan.skipped_invulnerable;
  debug.skipped_dead = aim_state::scan.skipped_dead;
  debug.skipped_type = aim_state::scan.skipped_type;
  debug.last_reject = aim_state::scan.last_reject;
  debug.last_skip = aim_state::scan.last_skip;
  debug.best_reject = aim_state::scan.best_reject;
  debug.pose = ctx.target.player != nullptr
    ? aimbot_get_pose_debug(ctx.target.player)
    : aimbot_get_pose_debug_index(debug.best_reject.entity_index);

  if (ctx.target.entity == nullptr) {
    return;
  }

  debug.selected_entity_index = ctx.target.entity->get_index();
  debug.selected_hitbox = ctx.target.hitbox;
  debug.selected_team = static_cast<int>(ctx.target.entity->get_team());
  debug.selected_health = aimbot_entity_health(ctx.target.entity);
  debug.selected_handle = ctx.target.entity->get_ref_handle();
  debug.selected_backtrack = ctx.target.backtrack;
  debug.selected_aim_position = ctx.target.aim_position;
  debug.selected_simulation_time = ctx.target.simulation_time;
  debug.pose_timing_valid = ctx.target.pose_timing_valid;
  debug.compensation_applied = ctx.target.pose_timing_valid &&
    aimbot_vec3_is_finite(ctx.target.pose_offset) &&
    aimbot_distance_squared(ctx.target.pose_offset, {}) > 0.0001f;
  debug.pose_target_tick = ctx.target.pose_target_tick;
  debug.pose_command_tick = ctx.target.pose_command_tick;
  debug.pose_lead_seconds = ctx.target.pose_lead_seconds;
  debug.pose_lead_distance = std::sqrt(aimbot_distance_squared(ctx.target.pose_offset, {}));
  if (ctx.target.player != nullptr) {
    debug.target_velocity = ctx.target.player->get_velocity();
    debug.target_speed = std::sqrt(
      (debug.target_velocity.x * debug.target_velocity.x) +
      (debug.target_velocity.y * debug.target_velocity.y) +
      (debug.target_velocity.z * debug.target_velocity.z));
  }
  debug.pose_offset = ctx.target.pose_offset;
  debug.fov = ctx.target.fov;
  debug.distance = ctx.target.distance;
  debug.tick_count = ctx.target.tick_count;
  debug.backtrack_timing_error = ctx.target.backtrack_timing_error;
  debug.backtrack_capture_gap = ctx.target.backtrack_capture_gap;

  if (ctx.target.player == nullptr) {
    return;
  }

  const resolver::resolver_debug_info info = resolver::debug_for_player(ctx.target.player);
  debug.resolver_active = info.active;
  debug.resolver_candidates = info.yaw_candidates;
  debug.resolver_misses = info.misses;
  debug.resolver_hits = info.hits;
  debug.resolver_yaw = info.yaw;
  debug.resolver_pitch = info.pitch;
  debug.resolver_mode = static_cast<int>(info.mode);
}

aimbot_candidate find_best_hitscan_target(Player* localplayer,
  Weapon* weapon,
  user_cmd* cmd,
  const Vec3& view_angles) {
  aimbot_candidate best{};
  aimbot_candidate best_ready{};
  aim_state::scan = {};

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    ++aim_state::scan.candidates_total;

    const aimbot_player_skip_reason skip_reason = aimbot_player_skip_reason_for(localplayer, entry, weapon);
    if (skip_reason != aimbot_player_skip_reason::none) {
      aim_state::record_player_skip(skip_reason, player);
      continue;
    }

    const aimbot_candidate current_candidate = hitscan_aim_find_candidate(localplayer, weapon, player, view_angles);
    const aimbot_candidate backtrack_candidate = backtrack::find_hitscan_candidate(
      localplayer, weapon, player, view_angles, has_preference(player));
    aimbot_candidate candidate = current_candidate;
    const bool current_ready = aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, cmd, current_candidate);
    const bool backtrack_ready = aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, cmd, backtrack_candidate);
    if (backtrack_candidate.entity != nullptr &&
        (aim_targeting::hitscan_fast_head_backtrack_better(backtrack_candidate, candidate) ||
          (!current_ready && backtrack_ready && aimbot_candidate_better(backtrack_candidate, candidate)))) {
      candidate = backtrack_candidate;
    }

    if (candidate.entity == nullptr) {
      const aimbot_reject_debug reject = candidate.reject_debug.reason != aimbot_reject_reason::none
        ? candidate.reject_debug
        : aim_state::make_reject_debug(player, aimbot_reject_reason::no_candidate);
      aim_state::record_reject(reject);
      continue;
    }

    if (!hitscan_aim_candidate_matches_configured_hitbox(candidate, localplayer, weapon)) {
      aim_state::record_reject(
        aim_state::make_candidate_reject_debug(candidate, aimbot_reject_reason::wrong_hitbox));
      continue;
    }

    ++aim_state::scan.candidates_visible;
    const float fov_limit = aimbot_fov_limit(candidate.preferred ? 1.35f : 1.0f);
    if (aimbot_fov_exceeds_limit(candidate.fov, candidate.preferred ? 1.35f : 1.0f)) {
      aim_state::record_reject(aim_state::make_candidate_reject_debug(candidate, aimbot_reject_reason::fov, fov_limit));
      continue;
    }

    if (aimbot_candidate_better(candidate, best)) {
      best = candidate;
    }

    for (const aimbot_candidate& ready_candidate : {current_candidate, backtrack_candidate}) {
      if (ready_candidate.entity == nullptr ||
          !hitscan_aim_candidate_matches_configured_hitbox(ready_candidate, localplayer, weapon) ||
          !aimbot_fov_within_limit(ready_candidate.fov, ready_candidate.preferred ? 1.35f : 1.0f)) {
        continue;
      }
      if (aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, cmd, ready_candidate) &&
          aim_targeting::hitscan_ready_candidate_better(ready_candidate, best_ready)) {
        best_ready = ready_candidate;
      }
    }
  }

  const aimbot_candidate non_player = aim_targeting::find_best_non_player_candidate(localplayer, weapon, view_angles);
  if (aimbot_candidate_better(non_player, best)) {
    best = non_player;
  }

  if (best_ready.entity != nullptr && best.player != nullptr &&
      (!aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, cmd, best) ||
        aim_targeting::hitscan_fast_head_backtrack_better(best_ready, best))) {
    best = best_ready;
  }
  return best;
}

void find_target(aimbot_run_context& ctx) {
  ctx.target = ctx.projectile
    ? projectile_aim::find_candidate(ctx.local, ctx.weapon, ctx.source_angles)
    : ctx.melee
    ? aim_targeting::find_best_candidate(ctx.local, ctx.weapon, ctx.cmd, ctx.source_angles)
    : find_best_hitscan_target(ctx.local, ctx.weapon, ctx.cmd, ctx.source_angles);
  set_active_target(ctx.target.entity, ctx.target.player, ctx.target.predicted_origin,
    ctx.projectile && ctx.target.predicted_origin_valid);
  populate_debug(ctx);
}

bool melee_ready(const aimbot_run_context& ctx) {
  if (!ctx.melee || ctx.target.entity == nullptr) {
    return true;
  }
  const Vec3 shot_angles = aimbot_mode_uses_visible_steering() ? ctx.applied_angles : ctx.target_angles;
  return ctx.target.player != nullptr
    ? melee_aim_trace_candidate(
        ctx.local,
        ctx.weapon,
        ctx.target.player,
        ctx.target.melee_swing_tick > 0
          ? ctx.target.predicted_origin
          : ctx.target.player->get_origin(),
        ctx.target.melee_swing_tick > 0
          ? ctx.target.melee_swing_start
          : ctx.local->get_shoot_pos(),
        shot_angles)
    : aimbot_entity_melee_reachable(ctx.local, ctx.weapon, ctx.target.entity, shot_angles);
}

bool hitscan_settled(const aimbot_run_context& ctx) {
  if (!ctx.hitscan || !aimbot_mode_uses_visible_steering()) {
    return true;
  }
  return hitscan_aim_trace_candidate(ctx.local, ctx.weapon, ctx.target, ctx.applied_angles);
}

void compute_angles(aimbot_run_context& ctx) {
  aimbot_state& state = current_state();
  ctx.target_angles = candidate_command_angles(ctx.local, ctx.target, ctx.hitscan);
  ctx.applied_angles = aimbot_apply_mode_angles(
    ctx.source_angles,
    ctx.target_angles,
    state.last_input_angles,
    state.last_input_angles_valid,
    ctx.target);
  ctx.cmd->view_angles = ctx.applied_angles;
}

void compute_hitscan_fire(aimbot_run_context& ctx) {
  if (!ctx.hitscan) {
    return;
  }

  const Vec3 shot_angles = aimbot_mode_uses_visible_steering() ? ctx.applied_angles : ctx.target_angles;
  ctx.hitscan_fire = aim_spread::prepare_hitscan_fire_solution(
    ctx.local, ctx.weapon, ctx.cmd, ctx.target, shot_angles);
  if (ctx.hitscan_fire.ready) {
    ctx.target.command_angles = ctx.hitscan_fire.command_angles;
    ctx.target.spread_compensated = ctx.hitscan_fire.spread_compensated;
    ctx.target.pellet_index = ctx.hitscan_fire.pellet_index;
    ctx.target.pellet_count = ctx.hitscan_fire.pellet_count;
    ctx.target.spread = ctx.hitscan_fire.spread;
  }

  ctx.debug.final_trace_hit = ctx.hitscan_fire.ready;
  ctx.debug.spread_compensated = ctx.hitscan_fire.spread_compensated;
  ctx.debug.spread_signature = ctx.hitscan_fire.spread_signature;
  ctx.debug.spread_fixed = ctx.hitscan_fire.spread_fixed;
  ctx.debug.spread = ctx.hitscan_fire.spread;
  ctx.debug.pellet_count = ctx.hitscan_fire.pellet_count;
  ctx.debug.pellet_index = ctx.hitscan_fire.pellet_index;
  ctx.debug.trace_hitbox = ctx.hitscan_fire.trace_hitbox;
  ctx.debug.trace_entity_index = ctx.hitscan_fire.trace_entity_index;
  ctx.debug.final_trace_fraction = ctx.hitscan_fire.trace_fraction;
  ctx.debug.final_trace_contents = ctx.hitscan_fire.trace_contents;
  ctx.debug.final_trace_end = ctx.hitscan_fire.trace_end;
}

void compute_readiness(aimbot_run_context& ctx) {
  ctx.readiness.headshot = !ctx.hitscan ||
    (hitscan_aim_candidate_matches_configured_hitbox(ctx.target, ctx.local, ctx.weapon) &&
      hitscan_aim_head_only_fire_ready(ctx.local, ctx.weapon, ctx.target) &&
      hitscan_aim_headshot_ready(ctx.local, ctx.weapon, ctx.target));
  ctx.readiness.charge = !ctx.hitscan || hitscan_aim_charge_ready(ctx.local, ctx.weapon, ctx.target);
  ctx.readiness.trace = (!ctx.hitscan || ctx.hitscan_fire.ready) && melee_ready(ctx);
  ctx.readiness.settled = hitscan_settled(ctx);
  ctx.readiness.primary = weapon_allows_primary_fire(ctx.local, ctx.weapon) &&
    (ctx.cmd->buttons & IN_ATTACK2) == 0;
  ctx.readiness.attack = ctx.target.entity != nullptr &&
    aim_auto_shoot::weapon_has_primary_ammo(ctx.weapon);

  ctx.debug.attack_gate_ready = ctx.readiness.attack;
  ctx.debug.charge_ready = ctx.readiness.charge;
  ctx.debug.trace_ready = ctx.readiness.trace;
  ctx.debug.settled = ctx.readiness.settled;
  ctx.debug.primary_ready = ctx.readiness.primary;

  if (ctx.hitscan) {
    ctx.debug.final_trace_hit = ctx.hitscan_fire.ready && ctx.readiness.settled;
  }

  const bool hold_sniper_charge = should_hold_sniper_charge(ctx);
  if (hold_sniper_charge) {

    ctx.cmd->buttons |= IN_ATTACK;
  }

  if (!ctx.readiness.ready() && (ctx.hitscan || ctx.melee) && !hold_sniper_charge && !ctx.manual_attack) {
    ctx.cmd->buttons &= ~IN_ATTACK;
  }

  ctx.debug.headshot_ready = ctx.readiness.headshot;
  ctx.debug.attack_ready = ctx.readiness.ready();
}

void apply_auto_shoot(aimbot_run_context& ctx) {
  if (config.aimbot.auto_shoot && ctx.readiness.ready() && !ctx.manual_attack) {
    ctx.auto_shoot = aim_auto_shoot::apply(ctx.cmd, ctx.weapon, ctx.target, ctx.hitscan, ctx.melee);
    set_requested_shot(ctx.auto_shoot.requested);
    aim_state::requested_shot = ctx.auto_shoot.requested;
  }
}

void apply_fire_state(aimbot_run_context& ctx) {
  const bool firing = (ctx.cmd->buttons & IN_ATTACK) != 0 || ctx.auto_shoot.release_attack;
  const bool melee_swing_pending = ctx.melee && !ctx.manual_attack &&
    melee_swing_active(ctx.local, ctx.weapon);
  const bool visible_steering = aimbot_mode_uses_visible_steering() || ctx.manual_attack;

  if (ctx.target.player != nullptr) {
    if (ctx.readiness.ready()) {
      set_preference(ctx.target.player);
    } else if (current_state().preference.player == ctx.target.player && ctx.hitscan) {
      clear_preference();
    }
  }

  if (firing && visible_steering) {
    ctx.cmd->view_angles = ctx.applied_angles;
  } else if (firing && ctx.hitscan && ctx.hitscan_fire.ready) {
    ctx.cmd->view_angles = ctx.hitscan_fire.command_angles;
  } else if (firing || melee_swing_pending) {
    ctx.cmd->view_angles = ctx.target_angles;
  }

  if (firing && ctx.hitscan && ctx.hitscan_fire.ready && ctx.target.player != nullptr) {
    resolver::note_shot(ctx.target.player, ctx.target.hitbox, ctx.target.simulation_time, ctx.target.backtrack);
  }
  if (firing && ctx.hitscan && ctx.hitscan_fire.ready && ctx.target.player != nullptr &&
      ctx.target.tick_count > 0) {
    ctx.cmd->tick_count = ctx.target.tick_count;
  }
  const bool diagnostic_attempt = ctx.auto_shoot.requested || ctx.auto_shoot.release_attack ||
    (firing && ctx.readiness.ready());
  if (diagnostic_attempt && ctx.hitscan && ctx.hitscan_fire.ready && ctx.target.player != nullptr) {
    note_shot_diagnostic(ctx);
  }

  ctx.psilent = config.aimbot.aim_mode == Aim::AimMode::PSILENT &&
    (firing || melee_swing_pending) && !ctx.manual_attack;
  ctx.debug.final_command_angles = ctx.cmd->view_angles;
  if (config.aimbot.aim_mode == Aim::AimMode::PSILENT && !ctx.psilent && !ctx.manual_attack) {
    ctx.cmd->view_angles = ctx.source_angles;
  }
  apply_visible_view(ctx.cmd, ctx.manual_attack);
}

aimbot_debug_reason classify_outcome(const aimbot_run_context& ctx) {
  if (ctx.target.entity == nullptr) {
    return aimbot_debug_reason::no_target;
  }
  if (!ctx.readiness.headshot) {
    return aimbot_debug_reason::headshot_wait;
  }
  if (!ctx.readiness.charge) {
    return aimbot_debug_reason::charge_wait;
  }
  if ((ctx.hitscan || ctx.melee) && !ctx.readiness.trace) {
    if (ctx.hitscan_fire.seed_missing) {
      return aimbot_debug_reason::spread_seed_missing;
    }
    if (ctx.hitscan_fire.hit_wrong_hitbox) {
      return aimbot_debug_reason::hitbox_miss;
    }
    return aimbot_debug_reason::final_trace_miss;
  }
  if (!ctx.readiness.settled) {
    return aimbot_debug_reason::settle_wait;
  }
  if (!ctx.readiness.primary) {
    return aimbot_debug_reason::primary_wait;
  }
  if (!ctx.readiness.ready()) {
    return aimbot_debug_reason::attack_not_ready;
  }
  return aimbot_debug_reason::attack_ready;
}

bool validate_context(aimbot_run_context& ctx) {
  if (!config.aimbot.master) {
    reset_all_state();
    ctx.finish(aimbot_debug_reason::disabled);
    return false;
  }

  ctx.local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (ctx.local == nullptr || !ctx.local->is_alive()) {
    if (config.aimbot.auto_shoot && !ctx.manual_attack) {
      ctx.cmd->buttons &= ~IN_ATTACK;
    }
    projectile_aim::reset_charge_tracking();
    reset_all_state();
    ctx.finish(aimbot_debug_reason::no_localplayer);
    return false;
  }

  update_aimbot_scope_timing(ctx.local);
  ctx.weapon = ctx.local->get_weapon();
  if (ctx.weapon == nullptr) {
    if (config.aimbot.auto_shoot && !ctx.manual_attack) {
      ctx.cmd->buttons &= ~IN_ATTACK;
    }
    projectile_aim::reset_charge_tracking();
    clear_target_state();
    reset_aimbot_scope_timing();
    ctx.finish(aimbot_debug_reason::no_weapon);
    return false;
  }

  ctx.debug.weapon_def_id = ctx.weapon->get_def_id();
  ctx.projectile = aimbot_is_projectile_weapon(ctx.weapon);
  if (ctx.projectile) {
    ctx.hitscan = false;
    ctx.melee = false;
    return true;
  }

  projectile_aim::reset_charge_tracking();
  ctx.melee = aimbot_is_melee_weapon(ctx.weapon);
  ctx.hitscan = !ctx.melee;
  return true;
}

}

void capture_latest_network_pose(Player* player, bool animation_already_updated) {
  Player* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (player == nullptr || player == localplayer) {
    return;
  }
  aimbot_capture_latest_network_pose(player, animation_already_updated);
}

void update_local_client_side_animation() {
  if (!nographics::is_enabled() || entity_list == nullptr) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  void** vtable = *reinterpret_cast<void***>(localplayer);
  constexpr std::size_t update_client_side_animation_index = 256;
  if (vtable == nullptr || vtable[update_client_side_animation_index] == nullptr) {
    return;
  }

  using update_client_side_animation_fn = void (*)(void*);
  reinterpret_cast<update_client_side_animation_fn>(
    vtable[update_client_side_animation_index])(localplayer);
}

void clear_network_pose(Player* player) {
  aimbot_clear_network_pose(player);
}

void update_shot_diagnostics() {
  if (global_vars == nullptr) {
    return;
  }
  const float now = global_vars->realtime;
  for (pending_shot_diagnostic& shot : get_shot_log_state().pending) {
    if (!shot.active) {
      continue;
    }
    const float timeout = shot.fire_event ? 0.8f : 0.4f;
    const float start = shot.fire_event ? shot.fired_realtime : shot.issued_realtime;
    if (now - start < timeout) {
      continue;
    }

    shot.active = false;
  }
}

void on_weapon_fire(Player* shooter) {
  if (shooter == nullptr || entity_list == nullptr || global_vars == nullptr ||
      shooter != entity_list->get_localplayer()) {
    return;
  }

  pending_shot_diagnostic* best = nullptr;
  for (pending_shot_diagnostic& shot : get_shot_log_state().pending) {
    if (!shot.active || shot.fire_event || global_vars->realtime - shot.issued_realtime > 0.5f) {
      continue;
    }
    if (best == nullptr || shot.issued_realtime > best->issued_realtime) {
      best = &shot;
    }
  }
  if (best == nullptr) {
    return;
  }
  best->fire_event = true;
  best->fired_realtime = global_vars->realtime;

}

void on_player_hurt(Player* attacker, Player* victim, int damage) {
  if (attacker == nullptr || victim == nullptr || entity_list == nullptr || global_vars == nullptr ||
      attacker != entity_list->get_localplayer()) {
    return;
  }

  pending_shot_diagnostic* best = nullptr;
  for (pending_shot_diagnostic& shot : get_shot_log_state().pending) {
    if (!shot.active || shot.target_index != victim->get_index() ||
        global_vars->realtime - shot.issued_realtime > 1.0f) {
      continue;
    }
    if (best == nullptr || (shot.fire_event && !best->fire_event) ||
        (shot.fire_event == best->fire_event && shot.issued_realtime > best->issued_realtime)) {
      best = &shot;
    }
  }
  if (best == nullptr) {
    return;
  }

  best->active = false;
}

aimbot_run_result run(user_cmd* cmd, const Vec3& original_view_angles, bool manual_attack) {
  aim_state::requested_shot = false;
  clear_frame_target();

  aimbot_run_context ctx{};
  ctx.cmd = cmd;
  ctx.source_angles = original_view_angles;
  ctx.manual_attack = manual_attack;
  ctx.debug.active = config.aimbot.master;
  ctx.debug.aim_mode = static_cast<int>(config.aimbot.aim_mode);

  if (cmd == nullptr) {
    return ctx.finish(aimbot_debug_reason::disabled);
  }

  if (!validate_context(ctx)) {
    return {
      .psilent_command = false,
      .requested_shot = current_state().requested_shot,
      .active_target = current_state().active_target
    };
  }

  if (!ctx.melee) {
    aim_state::clear_walk();
  }

  ctx.debug.aim_mode = static_cast<int>(config.aimbot.aim_mode);

  clear_invalid_state(ctx.local);
  find_target(ctx);
  const aim_scope::decision scoped_command = aim_scope::resolve(ctx.local, ctx.weapon, ctx.target);
  if (apply_scope_command(ctx, scoped_command)) {
    return ctx.finish(scoped_command.reason);
  }

  if (aimbot_should_auto_unrev(ctx.local, ctx.weapon, ctx.target)) {
    ctx.cmd->buttons |= IN_ATTACK2;
  }
  if (ctx.target.entity == nullptr) {

    if (config.aimbot.auto_shoot && !ctx.manual_attack) {
      ctx.cmd->buttons &= ~IN_ATTACK;
    }
    aim_state::clear_walk();
    if (ctx.projectile) {

      projectile_aim::cancel_charge_if_needed(ctx.cmd, ctx.local, ctx.weapon);
      if ((ctx.cmd->buttons & IN_ATTACK) == 0) {
        projectile_aim::reset_charge_tracking();
      }
    }
    return ctx.finish(aimbot_debug_reason::no_target);
  }

  ctx.cmd->buttons &= ~IN_RELOAD;
  if (!ctx.manual_attack && aimbot_should_auto_rev(ctx.local, ctx.weapon, ctx.target)) {
    ctx.cmd->buttons |= IN_ATTACK2;
    if (!ctx.manual_attack) {
      ctx.cmd->buttons &= ~IN_ATTACK;
    }
    return ctx.finish(aimbot_debug_reason::auto_rev);
  }

  if (!aim_scope::fire_ready(ctx.local, ctx.weapon)) {
    if (!ctx.manual_attack) {
      ctx.cmd->buttons &= ~IN_ATTACK;
    }
    ctx.debug.scoped = aimbot_sniper_scope_active(ctx.local);
    ctx.debug.scoped_ready = false;
    return ctx.finish(aimbot_debug_reason::scoped_only);
  }

  if (ctx.projectile) {
    const projectile_aim::apply_result projectile_result = projectile_aim::apply(
      ctx.cmd, ctx.local, ctx.weapon, ctx.source_angles, ctx.target, ctx.manual_attack);
    set_requested_shot(projectile_result.requested_shot);
    aim_state::requested_shot = projectile_result.requested_shot;
    ctx.psilent = projectile_result.psilent;
    ctx.debug.attack_ready = projectile_result.attack_ready;
    ctx.debug.final_trace_hit = true;
    ctx.debug.final_command_angles = ctx.cmd->view_angles;
    return ctx.finish(projectile_result.attack_ready
      ? aimbot_debug_reason::attack_ready
      : aimbot_debug_reason::attack_not_ready);
  }

  aim_walk::request(ctx.local, ctx.weapon, ctx.target);
  compute_angles(ctx);
  compute_hitscan_fire(ctx);
  compute_readiness(ctx);
  apply_auto_shoot(ctx);
  apply_fire_state(ctx);

  ctx.debug.scoped = aimbot_sniper_scope_active(ctx.local);
  ctx.debug.scoped_ready = true;
  return ctx.finish(classify_outcome(ctx));
}

void apply_walk_to_target(Player* localplayer, user_cmd* cmd) {
  aim_walk::apply(localplayer, cmd);
}

}
