#include "features/combat/backtrack/backtrack.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <optional>
#include "core/entity_cache.hpp"
#include "features/combat/aimbot/aim_utils.hpp"
#include "features/combat/aimbot/resolver.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/net_messages.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/model_info.hpp"
#include "core/hooks/cl_read_packets.hpp"

bool write_to_table(void** vtable, int index, void* func);
void* read_vtable_entry(void** vtable, int index, const char* hook_name);

namespace backtrack
{

namespace
{

constexpr int flow_outgoing = 0;
constexpr int flow_incoming = 1;
constexpr int net_channel_send_datagram_index = 44;
constexpr float fallback_max_unlag_seconds = 1.0f;
constexpr float record_hitbox_expansion = 1.25f;
constexpr float lag_compensation_delta_limit = 0.2f;

struct incoming_sequence {
  int reliable_state = 0;
  int sequence_number = 0;
  float realtime = 0.0f;
};

using send_datagram_fn = int (*)(net_channel*, bf_write*);

std::array<backtrack_history, max_entities> g_records{};
std::array<bool, max_entities> g_did_shoot{};
std::deque<incoming_sequence> g_sequences{};
int g_last_incoming_sequence = 0;
float g_latency_ramp = 0.0f;
float g_last_sent_interp = -1.0f;
float g_next_interp_send_time = 0.0f;
std::optional<Vec3> g_selected_position{};
void** g_hooked_net_channel_vtable = nullptr;
send_datagram_fn g_send_datagram_original = nullptr;

[[nodiscard]] float tick_interval()
{
  return global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : static_cast<float>(TICK_INTERVAL);
}

[[nodiscard]] float max_unlag_seconds()
{
  static Convar* sv_maxunlag = nullptr;
  if (sv_maxunlag == nullptr && convar_system != nullptr) {
    sv_maxunlag = convar_system->find_var("sv_maxunlag");
  }

  const float value = sv_maxunlag != nullptr ? sv_maxunlag->get_float() : fallback_max_unlag_seconds;
  return std::clamp(value, tick_interval(), fallback_max_unlag_seconds);
}

[[nodiscard]] float window_seconds()
{
  return std::clamp(static_cast<float>(config.backtrack.window_ms) * 0.001f, 0.0f, max_unlag_seconds());
}

[[nodiscard]] int time_to_ticks(float seconds)
{
  return static_cast<int>(0.5f + (seconds / std::max(tick_interval(), 0.0001f)));
}

[[nodiscard]] float ticks_to_time(int ticks)
{
  return static_cast<float>(ticks) * tick_interval();
}

[[nodiscard]] float interpolation_time_value()
{
  static Convar* cl_interp = nullptr;
  static Convar* cl_interp_ratio = nullptr;
  static Convar* cl_updaterate = nullptr;
  if (convar_system != nullptr) {
    if (cl_interp == nullptr) cl_interp = convar_system->find_var("cl_interp");
    if (cl_interp_ratio == nullptr) cl_interp_ratio = convar_system->find_var("cl_interp_ratio");
    if (cl_updaterate == nullptr) cl_updaterate = convar_system->find_var("cl_updaterate");
  }

  const float interp = cl_interp != nullptr ? cl_interp->get_float() : 0.0f;
  const float ratio = cl_interp_ratio != nullptr ? cl_interp_ratio->get_float() : 0.0f;
  const float updaterate = cl_updaterate != nullptr ? cl_updaterate->get_float() : 66.0f;
  return std::max(interp, ratio / std::max(updaterate, 1.0f));
}

[[nodiscard]] float requested_interpolation_time()
{
  const float requested = config.backtrack.interp_ms > 0.0f
    ? config.backtrack.interp_ms * 0.001f
    : window_seconds();
  return std::clamp(requested, interpolation_time_value(), max_unlag_seconds());
}

[[nodiscard]] net_channel* current_net_channel()
{
  return client_state != nullptr ? client_state->m_NetChannel : nullptr;
}

[[nodiscard]] bool localplayer_valid()
{
  if (entity_list == nullptr) {
    return false;
  }

  Player* localplayer = entity_list->get_localplayer();
  return localplayer != nullptr && localplayer->is_alive();
}

[[nodiscard]] bool should_run_network_state()
{
  net_channel* channel = current_net_channel();
  return is_enabled() &&
         engine != nullptr &&
         global_vars != nullptr &&
         engine->is_in_game() &&
         channel != nullptr &&
         !channel->is_loopback() &&
         localplayer_valid();
}

[[nodiscard]] bool should_run_fake_latency_network()
{
  return should_run_network_state() && config.backtrack.fake_latency_ms > 0.0f;
}

[[nodiscard]] bool should_record()
{
  return is_enabled() &&
         engine != nullptr &&
         global_vars != nullptr &&
         engine->is_in_game();
}

[[nodiscard]] float teleport_distance_sqr()
{
  static Convar* sv_lagcompensation_teleport_dist = nullptr;
  if (sv_lagcompensation_teleport_dist == nullptr && convar_system != nullptr) {
    sv_lagcompensation_teleport_dist = convar_system->find_var("sv_lagcompensation_teleport_dist");
  }

  const float distance = sv_lagcompensation_teleport_dist != nullptr
    ? std::max(sv_lagcompensation_teleport_dist->get_float(), 1.0f)
    : 64.0f;
  return distance * distance;
}

[[nodiscard]] float raw_fake_latency_seconds(float max_unlag, float fake_interp, net_channel* channel)
{
  if (channel == nullptr) {
    return 0.0f;
  }

  const float requested = std::clamp(config.backtrack.fake_latency_ms * 0.001f, 0.0f, max_unlag);
  const float real_latency = std::clamp(
    channel->get_latency(flow_outgoing) + channel->get_latency(flow_incoming),
    0.0f,
    max_unlag);
  const float available = std::max(0.0f, max_unlag - real_latency - fake_interp);
  return g_latency_ramp * std::clamp(requested, 0.0f, available);
}

[[nodiscard]] float round_to_tick_seconds(float seconds)
{
  return ticks_to_time(time_to_ticks(seconds));
}

[[nodiscard]] std::uint32_t configured_hitbox_mask(Weapon* weapon)
{
  const std::uint32_t mask = config.aimbot.hitscan_hitboxes & aim_hitbox_mask_all;
  if (weapon != nullptr && weapon->is_headshot_weapon() &&
      aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_headshot)) {
    return aim_hitbox_mask_head;
  }
  return mask != 0 ? mask : aim_hitbox_mask_default_hitscan;
}

[[nodiscard]] Vec3 command_angles(Player* localplayer, const Vec3& bullet_angles)
{
  return localplayer != nullptr ? bullet_angles - localplayer->get_punch_angles() : bullet_angles;
}

[[nodiscard]] bool point_within_weapon_range(Weapon* weapon, const Vec3& start_pos, const Vec3& point)
{
  if (weapon == nullptr || !aimbot_vec3_is_finite(start_pos) || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  const float range = weapon->get_hitscan_range();
  if (range <= 0.0f) {
    return true;
  }

  const Vec3 delta = point - start_pos;
  const float distance_sqr = (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z);
  return std::isfinite(distance_sqr) && distance_sqr <= (range * range);
}

[[nodiscard]] backtrack_timing build_timing(bool apply_offset = true)
{
  backtrack_timing timing{};
  if (global_vars == nullptr) {
    return timing;
  }

  net_channel* channel = current_net_channel();
  timing.max_unlag = max_unlag_seconds();
  timing.window = window_seconds();
  timing.fake_interp = config.backtrack.fake_interp
    ? std::clamp(requested_interpolation_time(), interpolation_time_value(), timing.max_unlag)
    : interpolation_time_value();
  timing.lerp_ticks = std::max(time_to_ticks(timing.fake_interp), 0);
  timing.server_tick = network_fix::adjusted_tick_count(global_vars->tickcount);

  timing.frame_gap = std::clamp(global_vars->frametime, tick_interval(), 0.25f);

  if (channel != nullptr) {
    timing.outgoing_latency = std::clamp(channel->get_latency(flow_outgoing), 0.0f, timing.max_unlag);
    timing.incoming_latency = std::clamp(channel->get_latency(flow_incoming), 0.0f, timing.max_unlag);
    timing.fake_latency = raw_fake_latency_seconds(timing.max_unlag, timing.fake_interp, channel);
    timing.server_tick += time_to_ticks(timing.outgoing_latency);
  }
  if (apply_offset) {
    timing.server_tick += std::clamp(config.backtrack.offset_ticks, -4, 4);
  }

  timing.correct = std::clamp(
    timing.outgoing_latency + timing.incoming_latency - timing.fake_latency + round_to_tick_seconds(timing.fake_interp),
    0.0f,
    timing.max_unlag);
  const float frame_excess = std::max(0.0f, timing.frame_gap - tick_interval());
  const float incoming_component = std::min(timing.incoming_latency * 0.10f, tick_interval() * 2.0f);
  timing.selection_slack = std::min((frame_excess * 0.5f) + incoming_component, tick_interval() * 4.0f);
  timing.valid = true;
  return timing;
}

[[nodiscard]] float record_delta_for_target_tick(const backtrack_timing& timing, int target_tick)
{
  return std::fabs(timing.correct - ticks_to_time(timing.server_tick - target_tick));
}

[[nodiscard]] float record_delta(const backtrack_timing& timing, const backtrack_record& record)
{
  return record_delta_for_target_tick(timing, time_to_ticks(record.sim_time));
}

[[nodiscard]] float record_capture_gap(const backtrack_record& record)
{
  const float choke_gap = ticks_to_time(std::max(record.choked_ticks + 1, 1));
  return std::max({tick_interval(), choke_gap, record.sample_gap});
}

[[nodiscard]] float record_timing_score(const backtrack_timing& timing, const backtrack_record& record)
{
  const float capture_excess = std::max(0.0f, record_capture_gap(record) - tick_interval());
  const float uncertainty = std::min(
    (capture_excess * 0.5f) + std::min(capture_excess, timing.selection_slack),
    tick_interval() * 6.0f);
  return record_delta(timing, record) + uncertainty;
}

[[nodiscard]] bool command_tick_for_record_timing(const backtrack_record& record,
  Player* player,
  const backtrack_timing& timing,
  int* tick_count)
{
  if (!timing.valid ||
      !record.valid ||
      record.invalid ||
      record.teleport ||
      record.player != player ||
      player == nullptr ||
      record.ent_index <= 0 ||
      record.hitbox_count <= 0 ||
      record.bone_count <= 0 ||
      !std::isfinite(record.sim_time)) {
    return false;
  }

  if (player->is_dormant() || !player->is_alive() || player->get_index() != record.ent_index) {
    return false;
  }

  const int target_tick = time_to_ticks(record.sim_time);
  const int command_tick = target_tick + timing.lerp_ticks;
  const float server_time = ticks_to_time(timing.server_tick);
  const float age = server_time - record.sim_time;
  const float max_window = std::min(
    timing.max_unlag,
    std::max(timing.window, lag_compensation_delta_limit));
  if (!std::isfinite(age) || age < -tick_interval() || age > max_window + tick_interval()) {
    return false;
  }

  const float delta = record_delta_for_target_tick(timing, target_tick);
  if (!std::isfinite(delta) || delta > std::max(lag_compensation_delta_limit, tick_interval())) {
    return false;
  }

  if (tick_count != nullptr) {
    *tick_count = command_tick;
  }
  return true;
}

[[nodiscard]] bool record_valid_for_timing(const backtrack_record& record, Player* player, const backtrack_timing& timing)
{
  return command_tick_for_record_timing(record, player, timing, nullptr);
}

[[nodiscard]] bool world_clear(const Vec3& start_pos, const Vec3& end_pos)
{
  if (engine_trace == nullptr || !aimbot_vec3_is_finite(start_pos) || !aimbot_vec3_is_finite(end_pos)) {
    return false;
  }

  Vec3 start = start_pos;
  Vec3 end = end_pos;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_world_trace_filter(&filter);
  trace_t trace_world{};
  engine_trace->trace_ray(&ray, aimbot_hitscan_trace_mask(), &filter, &trace_world);
  return !trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f;
}

[[nodiscard]] const backtrack_hitbox* find_hitbox(const backtrack_record& record, int hitbox_id)
{
  for (int index = 0; index < record.hitbox_count; ++index) {
    const backtrack_hitbox& hitbox = record.hitboxes[index];
    if (hitbox.valid && hitbox.hitbox == hitbox_id) {
      return &hitbox;
    }
  }

  return nullptr;
}

[[nodiscard]] bool ray_hits_record_hitbox(const backtrack_record& record,
  const backtrack_hitbox& hitbox,
  const Vec3& start_pos,
  const Vec3& end_pos)
{
  if (!hitbox.valid || hitbox.bone < 0 || hitbox.bone >= record.bone_count) {
    return false;
  }

  const matrix_3x4& bone_to_world = record.bones[hitbox.bone];
  const Vec3 local_start = aimbot_inverse_transform_point(start_pos, bone_to_world);
  const Vec3 local_end = aimbot_inverse_transform_point(end_pos, bone_to_world);
  const Vec3 expansion{record_hitbox_expansion, record_hitbox_expansion, record_hitbox_expansion};
  return aimbot_segment_intersects_aabb(local_start, local_end, hitbox.mins - expansion, hitbox.maxs + expansion);
}

[[nodiscard]] bool ray_hits_record_bounds(const backtrack_record& record,
  const Vec3& start_pos,
  const Vec3& end_pos)
{
  const Vec3 mins = record.origin + record.mins;
  const Vec3 maxs = record.origin + record.maxs;
  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}

struct manual_backtrack_candidate {
  const backtrack_record* record = nullptr;
  const backtrack_hitbox* hitbox = nullptr;
  Player* player = nullptr;
  int tick_count = 0;
  int priority = INT_MAX;
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  float delta = FLT_MAX;
  float timing_score = FLT_MAX;
};

[[nodiscard]] bool better_manual_candidate(const manual_backtrack_candidate& candidate,
  const manual_backtrack_candidate& best)
{
  if (candidate.record == nullptr || candidate.hitbox == nullptr || candidate.player == nullptr) {
    return false;
  }
  if (best.record == nullptr) {
    return true;
  }
  if (config.backtrack.prefer_on_shot && candidate.record->on_shot != best.record->on_shot) {
    return candidate.record->on_shot;
  }
  if (candidate.priority != best.priority) {
    return candidate.priority < best.priority;
  }
  if (std::fabs(candidate.fov - best.fov) > 0.05f) {
    return candidate.fov < best.fov;
  }
  if (std::fabs(candidate.distance - best.distance) > 1.0f) {
    return candidate.distance < best.distance;
  }
  return candidate.timing_score < best.timing_score;
}

void run_manual_backtrack(user_cmd* user_cmd)
{
  if (user_cmd == nullptr ||
      !is_enabled() ||
      (user_cmd->buttons & IN_ATTACK) == 0 ||
      entity_list == nullptr ||
      global_vars == nullptr) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  Weapon* weapon = localplayer->get_weapon();
  if (weapon == nullptr || aimbot_is_projectile_weapon(weapon) || aimbot_is_melee_weapon(weapon)) {
    return;
  }

  const backtrack_timing timing = build_timing();
  if (!timing.valid) {
    return;
  }

  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  Vec3 forward{};
  const Vec3 bullet_angles = user_cmd->view_angles + localplayer->get_punch_angles();
  angle_vectors(bullet_angles, &forward, nullptr, nullptr);
  if (!aimbot_vec3_is_finite(shoot_pos) || !aimbot_vec3_is_finite(forward)) {
    return;
  }

  constexpr float manual_ray_length = 8192.0f;
  const Vec3 end_pos = shoot_pos + (forward * manual_ray_length);
  const std::uint32_t hitbox_mask = configured_hitbox_mask(weapon);
  manual_backtrack_candidate best{};

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    if (player == nullptr ||
        player == localplayer ||
        player->get_team() == localplayer->get_team() ||
        player->is_friend() ||
        player->is_ignored()) {
      continue;
    }

    const bool head_only = (hitbox_mask & aim_hitbox_mask_head) != 0 &&
      (hitbox_mask & ~aim_hitbox_mask_head) == 0;
    const bool body_aim = !head_only && (localplayer->is_crit_boosted() ||
      aimbot_body_aim_lethal(localplayer, weapon, player));

    backtrack_record_view view = valid_records(player);
    for (int record_index = 0; record_index < view.count; ++record_index) {
      const backtrack_record* record = view.records[record_index];
      int command_tick = 0;
      if (record == nullptr ||
          !command_tick_for_record_timing(*record, player, timing, &command_tick) ||
          !ray_hits_record_bounds(*record, shoot_pos, end_pos)) {
        continue;
      }

      for (int hitbox_index = 0; hitbox_index < record->hitbox_count; ++hitbox_index) {
        const backtrack_hitbox& hitbox = record->hitboxes[hitbox_index];
        if (!hitbox.valid ||
            !aimbot_hitbox_matches_mask(hitbox.hitbox, hitbox_mask) ||
            (body_aim && hitbox.hitbox == aim_hitbox_head) ||
            !point_within_weapon_range(weapon, shoot_pos, hitbox.center) ||
            !ray_hits_record_hitbox(*record, hitbox, shoot_pos, end_pos) ||
            !world_clear(shoot_pos, hitbox.center)) {
          continue;
        }

        const int priority = aimbot_hitbox_priority(localplayer, player, weapon, hitbox.hitbox);
        if (priority == INT_MAX) {
          continue;
        }

        const Vec3 angles_to_center = command_angles(
          localplayer,
          aimbot_calculate_angles_to_position(shoot_pos, hitbox.center));
        manual_backtrack_candidate candidate{};
        candidate.record = record;
        candidate.hitbox = &hitbox;
        candidate.player = player;
        candidate.tick_count = command_tick;
        candidate.priority = priority;
        candidate.fov = aimbot_calculate_fov(angles_to_center, user_cmd->view_angles);
        candidate.distance = distance_3d(shoot_pos, hitbox.center);
        candidate.delta = record_delta(timing, *record);
        candidate.timing_score = record_timing_score(timing, *record);

        if (better_manual_candidate(candidate, best)) {
          best = candidate;
        }
      }
    }
  }

  if (best.record != nullptr && best.tick_count > 0) {
    user_cmd->tick_count = best.tick_count;
  }
}

[[nodiscard]] bool point_visible(Player* localplayer,
  const backtrack_record& record,
  const backtrack_hitbox& hitbox,
  const Vec3& point)
{
  if (localplayer == nullptr || !hitbox.valid || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  const Vec3 start_pos = localplayer->get_shoot_pos();
  if (!aimbot_vec3_is_finite(start_pos) || !world_clear(start_pos, point)) {
    return false;
  }

  const Vec3 bullet_angles = aimbot_calculate_angles_to_position(start_pos, point);
  Vec3 forward{};
  angle_vectors(bullet_angles, &forward, nullptr, nullptr);
  if (!aimbot_vec3_is_finite(forward)) {
    return false;
  }

  const float target_distance = distance_3d(start_pos, point);
  const Vec3 end_pos = start_pos + (forward * std::max(target_distance + 64.0f, 128.0f));
  return ray_hits_record_hitbox(record, hitbox, start_pos, end_pos) && ray_hits_record_bounds(record, start_pos, end_pos);
}

bool add_record_hitbox(backtrack_record* record,
  const studio_box& hitbox,
  int studio_hitbox_id,
  const matrix_3x4& bone_to_world)
{
  if (record == nullptr || record->hitbox_count >= max_hitboxes || hitbox.bone < 0 || hitbox.bone >= max_bones) {
    return false;
  }

  const Vec3 center = (hitbox.bbmin + hitbox.bbmax) * 0.5f;
  const Vec3 world_center = aimbot_transform_point(center, bone_to_world);
  if (!aimbot_vec3_is_finite(world_center)) {
    return false;
  }

  const int base_hitbox = aimbot_studio_hitbox_to_base(record->player, studio_hitbox_id);
  if (base_hitbox < 0) {
    return false;
  }

  backtrack_hitbox& out = record->hitboxes[record->hitbox_count++];
  out.valid = true;
  out.bone = hitbox.bone;
  out.hitbox = base_hitbox;
  out.studio_hitbox = studio_hitbox_id;
  out.group = hitbox.group;
  out.center = world_center;
  out.mins = hitbox.bbmin;
  out.maxs = hitbox.bbmax;
  return true;
}

[[nodiscard]] bool build_record(Player* player, backtrack_record* record)
{
  if (player == nullptr || record == nullptr || global_vars == nullptr || player->is_dormant() || !player->is_alive()) {
    return false;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return false;
  }

  *record = {};
  record->player = player;
  record->ent_index = ent_index;
  record->sim_time = player->get_simulation_time();
  record->receive_time = network_fix::adjusted_curtime(global_vars->curtime);
  record->origin = player->get_collision_origin();
  record->mins = player->get_collideable_mins();
  record->maxs = player->get_collideable_maxs();
  record->velocity = player->get_velocity();

  if (!std::isfinite(record->sim_time) ||
      record->sim_time <= 0.0f ||
      !aimbot_vec3_is_finite(record->origin) ||
      !aimbot_vec3_is_finite(record->mins) ||
      !aimbot_vec3_is_finite(record->maxs) ||
      record->maxs.x <= record->mins.x + 1.0f ||
      record->maxs.y <= record->mins.y + 1.0f ||
      record->maxs.z <= record->mins.z + 8.0f) {
    return false;
  }

  const model_t* model = player->get_model();
  if (model == nullptr || model_info == nullptr) {
    return false;
  }

  studio_hdr* hdr = model_info->get_studio_model(model);
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(player->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr) {
    return false;
  }

  matrix_3x4 bone_to_world[max_bones]{};
  int bone_count = 0;
  const float setup_time = record->sim_time;
  const int pose_frame = global_vars != nullptr ? global_vars->framecount : 0;
  if (!aimbot_setup_bones_at_time(player, bone_to_world, setup_time, pose_frame,
      player->get_origin(), true, false, &bone_count)) {
    return false;
  }

  for (int bone_index = 0; bone_index < bone_count; ++bone_index) {
    record->bones[bone_index] = bone_to_world[bone_index];
  }
  record->bone_count = bone_count;

  const int hitbox_count = std::min(hitbox_set->num_hitboxes, max_hitboxes);
  for (int hitbox_id = 0; hitbox_id < hitbox_count; ++hitbox_id) {
    studio_box* hitbox = hitbox_set->hitbox(hitbox_id);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= bone_count) {
      continue;
    }

    add_record_hitbox(record, *hitbox, hitbox_id, bone_to_world[hitbox->bone]);
  }

  record->valid = record->hitbox_count > 0;
  return record->valid;
}

void update_sequences(net_channel* channel)
{
  if (channel == nullptr || global_vars == nullptr) {
    return;
  }

  net_channel_storage* storage = reinterpret_cast<net_channel_storage*>(channel);
  if (storage->in_sequence_number > g_last_incoming_sequence) {
    g_last_incoming_sequence = storage->in_sequence_number;
    g_sequences.push_front({
      storage->in_reliable_state,
      storage->in_sequence_number,
      global_vars->realtime
    });
  }

  while (g_sequences.size() > 96) {
    g_sequences.pop_back();
  }
}

void apply_fake_latency(net_channel* channel)
{
  if (channel == nullptr || g_sequences.empty() || global_vars == nullptr) {
    return;
  }

  const float target_latency = fake_latency_seconds();
  if (target_latency <= 0.0001f) {
    return;
  }

  net_channel_storage* storage = reinterpret_cast<net_channel_storage*>(channel);
  for (const incoming_sequence& sequence : g_sequences) {
    if (global_vars->realtime - sequence.realtime >= target_latency) {
      storage->in_reliable_state = sequence.reliable_state;
      storage->in_sequence_number = sequence.sequence_number;
      break;
    }
  }
}

void send_interp_settings(net_channel* channel, float interp)
{
  if (channel == nullptr || global_vars == nullptr || !config.backtrack.fake_interp) {
    return;
  }

  if (std::fabs(g_last_sent_interp - interp) <= 0.0005f && global_vars->realtime < g_next_interp_send_time) {
    channel->set_interpolation_amount(interp);
    return;
  }

  char value[32]{};
  std::snprintf(value, sizeof(value), "%.6f", interp);
  net_set_convar_message interp_message("cl_interp", value);
  net_set_convar_message ratio_message("cl_interp_ratio", "1");
  net_set_convar_message interpolate_message("cl_interpolate", "1");
  channel->send_net_msg(interp_message, false, false);
  channel->send_net_msg(ratio_message, false, false);
  channel->send_net_msg(interpolate_message, false, false);
  channel->set_interpolation_amount(interp);
  g_last_sent_interp = interp;
  g_next_interp_send_time = global_vars->realtime + 0.1f;
}

int send_datagram_hook(net_channel* channel, bf_write* data)
{
  CATHOOK_HOOK_GUARD();
  if (g_send_datagram_original == nullptr || channel == nullptr || !should_run_fake_latency_network()) {
    return g_send_datagram_original != nullptr ? g_send_datagram_original(channel, data) : 0;
  }

  net_channel_storage* storage = reinterpret_cast<net_channel_storage*>(channel);
  const int in_sequence_number = storage->in_sequence_number;
  const int in_reliable_state = storage->in_reliable_state;

  apply_fake_latency(channel);
  const int result = g_send_datagram_original(channel, data);

  storage->in_sequence_number = in_sequence_number;
  storage->in_reliable_state = in_reliable_state;
  return result;
}

[[nodiscard]] bool better_backtrack_candidate(const aimbot_candidate& candidate,
  const aimbot_candidate& best,
  int candidate_priority,
  int best_priority,
  float candidate_score,
  float best_score)
{
  if (candidate.entity == nullptr) {
    return false;
  }
  if (best.entity == nullptr) {
    return true;
  }
  if (config.backtrack.prefer_on_shot && candidate.backtrack_on_shot != best.backtrack_on_shot) {
    return candidate.backtrack_on_shot;
  }
  if (candidate_priority != best_priority) {
    return candidate_priority < best_priority;
  }
  if (std::fabs(candidate_score - best_score) > tick_interval() * 0.5f) {
    return candidate_score < best_score;
  }
  return candidate.fov < best.fov;
}

}

