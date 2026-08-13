/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_controller.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/navbot/navbot_controller.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include "imgui/imgui.h"
#include "core/entity_cache.hpp"
#include "core/math/math.hpp"
#include "core/print.hpp"
#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/team_objective_resource.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

namespace navbot
{

namespace
{

navbot_controller* global_controller = nullptr;
constexpr float goal_refresh_interval = 1.0f;
constexpr float goal_retry_interval = 0.2f;
constexpr float path_retry_interval = 1.0f;
constexpr float transition_failure_retry_seconds = 5.0f;
constexpr float path_job_timeout = 5.0f;
constexpr uint32_t hazard_intersection_blacklist_failures = 8;
constexpr float weapon_switch_interval = 0.35f;
constexpr float navbot_throwable_look_suppress_seconds = 0.55f;
constexpr int team_unassigned = 0;
constexpr int tf_team_blue_value = 3;
constexpr int gr_state_preround = 3;
constexpr int gr_state_between_rounds = 10;
constexpr int tf_stun_controls = 1 << 1;
constexpr int tf_stun_loser_state = 1 << 6;
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

constexpr bool textmode_build = true;
constexpr float hazard_refresh_interval = 1.0f;
#else

constexpr bool textmode_build = false;
constexpr float hazard_refresh_interval = 0.25f;
#endif

static float g_navbot_throwable_look_suppress_until = -1.0e9f;
static float g_navbot_create_move_log_until = -1.0e9f;

bool navbot_weapon_is_arc_throwable(Weapon* weapon)
{
  if (weapon == nullptr)
  {
    return false;
  }

  switch (weapon->get_def_id())
  {
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return true;
  default:
    return false;
  }
}

void navbot_update_throwable_look_suppress(Weapon* weapon, user_cmd* user_cmd, float current_time)
{
  if (user_cmd == nullptr)
  {
    return;
  }

  if ((user_cmd->buttons & IN_ATTACK2) != 0 && navbot_weapon_is_arc_throwable(weapon))
  {
    g_navbot_throwable_look_suppress_until = std::max(
      g_navbot_throwable_look_suppress_until,
      current_time + navbot_throwable_look_suppress_seconds);
  }
}

bool navbot_throwable_look_suppresses_path_look(float current_time)
{
  return current_time < g_navbot_throwable_look_suppress_until;
}

enum class navbot_weapon_slot
{
  none = 0,
  primary = 1,
  secondary = 2,
  melee = 3
};

const char* weapon_slot_command(navbot_weapon_slot slot)
{
  switch (slot)
  {
    case navbot_weapon_slot::primary:
      return "slot1";
    case navbot_weapon_slot::secondary:
      return "slot2";
    case navbot_weapon_slot::melee:
      return "slot3";
    default:
      return nullptr;
  }
}

bool goal_is_supply(goal_type type)
{
  return type == goal_type::get_health || type == goal_type::get_ammo;
}

bool goal_is_reload(goal_type type)
{
  return type == goal_type::reload_weapons;
}

bool goal_is_combat(goal_type type)
{
  return type == goal_type::hold_range_on_enemy
    || type == goal_type::melee_chase
    || type == goal_type::sentry_snipe
    || type == goal_type::mvm_tank
    || type == goal_type::mvm_combat;
}

bool mvm_goal(goal_type type)
{
  return type == goal_type::mvm_tank || type == goal_type::mvm_combat;
}

bool goal_is_payload(goal_type type)
{
  return type == goal_type::push_payload || type == goal_type::defend_payload;
}

bool goal_is_disabled(goal_type type)
{
  return goal_type_can_be_excluded(type)
    && (config.misc.automation.navbot_excluded_jobs_mask & goal_type_bit(type)) != 0;
}

float goal_destination_shift_sq(const navbot_goal_state& left, const navbot_goal_state& right)
{
  auto dx = left.goal.destination.x - right.goal.destination.x;
  auto dy = left.goal.destination.y - right.goal.destination.y;
  auto dz = left.goal.destination.z - right.goal.destination.z;
  return dx * dx + dy * dy + dz * dz;
}

float destination_reach_distance_for_goal(goal_type type)
{
  if (goal_is_supply(type))
  {
    return pickup_destination_reach_distance;
  }
  if (type == goal_type::push_payload)
  {
    return 45.0f;
  }
  if (type == goal_type::reload_weapons)
  {
    return 90.0f;
  }
  if (type == goal_type::heal_follow)
  {
    return 170.0f;
  }
  if (type == goal_type::defend_payload)
  {
    return 90.0f;
  }
  if (type == goal_type::capture_objective)
  {
    return 30.0f;
  }
  if (type == goal_type::mvm_teleporter)
  {
    return 90.0f;
  }
  if (type == goal_type::mvm_upgrade_station)
  {
    return 90.0f;
  }
  if (type == goal_type::melee_chase)
  {
    return melee_destination_reach_distance;
  }
  if (type == goal_type::hold_range_on_enemy
    && config.misc.automation.enemy_stalk_mode == Misc::Automation::navbot_enemy_stalk_mode::YOLO)
  {
    return enemy_yolo_reach_distance;
  }
  if (type == goal_type::followbot)
  {
    return config.misc.automation.followbot_follow_distance;
  }

  return crumb_reach_distance;
}

bool reload_job_still_needed(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  auto* weapon = localplayer->get_weapon();
  return weapon != nullptr
    && weapon->get_clip1() == 0
    && localplayer->get_ammo_count(weapon->get_primary_ammo_type()) > 0;
}

Weapon* controller_primary_weapon(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return nullptr;
  }

  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon != nullptr && weapon->get_slot() == 0)
    {
      return weapon;
    }
  }

  return nullptr;
}

bool controller_primary_weapon_needs_ammo(Player* localplayer)
{
  auto* weapon = controller_primary_weapon(localplayer);
  if (weapon == nullptr || weapon->is_melee())
  {
    return false;
  }

  return weapon->get_clip1() <= 0
    && localplayer->get_ammo_count(weapon->get_primary_ammo_type()) <= 0;
}

bool supply_goal_still_needed(goal_type type, Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  if (type == goal_type::get_health)
  {
    auto max_health = localplayer->get_max_health();
    if (max_health <= 0)
    {
      return false;
    }

    auto health_ratio = static_cast<float>(localplayer->get_health()) / static_cast<float>(max_health);
    return health_ratio < 0.88f;
  }

  if (type == goal_type::get_ammo)
  {
    return controller_primary_weapon_needs_ammo(localplayer);
  }

  return false;
}

bool map_has_cp_or_pl_prefix(const std::string& map_name)
{
  return map_name.starts_with("cp_")
    || map_name.starts_with("pl_")
    || map_name.starts_with("plr_")
    || map_name.starts_with("koth_");
}

navbot_weapon_slot weapon_slot_for_type(int type_id, tf_class class_type)
{
  switch (type_id)
  {
    case TF_WEAPON_BAT:
    case TF_WEAPON_BAT_WOOD:
    case TF_WEAPON_BOTTLE:
    case TF_WEAPON_FIREAXE:
    case TF_WEAPON_CLUB:
    case TF_WEAPON_CROWBAR:
    case TF_WEAPON_KNIFE:
    case TF_WEAPON_FISTS:
    case TF_WEAPON_SHOVEL:
    case TF_WEAPON_WRENCH:
    case TF_WEAPON_BONESAW:
    case TF_WEAPON_SWORD:
    case TF_WEAPON_BAT_FISH:
    case TF_WEAPON_BAT_GIFTWRAP:
    case TF_WEAPON_STICKBOMB:
    case TF_WEAPON_HARVESTER_SAW:
      return navbot_weapon_slot::melee;
    case TF_WEAPON_PDA:
    case TF_WEAPON_PDA_ENGINEER_BUILD:
    case TF_WEAPON_PDA_ENGINEER_DESTROY:
    case TF_WEAPON_PDA_SPY:
    case TF_WEAPON_PDA_SPY_BUILD:
    case TF_WEAPON_BUILDER:
      return navbot_weapon_slot::none;
    case TF_WEAPON_SCATTERGUN:
    case TF_WEAPON_SNIPERRIFLE:
    case TF_WEAPON_MINIGUN:
    case TF_WEAPON_SYRINGEGUN_MEDIC:
    case TF_WEAPON_ROCKETLAUNCHER:
    case TF_WEAPON_GRENADELAUNCHER:
    case TF_WEAPON_FLAMETHROWER:
    case TF_WEAPON_REVOLVER:
    case TF_WEAPON_SHOTGUN_PRIMARY:
    case TF_WEAPON_SHOTGUN_SOLDIER:
    case TF_WEAPON_SHOTGUN_HWG:
    case TF_WEAPON_SHOTGUN_PYRO:
    case TF_WEAPON_COMPOUND_BOW:
    case TF_WEAPON_HANDGUN_SCOUT_PRIMARY:
    case TF_WEAPON_CROSSBOW:
    case TF_WEAPON_SODA_POPPER:
    case TF_WEAPON_SNIPERRIFLE_DECAP:
    case TF_WEAPON_PARTICLE_CANNON:
    case TF_WEAPON_DRG_POMSON:
    case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
    case TF_WEAPON_CANNON:
    case TF_WEAPON_SNIPERRIFLE_CLASSIC:
    case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
      return navbot_weapon_slot::primary;
    case TF_WEAPON_PIPEBOMBLAUNCHER:
    case TF_WEAPON_PISTOL:
    case TF_WEAPON_PISTOL_SCOUT:
    case TF_WEAPON_SMG:
    case TF_WEAPON_MEDIGUN:
    case TF_WEAPON_INVIS:
    case TF_WEAPON_FLAREGUN:
    case TF_WEAPON_LUNCHBOX:
    case TF_WEAPON_JAR:
    case TF_WEAPON_BUFF_ITEM:
    case TF_WEAPON_LASER_POINTER:
    case TF_WEAPON_SENTRY_REVENGE:
    case TF_WEAPON_JAR_MILK:
    case TF_WEAPON_HANDGUN_SCOUT_SECONDARY:
    case TF_WEAPON_RAYGUN:
    case TF_WEAPON_MECHANICAL_ARM:
    case TF_WEAPON_FLAREGUN_REVENGE:
    case TF_WEAPON_CLEAVER:
    case TF_WEAPON_THROWABLE:
    case TF_WEAPON_PARACHUTE:
      return navbot_weapon_slot::secondary;
    default:
      break;
  }

  switch (class_type)
  {
    case tf_class::MEDIC:
      return navbot_weapon_slot::secondary;
    case tf_class::SPY:
      return navbot_weapon_slot::primary;
    default:
      return navbot_weapon_slot::primary;
  }
}

