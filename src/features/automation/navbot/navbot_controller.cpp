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
#include <cstring>
#include <limits>

#include "imgui/dearimgui.hpp"

#include "core/entity_cache.hpp"

#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/menu/config.hpp"

#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
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
constexpr float path_retry_interval = 1.0f;
constexpr float weapon_switch_interval = 0.35f;

enum class navbot_weapon_slot
{
  none = 0,
  primary = 1,
  secondary = 2,
  melee = 3
};

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
    || type == goal_type::sentry_snipe;
}

bool goal_is_payload(goal_type type)
{
  return type == goal_type::push_payload || type == goal_type::defend_payload;
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

  return crumb_reach_distance;
}

bool reload_job_still_needed(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  auto* weapon = localplayer->get_weapon();
  return weapon != nullptr && weapon->get_clip1() == 0;
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
    auto* weapon = localplayer->get_weapon();
    return weapon != nullptr && weapon->get_clip1() <= 0;
  }

  return false;
}

bool map_has_cp_or_pl_prefix(const std::string& map_name)
{
  return map_name.starts_with("cp_") || map_name.starts_with("pl_") || map_name.starts_with("plr_");
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

  if (weapon->is_melee())
  {
    return navbot_weapon_slot::melee;
  }

  if (weapon->is_sniper_rifle() || weapon->is_minigun())
  {
    return navbot_weapon_slot::primary;
  }

  switch (weapon->get_def_id())
  {
    case Scout_m_Scattergun:
    case Scout_m_ScattergunR:
    case Scout_m_ForceANature:
    case Scout_m_TheShortstop:
    case Scout_m_TheSodaPopper:
    case Scout_m_FestiveScattergun:
    case Scout_m_BabyFacesBlaster:
    case Scout_m_TheBackScatter:
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
    case Pyro_m_FlameThrower:
    case Pyro_m_FlameThrowerR:
    case Pyro_m_TheBackburner:
    case Pyro_m_TheDegreaser:
    case Pyro_m_ThePhlogistinator:
    case Pyro_m_FestiveFlameThrower:
    case Pyro_m_TheRainblower:
    case Pyro_m_FestiveBackburner:
    case Pyro_m_DragonsFury:
    case Demoman_m_GrenadeLauncher:
    case Demoman_m_GrenadeLauncherR:
    case Demoman_m_TheLochnLoad:
    case Demoman_m_TheLooseCannon:
    case Demoman_m_FestiveGrenadeLauncher:
    case Demoman_m_TheIronBomber:
    case Medic_m_SyringeGun:
    case Medic_m_SyringeGunR:
    case Medic_m_TheBlutsauger:
    case Medic_m_CrusadersCrossbow:
    case Medic_m_TheOverdose:
    case Medic_m_FestiveCrusadersCrossbow:
    case Spy_m_Revolver:
    case Spy_m_RevolverR:
    case Spy_m_TheAmbassador:
    case Spy_m_BigKill:
    case Spy_m_LEtranger:
    case Spy_m_TheEnforcer:
    case Spy_m_TheDiamondback:
    case Spy_m_FestiveAmbassador:
    case Spy_m_FestiveRevolver:
      return navbot_weapon_slot::primary;

    case Scout_s_ScoutsPistol:
    case Scout_s_PistolR:
    case Scout_s_BonkAtomicPunch:
    case Scout_s_VintageLugermorph:
    case Scout_s_CritaCola:
    case Scout_s_MadMilk:
    case Scout_s_Lugermorph:
    case Scout_s_TheWinger:
    case Scout_s_PrettyBoysPocketPistol:
    case Scout_s_TheFlyingGuillotine:
    case Scout_s_TheFlyingGuillotineG:
    case Scout_s_MutatedMilk:
    case Scout_s_FestiveBonk:
    case Pyro_s_TheFlareGun:
    case Pyro_s_TheDetonator:
    case Pyro_s_TheReserveShooter:
    case Pyro_s_TheManmelter:
    case Pyro_s_TheScorchShot:
    case Pyro_s_FestiveFlareGun:
    case Pyro_s_ThermalThruster:
    case Pyro_s_GasPasser:
    case Heavy_s_Sandvich:
    case Heavy_s_TheBuffaloSteakSandvich:
    case Heavy_s_Fishcake:
    case Heavy_s_SecondBanana:
    case Medic_s_MediGun:
    case Medic_s_TheQuickFix:
    case Medic_s_TheVaccinator:
    case Sniper_s_SMG:
    case Sniper_s_SMGR:
    case Sniper_s_TheCleanersCarbine:
    case Sniper_s_DarwinsDangerShield:
    case Sniper_s_Jarate:
    case Sniper_s_TheSelfAwareBeautyMark:
    case Sniper_s_CozyCamper:
    case Spy_w_InvisWatch:
    case Spy_w_InvisWatchR:
    case Spy_w_TheDeadRinger:
    case Spy_w_TheCloakandDagger:
    case Spy_w_EnthusiastsTimepiece:
    case Spy_w_TheQuackenbirdt:
      return navbot_weapon_slot::secondary;
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

float distance_to_enemy(Player* localplayer, Player* enemy)
{
  if (localplayer == nullptr || enemy == nullptr)
  {
    return 8192.0f;
  }

  return std::sqrt(distance_squared_2d(localplayer->get_origin(), enemy->get_origin()));
}

Player* choose_navbot_enemy(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return nullptr;
  }

  if (target_player != nullptr
    && !target_player->is_dormant()
    && target_player->is_alive()
    && target_player->get_team() != localplayer->get_team())
  {
    return target_player;
  }

  Player* best_enemy = nullptr;
  auto best_distance_sq = std::numeric_limits<float>::max();
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

    auto distance_sq = distance_squared_2d(localplayer->get_origin(), player->get_origin());
    if (distance_sq < best_distance_sq)
    {
      best_distance_sq = distance_sq;
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

  auto enemy_distance = distance_to_enemy(localplayer, enemy);
  switch (localplayer->get_tf_class())
  {
    case tf_class::SNIPER:
      if (enemy_distance <= 150.0f)
      {
        return navbot_weapon_slot::melee;
      }
      if (enemy_distance <= 425.0f)
      {
        return navbot_weapon_slot::secondary;
      }
      return navbot_weapon_slot::primary;
    case tf_class::SOLDIER:
    case tf_class::DEMOMAN:
      if (enemy_distance <= 225.0f)
      {
        return navbot_weapon_slot::secondary;
      }
      return navbot_weapon_slot::primary;
    case tf_class::PYRO:
      if (enemy_distance <= 360.0f)
      {
        return navbot_weapon_slot::primary;
      }
      return navbot_weapon_slot::secondary;
    case tf_class::MEDIC:
      if (goal_is_combat(goal))
      {
        if (enemy_distance <= 120.0f)
        {
          return navbot_weapon_slot::melee;
        }
        return navbot_weapon_slot::primary;
      }
      return navbot_weapon_slot::secondary;
    case tf_class::SPY:
      if (enemy_distance <= 110.0f)
      {
        return navbot_weapon_slot::melee;
      }
      return navbot_weapon_slot::primary;
    case tf_class::ENGINEER:
      if (goal == goal_type::sentry_snipe)
      {
        return navbot_weapon_slot::primary;
      }
      if (enemy_distance <= 260.0f)
      {
        return navbot_weapon_slot::primary;
      }
      return navbot_weapon_slot::secondary;
    case tf_class::SCOUT:
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
    return medic_automation::controller().wants_crossbow()
      ? navbot_weapon_slot::primary
      : navbot_weapon_slot::secondary;
  }

  auto* enemy = choose_navbot_enemy(localplayer);
  auto desired_slot = goal_is_combat(goal)
    ? choose_combat_slot(localplayer, goal, enemy)
    : choose_default_slot(localplayer->get_tf_class());

  if (weapon_slot_available(localplayer, desired_slot))
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
    if (slot != desired_slot && weapon_slot_available(localplayer, slot))
    {
      return slot;
    }
  }

  return navbot_weapon_slot::none;
}

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
    case navbot_weapon_slot::none:
    default:
      return nullptr;
  }
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