bool is_enabled()
{
  return config.backtrack.enabled;
}

void report_shot(Player* player)
{
  if (player == nullptr || !is_enabled() || !config.backtrack.prefer_on_shot) {
    return;
  }

  const int ent_index = player->get_index();
  if (ent_index > 0 && ent_index < max_entities) {
    g_did_shoot[ent_index] = true;
  }
}

float fake_latency_seconds()
{
  net_channel* channel = current_net_channel();
  const float max_unlag = max_unlag_seconds();
  const float interp = config.backtrack.fake_interp
    ? std::clamp(requested_interpolation_time(), interpolation_time_value(), max_unlag)
    : interpolation_time_value();
  return raw_fake_latency_seconds(max_unlag, interp, channel);
}

float interpolation_time()
{
  if (is_enabled() && config.backtrack.fake_interp) {
    return std::clamp(requested_interpolation_time(), interpolation_time_value(), max_unlag_seconds());
  }

  return interpolation_time_value();
}

backtrack_timing current_timing()
{
  return build_timing();
}

bool command_tick_for_current_pose(float simulation_time, int* tick_count)
{
  const backtrack_timing timing = build_timing(false);
  if (!timing.valid || !std::isfinite(simulation_time) || simulation_time <= 0.0f) {
    return false;
  }

  const int target_tick = time_to_ticks(simulation_time);
  const int command_tick = target_tick + timing.lerp_ticks;
  const float age = ticks_to_time(timing.server_tick) - simulation_time;
  if (!std::isfinite(age) || age < -tick_interval() || age > timing.max_unlag + tick_interval()) {
    return false;
  }

  const float current_correct = std::clamp(
    timing.outgoing_latency + timing.incoming_latency - timing.fake_latency + ticks_to_time(timing.lerp_ticks),
    0.0f,
    timing.max_unlag);
  const float delta = std::fabs(current_correct - age);
  if (!std::isfinite(delta) || delta > std::max(lag_compensation_delta_limit, tick_interval())) {
    return false;
  }

  if (tick_count != nullptr) {
    *tick_count = command_tick;
  }
  return true;
}