navbot_weapon_slot weapon_slot_for(Weapon* weapon, tf_class class_type)
{
  if (weapon == nullptr)
  {
    return navbot_weapon_slot::none;
  }

  switch (weapon->get_slot())
  {
    case 0:
      return navbot_weapon_slot::primary;
    case 1:
      return navbot_weapon_slot::secondary;
    case 2:
      return navbot_weapon_slot::melee;
    default:
      break;
  }

  if (weapon->is_melee())
  {
    return navbot_weapon_slot::melee;
  }

  return weapon_slot_for_type(weapon->get_weapon_id(), class_type);
}

bool weapon_slot_available(Player* localplayer, navbot_weapon_slot slot)
{
  if (localplayer == nullptr || slot == navbot_weapon_slot::none)
  {
    return false;
  }

  auto class_type = localplayer->get_tf_class();
  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon == nullptr)
    {
      continue;
    }

    if (weapon_slot_for(weapon, class_type) == slot)
    {
      return true;
    }
  }

  return false;
}

Weapon* weapon_for_slot(Player* localplayer, navbot_weapon_slot slot)
{
  if (localplayer == nullptr || slot == navbot_weapon_slot::none)
  {
    return nullptr;
  }

  const auto class_type = localplayer->get_tf_class();
  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon != nullptr && weapon_slot_for(weapon, class_type) == slot)
    {
      return weapon;
    }
  }

  return nullptr;
}

bool weapon_slot_loaded(Player* localplayer, navbot_weapon_slot slot)
{
  if (localplayer == nullptr || slot == navbot_weapon_slot::none)
  {
    return false;
  }

  auto class_type = localplayer->get_tf_class();
  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon == nullptr || weapon_slot_for(weapon, class_type) != slot)
    {
      continue;
    }

    auto clip = weapon->get_clip1();
    if (clip > 0
      || weapon->is_melee()
      || weapon->is_medigun()
      || localplayer->get_ammo_count(weapon->get_primary_ammo_type()) > 0)
    {
      return true;
    }
  }

  return false;
}

float distance_to_enemy(Player* localplayer, Player* enemy)
{
  if (localplayer == nullptr || enemy == nullptr)
  {
    return 8192.0f;
  }

  return distance_3d(localplayer->get_origin(), enemy->get_origin());
}

Player* choose_navbot_enemy(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return nullptr;
  }

  Player* aimbot_target = aimbot::active_target_player();
  if (aimbot_target != nullptr
    && !aimbot_target->is_dormant()
    && aimbot_target->is_alive()
    && aimbot_target->get_team() != localplayer->get_team())
  {
    return aimbot_target;
  }

  Player* best_enemy = nullptr;
  auto best_distance = std::numeric_limits<float>::max();
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player == localplayer || player->is_dormant())
    {
      continue;
    }

    if (player->get_team() == localplayer->get_team() || !player->is_alive())
    {
      continue;
    }

    auto distance = distance_3d(localplayer->get_origin(), player->get_origin());
    if (distance < best_distance)
    {
      best_distance = distance;
      best_enemy = player;
    }
  }

  return best_enemy;
}

navbot_weapon_slot choose_default_slot(tf_class class_type)
{
  switch (class_type)
  {
    case tf_class::MEDIC:
      return navbot_weapon_slot::secondary;
    default:
      return navbot_weapon_slot::primary;
  }
}

navbot_weapon_slot choose_combat_slot(Player* localplayer, goal_type goal, Player* enemy)
{
  if (localplayer == nullptr)
  {
    return navbot_weapon_slot::none;
  }

  if (goal == goal_type::engineer_build || goal == goal_type::engineer_maintain)
  {
    return navbot_weapon_slot::melee;
  }

  if (enemy == nullptr)
  {
    return choose_default_slot(localplayer->get_tf_class());
  }

  const auto enemy_distance = distance_to_enemy(localplayer, enemy);
  const auto melee_reachable = std::fabs(enemy->get_origin().z - localplayer->get_origin().z) <= 80.0f;
  const auto primary_loaded = weapon_slot_loaded(localplayer, navbot_weapon_slot::primary);
  const auto secondary_loaded = weapon_slot_loaded(localplayer, navbot_weapon_slot::secondary);
  switch (localplayer->get_tf_class())
  {
    case tf_class::SCOUT:
      if (melee_reachable && !primary_loaded && !secondary_loaded && enemy_distance <= 200.0f)
      {
        return navbot_weapon_slot::melee;
      }
      if (secondary_loaded && (!primary_loaded || enemy_distance > 900.0f))
      {
        return navbot_weapon_slot::secondary;
      }
      return navbot_weapon_slot::primary;
    case tf_class::SNIPER:
      if (melee_reachable && enemy_distance <= 200.0f)
      {
        return navbot_weapon_slot::melee;
      }
      if (secondary_loaded && enemy_distance <= 240.0f && !primary_loaded)
      {
        return navbot_weapon_slot::secondary;
      }
      return navbot_weapon_slot::primary;
    case tf_class::SOLDIER:
    {
      auto* enemy_weapon = enemy->get_weapon();
      const auto enemy_can_airblast = enemy_weapon != nullptr
        && enemy_weapon->is_flamethrower()
        && enemy_weapon->get_def_id() != Pyro_m_ThePhlogistinator;
      if (secondary_loaded && (!primary_loaded || enemy_can_airblast
        || (enemy_distance <= 250.0f && enemy->get_health() <= 90)))
      {
        return navbot_weapon_slot::secondary;
      }
      return navbot_weapon_slot::primary;
    }
    case tf_class::DEMOMAN:
      return primary_loaded ? navbot_weapon_slot::primary
        : (melee_reachable && enemy_distance <= 260.0f ? navbot_weapon_slot::melee : navbot_weapon_slot::primary);
    case tf_class::PYRO:
      if (primary_loaded && enemy_distance <= 520.0f)
      {
        return navbot_weapon_slot::primary;
      }
      return navbot_weapon_slot::secondary;
    case tf_class::MEDIC:
      if (weapon_slot_loaded(localplayer, navbot_weapon_slot::secondary))
      {
        auto* medigun = weapon_for_slot(localplayer, navbot_weapon_slot::secondary);
        if (medigun != nullptr && medigun->is_medigun() && medigun->medigun_healing_target() != nullptr)
          return navbot_weapon_slot::secondary;
      }
      if (melee_reachable && (!primary_loaded || enemy_distance <= 120.0f))
        return navbot_weapon_slot::melee;
      return navbot_weapon_slot::primary;
    case tf_class::SPY:
      if (melee_reachable && (localplayer->in_cond(TF_COND_STEALTHED) || enemy_distance <= 110.0f))
      {
        return navbot_weapon_slot::melee;
      }
      return navbot_weapon_slot::primary;
    case tf_class::ENGINEER:
      if (goal == goal_type::sentry_snipe)
      {
        return navbot_weapon_slot::primary;
      }
      if (primary_loaded && enemy_distance <= 1000.0f)
      {
        return navbot_weapon_slot::primary;
      }
      return secondary_loaded ? navbot_weapon_slot::secondary : navbot_weapon_slot::primary;
    case tf_class::HEAVYWEAPONS:
    case tf_class::UNDEFINED:
    default:
      return navbot_weapon_slot::primary;
  }
}

navbot_weapon_slot choose_navbot_weapon_slot(Player* localplayer, const navbot_goal_state& goal_state)
{
  if (localplayer == nullptr)
  {
    return navbot_weapon_slot::none;
  }

  const auto weapon_mode = config.misc.automation.navbot_weapon_selection;
  if (weapon_mode != Misc::Automation::navbot_weapon_mode::AUTO)
  {
    switch (weapon_mode)
    {
      case Misc::Automation::navbot_weapon_mode::PRIMARY:
        return navbot_weapon_slot::primary;
      case Misc::Automation::navbot_weapon_mode::SECONDARY:
        return navbot_weapon_slot::secondary;
      case Misc::Automation::navbot_weapon_mode::MELEE:
        return navbot_weapon_slot::melee;
      case Misc::Automation::navbot_weapon_mode::OFF:
      default:
        return navbot_weapon_slot::none;
    }
  }

  auto goal = goal_state.valid ? goal_state.goal.type : goal_type::roam;
  if (goal == goal_type::engineer_build || goal == goal_type::engineer_maintain)
  {
    if (weapon_slot_available(localplayer, navbot_weapon_slot::melee))
    {
      return navbot_weapon_slot::melee;
    }
  }

  if (goal == goal_type::heal_follow && localplayer->get_tf_class() == tf_class::MEDIC)
  {
    return navbot_weapon_slot::secondary;
  }

  if (mvm_goal(goal)
    && localplayer->get_tf_class() == tf_class::SNIPER
    && weapon_slot_available(localplayer, navbot_weapon_slot::primary))
  {
    return navbot_weapon_slot::primary;
  }

  auto* enemy = choose_navbot_enemy(localplayer);

  auto desired_slot = goal_is_combat(goal)
    ? choose_combat_slot(localplayer, goal, enemy)
    : choose_default_slot(localplayer->get_tf_class());

  if (weapon_slot_loaded(localplayer, desired_slot))
  {
    return desired_slot;
  }

  constexpr navbot_weapon_slot fallback_slots[] = {
    navbot_weapon_slot::primary,
    navbot_weapon_slot::secondary,
    navbot_weapon_slot::melee
  };
  for (auto slot : fallback_slots)
  {
    if (slot != desired_slot && weapon_slot_loaded(localplayer, slot))
    {
      return slot;
    }
  }
  for (auto slot : fallback_slots)
  {
    if (weapon_slot_available(localplayer, slot))
    {
      return slot;
    }
  }

  return desired_slot;
}