void apply_look_at_target(Player* localplayer, user_cmd* user_cmd, const Vec3& target)
{
  if (localplayer == nullptr || user_cmd == nullptr || global_vars == nullptr)
  {
    return;
  }

  auto origin = localplayer->get_origin() + localplayer->get_view_offset();
  auto target_position = Vec3{target.x, target.y, origin.z};
  auto delta = Vec3{
    target_position.x - origin.x,
    target_position.y - origin.y,
    target_position.z - origin.z
  };

  auto planar_distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  if (planar_distance <= 0.001f)
  {
    return;
  }

  auto target_angles = Vec3{};
  target_angles.x = -std::atan2(delta.z, planar_distance) * 57.295779513082f;
  target_angles.y = std::atan2(delta.y, delta.x) * 57.295779513082f;

  auto normalize_angle = [](float angle)
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
  };

  auto approach_angle = [frame_time = std::max(global_vars->interval_per_tick, 0.001f), normalize_angle](float current, float target_value)
  {
    auto delta_angle = target_value - current;
    while (delta_angle > 180.0f)
    {
      delta_angle -= 360.0f;
    }
    while (delta_angle < -180.0f)
    {
      delta_angle += 360.0f;
    }

    auto step = std::max(config.misc.automation.navbot_look_at_path_speed, 1.0f) * frame_time;
    if (delta_angle > step)
    {
      delta_angle = step;
    }
    else if (delta_angle < -step)
    {
      delta_angle = -step;
    }

    return normalize_angle(current + delta_angle);
  };

  user_cmd->view_angles.x = std::clamp(approach_angle(user_cmd->view_angles.x, target_angles.x), -89.0f, 89.0f);
  user_cmd->view_angles.y = approach_angle(user_cmd->view_angles.y, target_angles.y);
  user_cmd->view_angles.z = 0.0f;

  if (prediction != nullptr)
  {
    auto predicted_angles = user_cmd->view_angles;
    prediction->set_local_view_angles(predicted_angles);
    prediction->set_view_angles(predicted_angles);
  }

  if (engine != nullptr)
  {
    auto engine_angles = user_cmd->view_angles;
    engine->set_view_angles(engine_angles);
  }
}

} // namespace