bool command_tick_for_record(const backtrack_record& record, Player* player, int* tick_count)
{
  return command_tick_for_record_timing(record, player, build_timing(), tick_count);
}

void on_create_move(user_cmd* user_cmd)
{
  if (is_enabled() && config.backtrack.fake_latency_ms > 0.0f) {
    install_net_channel_hook();
  } else {
    restore_net_channel_hook();
  }

  static bool was_enabled = false;
  const bool enabled_now = is_enabled();
  if (was_enabled && !enabled_now) {
    clear();
  }
  was_enabled = enabled_now;

  net_channel* channel = current_net_channel();
  if (!should_run_network_state()) {
    g_latency_ramp = 0.0f;
    if (channel == nullptr) {
      g_sequences.clear();
      g_last_incoming_sequence = 0;
    }
    return;
  }

  update_sequences(channel);
  g_latency_ramp = config.backtrack.fake_latency_ms > 0.0f
    ? std::min(1.0f, g_latency_ramp + tick_interval())
    : 0.0f;
  send_interp_settings(channel, interpolation_time());
  run_manual_backtrack(user_cmd);
}

void record_player(Player* player)
{
  if (!should_record() || player == nullptr || entity_list == nullptr) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return;
  }

  if (localplayer == nullptr ||
      player == localplayer ||
      player->get_team() == localplayer->get_team() ||
      player->is_friend() ||
      player->is_ignored()) {
    g_records[ent_index] = {};
    return;
  }

  backtrack_record record{};
  if (!build_record(player, &record)) {
    return;
  }

  backtrack_history& history = g_records[record.ent_index];
  history.ent_index = record.ent_index;
  record.on_shot = g_did_shoot[record.ent_index];

  if (history.record_count > 0) {
    const backtrack_record& last = history.records[0];
    if (std::isfinite(last.sim_time) && record.sim_time <= last.sim_time + 0.0001f) {
      return;
    }

    const float sim_delta = record.sim_time - last.sim_time;
    if (std::isfinite(sim_delta) && sim_delta > 0.0f) {
      record.choked_ticks = std::max(time_to_ticks(sim_delta) - 1, 0);
      record.velocity = (record.origin - last.origin) * (1.0f / std::max(sim_delta, 0.0001f));
      record.sample_gap = sim_delta;
    }
    const float receive_delta = record.receive_time - last.receive_time;
    if (std::isfinite(receive_delta) && receive_delta > 0.0f) {
      record.sample_gap = std::max(record.sample_gap, receive_delta);
    }

    const Vec3 delta = record.origin - last.origin;
    const float delta_sqr = (delta.x * delta.x) + (delta.y * delta.y);
    if (delta_sqr > teleport_distance_sqr()) {
      for (int index = 0; index < history.record_count; ++index) {
        history.records[index].invalid = true;
        history.records[index].teleport = true;
      }
      record.teleport = false;
    }
  }

  const int shift_count = std::min(history.record_count, max_records - 1);
  for (int index = shift_count; index > 0; --index) {
    history.records[index] = history.records[index - 1];
  }

  history.records[0] = record;
  history.record_count = std::min(history.record_count + 1, max_records);

  const float retention = max_unlag_seconds() + (tick_interval() * 2.0f);
  while (history.record_count > 1 &&
         history.records[0].sim_time - history.records[history.record_count - 1].sim_time > retention) {
    --history.record_count;
  }
  g_did_shoot[record.ent_index] = false;
}