std::string sanitize_level_name(const char* raw_name)
{
  if (raw_name == nullptr)
  {
    return {};
  }

  auto map_name = std::string(raw_name);
  auto slash = map_name.find_last_of("/\\");
  if (slash != std::string::npos)
  {
    map_name = map_name.substr(slash + 1);
  }

  if (map_name.ends_with(".bsp"))
  {
    map_name.resize(map_name.size() - 4);
  }

  return map_name;
}

bool same_goal_destination(const navbot_goal_state& left, const navbot_goal_state& right)
{
  if (left.valid != right.valid)
  {
    return false;
  }
  if (!left.valid)
  {
    return true;
  }

  if (left.goal.type != right.goal.type || left.goal.destination_area.value != right.goal.destination_area.value)
  {
    return false;
  }
  if (left.goal.entity_index != right.goal.entity_index)
  {
    return false;
  }

  auto shift_limit = goal_is_supply(left.goal.type) ? 24.0f : 96.0f;
  if (goal_is_payload(left.goal.type))
  {
    shift_limit = 40.0f;
  }
  if (left.goal.type == goal_type::heal_follow)
  {
    shift_limit = 72.0f;
  }
  if (left.goal.type == goal_type::melee_chase)
  {
    shift_limit = 128.0f;
  }
  if (left.goal.type == goal_type::followbot)
  {
    shift_limit = 72.0f;
  }
  if (left.goal.type == goal_type::mvm_combat || left.goal.type == goal_type::mvm_tank)
  {
    shift_limit = 160.0f;
  }

  return goal_destination_shift_sq(left, right) <= shift_limit * shift_limit;
}

bool should_replace_goal(const navbot_goal_state& active_goal, const navbot_goal_state& next_goal, bool has_path, Player* localplayer)
{
  if (!next_goal.valid)
  {
    return false;
  }
  if (!active_goal.valid)
  {
    return true;
  }

  if (active_goal.goal.type == goal_type::heal_follow)
  {
    auto* heal_target = medic_automation::controller().heal_target();
    if (heal_target == nullptr || heal_target->is_dormant() || !heal_target->is_alive())
    {
      return true;
    }
    if (next_goal.goal.type != goal_type::heal_follow || next_goal.goal.entity_index != active_goal.goal.entity_index)
    {
      return true;
    }
  }

  if (active_goal.goal.type == goal_type::followbot)
  {
    Vec3 destination{};
    int entity_index = 0;
    if (!followbot::controller().get_nav_target(&destination, &entity_index) ||
        entity_index != active_goal.goal.entity_index)
    {
      return true;
    }
  }

  if (active_goal.goal.type == goal_type::melee_chase
    && next_goal.goal.type == goal_type::melee_chase
    && !has_path
    && localplayer != nullptr
    && distance_3d(localplayer->get_origin(), active_goal.goal.destination) <= melee_destination_reach_distance
    && distance_3d(localplayer->get_origin(), next_goal.goal.destination) > melee_destination_reach_distance)
  {
    return true;
  }

  if (has_path && goal_is_supply(active_goal.goal.type))
  {
    if (!supply_goal_still_needed(active_goal.goal.type, localplayer))
    {
      return true;
    }

    if (next_goal.goal.type == goal_type::roam || goal_is_supply(next_goal.goal.type))
    {
      return false;
    }
  }

  if (has_path && goal_is_reload(active_goal.goal.type))
  {
    if (!reload_job_still_needed(localplayer))
    {
      return true;
    }

    return next_goal.goal.type == goal_type::reload_weapons
      && !same_goal_destination(active_goal, next_goal);
  }

  if (same_goal_destination(active_goal, next_goal))
  {
    return false;
  }
  if (!has_path)
  {
    return true;
  }
  if (goal_is_payload(active_goal.goal.type) && active_goal.goal.type == next_goal.goal.type)
  {
    return true;
  }
  if (active_goal.goal.type == goal_type::hold_range_on_enemy && next_goal.goal.type == goal_type::hold_range_on_enemy)
  {
    return true;
  }
  if (active_goal.goal.type == goal_type::melee_chase && next_goal.goal.type == goal_type::melee_chase)
  {
    return !same_goal_destination(active_goal, next_goal);
  }
  if (active_goal.goal.type == goal_type::followbot && next_goal.goal.type == goal_type::followbot)
  {
    return !same_goal_destination(active_goal, next_goal);
  }
  if (next_goal.goal.type == active_goal.goal.type)
  {
    return false;
  }
  if (active_goal.goal.type == goal_type::roam)
  {
    return true;
  }

  return next_goal.score > active_goal.score + 20.0f;
}

bool same_nav_edge(nav_edge_id left, nav_edge_id right)
{
  return left.from_area == right.from_area && left.connection_index == right.connection_index;
}

bool nav_edge_valid(nav_edge_id edge_id)
{
  return edge_id.from_area != 0;
}

void reset_debug_runtime(navbot_debug_state& debug_state)
{
  debug_state.goal_valid = false;
  debug_state.has_active_path = false;
  debug_state.active_crumb_count = 0;
  debug_state.current_goal = goal_type::roam;
  debug_state.current_path_status = path_status::failed;
  debug_state.last_failure = follower_failure_reason::none;
  debug_state.pending_generation_id = 0;
  debug_state.job_availability = {};
  debug_state.runtime_state = "idle";
}

struct path_spin_runtime_state
{
  const std::vector<crumb>* crumbs = nullptr;
  size_t crumb_index = std::numeric_limits<size_t>::max();
  uint32_t crumb_identity = 0;
  float remaining_degrees = 0.0f;
  float direction = 1.0f;
  bool active = false;
};

path_spin_runtime_state g_path_spin_state{};

uint32_t path_spin_hash_value(uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

uint32_t path_spin_hash_crumb(const crumb& path_crumb, size_t crumb_index, int command_number)
{
  uint32_t value = static_cast<uint32_t>(crumb_index + 1);
  value ^= path_crumb.area_id.value + 0x9e3779b9u + (value << 6) + (value >> 2);
  value ^= static_cast<uint32_t>(static_cast<int>(std::floor(path_crumb.world.x))) + 0x9e3779b9u + (value << 6) + (value >> 2);
  value ^= static_cast<uint32_t>(static_cast<int>(std::floor(path_crumb.world.y))) + 0x9e3779b9u + (value << 6) + (value >> 2);
  value ^= static_cast<uint32_t>(command_number) + 0x9e3779b9u + (value << 6) + (value >> 2);
  return path_spin_hash_value(value);
}

void reset_path_spin_state()
{
  g_path_spin_state = {};
  g_path_spin_state.crumb_index = std::numeric_limits<size_t>::max();
}

struct path_look_silent_state
{
  bool active = false;
  float pitch = 0.0f;
  float yaw = 0.0f;
};

path_look_silent_state g_path_look_silent_state{};

void reset_path_look_silent_state()
{
  g_path_look_silent_state = {};
}

bool path_spin_trigger_matches(user_cmd* user_cmd, const std::vector<crumb>& crumbs, size_t current_index)
{
  if (current_index >= crumbs.size() || crumbs[current_index].kind == crumb_kind::destination)
  {
    return false;
  }

  const int chance = std::clamp(config.misc.automation.navbot_look_at_path_spin_chance, 0, 100);
  if (chance <= 0)
  {
    return false;
  }

  if (chance >= 100)
  {
    return true;
  }

  const int command_number = user_cmd != nullptr ? user_cmd->command_number : 0;
  return static_cast<int>(path_spin_hash_crumb(crumbs[current_index], current_index, command_number) % 100u) < chance;
}

bool path_spin_active(user_cmd* user_cmd, const std::vector<crumb>& crumbs, size_t current_index)
{
  if (current_index >= crumbs.size())
  {
    reset_path_spin_state();
    return false;
  }

  const uint32_t crumb_identity = path_spin_hash_crumb(crumbs[current_index], current_index, 0);
  if (g_path_spin_state.crumbs != &crumbs
    || g_path_spin_state.crumb_index != current_index
    || g_path_spin_state.crumb_identity != crumb_identity)
  {
    g_path_spin_state.crumbs = &crumbs;
    g_path_spin_state.crumb_index = current_index;
    g_path_spin_state.crumb_identity = crumb_identity;
    g_path_spin_state.active = path_spin_trigger_matches(user_cmd, crumbs, current_index);
    g_path_spin_state.remaining_degrees = g_path_spin_state.active ? 360.0f : 0.0f;
    const int command_number = user_cmd != nullptr ? user_cmd->command_number : 0;
    g_path_spin_state.direction = (path_spin_hash_crumb(crumbs[current_index], current_index, command_number) & 1u) != 0u ? 1.0f : -1.0f;
  }

  return g_path_spin_state.active && g_path_spin_state.remaining_degrees > 0.0f;
}

float path_spin_yaw_move(float tick_interval)
{
  constexpr float path_spin_speed = 720.0f;
  const float spin_step = std::min(g_path_spin_state.remaining_degrees, path_spin_speed * tick_interval);
  g_path_spin_state.remaining_degrees -= spin_step;
  if (g_path_spin_state.remaining_degrees <= 0.001f)
  {
    g_path_spin_state.active = false;
    g_path_spin_state.remaining_degrees = 0.0f;
  }
  return spin_step * g_path_spin_state.direction;
}

float normalize_angle_180(float angle);

bool apply_path_spin(user_cmd* user_cmd, const std::vector<crumb>& crumbs, size_t current_index)
{
  if (user_cmd == nullptr || !path_spin_active(user_cmd, crumbs, current_index))
  {
    return false;
  }

  const float tick_interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : static_cast<float>(TICK_INTERVAL);
  user_cmd->view_angles.y = normalize_angle_180(user_cmd->view_angles.y + path_spin_yaw_move(tick_interval));
  user_cmd->view_angles.z = 0.0f;
  return true;
}

void apply_reload_controls(user_cmd* user_cmd)
{
  if (user_cmd == nullptr)
  {
    return;
  }

  user_cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2 | IN_ATTACK3);
  user_cmd->buttons |= IN_RELOAD;
}