void navbot_controller::clear_runtime_state()
{
  follower_.clear();
  active_path_ = path_result{};
  active_goal_ = {};
  pending_job_ = {};
  next_weapon_switch_time_ = 0.0f;
  last_requested_weapon_slot_ = 0;
  crumb_failure_ = {};
  suppress_aimbot_for_reload_ = false;
  reset_debug_runtime(debug_state_);
}

bool navbot_controller::record_crumb_failure(const follower_tick_result& follow_result, float current_time)
{
  if (!follow_result.failed)
  {
    return false;
  }

  if (!follow_result.failed_crumb_area.valid() && !nav_edge_valid(follow_result.failed_edge))
  {
    crumb_failure_ = {};
    return false;
  }

  auto blacklist_seconds = std::clamp(config.misc.automation.navbot_crumb_blacklist_seconds, 50.0f, 150.0f);
  hazards_.refresh_crumb_blacklists(current_time, blacklist_seconds);

  auto same_failed_crumb = crumb_failure_.area_id.value == follow_result.failed_crumb_area.value
    && same_nav_edge(crumb_failure_.edge_id, follow_result.failed_edge);
  if (!same_failed_crumb || current_time - crumb_failure_.last_failure_time > blocked_fail_time)
  {
    crumb_failure_.area_id = follow_result.failed_crumb_area;
    crumb_failure_.edge_id = follow_result.failed_edge;
    crumb_failure_.count = 1;
    crumb_failure_.last_failure_time = current_time;
    return false;
  }

  ++crumb_failure_.count;
  crumb_failure_.last_failure_time = current_time;
  if (crumb_failure_.count < 2)
  {
    return false;
  }

  hazards_.add_crumb_blacklist(follow_result.failed_crumb_area, follow_result.failed_edge, current_time, blacklist_seconds);
  crumb_failure_ = {};
  return true;
}