void store()
{
  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    record_player(entry.player);
  }
}

void clear()
{
  g_records = {};
  g_did_shoot = {};
  g_sequences.clear();
  g_last_incoming_sequence = 0;
  g_latency_ramp = 0.0f;
  g_last_sent_interp = -1.0f;
  g_next_interp_send_time = 0.0f;
  g_selected_position = std::nullopt;
}

const backtrack_history* records_for_player(Player* player)
{
  if (player == nullptr) {
    return nullptr;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return nullptr;
  }

  return &g_records[ent_index];
}

backtrack_record_view valid_records(Player* player)
{
  backtrack_record_view view{};
  const backtrack_history* history = records_for_player(player);
  if (history == nullptr || history->record_count <= 0) {
    return view;
  }

  const backtrack_timing timing = build_timing();
  const float current_sim_time = player != nullptr ? player->get_simulation_time() : 0.0f;

  for (int index = 0; index < history->record_count && view.count < max_records; ++index) {
    const backtrack_record& record = history->records[index];
    if (record_valid_for_timing(record, player, timing)) {
      if (std::fabs(record.sim_time - current_sim_time) <= 0.0001f) {
        continue;
      }

      view.records[view.count++] = &record;
    }
  }

  std::sort(view.records.begin(), view.records.begin() + view.count, [&](const backtrack_record* left, const backtrack_record* right) {
    if (left == nullptr || right == nullptr) {
      return right != nullptr;
    }
    if (config.backtrack.prefer_on_shot && left->on_shot != right->on_shot) {
      return left->on_shot;
    }
    return record_timing_score(timing, *left) < record_timing_score(timing, *right);
  });
  return view;
}