float normalize_angle_180(float angle)
{
  while (angle > 180.0f)
  {
    angle -= 360.0f;
  }
  while (angle < -180.0f)
  {
    angle += 360.0f;
  }
  return angle;
}

bool apply_look_at_path(Player* localplayer, user_cmd* user_cmd, const std::vector<crumb>& crumbs, size_t current_index)
{
  if (localplayer == nullptr || user_cmd == nullptr || global_vars == nullptr || crumbs.empty() || current_index >= crumbs.size())
  {
    return false;
  }

  Vec3 eye_origin = localplayer->get_origin() + localplayer->get_view_offset();
  if (path_spin_active(user_cmd, crumbs, current_index))
  {

    return false;
  }

  const Vec3& target = crumbs[current_index].world;
  const float delta_x = target.x - eye_origin.x;
  const float delta_y = target.y - eye_origin.y;
  if (std::fabs(delta_x) <= 0.001f && std::fabs(delta_y) <= 0.001f)
  {
    return false;
  }

  const int slow_aim = std::clamp(config.misc.automation.navbot_look_at_path_speed, 1, 100);
  const float desired_yaw = std::atan2(delta_y, delta_x) * radpi;

  const bool silent = config.misc.automation.navbot_look_at_path_silent;
  if (!silent)
  {
    reset_path_look_silent_state();
  }

  if (silent && !g_path_look_silent_state.active)
  {
    g_path_look_silent_state.active = true;
    g_path_look_silent_state.pitch = user_cmd->view_angles.x;
    g_path_look_silent_state.yaw = user_cmd->view_angles.y;
  }

  const float current_pitch = silent ? g_path_look_silent_state.pitch : user_cmd->view_angles.x;
  const float current_yaw = silent ? g_path_look_silent_state.yaw : user_cmd->view_angles.y;
  const float yaw_delta = normalize_angle_180(desired_yaw - current_yaw);
  const float pitch_delta = -current_pitch;

  user_cmd->view_angles.x = std::clamp(current_pitch + (pitch_delta / slow_aim), -89.0f, 89.0f);
  user_cmd->view_angles.y = normalize_angle_180(current_yaw + (yaw_delta / slow_aim));
  user_cmd->view_angles.z = 0.0f;

  if (silent)
  {
    g_path_look_silent_state.pitch = user_cmd->view_angles.x;
    g_path_look_silent_state.yaw = user_cmd->view_angles.y;
    return true;
  }

  if (prediction != nullptr)
  {
    Vec3 predicted_angles = user_cmd->view_angles;
    prediction->set_local_view_angles(predicted_angles);
    prediction->set_view_angles(predicted_angles);
  }

  if (engine != nullptr)
  {
    Vec3 engine_angles = user_cmd->view_angles;
    engine->set_view_angles(engine_angles);
  }

  return true;
}

}

void navbot_controller::apply_post_anti_aim(user_cmd* user_cmd)
{
  if (user_cmd == nullptr
    || !config.misc.automation.navbot_enabled
    || !config.misc.automation.navbot_look_at_path
    || navbot_throwable_look_suppresses_path_look(global_vars != nullptr ? global_vars->curtime : 0.0f)
    || follower_.current_crumb() == nullptr)
  {
    if (!config.misc.automation.navbot_look_at_path)
    {
      reset_path_spin_state();
      reset_path_look_silent_state();
    }
    return;
  }

  if ((user_cmd->buttons & (IN_ATTACK | IN_ATTACK2 | IN_ATTACK3)) != 0)
  {
    return;
  }

  if (!apply_path_spin(user_cmd, follower_.crumbs(), follower_.current_crumb_index()))
  {
    const bool look_applied = apply_look_at_path(
      entity_list != nullptr ? entity_list->get_localplayer() : nullptr,
      user_cmd,
      follower_.crumbs(),
      follower_.current_crumb_index());
    silent_path_look_ = look_applied && config.misc.automation.navbot_look_at_path_silent;
    if (silent_path_look_)
    {
      silent_path_look_angles_ = user_cmd->view_angles;
    }
    return;
  }

  if (config.misc.automation.navbot_look_at_path_silent)
  {
    silent_path_look_ = true;
    silent_path_look_angles_ = user_cmd->view_angles;
    return;
  }

  if (engine != nullptr)
  {
    Vec3 engine_angles = user_cmd->view_angles;
    engine->set_view_angles(engine_angles);
  }
}

void navbot_controller::clear_runtime_state()
{
  clear_active_path_state();
  reset_path_spin_state();
  active_goal_ = {};
  pending_job_ = {};
  pending_job_submitted_at_ = 0.0f;
  next_goal_retry_time_ = 0.0f;
  next_hazard_update_time_ = 0.0f;
  crumb_failure_ = {};
  suppress_aimbot_for_reload_ = false;
  silent_path_look_ = false;
  silent_path_look_angles_ = {};
  reset_path_look_silent_state();
  reset_debug_runtime(debug_state_);
}

void navbot_controller::shutdown()
{
  jobs_.stop();
  jobs_started_ = false;
  clear_runtime_state();
}

void navbot_controller::clear_active_path_state()
{
  follower_.clear();
  active_path_ = path_result{};
  clear_draw_snapshot();
}

void navbot_controller::invalidate_active_path(bool clear_goal)
{
  jobs_.cancel_generation(current_generation_id_);
  ++current_generation_id_;
  pending_job_ = {};
  pending_job_submitted_at_ = 0.0f;
  clear_active_path_state();
  if (clear_goal)
  {
    active_goal_ = {};
  }
}

bool navbot_controller::active_goal_needs_reset(Player* localplayer) const
{
  if (!active_goal_.valid)
  {
    return false;
  }

  if (goal_is_disabled(active_goal_.goal.type))
  {
    return true;
  }

  if (active_goal_.goal.type == goal_type::reload_weapons)
  {
    return !reload_job_still_needed(localplayer);
  }

  if (active_goal_.goal.type == goal_type::melee_chase)
  {
    auto* target_entity = entity_list != nullptr
      ? entity_list->entity_from_index(static_cast<unsigned int>(active_goal_.goal.entity_index))
      : nullptr;
    auto* target = target_entity != nullptr && target_entity->get_class_id() == class_id::PLAYER
      ? reinterpret_cast<Player*>(target_entity)
      : nullptr;
    if (target == nullptr
      || target->is_dormant()
      || !target->is_alive()
      || target->get_team() == localplayer->get_team())
    {
      return true;
    }

    const auto target_distance = distance_3d(localplayer->get_origin(), target->get_origin());
    const auto target_range = std::clamp(config.misc.automation.navbot_melee_target_range, 150.0f, 4000.0f);
    if (target_distance > target_range
      || std::fabs(target->get_origin().z - localplayer->get_origin().z) > melee_chase_vertical_limit)
    {
      return true;
    }

    if (localplayer->get_tf_class() == tf_class::SPY)
    {
      return !localplayer->in_cond(TF_COND_STEALTHED)
        && target_distance > melee_chase_spy_approach_distance;
    }

    return target_distance > melee_chase_switch_distance;
  }

  if (active_goal_.goal.type == goal_type::mvm_tank
    || active_goal_.goal.type == goal_type::mvm_combat
    || active_goal_.goal.type == goal_type::mvm_money
    || active_goal_.goal.type == goal_type::mvm_teleporter
    || active_goal_.goal.type == goal_type::mvm_upgrade_station)
  {
    auto* target = entity_list != nullptr
      ? entity_list->entity_from_index(static_cast<unsigned int>(active_goal_.goal.entity_index))
      : nullptr;
    if (target == nullptr || target->is_dormant())
    {
      return true;
    }
    if (active_goal_.goal.type == goal_type::mvm_combat
      && (target->get_class_id() != class_id::PLAYER || !reinterpret_cast<Player*>(target)->is_alive()))
    {
      return true;
    }
    if (active_goal_.goal.type == goal_type::mvm_tank && !target->is_network_class("CTFTankBoss"))
    {
      return true;
    }
    if (active_goal_.goal.type == goal_type::mvm_upgrade_station
      && !target->is_network_class("CFuncUpgrades")
      && !target->is_network_class("CUpgrades")
      && !target->is_network_class("CFuncUpgradeStation"))
    {
      return true;
    }
  }

  if (active_goal_.goal.type == goal_type::followbot)
  {
    Vec3 destination{};
    int entity_index = 0;
    return !followbot::controller().get_nav_target(&destination, &entity_index)
      || entity_index != active_goal_.goal.entity_index;
  }

  if (active_goal_.goal.type != goal_type::heal_follow)
  {
    return false;
  }

  auto* heal_target = medic_automation::controller().heal_target();
  return heal_target == nullptr
    || heal_target->is_dormant()
    || !heal_target->is_alive()
    || heal_target->get_team() != localplayer->get_team()
    || heal_target->get_index() != active_goal_.goal.entity_index;
}

