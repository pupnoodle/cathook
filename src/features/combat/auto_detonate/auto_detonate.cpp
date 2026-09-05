/*
/^-----^\   data: 2026-08-22
V  o o  V  file: src/features/combat/auto_detonate/auto_detonate.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |     \     )
  || (___\====
*/
#include "features/combat/auto_detonate/auto_detonate.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include "core/entity_cache.hpp"
#include "core/math/math.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/base_handle.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/netvars.hpp"

namespace auto_detonate
{

namespace
{

constexpr float sticky_self_blast_radius = 130.0f;

bool world_visible(const Vec3& start, const Vec3& end)
{
  if (engine_trace == nullptr)
  {
    return true;
  }

  Vec3 ray_start = start;
  Vec3 ray_end = end;
  ray_t ray = engine_trace->init_ray(&ray_start, &ray_end);
  trace_filter filter{};
  engine_trace->init_world_and_props_trace_filter(&filter);
  trace_t trace{};
  engine_trace->trace_ray(&ray, MASK_SOLID, &filter, &trace);
  return !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
}

bool owned_by_local(Player* localplayer, Entity* projectile)
{
  if (localplayer == nullptr || projectile == nullptr)
  {
    return false;
  }

  Entity* owner = projectile->get_owner_entity();
  if (owner == reinterpret_cast<Entity*>(localplayer))
  {
    return true;
  }
  if (owner != nullptr && owner->get_owner_entity() == reinterpret_cast<Entity*>(localplayer))
  {
    return true;
  }

  static const int thrower_offset = tf2_netvars::find_offset("DT_BaseGrenade", { "m_hThrower" });
  if (thrower_offset > 0 && engine != nullptr)
  {
    const int handle = *reinterpret_cast<int*>(
      reinterpret_cast<std::uintptr_t>(projectile) + static_cast<std::uintptr_t>(thrower_offset));
    if ((handle & ent_entry_mask) == engine->get_localplayer_index())
    {
      return true;
    }
  }

  return false;
}

int pipe_type_of(Entity* projectile)
{
  static const int pipe_type_offset =
    tf2_netvars::find_offset("DT_TFGrenadePipebombProjectile", { "m_iPipeType" });
  if (pipe_type_offset <= 0)
  {
    return -1;
  }

  return *reinterpret_cast<int*>(
    reinterpret_cast<std::uintptr_t>(projectile) + static_cast<std::uintptr_t>(pipe_type_offset));
}

bool projectile_touched(Entity* projectile)
{
  static const int touched_offset =
    tf2_netvars::find_offset("DT_TFGrenadePipebombProjectile", { "m_bTouched" });
  if (touched_offset <= 0)
  {
    return true;
  }

  return *reinterpret_cast<bool*>(
    reinterpret_cast<std::uintptr_t>(projectile) + static_cast<std::uintptr_t>(touched_offset));
}

void aim_at(user_cmd* cmd, const Vec3& from, const Vec3& to)
{
  if (cmd == nullptr)
  {
    return;
  }

  const Vec3 delta = to - from;
  const float horizontal = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  cmd->view_angles = Vec3{
    -std::atan2(delta.z, horizontal) * radpi,
    std::atan2(delta.y, delta.x) * radpi,
    0.0f
  };
}

bool target_player_valid(Player* localplayer, Player* player, bool ignore_cloaked)
{
  if (player == nullptr ||
      !player->is_alive() ||
      player->is_invulnerable() ||
      (ignore_cloaked && player->is_cloaked()))
  {
    return false;
  }
  (void)localplayer;
  return true;
}

template <typename callback>
bool for_each_enemy_target(Player* localplayer, bool include_buildings, bool ignore_cloaked, callback&& visit)
{
  for (const auto& entry : entity_cache_players())
  {
    if (!entry.alive || entry.dormant || entry.friendly || entry.player == nullptr)
    {
      continue;
    }

    const tf_team target_team = entry.team;
    if (target_team == localplayer->get_team() ||
        target_team == tf_team::UNKNOWN ||
        target_team == tf_team::SPECTATOR)
    {
      continue;
    }

    if (!target_player_valid(localplayer, entry.player, ignore_cloaked))
    {
      continue;
    }

    if (visit(entry.player->get_origin()))
    {
      return true;
    }
  }

  if (!include_buildings)
  {
    return false;
  }

  constexpr std::array<enum class_id, 4> building_ids{
    class_id::SENTRY,
    class_id::DISPENSER,
    class_id::TELEPORTER,
    class_id::OBJECT_CART_DISPENSER
  };
  for (const enum class_id building_id : building_ids)
  {
    for (Entity* building : entity_cache_entities(building_id))
    {
      if (building == nullptr || building->is_dormant())
      {
        continue;
      }

      const tf_team building_team = building->get_team();
      if (building_team == localplayer->get_team() ||
          building_team == tf_team::UNKNOWN ||
          building_team == tf_team::SPECTATOR)
      {
        continue;
      }

      if (visit(building->get_origin()))
      {
        return true;
      }
    }
  }

  return false;
}

void run_sticky_detonation(Player* localplayer, Weapon* weapon, user_cmd* cmd)
{
  if (weapon == nullptr || weapon->get_weapon_id() != TF_WEAPON_PIPEBOMBLAUNCHER)
  {
    return;
  }

  const short weapon_def = weapon->get_def_id();
  if (weapon_def == Demoman_s_StickyJumper)
  {
    return;
  }

  if ((cmd->buttons & IN_ATTACK) != 0)
  {
    return;
  }

  const float radius = std::clamp(config.auto_detonate.sticky_radius, 40.0f, 400.0f);
  const bool scottish_resistance = weapon_def == Demoman_s_TheScottishResistance;
  const Vec3 eye_position = localplayer->get_shoot_pos();

  const auto is_own_armed_sticky = [&](Entity* entity)
  {
    if (entity == nullptr ||
        entity->is_dormant() ||
        entity->get_class_id() != class_id::PILL_OR_STICKY ||
        pipe_type_of(entity) != 1 ||
        !projectile_touched(entity) ||
        !owned_by_local(localplayer, entity))
    {
      return false;
    }
    return true;
  };

  bool found_target = false;
  Vec3 best_bomb_origin{};
  float best_distance = 0.0f;

  for (Entity* bomb : entity_cache_entities(class_id::PILL_OR_STICKY))
  {
    if (!is_own_armed_sticky(bomb))
    {
      continue;
    }

    const Vec3 bomb_origin = bomb->get_origin();

    if (config.auto_detonate.dont_blow_me_up &&
        distance_3d(bomb_origin, localplayer->get_origin()) < sticky_self_blast_radius)
    {
      if (world_visible(bomb_origin, localplayer->get_shoot_pos()))
      {
        return;
      }
    }

    const bool hit = for_each_enemy_target(
      localplayer,
      config.auto_detonate.buildings,
      config.auto_detonate.ignore_cloaked,
      [&](const Vec3& target_origin)
      {
        if (distance_3d(bomb_origin, target_origin) > radius)
        {
          return false;
        }
        return world_visible(bomb_origin, target_origin);
      });

    if (!hit)
    {
      continue;
    }

    const float distance_to_eye = distance_3d(bomb_origin, eye_position);
    if (!found_target || distance_to_eye < best_distance)
    {
      found_target = true;
      best_distance = distance_to_eye;
      best_bomb_origin = bomb_origin;
    }
  }

  if (!found_target)
  {
    return;
  }

  if (scottish_resistance)
  {
    aim_at(cmd, eye_position, best_bomb_origin);
  }

  cmd->buttons |= IN_ATTACK2;
}

void run_flare_detonation(Player* localplayer, Weapon* weapon, user_cmd* cmd)
{
  if (weapon == nullptr)
  {
    return;
  }

  const short weapon_def = weapon->get_def_id();
  if (weapon_def != Pyro_s_TheDetonator && weapon_def != Pyro_s_TheScorchShot)
  {
    return;
  }

  const float radius = std::clamp(config.auto_detonate.flare_radius, 40.0f, 400.0f);

  for (Entity* flare : entity_cache_entities(class_id::FLARE))
  {
    if (flare == nullptr ||
        flare->is_dormant() ||
        !owned_by_local(localplayer, flare))
    {
      continue;
    }

    const Vec3 flare_origin = flare->get_origin();

    const bool hit = for_each_enemy_target(
      localplayer,
      false,
      config.auto_detonate.ignore_cloaked,
      [&](const Vec3& target_origin)
      {
        if (distance_3d(flare_origin, target_origin) > radius)
        {
          return false;
        }
        return world_visible(flare_origin, target_origin);
      });

    if (hit)
    {
      cmd->buttons |= IN_ATTACK2;
      return;
    }
  }
}

}

void on_create_move(user_cmd* cmd)
{
  if (!config.auto_detonate.stickies && !config.auto_detonate.flares)
  {
    return;
  }

  if (engine == nullptr || entity_list == nullptr || cmd == nullptr)
  {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive())
  {
    return;
  }

  switch (localplayer->get_tf_class())
  {
    case tf_class::DEMOMAN:
      if (config.auto_detonate.stickies)
      {
        run_sticky_detonation(localplayer, localplayer->get_weapon(), cmd);
      }
      break;

    case tf_class::PYRO:
      if (config.auto_detonate.flares)
      {
        run_flare_detonation(localplayer, localplayer->get_weapon(), cmd);
      }
      break;

    default:
      break;
  }
}

}
