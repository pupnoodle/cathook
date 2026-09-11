/*
/^-----^\   data: 2026-08-10
V  o o  V  file: src/features/automation/followbot/followbot.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

namespace followbot
{

struct trail_node
{
  Vec3 origin{};
  Vec3 target_angles{};
};

struct candidate
{
  Player* player = nullptr;
  CBaseHandle handle{};
  int index = -1;
  int serial = 0;
  std::uint32_t account_id = 0;
  int priority = 0;
  int preference = 0;
  int class_preference = 0;
  float distance = std::numeric_limits<float>::max();
  bool exact = false;
};

namespace
{

std::atomic<std::uint32_t> g_ipc_target{0};

constexpr float pi_degrees = 57.29577951308232f;
constexpr float follow_jump_height = 28.0f;
constexpr float follow_jump_run = 60.0f;
constexpr float trail_node_spacing = 20.0f;
constexpr float trail_reach_distance = 20.0f;
constexpr float follow_move_speed = 450.0f;
constexpr float stuck_jump_delay = 0.5f;
constexpr float afk_timeout = 8.0f;

bool finite_origin(const Vec3& origin)
{
  return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z);
}

float distance_sq_2d(const Vec3& left, const Vec3& right)
{
  const float x = left.x - right.x;
  const float y = left.y - right.y;
  return x * x + y * y;
}

float distance_2d(const Vec3& left, const Vec3& right)
{
  return std::sqrt(distance_sq_2d(left, right));
}

float normalize_yaw(float yaw)
{
  while (yaw > 180.0f) yaw -= 360.0f;
  while (yaw < -180.0f) yaw += 360.0f;
  return yaw;
}

Vec3 clamp_angles(Vec3 angles)
{
  angles.x = std::clamp(angles.x, -89.0f, 89.0f);
  angles.y = normalize_yaw(angles.y);
  angles.z = 0.0f;
  return angles;
}

Vec3 calculate_angles(const Vec3& source, const Vec3& target)
{
  const auto delta = target - source;
  const auto planar = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  return clamp_angles(Vec3{
    -std::atan2(delta.z, planar) * pi_degrees,
    std::atan2(delta.y, delta.x) * pi_degrees,
    0.0f
  });
}

int role_priority(std::uint32_t account_id)
{
  using namespace cathook::core::players;
  if (has_role(account_id, ipc_role)) return 10;
  if (has_role(account_id, identified_role)) return 9;
  if (has_role(account_id, party_role)) return 8;
  if (has_role(account_id, friend_role)) return 7;
  if (has_role(account_id, cheater_role)) return 6;
  return 0;
}

int class_preference(Player* localplayer, Player* player, int priority, int preference)
{
  if (localplayer == nullptr || player == nullptr || priority != 0 || preference != 0 ||
      player->get_team() != localplayer->get_team())
  {
    return 0;
  }

  switch (player->get_tf_class())
  {
    case tf_class::HEAVYWEAPONS:
    case tf_class::SOLDIER:
    case tf_class::DEMOMAN:
      return 2;
    case tf_class::ENGINEER:
    case tf_class::SPY:
      return 0;
    default:
      return 1;
  }
}

bool has_manual_movement(const user_cmd* user_cmd)
{
  return user_cmd != nullptr && (user_cmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT)) != 0;
}

}

struct controller_t::impl
{
  candidate target{};
  std::deque<trail_node> trail{};
  std::array<float, 65> last_active_times{};
  std::array<Vec3, 65> last_origins{};
  std::array<Vec3, 65> last_angles{};
  std::uint32_t active_account_id = 0;
  bool active = false;
  bool nav_requested = false;
  float last_jump_time = 0.0f;

  void reset(bool reset_activity)
  {
    target = {};
    trail.clear();
    active = false;
    nav_requested = false;
    active_account_id = 0;
    last_jump_time = 0.0f;
    if (reset_activity)
    {
      last_active_times.fill(0.0f);
      last_origins.fill({});
      last_angles.fill({});
    }
  }

  bool is_afk(Player* player, float current_time)
  {
    if (player == nullptr || player->is_dormant()) return false;
    const auto index = player->get_index();
    if (index <= 0 || index >= static_cast<int>(last_active_times.size())) return false;

    const auto origin = player->get_origin();
    const auto angles = player->get_eye_angles();
    const bool moved = last_origins[index].x == 0.0f && last_origins[index].y == 0.0f && last_origins[index].z == 0.0f
      ? true : distance_sq_2d(origin, last_origins[index]) > 64.0f;
    const bool looked = std::fabs(normalize_yaw(angles.y - last_angles[index].y)) > 2.0f ||
      std::fabs(angles.x - last_angles[index].x) > 2.0f;
    const auto velocity = player->get_velocity();
    const bool moving = std::hypot(velocity.x, velocity.y) > 10.0f;
    const bool pressing = (player->get_buttons() & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT | IN_JUMP | IN_DUCK | IN_ATTACK | IN_ATTACK2)) != 0;

    if (moved || looked || moving || pressing || last_active_times[index] <= 0.0f)
    {
      last_active_times[index] = current_time;
    }
    last_origins[index] = origin;
    last_angles[index] = angles;
    return current_time - last_active_times[index] > afk_timeout;
  }

  bool allowed_team(Player* localplayer, Player* player, bool exact) const
  {
    if (exact) return true;
    if (localplayer == nullptr || player == nullptr) return false;
    if (player->get_team() == localplayer->get_team())
      return (config.misc.automation.followbot_targets & Misc::Automation::followbot_teammates) != 0;
    return (config.misc.automation.followbot_targets & Misc::Automation::followbot_enemies) != 0;
  }

  candidate make_candidate(Player* localplayer, Player* player, std::uint32_t exact_account, float current_time)
  {
    candidate result{};
    if (player == nullptr || player == localplayer || !player->is_alive() || player->is_taunting()) return result;

    player_info info{};
    if (engine == nullptr || !engine->get_player_info(player->get_index(), &info) || info.friends_id == 0) return result;
    result.player = player;
    result.handle = player->get_ref_ehandle();
    result.index = player->get_index();
    result.serial = player->get_ref_ehandle().GetSerialNumber();
    result.account_id = static_cast<std::uint32_t>(info.friends_id);
    result.exact = exact_account != 0 && result.account_id == exact_account;
    if (!allowed_team(localplayer, player, result.exact) || cathook::core::players::is_ignored(result.account_id)) return {};
    if (config.misc.automation.followbot_ignore_afk && !result.exact && is_afk(player, current_time)) return {};

    result.priority = result.exact ? std::numeric_limits<int>::max() : role_priority(result.account_id);
    if (!result.exact && result.priority < config.misc.automation.followbot_min_priority) return {};
    const auto prefer = config.misc.automation.followbot_preference;
    result.preference = prefer == Misc::Automation::followbot_prefer::FRIENDS && cathook::core::players::has_role(result.account_id, cathook::core::players::friend_role)
      ? 1 : prefer == Misc::Automation::followbot_prefer::PARTY && cathook::core::players::has_role(result.account_id, cathook::core::players::party_role) ? 1 : 0;
    result.class_preference = class_preference(localplayer, player, result.priority, result.preference);
    result.distance = distance_2d(localplayer->get_origin(), player->get_origin());
    const auto max_distance = config.misc.automation.followbot_use_nav != Misc::Automation::followbot_nav_mode::OFF
      ? config.misc.automation.followbot_nav_abandon_distance
      : config.misc.automation.followbot_activation_distance;
    if (!result.exact && result.distance > max_distance) return {};
    return result;
  }

  void update_target(Player* localplayer, float current_time)
  {
    const auto exact_account = g_ipc_target.load(std::memory_order_acquire);
    candidate best{};
    const auto choose = [&](const candidate& value) {
      if (value.player == nullptr) return;
      if (best.player == nullptr || value.priority != best.priority) { if (best.player == nullptr || value.priority > best.priority) best = value; return; }
      if (value.preference != best.preference) { if (value.preference > best.preference) best = value; return; }
      if (value.class_preference != best.class_preference) { if (value.class_preference > best.class_preference) best = value; return; }
      if (value.distance < best.distance) best = value;
    };

    for (const auto& entry : entity_cache_players())
    {
      if (entry.player == nullptr || !entry.alive || entry.dormant) continue;
      choose(make_candidate(localplayer, entry.player, exact_account, current_time));
    }

    if (best.player == nullptr)
    {
      if (config.misc.automation.followbot_use_nav == Misc::Automation::followbot_nav_mode::DORMANT &&
          target.player != nullptr && target.index > 0 && entity_list != nullptr)
      {
        auto* entity = entity_list->entity_from_index(static_cast<unsigned int>(target.index));
        player_info info{};
        if (entity != nullptr && entity->get_class_id() == class_id::PLAYER &&
            reinterpret_cast<Player*>(entity)->is_alive() && engine != nullptr &&
            engine->get_player_info(target.index, &info) &&
            static_cast<std::uint32_t>(info.friends_id) == target.account_id)
        {
          target.player = reinterpret_cast<Player*>(entity);
          target.distance = config.misc.automation.followbot_nav_abandon_distance;
          nav_requested = true;
          return;
        }
      }
      reset(false);
      return;
    }

    if (active_account_id != best.account_id)
    {
      trail.clear();
      active_account_id = best.account_id;
      last_jump_time = 0.0f;
    }
    target = best;
  }

  void append_target_node(const Vec3& origin, const Vec3& angles)
  {
    if (!finite_origin(origin)) return;
    if (trail.empty() || distance_2d(trail.back().origin, origin) >= trail_node_spacing)
      trail.push_back({origin, angles});
    else
      trail.back() = {origin, angles};

    const auto max_nodes = static_cast<std::size_t>(std::clamp(config.misc.automation.followbot_max_nodes, 50, 500));
    while (trail.size() > max_nodes) trail.pop_front();
  }

  void apply_look(Player* localplayer, user_cmd* user_cmd, const Vec3& destination)
  {
    const auto mode = config.misc.automation.followbot_look;
    if (mode == Misc::Automation::followbot_look_mode::OFF || user_cmd == nullptr || localplayer == nullptr) return;

    Vec3 angles = calculate_angles(localplayer->get_origin() + localplayer->get_view_offset(), destination);
    if (mode == Misc::Automation::followbot_look_mode::COPY_TARGET && target.player != nullptr)
      angles = clamp_angles(target.player->get_eye_angles());
    else if (mode == Misc::Automation::followbot_look_mode::AT_TARGET && target.player != nullptr)
      angles = calculate_angles(localplayer->get_origin() + localplayer->get_view_offset(), target.player->get_origin() + target.player->get_view_offset());

    if (config.misc.automation.followbot_look_no_snap)
    {
      const auto delta = normalize_yaw(angles.y - user_cmd->view_angles.y);
      angles.y = normalize_yaw(user_cmd->view_angles.y + std::clamp(delta, -12.0f, 12.0f));
      angles.x = user_cmd->view_angles.x + std::clamp(angles.x - user_cmd->view_angles.x, -12.0f, 12.0f);
    }
    user_cmd->view_angles = clamp_angles(angles);
  }

  void move_to(Player* localplayer, user_cmd* user_cmd, float current_time)
  {
    if (localplayer == nullptr || user_cmd == nullptr || target.player == nullptr) return;
    if (entity_list == nullptr || !target.handle.IsValid()) {
      reset(false);
      return;
    }
    auto* target_entity = entity_list->entity_from_handle(target.handle);
    if (target_entity == nullptr || target_entity->get_class_id() != class_id::PLAYER) {
      reset(false);
      return;
    }
    target.player = reinterpret_cast<Player*>(target_entity);
    const auto local_origin = localplayer->get_origin();
    const auto target_origin = target.player->get_origin();
    const auto target_distance = distance_2d(local_origin, target_origin);
    const auto abandon_distance = config.misc.automation.followbot_use_nav != Misc::Automation::followbot_nav_mode::OFF
      ? config.misc.automation.followbot_nav_abandon_distance
      : config.misc.automation.followbot_abandon_distance;
    if (target_distance > abandon_distance)
    {
      reset(false);
      return;
    }
    nav_requested = config.misc.automation.followbot_use_nav != Misc::Automation::followbot_nav_mode::OFF &&
      (target_distance > config.misc.automation.followbot_activation_distance ||
       trail.size() >= static_cast<std::size_t>(std::clamp(config.misc.automation.followbot_max_nodes, 50, 500)));
    if (nav_requested)
    {
      trail.clear();
      active = false;
      return;
    }
    if (target_distance <= config.misc.automation.followbot_follow_distance)
    {
      trail.clear();
      active = false;
      return;
    }

    append_target_node(target_origin, target.player->get_eye_angles());
    while (trail.size() > 1 && distance_2d(local_origin, trail.front().origin) <= trail_reach_distance)
      trail.pop_front();
    if (trail.empty()) return;

    auto destination = trail.front();
    const auto delta = destination.origin - local_origin;
    const auto planar_distance = std::hypot(delta.x, delta.y);
    if (planar_distance <= trail_reach_distance)
    {
      trail.pop_front();
      if (trail.empty()) return;
      destination = trail.front();
    }

    const auto yaw = std::atan2(destination.origin.y - local_origin.y, destination.origin.x - local_origin.x) * pi_degrees;
    const auto yaw_delta = (yaw - user_cmd->view_angles.y) * (1.0f / pi_degrees);
    const auto speed = std::clamp(distance_2d(local_origin, destination.origin) / std::max(1.0f, config.misc.automation.followbot_follow_distance), 0.25f, 1.0f) * follow_move_speed;
    user_cmd->forwardmove = std::cos(yaw_delta) * speed;
    user_cmd->sidemove = -std::sin(yaw_delta) * speed;
    if (destination.origin.z - local_origin.z > follow_jump_height && planar_distance <= follow_jump_run && localplayer->is_on_ground())
    {
      user_cmd->buttons |= IN_JUMP;
      last_jump_time = current_time;
    }
    else if (current_time - last_jump_time >= stuck_jump_delay && localplayer->is_on_ground() && trail.size() > 1)
    {

      user_cmd->buttons |= IN_JUMP;
      last_jump_time = current_time;
    }

    active = true;
    apply_look(localplayer, user_cmd, destination.origin);
  }

  void run(user_cmd* user_cmd)
  {
    active = false;
    nav_requested = false;
    if (!config.misc.automation.followbot_enabled || user_cmd == nullptr || engine == nullptr || entity_list == nullptr || global_vars == nullptr ||
        !engine->is_in_game() || has_manual_movement(user_cmd))
    {
      reset(true);
      return;
    }

    auto* localplayer = entity_list->get_localplayer();
    if (localplayer == nullptr || !localplayer->is_alive() || localplayer->is_taunting())
    {
      reset(false);
      return;
    }

    update_target(localplayer, global_vars->curtime);
    if (target.player == nullptr) return;
    move_to(localplayer, user_cmd, global_vars->curtime);
  }
};

controller_t& controller()
{
  static controller_t instance{};
  return instance;
}

void controller_t::on_create_move(user_cmd* user_cmd)
{
  if (state_ == nullptr) state_ = new impl{};
  state_->run(user_cmd);
}

void controller_t::shutdown()
{
  if (state_ != nullptr)
  {
    state_->reset(true);
    delete state_;
    state_ = nullptr;
  }
}

bool controller_t::is_active() const
{
  return state_ != nullptr && state_->active;
}

bool controller_t::wants_nav() const
{
  return state_ != nullptr && state_->nav_requested;
}

bool controller_t::get_nav_target(Vec3* origin, int* entity_index) const
{
  if (!wants_nav() || state_->target.player == nullptr)
  {
    return false;
  }
  if (entity_list == nullptr || !state_->target.handle.IsValid()) {
    return false;
  }
  auto* target_entity = entity_list->entity_from_handle(state_->target.handle);
  if (target_entity == nullptr || target_entity->get_class_id() != class_id::PLAYER) {
    return false;
  }
  state_->target.player = reinterpret_cast<Player*>(target_entity);
  if (origin != nullptr) *origin = state_->target.player->get_origin();
  if (entity_index != nullptr) *entity_index = state_->target.index;
  return true;
}

std::uint32_t controller_t::target_account_id() const
{
  return state_ != nullptr ? state_->active_account_id : 0;
}

void controller_t::set_ipc_target(std::uint32_t account_id)
{
  g_ipc_target.store(account_id, std::memory_order_release);
}

}