void navbot_controller::refresh_goal(Player* localplayer, float current_time)
{
  const bool has_active_path_or_pending_request = follower_.has_path()
    || (pending_job_.id != 0 && pending_job_.generation_id == current_generation_id_);
  const bool needs_new_goal = !active_goal_.valid || !has_active_path_or_pending_request;
  if ((!needs_new_goal || current_time < next_goal_retry_time_) && current_time < next_goal_refresh_time_)
  {
    return;
  }

  next_goal_refresh_time_ = current_time + goal_refresh_interval;
  next_goal_retry_time_ = current_time + goal_retry_interval;

  auto next_goal = goals_.select_goal(mesh_, localplayer, current_time, mvm_wave_started_);
  Vec3 follow_destination{};
  int follow_entity_index = 0;
  if (!goal_is_disabled(goal_type::followbot)
    && followbot::controller().get_nav_target(&follow_destination, &follow_entity_index))
  {
    auto follow_goal = goal_candidate{
      goal_type::followbot,
      std::numeric_limits<float>::max(),
      follow_destination,
      mesh_.find_closest_area(follow_destination),
      follow_entity_index,
      false
    };
    if (!goals_.is_goal_rejected(follow_goal, current_time))
    {
      next_goal = {};
      next_goal.valid = true;
      next_goal.score = follow_goal.score;
      next_goal.goal = follow_goal;
    }
  }
  if (!should_replace_goal(active_goal_, next_goal, has_active_path_or_pending_request, localplayer))
  {
    return;
  }

  invalidate_active_path(false);
  active_goal_ = std::move(next_goal);
  crumb_failure_ = {};
  next_path_request_time_ = 0.0f;
}

void navbot_controller::update_runtime_debug()
{
  debug_state_.current_goal = active_goal_.goal.type;
  debug_state_.active_generation_id = current_generation_id_;
  debug_state_.active_world_generation = world_generation_id_;
  debug_state_.active_hazard_generation = hazards_.generation();
  debug_state_.pending_generation_id = pending_job_.generation_id;
  debug_state_.mesh_ready = mesh_.is_ready();
  debug_state_.goal_valid = active_goal_.valid;
  debug_state_.map_name = mesh_.map_name();
  debug_state_.nav_file_path = mesh_.nav_file_path();
  debug_state_.captured_point_index = current_captured_point_index();
  debug_state_.mini_round_mask = current_mini_round_mask();
  debug_state_.setup_finished = setup_finished_;
  debug_state_.job_availability = goals_.job_availability();

  auto& follow_availability = debug_state_.job_availability[goal_type_index(goal_type::followbot)];
  follow_availability.enabled = !goal_is_disabled(goal_type::followbot);
  Vec3 follow_destination{};
  int follow_entity_index = 0;
  if (follow_availability.enabled
    && followbot::controller().get_nav_target(&follow_destination, &follow_entity_index))
  {
    const auto follow_area = mesh_.find_closest_area(follow_destination);
    const auto follow_goal = goal_candidate{
      goal_type::followbot,
      std::numeric_limits<float>::max(),
      follow_destination,
      follow_area,
      follow_entity_index,
      false
    };
    follow_availability.candidate_available = follow_area.valid()
      && !goals_.is_goal_rejected(follow_goal, global_vars != nullptr ? global_vars->curtime : 0.0f);
  }

}

void navbot_controller::install_active_path(const path_result& path)
{
  active_path_ = path;
  follower_.set_path(path_result(path));
  update_draw_snapshot();
}

void navbot_controller::update_draw_snapshot()
{
  draw_path_snapshot snapshot{};
  if (active_path_.status == path_status::success && follower_.has_path())
  {
    snapshot.path = active_path_;
    snapshot.current_crumb_index = follower_.current_crumb_index();
    snapshot.reached_crumb_times = follower_.reached_crumb_times();
  }

  std::scoped_lock lock(draw_snapshot_mutex_);
  draw_snapshot_ = std::move(snapshot);
}

void navbot_controller::clear_draw_snapshot()
{
  std::scoped_lock lock(draw_snapshot_mutex_);
  draw_snapshot_ = {};
}

bool navbot_controller::record_crumb_failure(const follower_tick_result& follow_result, float current_time)
{
  if (!follow_result.failed || !config.misc.automation.navbot_hazards)
  {
    return false;
  }

  if (!nav_edge_valid(follow_result.failed_edge))
  {
    crumb_failure_ = {};
    return false;
  }

  auto blacklist_seconds = std::clamp(config.misc.automation.navbot_crumb_blacklist_seconds, 50.0f, 150.0f);
  auto same_failed_crumb = same_nav_edge(crumb_failure_.edge_id, follow_result.failed_edge);
  if (!same_failed_crumb || current_time - crumb_failure_.last_failure_time > blacklist_seconds)
  {
    crumb_failure_.area_id = follow_result.failed_crumb_area;
    crumb_failure_.edge_id = follow_result.failed_edge;
    crumb_failure_.count = 1;
    crumb_failure_.last_failure_time = current_time;
    return false;
  }

  ++crumb_failure_.count;
  crumb_failure_.last_failure_time = current_time;
  const auto required_failures = follow_result.failure_reason == follower_failure_reason::hazard_intersection
    ? hazard_intersection_blacklist_failures
    : 2u;
  if (crumb_failure_.count < required_failures)
  {
    return false;
  }

  hazards_.add_crumb_blacklist(follow_result.failed_edge, current_time, blacklist_seconds);
  crumb_failure_ = {};
  return true;
}

int navbot_controller::current_captured_point_index() const
{
  if (mesh_.map_name().starts_with("koth_"))
  {
    return -1;
  }

  if (last_captured_point_index_ >= 0)
  {
    return last_captured_point_index_;
  }

  int highest_point_index = -1;
  for (auto* entity : entity_cache[class_id::OBJECTIVE_RESOURCE])
  {
    TeamObjectiveResource* objective = reinterpret_cast<TeamObjectiveResource*>(entity);
    if (objective == nullptr)
    {
      continue;
    }

    const int point_count = std::clamp(objective->get_num_control_points(), 0, MAX_CONTROL_POINTS);
    const bool playing_mini_rounds = objective->is_playing_mini_rounds();
    for (int point_index = 0; point_index < point_count; ++point_index)
    {
      if (playing_mini_rounds && !objective->is_in_mini_round(point_index))
      {
        continue;
      }

      const int owning_team = objective->get_owning_team(point_index);
      if (owning_team == team_unassigned)
      {
        continue;
      }

      if (owning_team == tf_team_blue_value)
      {
        highest_point_index = std::max(highest_point_index, point_index);
      }
    }
  }

  return highest_point_index;
}

uint32_t navbot_controller::current_mini_round_mask() const
{
  uint32_t mask = 0;
  for (auto* entity : entity_cache[class_id::OBJECTIVE_RESOURCE])
  {
    TeamObjectiveResource* objective = reinterpret_cast<TeamObjectiveResource*>(entity);
    if (objective == nullptr)
    {
      continue;
    }

    const int point_count = std::clamp(objective->get_num_control_points(), 0, MAX_CONTROL_POINTS);
    for (int point_index = 0; point_index < point_count; ++point_index)
    {
      if (objective->is_in_mini_round(point_index))
      {
        mask |= 1u << static_cast<uint32_t>(point_index);
      }
    }
  }

  return mask;
}

bool navbot_controller::should_block_pathing(Player* localplayer) const
{
  if (localplayer == nullptr)
  {
    return false;
  }

  const bool controls_stunned = localplayer->in_cond(TF_COND_STUNNED)
    && (localplayer->get_stun_flags() & (tf_stun_controls | tf_stun_loser_state)) != 0;
  const bool immobile_taunt = localplayer->in_cond(TF_COND_TAUNTING)
    && !localplayer->allow_move_during_taunt();
  if ((localplayer->get_flags() & FL_FROZEN) != 0
    || localplayer->in_cond(TF_COND_FREEZE_INPUT)
    || controls_stunned
    || immobile_taunt)
  {
    return true;
  }

  bool waiting_for_players = false;
  int round_state = -1;
  if (entity_list != nullptr)
  {
    auto* proxy = entity_list->get_game_rules_proxy();
    if (proxy != nullptr)
    {
      static const int waiting_offset = tf2_netvars::find_offset("DT_TFGameRulesProxy", { "m_bInWaitingForPlayers" });
      static const int state_offset = tf2_netvars::find_offset("DT_TFGameRulesProxy", { "m_iRoundState" });
      const auto proxy_address = reinterpret_cast<std::uintptr_t>(proxy);
      waiting_for_players = waiting_offset > 0
        && *reinterpret_cast<bool*>(proxy_address + waiting_offset);
      round_state = state_offset > 0
        ? *reinterpret_cast<int*>(proxy_address + state_offset)
        : -1;
    }
  }

  if (waiting_for_players
    || round_state == gr_state_preround
    || round_state == gr_state_between_rounds)
  {
    return true;
  }

  return config.misc.automation.navbot_dont_path_during_warmup
    && automation::controller().is_setup_time();
}