backtrack_record_view visual_records(Player* player)
{
  backtrack_record_view view{};
  const backtrack_history* history = records_for_player(player);
  if (history == nullptr || history->record_count <= 0 || player == nullptr) {
    return view;
  }

  const backtrack_timing timing = build_timing();
  const float current_sim_time = player->get_simulation_time();
  for (int index = 0; index < history->record_count && view.count < max_records; ++index) {
    const backtrack_record& record = history->records[index];
    if (!record_valid_for_timing(record, player, timing) ||
        std::fabs(record.sim_time - current_sim_time) <= 0.0001f) {
      continue;
    }

    view.records[view.count++] = &record;
  }
  return view;
}

bool is_record_valid(const backtrack_record& record, Player* player)
{
  return record_valid_for_timing(record, player, build_timing());
}

bool selected_position(Vec3* position)
{
  if (position == nullptr || !g_selected_position) {
    return false;
  }

  *position = *g_selected_position;
  return true;
}

void backtrack_to_crosshair(user_cmd* user_cmd, Player* localplayer, Weapon* weapon)
{
  if (user_cmd == nullptr || localplayer == nullptr || weapon == nullptr ||
      !is_enabled() || !config.backtrack.to_crosshair ||
      (user_cmd->buttons & IN_ATTACK) == 0 || !localplayer->is_alive() ||
      aimbot_is_projectile_weapon(weapon) || global_vars == nullptr) {
    return;
  }

  const backtrack_timing timing = build_timing();
  if (!timing.valid) {
    return;
  }

  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  if (!aimbot_vec3_is_finite(shoot_pos)) {
    return;
  }

  struct crosshair_candidate {
    const backtrack_record* record = nullptr;
    Player* player = nullptr;
    int tick_count = 0;
    bool inside = false;
    float fov = FLT_MAX;
    float distance = FLT_MAX;
    float timing_score = FLT_MAX;
  };

  crosshair_candidate best{};
  const bool melee = aimbot_is_melee_weapon(weapon);

  auto better = [&](const crosshair_candidate& candidate) {
    if (candidate.record == nullptr || candidate.player == nullptr) {
      return false;
    }
    if (best.record == nullptr) {
      return true;
    }
    if (config.backtrack.prefer_on_shot && candidate.record->on_shot != best.record->on_shot) {
      return candidate.record->on_shot;
    }
    if (!melee && candidate.inside != best.inside) {
      return candidate.inside;
    }
    if (melee || candidate.inside) {
      if (std::fabs(candidate.distance - best.distance) > 1.0f) {
        return candidate.distance < best.distance;
      }
    } else if (std::fabs(candidate.fov - best.fov) > 0.05f) {
      return candidate.fov < best.fov;
    }
    return candidate.timing_score < best.timing_score;
  };

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    if (player == nullptr || player == localplayer || !player->is_alive() || player->is_dormant() ||
        player->get_team() == localplayer->get_team() || player->is_friend() ||
        player->is_ignored() || player->is_invulnerable()) {
      continue;
    }

    const backtrack_record_view view = valid_records(player);
    for (int record_index = 0; record_index < view.count; ++record_index) {
      const backtrack_record* record = view.records[record_index];
      int tick_count = 0;
      if (record == nullptr || !command_tick_for_record_timing(*record, player, timing, &tick_count)) {
        continue;
      }

      if (melee) {
        const float distance = distance_3d(shoot_pos, record->origin);
        if (!std::isfinite(distance) || distance > 300.0f) {
          continue;
        }

        crosshair_candidate candidate{};
        candidate.record = record;
        candidate.player = player;
        candidate.tick_count = tick_count;
        candidate.distance = distance;
        candidate.timing_score = record_timing_score(timing, *record);
        if (better(candidate)) {
          best = candidate;
        }
        continue;
      }

      for (int hitbox_index = 0; hitbox_index < record->hitbox_count; ++hitbox_index) {
        const backtrack_hitbox& hitbox = record->hitboxes[hitbox_index];
        if (!hitbox.valid || !aimbot_vec3_is_finite(hitbox.center) ||
            !point_within_weapon_range(weapon, shoot_pos, hitbox.center)) {
          continue;
        }

        const float distance = distance_3d(shoot_pos, hitbox.center);
        const float fov = aimbot_calculate_fov(
          user_cmd->view_angles,
          aimbot_calculate_angles_to_position(shoot_pos, hitbox.center));
        if (!std::isfinite(distance) || !std::isfinite(fov)) {
          continue;
        }

        crosshair_candidate candidate{};
        candidate.record = record;
        candidate.player = player;
        candidate.tick_count = tick_count;
        candidate.inside = distance < 50.0f;
        candidate.fov = fov;
        candidate.distance = distance;
        candidate.timing_score = record_timing_score(timing, *record);
        if (!candidate.inside && candidate.fov >= 45.0f) {
          continue;
        }
        if (better(candidate)) {
          best = candidate;
        }
      }
    }
  }

  if (best.record != nullptr && best.tick_count > 0) {
    user_cmd->tick_count = best.tick_count;
  }
}