bool navbot_controller::should_block_pathing(Player* localplayer) const
{
  if (localplayer == nullptr)
  {
    return false;
  }

  if (localplayer->in_cond(TF_COND_TAUNTING))
  {
    return true;
  }

  auto map_name = mesh_.map_name().empty() ? loaded_map_name_ : mesh_.map_name();
  auto on_cp_or_pl_map = map_has_cp_or_pl_prefix(map_name);
  auto warmup_active = warmup_active_;
  auto local_team = localplayer->get_team();
  auto setup_active = round_started_ && on_cp_or_pl_map && !setup_finished_;
  auto match_fully_started = round_started_ && (!on_cp_or_pl_map || setup_finished_);

  if (local_team == tf_team::BLU && setup_active)
  {
    return true;
  }

  if (config.misc.automation.navbot_dont_path_unless_match_started && !match_fully_started)
  {
    return true;
  }

  if (!config.misc.automation.navbot_dont_path_during_warmup || !warmup_active)
  {
    return false;
  }

  if (!config.misc.automation.navbot_warmup_only_blu_cp_pl)
  {
    return true;
  }

  return local_team == tf_team::BLU && on_cp_or_pl_map;
}

void navbot_controller::on_create_move(user_cmd* user_cmd)
{
  suppress_aimbot_for_reload_ = false;
  if (!config.misc.automation.navbot_enabled)
  {
    return;
  }

  ensure_started();

  if (engine == nullptr || !engine->is_in_game())
  {
    clear_runtime_state();
    round_started_ = false;
    setup_finished_ = false;
    warmup_active_ = false;
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive() || localplayer->get_team() == tf_team::UNKNOWN)
  {
    clear_runtime_state();
    return;
  }

  rebuild_mesh_if_needed();
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
    clear_runtime_state();
    return;
  }

  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  hazards_.update_expired(current_time);
  poll_path_results();

  if (active_goal_.valid && active_goal_.goal.type == goal_type::reload_weapons && !reload_job_still_needed(localplayer))
  {
    jobs_.cancel_generation(current_generation_id_);
    ++current_generation_id_;
    pending_job_ = {};
    next_goal_refresh_time_ = 0.0f;
    next_path_request_time_ = 0.0f;
    follower_.clear();
    active_path_ = path_result{};
    active_goal_ = {};
  }

  if (active_goal_.valid && active_goal_.goal.type == goal_type::heal_follow)
  {
    auto* heal_target = medic_automation::controller().heal_target();
    if (heal_target == nullptr
      || heal_target->is_dormant()
      || !heal_target->is_alive()
      || heal_target->get_team() != localplayer->get_team()
      || heal_target->get_index() != active_goal_.goal.entity_index)
    {
      jobs_.cancel_generation(current_generation_id_);
      ++current_generation_id_;
      pending_job_ = {};
      next_goal_refresh_time_ = 0.0f;
      next_path_request_time_ = 0.0f;
      follower_.clear();
      active_path_ = path_result{};
      active_goal_ = {};
    }
  }

  const auto has_active_path_or_pending_request = follower_.has_path() || pending_job_.generation_id == current_generation_id_;
  if (!active_goal_.valid || current_time >= next_goal_refresh_time_ || !has_active_path_or_pending_request)
  {
    next_goal_refresh_time_ = current_time + goal_refresh_interval;

    auto next_goal = goals_.select_goal(mesh_, localplayer, current_time);
    if (should_replace_goal(active_goal_, next_goal, has_active_path_or_pending_request, localplayer))
    {
      jobs_.cancel_generation(current_generation_id_);
      active_goal_ = next_goal;
      crumb_failure_ = {};
      ++current_generation_id_;
      pending_job_ = {};
      next_path_request_time_ = 0.0f;
    }
  }

  debug_state_.current_goal = active_goal_.goal.type;
  debug_state_.active_generation_id = current_generation_id_;
  debug_state_.active_world_generation = world_generation_id_;
  debug_state_.mesh_ready = mesh_.is_ready();
  debug_state_.goal_valid = active_goal_.valid;
  debug_state_.map_name = mesh_.map_name();
  debug_state_.nav_file_path = mesh_.nav_file_path();
  request_path_if_needed();
  if (active_goal_.valid && active_goal_.goal.type == goal_type::reload_weapons && reload_job_still_needed(localplayer))
  {
    suppress_aimbot_for_reload_ = true;
    apply_reload_controls(user_cmd);
  }
  else
  {
    update_weapon_choice(localplayer);
  }

  auto follow_result = follower_.tick(mesh_, localplayer, user_cmd, current_time);
  debug_state_.has_active_path = follower_.has_path();
  debug_state_.active_crumb_count = static_cast<uint32_t>(follower_.crumbs().size());
  if (config.misc.automation.navbot_look_at_path)
  {
    auto current_crumb = follower_.current_crumb();
    if (current_crumb != nullptr)
    {
      apply_look_at_target(localplayer, user_cmd, current_crumb->world);
    }
  }

  if (follow_result.failed)
  {
    const auto blacklisted_crumb = record_crumb_failure(follow_result, current_time);
    if (follow_result.failure_reason == follower_failure_reason::hazard_intersection
      && !blacklisted_crumb
      && !hazards_.has_active_world_hazard(current_time))
    {
      debug_state_.last_failure = follower_failure_reason::none;
      return;
    }

    debug_state_.last_failure = follow_result.failure_reason;
    debug_state_.has_active_path = false;
    if ((blacklisted_crumb || follow_result.failure_reason == follower_failure_reason::hazard_intersection) && active_goal_.valid)
    {
      jobs_.cancel_generation(current_generation_id_);
      pending_job_ = {};
      next_path_request_time_ = current_time;
      follower_.clear();
      active_path_ = path_result{};
      return;
    }

    jobs_.cancel_generation(current_generation_id_);
    ++current_generation_id_;
    pending_job_ = {};
    next_goal_refresh_time_ = 0.0f;
    next_path_request_time_ = current_time + path_retry_interval;
    follower_.clear();
    active_path_ = path_result{};
  }
}