void navbot_controller::on_create_move(user_cmd* user_cmd)
{
  suppress_aimbot_for_reload_ = false;
  silent_path_look_ = false;
  silent_path_look_angles_ = {};
  if (!config.misc.automation.navbot_enabled && !followbot::controller().wants_nav())
  {
    return;
  }

  ensure_started();

  if (config.misc.automation.navbot_debug_text
    && global_vars != nullptr
    && global_vars->curtime >= g_navbot_create_move_log_until)
  {
    print("[navbot] CreateMove active weapon_mode=%d\n",
      static_cast<int>(config.misc.automation.navbot_weapon_selection));
    g_navbot_create_move_log_until = global_vars->curtime + 5.0f;
  }

  if (engine == nullptr || !engine->is_in_game())
  {
    clear_runtime_state();
    round_started_ = false;
    setup_finished_ = false;
    warmup_active_ = false;
    debug_state_.runtime_state = "waiting for game";
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive() || localplayer->get_team() == tf_team::UNKNOWN)
  {
    clear_runtime_state();
    debug_state_.runtime_state = "waiting for local player";
    return;
  }

  rebuild_mesh_if_needed();
  update_mvm_wave_state(localplayer);
  if (!round_started_ && !warmup_active_)
  {
    round_started_ = true;
    if (map_has_cp_or_pl_prefix(loaded_map_name_))
    {
      setup_finished_ = true;
    }
  }
  if (should_block_pathing(localplayer))
  {
    invalidate_active_path(true);
    clear_runtime_state();
    update_weapon_choice(localplayer, user_cmd);
    debug_state_.path_request_message = "round blocked";
    debug_state_.runtime_state = "round blocked";
    return;
  }

  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  hazards_.update_expired(current_time);
  poll_path_results();

  if (active_goal_needs_reset(localplayer))
  {
    invalidate_active_path(true);
    next_goal_refresh_time_ = 0.0f;
    next_path_request_time_ = 0.0f;
  }

  refresh_goal(localplayer, current_time);
  update_runtime_debug();
  request_path_if_needed();
  update_weapon_choice(localplayer, user_cmd);
  debug_state_.runtime_state = follower_.has_path()
    ? "following"
    : pending_job_.id != 0 && pending_job_.generation_id == current_generation_id_
      ? "path pending"
      : active_goal_.valid ? "acquiring path" : "acquiring goal";
  if (active_goal_.valid && active_goal_.goal.type == goal_type::reload_weapons && reload_job_still_needed(localplayer))
  {
    suppress_aimbot_for_reload_ = true;
    apply_reload_controls(user_cmd);
  }

  auto follow_result = follower_.tick(localplayer, user_cmd, current_time);
  apply_mvm_combat_controls(localplayer, user_cmd);
  debug_state_.has_active_path = follower_.has_path();
  debug_state_.active_crumb_count = static_cast<uint32_t>(follower_.crumbs().size());
  update_draw_snapshot();

  if (config.misc.movement.moonwalk
    && config.misc.movement.moonwalk_navbot_compat
    && !localplayer->is_scoped()
    && (user_cmd->buttons & IN_JUMP) == 0)
  {

    user_cmd->buttons |= IN_DUCK;
  }

  navbot_update_throwable_look_suppress(localplayer->get_weapon(), user_cmd, current_time);
  if (!config.misc.automation.navbot_look_at_path)
  {
    reset_path_spin_state();
  }

  if (follow_result.failed)
  {
    const auto blacklisted_crumb = record_crumb_failure(follow_result, current_time);
    const auto transition_failed = (follow_result.failure_reason == follower_failure_reason::blocked
      || follow_result.failure_reason == follower_failure_reason::no_progress)
      && nav_edge_valid(follow_result.failed_edge);
    if (transition_failed && config.misc.automation.navbot_hazards)
    {
      hazards_.add_transition_failure(
        follow_result.failed_edge,
        current_time,
        transition_failure_retry_seconds);
    }

    if (follow_result.failure_reason == follower_failure_reason::hazard_intersection
      && !blacklisted_crumb)
    {
      debug_state_.last_failure = follower_failure_reason::none;
      debug_state_.runtime_state = "following";
      debug_state_.has_active_path = follower_.has_path();
      return;
    }

    debug_state_.last_failure = follow_result.failure_reason;
    debug_state_.runtime_state = "recovering";
    debug_state_.has_active_path = false;
    if (blacklisted_crumb && active_goal_.valid)
    {
      jobs_.cancel_generation(current_generation_id_);
      pending_job_ = {};
      next_path_request_time_ = current_time;
      clear_active_path_state();
      return;
    }

    if ((follow_result.failure_reason == follower_failure_reason::blocked
      || follow_result.failure_reason == follower_failure_reason::no_progress
      || follow_result.failure_reason == follower_failure_reason::destination_invalid
      || follow_result.failure_reason == follower_failure_reason::invalid_local_area)
      && active_goal_.valid)
    {
      goals_.reject_goal(active_goal_.goal, current_time);
      active_goal_ = {};
      next_goal_refresh_time_ = 0.0f;
      next_goal_retry_time_ = current_time;
      next_path_request_time_ = current_time;
      jobs_.cancel_generation(current_generation_id_);
      ++current_generation_id_;
      pending_job_ = {};
      clear_active_path_state();
      return;
    }

    jobs_.cancel_generation(current_generation_id_);
    ++current_generation_id_;
    pending_job_ = {};
    next_goal_refresh_time_ = 0.0f;
    next_path_request_time_ = current_time + path_retry_interval;
    clear_active_path_state();
  }
}

bool navbot_controller::has_silent_path_look() const
{
  return silent_path_look_;
}

Vec3 navbot_controller::silent_path_look_angles() const
{
  return silent_path_look_angles_;
}

void navbot_controller::update_weapon_choice(Player* localplayer, user_cmd* user_cmd)
{
  if (config.misc.automation.navbot_weapon_selection == Misc::Automation::navbot_weapon_mode::OFF
    || localplayer == nullptr || engine == nullptr || user_cmd == nullptr)
  {
    return;
  }

  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  auto* enemy = choose_navbot_enemy(localplayer);
  auto desired_slot = choose_navbot_weapon_slot(localplayer, active_goal_);
  if (desired_slot == navbot_weapon_slot::none)
  {
    return;
  }

  const bool urgent_melee = desired_slot == navbot_weapon_slot::melee
    && enemy != nullptr
    && distance_to_enemy(localplayer, enemy) <= 160.0f;
  const auto weapon_mode = static_cast<int>(config.misc.automation.navbot_weapon_selection);
  const bool mode_changed = last_weapon_selection_mode_ != weapon_mode;
  if (mode_changed)
  {
    last_weapon_selection_mode_ = weapon_mode;
    pending_desired_weapon_slot_ = 0;
    pending_desired_since_ = current_time;
    next_weapon_switch_time_ = 0.0f;
  }

  if (current_time < next_weapon_switch_time_ && !urgent_melee && !mode_changed)
  {
    return;
  }

  auto* active_weapon = localplayer->get_weapon();
  if (active_weapon != nullptr)
  {
    (void)active_weapon;
  }

  auto current_slot = weapon_slot_for(active_weapon, localplayer->get_tf_class());
  auto desired_slot_value = static_cast<int>(desired_slot);
  if (current_slot == desired_slot && !mode_changed)
  {
    last_requested_weapon_slot_ = desired_slot_value;
    pending_desired_weapon_slot_ = desired_slot_value;
    pending_desired_since_ = current_time;
    return;
  }

  const auto command = weapon_slot_command(desired_slot);
  if (command == nullptr)
  {
    return;
  }

  if (config.misc.automation.navbot_debug_text
    && last_requested_weapon_slot_ != desired_slot_value)
  {
    print("[navbot] weapon selection requesting %s (mode=%d)\n",
      command,
      weapon_mode);
  }
  if (auto* desired_weapon = weapon_for_slot(localplayer, desired_slot);
      desired_weapon != nullptr)
  {
    const auto weapon_index = reinterpret_cast<Entity*>(desired_weapon)->get_index();
    if (weapon_index > 0)
    {
      user_cmd->weapon_select = weapon_index;
      user_cmd->weapon_subtype = 0;
    }
  }
  engine->client_cmd_unrestricted(command);
  last_requested_weapon_slot_ = desired_slot_value;
  next_weapon_switch_time_ = current_time + weapon_switch_interval;
}

void navbot_controller::update_mvm_wave_state(Player* localplayer)
{
  if (localplayer == nullptr
    || config.misc.automation.navbot_behavior != Misc::Automation::navbot_mode::COMPLETE_MVM_SNIPER
    || !loaded_map_name_.starts_with("mvm_")
    || mvm_wave_started_)
  {
    return;
  }

  const auto& objectives = entity_cache_entities(class_id::OBJECTIVE_RESOURCE);
  if (!objectives.empty())
  {
    auto* objective = reinterpret_cast<TeamObjectiveResource*>(objectives.front());
    if (objective->is_mvm_between_waves())
    {
      return;
    }
    if (objective->get_mvm_wave_count() > 0 && objective->get_mvm_max_wave_count() > 0)
    {
      mvm_wave_started_ = true;
      return;
    }
  }
  for (auto* entity : entity_cache_npcs())
  {
    if (entity != nullptr && !entity->is_dormant()
      && entity->is_network_class("CTFTankBoss")
      && reinterpret_cast<Building*>(entity)->get_health() > 0)
    {
      mvm_wave_started_ = true;
      return;
    }
  }

  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = entity != nullptr && entity->get_class_id() == class_id::PLAYER
      ? reinterpret_cast<Player*>(entity)
      : nullptr;
    if (player != nullptr && !player->is_dormant() && player->is_alive()
      && player->get_team() != localplayer->get_team())
    {
      mvm_wave_started_ = true;
      return;
    }
  }
}