aimbot_candidate find_hitscan_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& original_view_angles,
  bool preferred)
{
  g_selected_position = std::nullopt;
  if (!is_enabled() ||
      localplayer == nullptr ||
      weapon == nullptr ||
      player == nullptr ||
      global_vars == nullptr) {
    return {};
  }

  const backtrack_timing timing = build_timing();
  backtrack_record_view view = valid_records(player);
  if (!timing.valid || view.count <= 0) {
    return {};
  }

  const std::uint32_t hitbox_mask = configured_hitbox_mask(weapon);
  const bool head_only = (hitbox_mask & aim_hitbox_mask_head) != 0 &&
    (hitbox_mask & ~aim_hitbox_mask_head) == 0;
  const bool body_aim = !head_only && (localplayer->is_crit_boosted() ||
    aimbot_body_aim_lethal(localplayer, weapon, player));
  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  aimbot_candidate best_candidate{};
  int best_priority = INT_MAX;
  float best_score = FLT_MAX;

  for (int record_index = 0; record_index < view.count; ++record_index) {
    const backtrack_record* record = view.records[record_index];
    if (record == nullptr) {
      continue;
    }

    const float timing_error = record_timing_score(timing, *record);
    for (int hitbox_index = 0; hitbox_index < record->hitbox_count; ++hitbox_index) {
      const backtrack_hitbox& hitbox = record->hitboxes[hitbox_index];
      if (!hitbox.valid ||
          !aimbot_hitbox_matches_mask(hitbox.hitbox, hitbox_mask) ||
          (body_aim && hitbox.hitbox == aim_hitbox_head)) {
        continue;
      }

      const int priority = aimbot_hitbox_priority(localplayer, player, weapon, hitbox.hitbox);
      if (priority == INT_MAX) {
        continue;
      }

      studio_box box{};
      box.bone = hitbox.bone;
      box.group = hitbox.group;
      box.bbmin = hitbox.mins;
      box.bbmax = hitbox.maxs;

      constexpr int max_local_points = 21;
      Vec3 local_points[max_local_points]{};
      const bool use_multipoint =
        priority == 0 &&
        (hitbox.hitbox == aim_hitbox_head || config.aimbot.multipoint_scale > 0.0f);
      const int point_count = aimbot_build_local_hitbox_points(
        box,
        record->bones[hitbox.bone],
        shoot_pos,
        local_points,
        max_local_points,
        use_multipoint,
        hitbox.hitbox);

      for (int point_index = 0; point_index < point_count; ++point_index) {
        const Vec3 point = aimbot_transform_point(local_points[point_index], record->bones[hitbox.bone]);
        if (!point_within_weapon_range(weapon, shoot_pos, point)) {
          continue;
        }
        if (!point_visible(localplayer, *record, hitbox, point)) {
          continue;
        }

        const Vec3 aim_angles = aimbot_calculate_angles_to_position(shoot_pos, point);
        const Vec3 view_angles = command_angles(localplayer, aim_angles);
        const float fov = aimbot_calculate_fov(view_angles, original_view_angles);

        aimbot_candidate candidate{};
        candidate.entity = player;
        candidate.player = player;
        candidate.preferred = preferred;
        candidate.bone = hitbox.bone;
        candidate.hitbox = hitbox.hitbox;
        candidate.studio_hitbox = hitbox.studio_hitbox;
        candidate.aim_position = point;
        candidate.aim_angles = aim_angles;
        candidate.fov = fov;
        candidate.distance = distance_3d(localplayer->get_origin(), record->origin);
        candidate.health = player->get_health();
        candidate.simulation_time = record->sim_time;
        candidate.backtrack_timing_error = timing_error;
        candidate.backtrack_capture_gap = record_capture_gap(*record);
        candidate.backtrack_on_shot = record->on_shot;
        if (!command_tick_for_record_timing(*record, player, timing, &candidate.tick_count)) {
          continue;
        }
        candidate.command_angles = view_angles;
        candidate.backtrack_mins = record->origin + record->mins;
        candidate.backtrack_maxs = record->origin + record->maxs;
        candidate.backtrack_hitbox_mins = hitbox.mins;
        candidate.backtrack_hitbox_maxs = hitbox.maxs;
        candidate.backtrack_bone = record->bones[hitbox.bone];
        candidate.backtrack_hitbox_valid = true;
        candidate.visible = true;
        candidate.backtrack = true;

        if (better_backtrack_candidate(candidate, best_candidate, priority, best_priority, timing_error, best_score)) {
          best_candidate = candidate;
          best_priority = priority;
          best_score = timing_error;
          g_selected_position = point;
        }
      }
    }
  }

  return best_candidate;
}