void navbot_controller::update_weapon_choice(Player* localplayer)
{
  if (!config.misc.automation.navbot_auto_weapon || localplayer == nullptr || engine == nullptr)
  {
    return;
  }

  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  if (current_time < next_weapon_switch_time_)
  {
    return;
  }

  auto* active_weapon = localplayer->get_weapon();
  if (active_weapon != nullptr)
  {
    if ((active_weapon->is_sniper_rifle() && localplayer->is_scoped())
      || (active_weapon->is_minigun() && localplayer->is_heavy_revved()))
    {
      return;
    }
  }

  auto desired_slot = choose_navbot_weapon_slot(localplayer, active_goal_);
  if (desired_slot == navbot_weapon_slot::none)
  {
    return;
  }

  auto current_slot = weapon_slot_for(active_weapon, localplayer->get_tf_class());
  auto desired_slot_value = static_cast<int>(desired_slot);
  if (current_slot == desired_slot)
  {
    last_requested_weapon_slot_ = desired_slot_value;
    return;
  }

  auto* slot_command = weapon_slot_command(desired_slot);
  if (slot_command == nullptr)
  {
    return;
  }

  engine->client_cmd_unrestricted(slot_command);
  last_requested_weapon_slot_ = desired_slot_value;
  next_weapon_switch_time_ = current_time + weapon_switch_interval;
}

void navbot_controller::on_frame_stage_notify()
{
  if (!config.misc.automation.navbot_enabled || !engine->is_in_game())
  {
    return;
  }

  rebuild_mesh_if_needed();
  update_hazards();
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

  if (std::strcmp(name, "item_pickup") == 0
    || std::strcmp(name, "teamplay_point_captured") == 0
    || std::strcmp(name, "teamplay_flag_event") == 0
    || std::strcmp(name, "teamplay_round_start") == 0
    || std::strcmp(name, "teamplay_setup_finished") == 0)
  {
    if (std::strcmp(name, "teamplay_round_start") == 0)
    {
      round_started_ = true;
      setup_finished_ = false;
      warmup_active_ = false;
    }
    else if (std::strcmp(name, "teamplay_setup_finished") == 0)
    {
      round_started_ = true;
      setup_finished_ = true;
      warmup_active_ = false;
    }

    jobs_.cancel_generation(current_generation_id_);
    ++world_generation_id_;
    pending_job_ = {};
  }

  if (std::strcmp(name, "teamplay_waiting_begins") == 0
    || std::strcmp(name, "teamplay_restart_round") == 0
    || std::strcmp(name, "teamplay_round_win") == 0)
  {
    round_started_ = false;
    setup_finished_ = false;
    warmup_active_ = true;
    jobs_.cancel_generation(current_generation_id_);
    ++world_generation_id_;
    pending_job_ = {};
  }
}