void navbot_controller::apply_mvm_combat_controls(Player* localplayer, user_cmd* user_cmd)
{
  if (localplayer == nullptr || user_cmd == nullptr || !active_goal_.valid
    || !mvm_goal(active_goal_.goal.type) || !mvm_wave_started_
    || config.misc.automation.navbot_behavior != Misc::Automation::navbot_mode::COMPLETE_MVM_SNIPER)
  {
    return;
  }

  auto* target = entity_list != nullptr
    ? entity_list->entity_from_index(static_cast<unsigned int>(active_goal_.goal.entity_index))
    : nullptr;
  if (target == nullptr || target->is_dormant())
  {
    return;
  }

  Vec3 target_position = target->get_origin();
  auto* target_player = target->get_class_id() == class_id::PLAYER
    ? reinterpret_cast<Player*>(target)
    : nullptr;
  if (target_player != nullptr)
  {
    if (!target_player->is_alive() || target_player->get_team() == localplayer->get_team())
    {
      return;
    }
    if (!target_player->get_hitbox_center(aim_hitbox_head, &target_position))
    {
      target_position = target_player->get_origin() + target_player->get_view_offset();
    }
    aimbot::set_preference(target_player, 0.25f);
  }
  else if (!target->is_network_class("CTFTankBoss"))
  {
    return;
  }
  else
  {
    if (reinterpret_cast<Building*>(target)->get_health() <= 0)
    {
      return;
    }
    target_position.z += 80.0f;
  }

  if (!aimbot_trace_visible_to_position(localplayer, target, target_position))
  {
    return;
  }

  auto* weapon = localplayer->get_weapon();
  if (weapon == nullptr || !localplayer->can_shoot(target))
  {
    return;
  }

  user_cmd->view_angles = aimbot_clamp_angles(
    aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), target_position));

  if (weapon->is_sniper_rifle() && !localplayer->is_scoped())
  {
    if (weapon->can_secondary_attack())
    {
      user_cmd->buttons |= IN_ATTACK2;
    }
    return;
  }

  if (weapon->can_primary_attack())
  {
    user_cmd->buttons |= IN_ATTACK;
  }
}

void navbot_controller::on_frame_stage_notify()
{
  if (!config.misc.automation.navbot_enabled || !engine->is_in_game())
  {
    return;
  }

  rebuild_mesh_if_needed();
  if (global_vars == nullptr || global_vars->curtime >= next_hazard_update_time_)
  {
    update_hazards();
    next_hazard_update_time_ = (global_vars != nullptr ? global_vars->curtime : 0.0f) + hazard_refresh_interval;
  }
}

void navbot_controller::on_game_event(GameEvent* event)
{
  if (!config.misc.automation.navbot_enabled || event == nullptr)
  {
    return;
  }

  auto name = event->get_name();
  if (name == nullptr)
  {
    return;
  }

  if (std::strcmp(name, "teamplay_point_captured") == 0)
  {
    last_captured_point_index_ = event->get_int("cp", -1);

    // Capturing invalidates the destination we were standing on. Keeping the
    // old capture goal alive causes the follower to rebuild a path to the
    // already-owned point, where it can remain indefinitely.
    active_goal_ = {};
    next_goal_refresh_time_ = 0.0f;
    next_goal_retry_time_ = 0.0f;
    next_path_request_time_ = global_vars != nullptr ? global_vars->curtime : 0.0f;
    invalidate_active_path(true);
  }

  if (std::strcmp(name, "mvm_wave_spawn") == 0
    || std::strcmp(name, "mvm_begin_wave") == 0
    || std::strcmp(name, "mvm_wave_start") == 0)
  {
    mvm_wave_started_ = true;
  }
  else if (std::strcmp(name, "mvm_wave_complete") == 0
    || std::strcmp(name, "mvm_wave_failed") == 0
    || std::strcmp(name, "mvm_reset_stats") == 0)
  {
    mvm_wave_started_ = false;
  }

  if (std::strcmp(name, "item_pickup") == 0
    || std::strcmp(name, "teamplay_point_captured") == 0
    || std::strcmp(name, "teamplay_point_unlocked") == 0
    || std::strcmp(name, "teamplay_flag_event") == 0
    || std::strcmp(name, "teamplay_round_start") == 0
    || std::strcmp(name, "teamplay_setup_finished") == 0)
  {
    if (std::strcmp(name, "teamplay_round_start") == 0)
    {
      round_started_ = true;
      setup_finished_ = false;
      warmup_active_ = false;
      last_captured_point_index_ = -1;
      mvm_wave_started_ = false;
    }
    else if (std::strcmp(name, "teamplay_setup_finished") == 0)
    {
      round_started_ = true;
      setup_finished_ = true;
      warmup_active_ = false;
      last_captured_point_index_ = -1;
    }

    jobs_.cancel_generation(current_generation_id_);
    ++world_generation_id_;
    pending_job_ = {};
    clear_active_path_state();
  }

  if (std::strcmp(name, "teamplay_waiting_begins") == 0
    || std::strcmp(name, "teamplay_restart_round") == 0
    || std::strcmp(name, "teamplay_round_win") == 0)
  {
    round_started_ = false;
    setup_finished_ = false;
    warmup_active_ = true;
    mvm_wave_started_ = false;
    last_captured_point_index_ = -1;
    jobs_.cancel_generation(current_generation_id_);
    ++world_generation_id_;
    pending_job_ = {};
    clear_active_path_state();
  }
}

void navbot_controller::draw_imgui()
{
  if (!config.misc.automation.navbot_enabled)
  {
    return;
  }

  auto* draw_list = ImGui::GetWindowDrawList();
  if (draw_list == nullptr)
  {
    return;
  }

  draw_path_snapshot snapshot{};
  {
    std::scoped_lock lock(draw_snapshot_mutex_);
    snapshot = draw_snapshot_;
  }

  if (config.misc.automation.navbot_draw_path && snapshot.path.status == path_status::success)
  {
    draw_path_imgui(
      draw_list,
      snapshot.path,
      config.misc.automation.navbot_draw_path_boxes,
      config.misc.automation.navbot_path_color);
  }

  if (config.misc.automation.navbot_debug_text)
  {
    draw_debug_overlay_imgui(draw_list, debug_state_);
  }
}

const navbot_debug_state& navbot_controller::debug_state() const
{
  return debug_state_;
}

bool navbot_controller::should_suppress_aimbot() const
{
  return config.misc.automation.navbot_enabled && suppress_aimbot_for_reload_;
}

bool navbot_controller::should_prioritize_danger_movement() const
{
  if (!config.misc.automation.navbot_enabled)
  {
    return false;
  }

  return active_goal_.valid && active_goal_.goal.type == goal_type::escape_danger;
}

bool navbot_controller::should_prioritize_melee_movement() const
{
  return config.misc.automation.navbot_enabled
    && active_goal_.valid
    && (active_goal_.goal.type == goal_type::melee_chase
      || mvm_goal(active_goal_.goal.type)
      || active_goal_.goal.type == goal_type::mvm_upgrade_station)
    && follower_.has_path();
}

Player* navbot_controller::melee_target() const
{
  if (!config.misc.automation.navbot_enabled || !active_goal_.valid ||
      active_goal_.goal.type != goal_type::melee_chase || entity_list == nullptr) {
    return nullptr;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || localplayer->get_weapon() == nullptr ||
      !localplayer->get_weapon()->is_melee()) {
    return nullptr;
  }

  Entity* target_entity = entity_list->entity_from_index(
    static_cast<unsigned int>(active_goal_.goal.entity_index));
  Player* target = target_entity != nullptr && target_entity->get_class_id() == class_id::PLAYER
    ? reinterpret_cast<Player*>(target_entity)
    : nullptr;
  if (target == nullptr || target->is_dormant() || !target->is_alive() ||
      target->get_team() == localplayer->get_team()) {
    return nullptr;
  }

  return target;
}

void navbot_controller::ensure_started()
{
  if (jobs_started_)
  {
    return;
  }

  jobs_.start();
  jobs_started_ = true;
}

void navbot_controller::rebuild_mesh_if_needed()
{
  auto current_map_name = engine != nullptr ? sanitize_level_name(engine->get_level_name()) : std::string{};
  if (current_map_name == loaded_map_name_)
  {
    return;
  }

  loaded_map_name_ = current_map_name;
  debug_state_.runtime_state = "loading map";

  jobs_.cancel_generation(current_generation_id_);
  mesh_.rebuild_from_current_map();
  hazards_.clear();
  clear_runtime_state();
  ++world_generation_id_;
  ++current_generation_id_;
  next_goal_refresh_time_ = 0.0f;
  next_path_request_time_ = 0.0f;
  round_started_ = false;
  setup_finished_ = false;
  warmup_active_ = false;
  mvm_wave_started_ = false;
  last_captured_point_index_ = -1;
  debug_state_.mesh_ready = mesh_.is_ready();
  debug_state_.map_name = mesh_.map_name();
  debug_state_.nav_file_path = mesh_.nav_file_path();
}

void navbot_controller::poll_path_results()
{
  while (true)
  {
    auto result = jobs_.poll_path_result();
    if (!result.has_value())
    {
      break;
    }

    auto& path = result->result;
    const bool matches_pending_job = pending_job_.id != 0 && result->handle.id == pending_job_.id;
    if (!matches_pending_job
      || path.generation_id != current_generation_id_
      || path.world_generation != world_generation_id_
      || path.hazard_generation != hazards_.generation())
    {
      ++debug_state_.stale_result_count;
      if (matches_pending_job)
      {
        pending_job_ = {};
        next_path_request_time_ = global_vars != nullptr ? global_vars->curtime : 0.0f;
      }
      continue;
    }
    pending_job_ = {};
    if (!active_goal_.valid || path.status != path_status::success)
    {
      debug_state_.current_path_status = path.status;
      if (path.status == path_status::no_path
        || path.status == path_status::no_start_area
        || path.status == path_status::no_goal_area
        || path.status == path_status::failed)
      {
        ++debug_state_.rejected_job_count;
        const auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
        if (active_goal_.valid)
        {
          goals_.reject_goal(active_goal_.goal, current_time);
          invalidate_active_path(true);
          next_goal_refresh_time_ = 0.0f;
          next_goal_retry_time_ = current_time;
          next_path_request_time_ = current_time;
          debug_state_.path_request_message = "target unreachable; selecting another";
        }
        else
        {
          next_goal_refresh_time_ = 0.0f;
          next_path_request_time_ = current_time + path_retry_interval;
        }
      }
      else if (path.status == path_status::canceled || path.status == path_status::stale)
      {
        next_path_request_time_ = global_vars != nullptr ? global_vars->curtime : 0.0f;
      }
      continue;
    }

    install_active_path(path);
    debug_state_.current_path_status = path.status;
    debug_state_.last_solve_time_ms = path.solve_time_ms;
    debug_state_.has_active_path = true;
    debug_state_.active_crumb_count = static_cast<uint32_t>(path.crumbs.size());
  }
}