void install_net_channel_hook()
{
  net_channel* channel = current_net_channel();
  if (channel == nullptr) {
    return;
  }

  void** vtable = *reinterpret_cast<void***>(channel);
  if (vtable == nullptr) {
    return;
  }

  if (g_hooked_net_channel_vtable == vtable &&
      g_send_datagram_original != nullptr &&
      vtable[net_channel_send_datagram_index] == reinterpret_cast<void*>(send_datagram_hook)) {
    return;
  }

  void* const entry = read_vtable_entry(vtable, net_channel_send_datagram_index, "INetChannel::SendDatagram");
  if (entry == nullptr) {
    return;
  }

  if (g_hooked_net_channel_vtable == vtable &&
      g_send_datagram_original != nullptr &&
      entry == reinterpret_cast<void*>(send_datagram_hook)) {
    return;
  }

  if (g_hooked_net_channel_vtable != nullptr && g_hooked_net_channel_vtable != vtable) {
    restore_net_channel_hook();
  }

  if (entry == reinterpret_cast<void*>(send_datagram_hook)) {
    g_hooked_net_channel_vtable = vtable;
    return;
  }

  g_send_datagram_original = reinterpret_cast<send_datagram_fn>(entry);

  if (write_to_table(vtable, net_channel_send_datagram_index, reinterpret_cast<void*>(send_datagram_hook))) {
    g_hooked_net_channel_vtable = vtable;
  } else {
    g_send_datagram_original = nullptr;
  }
}

void restore_net_channel_hook()
{
  if (g_hooked_net_channel_vtable == nullptr || g_send_datagram_original == nullptr) {
    g_hooked_net_channel_vtable = nullptr;
    g_send_datagram_original = nullptr;
    return;
  }

  void* const entry = read_vtable_entry(
    g_hooked_net_channel_vtable,
    net_channel_send_datagram_index,
    "INetChannel::SendDatagram restore");
  if (entry == reinterpret_cast<void*>(send_datagram_hook)) {
    write_to_table(
      g_hooked_net_channel_vtable,
      net_channel_send_datagram_index,
      reinterpret_cast<void*>(g_send_datagram_original));
  }

  g_hooked_net_channel_vtable = nullptr;
  g_send_datagram_original = nullptr;
}

}
