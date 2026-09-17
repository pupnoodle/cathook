/*
/^-----^\   data: 2026-05-07
V  o o  V  file: src/features/automation/medic_automation/medic_automation.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/medic_automation/medic_automation.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include "core/entity_cache.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/math/math.hpp"
#include "core/player_manager.hpp"
#include "features/combat/aimbot/aim_utils.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

namespace medic_automation
{

namespace
{

constexpr float beam_prefer_distance = 420.0f;
constexpr float danger_window = 1.35f;
constexpr float resist_cycle_interval = 0.20f;
constexpr float uber_input_interval = 0.35f;

enum class damage_type
{
  bullet = 0,
  blast = 1,
  fire = 2
};

bool any_medic_feature_enabled()
{
  return config.misc.automation.medic_autoheal
    || config.misc.automation.medic_autovacc
    || config.misc.automation.medic_autouber;
}

Weapon* find_medigun(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return nullptr;
  }

  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon != nullptr && weapon->is_medigun())
    {
      return weapon;
    }
  }

  return nullptr;
}

int player_account_id(Player* player)
{
  if (player == nullptr || engine == nullptr)
  {
    return 0;
  }

  player_info info{};
  if (!engine->get_player_info(player->get_index(), &info) || info.fakeplayer || info.friends_id == 0)
  {
    return 0;
  }

  return static_cast<int>(static_cast<std::uint32_t>(info.friends_id));
}

bool target_matches_mask(Player* player)
{
  const auto mask = config.misc.automation.medic_heal_targets_mask;
  const auto account_id = static_cast<std::uint32_t>(std::max(player_account_id(player), 0));
  const bool ipc_bot = cat_ipc::client::is_local_ipc_friend(account_id);
  const bool friendly = account_id != 0 && cathook::core::players::is_friendly(account_id);
  const bool ignored = account_id != 0 && cathook::core::players::is_ignored(account_id);

  return ((mask & Misc::Automation::medic_heal_target_friends) != 0 && (friendly || player->is_friend()))
    || ((mask & Misc::Automation::medic_heal_target_ignored) != 0 && (ignored || player->is_ignored()))
    || ((mask & Misc::Automation::medic_heal_target_ipc_bots) != 0 && ipc_bot);
}

bool valid_heal_target(Player* localplayer, Player* player)
{
  if (localplayer == nullptr || player == nullptr || player == localplayer)
  {
    return false;
  }
  if (player->get_class_id() != class_id::PLAYER || player->is_dormant() || !player->is_alive())
  {
    return false;
  }
  if (player->get_team() != localplayer->get_team())
  {
    return false;
  }

  return target_matches_mask(player);
}

bool valid_attached_patient(Player* localplayer, Entity* entity)
{
  if (localplayer == nullptr || entity == nullptr || entity->get_class_id() != class_id::PLAYER)
  {
    return false;
  }

  auto* player = reinterpret_cast<Player*>(entity);
  return player != localplayer
    && !player->is_dormant()
    && player->is_alive()
    && player->get_team() == localplayer->get_team()
    && target_matches_mask(player);
}

bool player_wounded(Player* player)
{
  return player != nullptr && player->get_max_health() > 0 && player->get_health() < player->get_max_health();
}

float health_ratio(Player* player)
{
  if (player == nullptr || player->get_max_health() <= 0)
  {
    return 1.0f;
  }

  return std::clamp(static_cast<float>(player->get_health()) / static_cast<float>(player->get_max_health()), 0.0f, 2.0f);
}

Vec3 target_aim_position(Player* player);
bool medigun_target_visible(Player* localplayer, Player* target);

Player* choose_heal_target(Player* localplayer)
{
  if (localplayer == nullptr || entity_list == nullptr)
  {
    return nullptr;
  }

  Player* best_wounded = nullptr;
  Player* best_other = nullptr;
  auto best_wounded_score = -std::numeric_limits<float>::max();
  auto best_other_score = -std::numeric_limits<float>::max();
  const auto local_origin = localplayer->get_origin();

  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (!valid_heal_target(localplayer, player) || !medigun_target_visible(localplayer, player))
    {
      continue;
    }

    float score = -std::sqrt(distance_squared_2d(local_origin, player->get_origin())) * 0.05f;
    if (player_wounded(player))
    {
      score += (1.0f - health_ratio(player)) * 300.0f;
      if (score > best_wounded_score)
      {
        best_wounded_score = score;
        best_wounded = player;
      }
    }
    else
    {
      score -= 30.0f;
      if (score > best_other_score)
      {
        best_other_score = score;
        best_other = player;
      }
    }
  }

  return best_wounded != nullptr ? best_wounded : best_other;
}

Vec3 target_aim_position(Player* player)
{
  if (player == nullptr)
  {
    return {};
  }

  return player->get_origin() + Vec3{0.0f, 0.0f, std::max(player->get_view_offset().z * 0.70f, 42.0f)};
}

bool world_visible_to_position(Player* localplayer, const Vec3& target_pos)
{
  if (localplayer == nullptr || engine_trace == nullptr || !aimbot_vec3_is_finite(target_pos))
  {
    return false;
  }

  Vec3 start_pos = localplayer->get_shoot_pos();
  Vec3 end_pos = target_pos;
  auto ray = engine_trace->init_ray(&start_pos, &end_pos);
  trace_filter filter{};
  engine_trace->init_world_trace_filter(&filter);

  trace_t trace{};
  engine_trace->trace_ray(&ray, MASK_VISIBLE, &filter, &trace);
  return !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
}

bool medigun_target_visible(Player* localplayer, Player* target)
{
  if (localplayer == nullptr || target == nullptr)
  {
    return false;
  }

  const auto origin = target->get_origin();
  const auto view_offset = target->get_view_offset();
  return world_visible_to_position(localplayer, target_aim_position(target))
    || world_visible_to_position(localplayer, origin + Vec3{0.0f, 0.0f, 36.0f})
    || world_visible_to_position(localplayer, origin + Vec3{0.0f, 0.0f, std::max(view_offset.z * 0.45f, 28.0f)});
}

bool visible_enemy_near(Player* localplayer, Player* patient, damage_type* fallback_type)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  const auto local_origin = localplayer->get_origin();
  const auto patient_origin = patient != nullptr ? patient->get_origin() : local_origin;
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* enemy = reinterpret_cast<Player*>(entity);
    if (enemy == nullptr || enemy == localplayer || enemy->is_dormant() || !enemy->is_alive())
    {
      continue;
    }
    if (enemy->get_team() == localplayer->get_team())
    {
      continue;
    }

    const auto enemy_origin = enemy->get_origin();
    if (distance_squared_2d(enemy_origin, local_origin) > 625.0f * 625.0f
      && distance_squared_2d(enemy_origin, patient_origin) > 625.0f * 625.0f)
    {
      continue;
    }

    if (!aimbot_trace_visible_to_position(localplayer, enemy, target_aim_position(enemy)))
    {
      continue;
    }

    if (fallback_type != nullptr)
    {
      switch (enemy->get_tf_class())
      {
        case tf_class::SOLDIER:
        case tf_class::DEMOMAN:
          *fallback_type = damage_type::blast;
          break;
        case tf_class::PYRO:
          *fallback_type = damage_type::fire;
          break;
        default:
          *fallback_type = damage_type::bullet;
          break;
      }
    }
    return true;
  }

  for (auto* sentry : entity_cache[class_id::SENTRY])
  {
    if (sentry == nullptr || sentry->is_dormant() || sentry->get_team() == localplayer->get_team())
    {
      continue;
    }
    if (distance_squared_2d(sentry->get_origin(), local_origin) <= 750.0f * 750.0f
      || distance_squared_2d(sentry->get_origin(), patient_origin) <= 750.0f * 750.0f)
    {
      if (fallback_type != nullptr)
      {
        *fallback_type = damage_type::bullet;
      }
      return true;
    }
  }

  return false;
}

bool current_heal_beam_target_matches(Weapon* medigun, Player* target)
{
  if (medigun == nullptr || target == nullptr)
  {
    return false;
  }

  return medigun->medigun_is_healing() && medigun->medigun_healing_target() == target->to_entity();
}

void apply_autoheal(user_cmd* user_cmd, Player* localplayer, Player* target, Weapon* medigun)
{
  if (!config.misc.automation.medic_autoheal || user_cmd == nullptr || localplayer == nullptr || target == nullptr || medigun == nullptr || !medigun->is_medigun())
  {
    return;
  }

  if (!current_heal_beam_target_matches(medigun, target) && !medigun_target_visible(localplayer, target))
  {
    return;
  }

  const Vec3 aim_pos = target_aim_position(target);
  user_cmd->view_angles = aimbot_clamp_angles(aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), aim_pos));
  if (!current_heal_beam_target_matches(medigun, target))
  {
    user_cmd->buttons |= IN_ATTACK;
  }
  push_view_angles(user_cmd->view_angles);
}

damage_type classify_weapon_damage(std::string_view weapon_name)
{
  if (weapon_name.find("flame") != std::string_view::npos
    || weapon_name.find("fire") != std::string_view::npos
    || weapon_name.find("flare") != std::string_view::npos
    || weapon_name.find("burn") != std::string_view::npos)
  {
    return damage_type::fire;
  }
  if (weapon_name.find("rocket") != std::string_view::npos
    || weapon_name.find("grenade") != std::string_view::npos
    || weapon_name.find("pipe") != std::string_view::npos
    || weapon_name.find("sticky") != std::string_view::npos
    || weapon_name.find("launcher") != std::string_view::npos)
  {
    return damage_type::blast;
  }

  return damage_type::bullet;
}

bool should_pop_uber(medic_controller& controller, Player* localplayer, Player* patient, damage_type* desired_type)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  int recent_damage_type = static_cast<int>(damage_type::bullet);
  if (controller.recent_danger_matches(localplayer, patient, &recent_damage_type))
  {
    if (desired_type != nullptr)
    {
      *desired_type = static_cast<damage_type>(recent_damage_type);
    }
    return true;
  }

  if (health_ratio(localplayer) <= 0.38f || (patient != nullptr && health_ratio(patient) <= 0.38f))
  {
    return true;
  }

  damage_type fallback_type = damage_type::bullet;
  const bool enemy_danger = visible_enemy_near(localplayer, patient, &fallback_type);
  if (enemy_danger && desired_type != nullptr)
  {
    *desired_type = fallback_type;
  }
  return enemy_danger;
}

void apply_autovacc(medic_controller& controller, user_cmd* user_cmd, Player* localplayer, Player* patient, Weapon* medigun)
{
  if (!config.misc.automation.medic_autovacc || user_cmd == nullptr || localplayer == nullptr || medigun == nullptr || !medigun->is_vaccinator())
  {
    return;
  }

  damage_type desired_type = damage_type::bullet;
  const bool danger = should_pop_uber(controller, localplayer, patient, &desired_type);
  const auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  const int desired_resist = static_cast<int>(desired_type);

  if (medigun->vaccinator_resist_type() != desired_resist && controller.can_cycle_resist(current_time))
  {
    user_cmd->buttons |= IN_RELOAD;
    controller.delay_resist_cycle(current_time + resist_cycle_interval);
  }

  if (!danger || medigun->medigun_charge_level() < 0.25f || !controller.can_press_uber(current_time))
  {
    return;
  }

  user_cmd->buttons |= IN_ATTACK2;
  controller.delay_uber(current_time + uber_input_interval);
}

void apply_autouber(medic_controller& controller, user_cmd* user_cmd, Player* localplayer, Player* patient, Weapon* medigun)
{
  if (!config.misc.automation.medic_autouber || user_cmd == nullptr || localplayer == nullptr || medigun == nullptr || !medigun->is_medigun() || medigun->is_vaccinator())
  {
    return;
  }

  if (medigun->medigun_charge_level() < 0.99f)
  {
    return;
  }

  damage_type unused_type = damage_type::bullet;
  const auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  if (!controller.can_press_uber(current_time) || !should_pop_uber(controller, localplayer, patient, &unused_type))
  {
    return;
  }

  user_cmd->buttons |= IN_ATTACK2;
  controller.delay_uber(current_time + uber_input_interval);
}

}

void medic_controller::clear_runtime_state()
{
  heal_target_index_ = 0;
  heal_target_position_ = {};
  suppress_aimbot_ = false;
}

void medic_controller::on_pre_navbot_create_move()
{
  clear_runtime_state();
  if (!any_medic_feature_enabled() || engine == nullptr || entity_list == nullptr || !engine->is_in_game())
  {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive() || localplayer->get_tf_class() != tf_class::MEDIC)
  {
    return;
  }

  auto* medigun = find_medigun(localplayer);
  auto* target = choose_heal_target(localplayer);
  if (target == nullptr && medigun != nullptr && valid_attached_patient(localplayer, medigun->medigun_healing_target()))
  {
    target = reinterpret_cast<Player*>(medigun->medigun_healing_target());
  }
  if (target == nullptr)
  {
    return;
  }

  heal_target_index_ = target->get_index();
  heal_target_position_ = target->get_origin();
}

void medic_controller::on_post_navbot_create_move(user_cmd* user_cmd)
{
  suppress_aimbot_ = false;
  if (user_cmd == nullptr || heal_target_index_ <= 0 || entity_list == nullptr)
  {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  auto* target = heal_target();
  if (localplayer == nullptr || target == nullptr || !valid_heal_target(localplayer, target))
  {
    clear_runtime_state();
    return;
  }

  auto* active_weapon = localplayer->get_weapon();
  if (active_weapon != nullptr && active_weapon->is_medigun())
  {
    auto* patient = target;
    if (active_weapon->medigun_healing_target() != nullptr)
    {
      auto* healing_entity = active_weapon->medigun_healing_target();
      if (healing_entity->get_class_id() == class_id::PLAYER)
      {
        patient = reinterpret_cast<Player*>(healing_entity);
      }
    }

    apply_autoheal(user_cmd, localplayer, target, active_weapon);
    apply_autovacc(*this, user_cmd, localplayer, patient, active_weapon);
    apply_autouber(*this, user_cmd, localplayer, patient, active_weapon);
    suppress_aimbot_ = true;
  }
}

void medic_controller::on_game_event(GameEvent* event)
{
  if (event == nullptr || global_vars == nullptr || engine == nullptr || entity_list == nullptr)
  {
    return;
  }

  const char* name = event->get_name();
  if (name == nullptr || std::strcmp(name, "player_hurt") != 0)
  {
    return;
  }

  auto* victim = entity_list->get_player_from_id(event->get_int("userid"));
  if (victim == nullptr)
  {
    return;
  }

  recent_damage_target_index_ = victim->get_index();
  recent_damage_type_ = static_cast<int>(classify_weapon_damage(event->get_string("weapon")));
  recent_damage_time_ = global_vars->curtime;
}

Player* medic_controller::heal_target() const
{
  if (entity_list == nullptr || heal_target_index_ <= 0)
  {
    return nullptr;
  }

  return entity_list->player_from_index(heal_target_index_);
}

int medic_controller::heal_target_index() const
{
  return heal_target_index_;
}

Vec3 medic_controller::heal_target_position() const
{
  return heal_target_position_;
}

bool medic_controller::should_suppress_aimbot() const
{
  return suppress_aimbot_;
}

bool medic_controller::recent_danger_matches(Player* localplayer, Player* patient, int* damage_type_out) const
{
  if (localplayer == nullptr || global_vars == nullptr || global_vars->curtime - recent_damage_time_ > danger_window)
  {
    return false;
  }

  const int local_index = localplayer->get_index();
  const int patient_index = patient != nullptr ? patient->get_index() : 0;
  if (recent_damage_target_index_ != local_index && (patient_index == 0 || recent_damage_target_index_ != patient_index))
  {
    return false;
  }

  if (damage_type_out != nullptr)
  {
    *damage_type_out = recent_damage_type_;
  }
  return true;
}

bool medic_controller::can_cycle_resist(float current_time) const
{
  return current_time >= next_resist_cycle_time_;
}

bool medic_controller::can_press_uber(float current_time) const
{
  return current_time >= next_uber_time_;
}

void medic_controller::delay_resist_cycle(float next_time)
{
  next_resist_cycle_time_ = next_time;
}

void medic_controller::delay_uber(float next_time)
{
  next_uber_time_ = next_time;
}

medic_controller& controller()
{
  static medic_controller instance{};
  return instance;
}

}