void navbot_controller::request_path_if_needed()
{
  debug_state_.path_request_message = "checking";
  if (!active_goal_.valid || !mesh_.is_ready())
  {
    debug_state_.path_request_message = !active_goal_.valid ? "no goal" : "mesh missing";
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    debug_state_.path_request_message = "no localplayer";
    return;
  }

  if (should_block_pathing(localplayer))
  {
    debug_state_.path_request_message = "round blocked";
    return;
  }

  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;

  const bool has_pending_job = pending_job_.id != 0 && pending_job_.generation_id == current_generation_id_;
  if (has_pending_job && current_time - pending_job_submitted_at_ >= path_job_timeout)
  {
    jobs_.cancel_generation(current_generation_id_);
    pending_job_ = {};
    pending_job_submitted_at_ = 0.0f;
    if (active_goal_.valid)
    {
      goals_.reject_goal(active_goal_.goal, current_time);
      invalidate_active_path(true);
      next_goal_refresh_time_ = 0.0f;
      next_goal_retry_time_ = current_time;
      next_path_request_time_ = current_time;
      debug_state_.path_request_message = "target timed out; selecting another";
    }
    else
    {
      debug_state_.path_request_message = "pending timed out";
    }
  }

  if ((pending_job_.id != 0 && pending_job_.generation_id == current_generation_id_)
    || follower_.generation_id() == current_generation_id_)
  {
    debug_state_.path_request_message = pending_job_.id != 0 && pending_job_.generation_id == current_generation_id_ ? "pending" : "active";
    return;
  }
  if (current_time < next_path_request_time_)
  {
    debug_state_.path_request_message = "waiting retry";
    return;
  }

  auto start_area = mesh_.find_closest_area(localplayer->get_origin());
  auto goal_area = active_goal_.goal.destination_area;
  if (!start_area.valid() || !goal_area.valid())
  {
    debug_state_.path_request_message = !start_area.valid() ? "no start area" : "no goal area";
    return;
  }

  path_request request{};
  request.request_id = next_request_id_++;
  request.generation_id = current_generation_id_;
  request.world_generation = world_generation_id_;
  request.goal = active_goal_.goal.type;
  request.start_area = start_area;
  request.goal_area = goal_area;
  request.start_world = localplayer->get_origin();
  request.goal_world = active_goal_.goal.destination;
  request.team = static_cast<uint32_t>(localplayer->get_team());
  request.class_id = static_cast<uint32_t>(localplayer->get_tf_class());
  request.hazard_generation = hazards_.generation();
  request.captured_point_index = current_captured_point_index();
  request.destination_reach_distance = destination_reach_distance_for_goal(active_goal_.goal.type);
  request.setup_finished = setup_finished_;
  request.require_exact_goal_area = active_goal_.goal.type == goal_type::push_payload;

  pending_job_ = jobs_.submit_path_request(request, mesh_, hazards_, current_time);
  pending_job_submitted_at_ = current_time;
  debug_state_.path_request_message = "submitted";
  debug_state_.pending_generation_id = pending_job_.generation_id;
  next_path_request_time_ = current_time;
}

namespace
{

int hazard_priority(hazard_kind kind)
{
  switch (kind)
  {
    case hazard_kind::sentry: return 100;
    case hazard_kind::sticky: return 80;
    case hazard_kind::enemy_pressure: return 30;
    case hazard_kind::static_blocked:
    case hazard_kind::transition_failure:
    case hazard_kind::crumb_blacklist:
    default:
      return 0;
  }
}

}

void navbot_controller::update_hazards()
{
  if (!config.misc.automation.navbot_hazards)
  {
    if (!hazards_.records().empty())
    {
      hazards_.clear();
    }
    return;
  }

  if constexpr (textmode_build)
  {
    hazards_.clear_soft_costs();
    return;
  }

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (localplayer == nullptr || !mesh_.is_ready())
  {
    return;
  }

  auto local_team = localplayer->get_team();
  auto local_class = localplayer->get_tf_class();
  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;

  hazards_.clear_soft_costs();

  std::unordered_map<uint32_t, hazard_record> per_area_hazards;
  per_area_hazards.reserve(128);

  auto apply_hazard = [&](nav_area_id area_id, hazard_kind kind, float cost, float expire_time)
  {
    if (!area_id.valid() || cost <= 0.0f)
    {
      return;
    }

    auto incoming_priority = hazard_priority(kind);
    auto& slot = per_area_hazards[area_id.value];
    auto existing_priority = slot.area_id.valid() ? hazard_priority(slot.kind) : -1;

    if (incoming_priority > existing_priority)
    {
      slot.kind = kind;
      slot.policy = hazard_policy::soft_cost;
      slot.area_id = area_id;
      slot.cost = std::max(slot.cost, cost);
      slot.expire_time = std::max(slot.expire_time, expire_time);
    }
    else if (incoming_priority == existing_priority)
    {
      slot.cost = std::max(slot.cost, cost);
      slot.expire_time = std::max(slot.expire_time, expire_time);
    }
  };

  constexpr float hazard_expire_seconds = 0.4f;
  const auto hazard_expire = current_time + hazard_expire_seconds;

  auto enemy_radius_for = [](tf_class cls)
  {
    switch (cls)
    {
      case tf_class::SNIPER:    return 1100.0f;
      case tf_class::HEAVYWEAPONS:
      case tf_class::ENGINEER:
      case tf_class::SCOUT:     return 320.0f;
      case tf_class::PYRO:      return 280.0f;
      case tf_class::SPY:       return 240.0f;
      default:                  return 500.0f;
    }
  };

  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player == localplayer || player->get_team() == local_team)
    {
      continue;
    }
    if (player->is_dormant() || !player->is_alive())
    {
      continue;
    }

    auto enemy_origin = player->get_origin();
    auto enemy_area_id = mesh_.find_closest_area(enemy_origin);
    if (enemy_area_id.valid() && mesh_.area_has_flag(enemy_area_id, nav_area_flag_spawn_room))
    {
      continue;
    }

    auto invuln = player->is_invulnerable();
    auto base_cost = invuln ? 1200.0f : 280.0f;
    auto enemy_class = player->get_tf_class();
    if (enemy_class == tf_class::SNIPER)
    {
      base_cost *= 1.8f;
    }

    auto radius = enemy_radius_for(enemy_class);
    auto areas = mesh_.areas_in_radius(enemy_origin, radius);
    auto radius_sq = radius * radius;
    auto kind = invuln ? hazard_kind::sentry : hazard_kind::enemy_pressure;

    for (const auto& nearby : areas)
    {
      auto falloff = 1.0f - std::clamp(nearby.distance_sq / radius_sq, 0.0f, 1.0f);
      auto cost = base_cost * (0.4f + 0.6f * falloff);
      apply_hazard(nearby.id, kind, cost, hazard_expire);
    }
  }

  constexpr float sentry_inner = 800.0f;
  constexpr float sentry_mid = 1050.0f;
  constexpr float sentry_outer = 1200.0f;

  for (auto* entity : entity_cache[class_id::SENTRY])
  {
    if (entity == nullptr || entity->is_dormant() || entity->get_team() == local_team)
    {
      continue;
    }

    auto sentry_origin = entity->get_origin();
    auto areas = mesh_.areas_in_radius(sentry_origin, sentry_outer);
    for (const auto& nearby : areas)
    {
      auto distance = std::sqrt(nearby.distance_sq);
      auto cost = 0.0f;
      if (distance <= sentry_inner)
      {
        auto falloff = 1.0f - std::clamp(distance / sentry_inner, 0.0f, 1.0f);
        cost = 800.0f + 400.0f * falloff;
      }
      else if (distance <= sentry_mid)
      {
        cost = 500.0f;
      }
      else
      {
        cost = 250.0f;
        if (local_class == tf_class::HEAVYWEAPONS || local_class == tf_class::SOLDIER)
        {
          cost *= 0.4f;
        }
      }

      if (const auto* area_data = mesh_.find_area(nearby.id))
      {
        auto vertical_delta = std::fabs(sentry_origin.z - area_data->center.z);
        if (vertical_delta > 200.0f)
        {
          cost *= 0.6f;
        }
      }

      apply_hazard(nearby.id, hazard_kind::sentry, cost, hazard_expire);
    }
  }

  constexpr float sticky_radius = 150.0f;
  for (auto* entity : entity_cache[class_id::PILL_OR_STICKY])
  {
    if (entity == nullptr || entity->is_dormant() || entity->get_team() == local_team)
    {
      continue;
    }

    auto sticky_origin = entity->get_origin();
    auto areas = mesh_.areas_in_radius(sticky_origin, sticky_radius);
    auto radius_sq = sticky_radius * sticky_radius;
    for (const auto& nearby : areas)
    {
      auto falloff = 1.0f - std::clamp(nearby.distance_sq / radius_sq, 0.0f, 1.0f);
      auto cost = 900.0f * (0.5f + 0.5f * falloff);
      apply_hazard(nearby.id, hazard_kind::sticky, cost, current_time + 1.5f);
    }
  }

  for (auto& [_, record] : per_area_hazards)
  {
    hazards_.add_area_hazard(record);
  }
}

navbot_controller& controller()
{
  if (global_controller == nullptr)
  {
    static navbot_controller instance{};
    global_controller = &instance;
  }

  return *global_controller;
}

}