void navbot_controller::draw_imgui()
{
  if (!config.misc.automation.navbot_enabled)
  {
    return;
  }

  auto* draw_list = ImGui::GetBackgroundDrawList();
  if (draw_list == nullptr)
  {
    return;
  }

  if (config.misc.automation.navbot_draw_path && active_path_.status == path_status::success)
  {
    auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
    draw_path_exact_imgui(draw_list, active_path_, follower_.current_crumb_index(), current_time, follower_.reached_crumb_times());
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

void navbot_controller::ensure_started()
{
  if (jobs_started_)
  {
    return;
  }

  jobs_.start(&mesh_, &hazards_);
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
    if (path.generation_id != current_generation_id_
      || path.world_generation != world_generation_id_
      || path.hazard_generation != hazards_.generation())
    {
      ++debug_state_.stale_result_count;
      continue;
    }
    pending_job_ = {};
    if (!active_goal_.valid || path.status != path_status::success)
    {
      debug_state_.current_path_status = path.status;
      if (path.status == path_status::no_path)
      {
        ++debug_state_.rejected_job_count;
        next_goal_refresh_time_ = 0.0f;
        next_path_request_time_ = (global_vars != nullptr ? global_vars->curtime : 0.0f) + path_retry_interval;
        if (active_goal_.valid && active_goal_.goal.type == goal_type::roam)
        {
          active_goal_ = {};
        }
      }
      continue;
    }

    active_path_ = path;
    follower_.set_path(path_result(path));
    debug_state_.current_path_status = path.status;
    debug_state_.last_solve_time_ms = path.solve_time_ms;
    debug_state_.has_active_path = true;
    debug_state_.active_crumb_count = static_cast<uint32_t>(path.crumbs.size());
  }
}

void navbot_controller::request_path_if_needed()
{
  if (!active_goal_.valid || !mesh_.is_ready())
  {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    return;
  }
  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;

  if (pending_job_.generation_id == current_generation_id_ || follower_.generation_id() == current_generation_id_)
  {
    return;
  }
  if (current_time < next_path_request_time_)
  {
    return;
  }

  auto start_area = mesh_.find_closest_area(localplayer->get_origin());
  auto goal_area = active_goal_.goal.destination_area;
  if (!start_area.valid() || !goal_area.valid())
  {
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
  request.destination_reach_distance = destination_reach_distance_for_goal(active_goal_.goal.type);
  request.require_exact_goal_area = active_goal_.goal.type == goal_type::push_payload;

  pending_job_ = jobs_.submit_path_request(request);
  next_path_request_time_ = current_time;
}

void navbot_controller::update_hazards()
{
  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player->is_dormant())
    {
      continue;
    }

    auto* localplayer = entity_list->get_localplayer();
    if (localplayer == nullptr || player == localplayer || player->get_team() == localplayer->get_team())
    {
      continue;
    }

    auto area_id = mesh_.find_closest_area(player->get_origin());
    if (!area_id.valid())
    {
      continue;
    }

    hazard_record record{};
    record.kind = hazard_kind::enemy_pressure;
    record.policy = hazard_policy::soft_cost;
    record.area_id = area_id;
    record.cost = 150.0f;
    record.expire_time = current_time + 0.25f;
    hazards_.add_area_hazard(record);
  }

  for (auto* entity : entity_cache[class_id::SENTRY])
  {
    if (entity == nullptr)
    {
      continue;
    }

    auto area_id = mesh_.find_closest_area(entity->get_origin());
    if (!area_id.valid())
    {
      continue;
    }

    hazard_record record{};
    record.kind = hazard_kind::sentry;
    record.policy = hazard_policy::soft_cost;
    record.area_id = area_id;
    record.cost = 400.0f;
    record.expire_time = current_time + 0.25f;
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

} // namespace navbot
