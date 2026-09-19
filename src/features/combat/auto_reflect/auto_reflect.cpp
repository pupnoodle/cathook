/*
/^-----^\   data: 2026-08-22
V  o o  V  file: src/features/combat/auto_reflect/auto_reflect.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |     \     )
  || (___\====
*/
#include "features/combat/auto_reflect/auto_reflect.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include "core/entity_cache.hpp"
#include "core/math/math.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/netvars.hpp"

namespace auto_reflect
{

namespace
{

constexpr int max_tracked_entities = 4096;
constexpr float min_projectile_speed_per_second = 50.0f;
constexpr int prediction_ticks = 2;

struct projectile_motion
{
  int tick_count = 0;
  Vec3 origin{};
  int serial = -1;
  bool valid = false;
};

std::array<projectile_motion, max_tracked_entities> g_motions{};

int deflected_offset()
{
  static tf2_netvars::lazy_offset rocket_offset{"DT_TFProjectile_Rocket", { "m_iDeflected" }};
  static tf2_netvars::lazy_offset pipebomb_offset{"DT_TFProjectile_Pipebomb", { "m_iDeflected" }};
  const int offset = rocket_offset;
  return offset > 0 ? offset : static_cast<int>(pipebomb_offset);
}

bool is_deflected(Entity* projectile)
{
  const int offset = deflected_offset();
  if (offset <= 0)
  {
    return false;
  }

  return *reinterpret_cast<int*>(
    reinterpret_cast<std::uintptr_t>(projectile) + static_cast<std::uintptr_t>(offset)) > 0;
}

tf_team effective_team(Entity* projectile)
{
  Entity* owner = projectile->get_owner_entity();
  if (owner != nullptr)
  {
    return owner->get_team();
  }

  return projectile->get_team();
}

Vec3 estimated_velocity(Entity* projectile)
{
  const int index = projectile->get_index();
  if (index <= 0 || index >= max_tracked_entities)
  {
    return {};
  }

  projectile_motion& motion = g_motions[static_cast<std::size_t>(index)];
  const int serial = projectile->get_ref_ehandle().GetSerialNumber();
  if (motion.serial != serial) {
    motion = {};
    motion.serial = serial;
  }
  const Vec3 origin = projectile->get_origin();
  const int tick_count = global_vars != nullptr ? global_vars->tickcount : 0;

  Vec3 velocity{};
  if (motion.valid && tick_count > motion.tick_count)
  {
    const int elapsed_ticks = tick_count - motion.tick_count;
    const float elapsed_seconds = static_cast<float>(elapsed_ticks) * tick_interval();
    if (elapsed_seconds > 0.0f && std::isfinite(elapsed_seconds))
    {
      velocity = (origin - motion.origin) * (1.0f / elapsed_seconds);
    }
  }
  else
  {
    motion.valid = false;
  }

  motion.origin = origin;
  motion.tick_count = tick_count;
  motion.serial = serial;
  motion.valid = true;
  return velocity;
}

bool projectile_touched(Entity* projectile)
{
  static tf2_netvars::lazy_offset touched_offset{"DT_TFProjectile_Pipebomb", { "m_bTouched" }};
  if (touched_offset <= 0 || projectile == nullptr) {
    return false;
  }
  return *reinterpret_cast<bool*>(
    reinterpret_cast<std::uintptr_t>(projectile) + static_cast<std::uintptr_t>(touched_offset));
}

float airblast_radius(Weapon* weapon)
{
  const float scale = attribute_manager != nullptr
    ? attribute_manager->attrib_hook_value(1.0f, "deflection_size_multiplier", weapon->to_entity())
    : 1.0f;
  return std::clamp(scale, 0.25f, 4.0f) * 128.0f;
}

bool type_enabled(Entity* projectile, enum class_id projectile_class)
{
  switch (projectile_class)
  {
    case class_id::ROCKET:
      return config.auto_reflect.rockets;

    case class_id::SENTRY_ROCKET:
      return config.auto_reflect.sentry_rockets;

    case class_id::PILL_OR_STICKY:
    {
      static tf2_netvars::lazy_offset pipe_type_offset{"DT_TFProjectile_Pipebomb", { "m_iType" }};
      if (pipe_type_offset <= 0)
      {
        return config.auto_reflect.pipes;
      }

      const int pipe_type = *reinterpret_cast<int*>(
        reinterpret_cast<std::uintptr_t>(projectile) + static_cast<std::uintptr_t>(pipe_type_offset));
      return pipe_type == 1 ? config.auto_reflect.stickies : config.auto_reflect.pipes;
    }

    case class_id::FLARE:
      return config.auto_reflect.flares;

    case class_id::ARROW:
    case class_id::CROSSBOW_BOLT:
      return config.auto_reflect.arrows;

    default:
      return false;
  }
}

void reset_motions()
{
  for (auto& motion : g_motions)
  {
    motion.valid = false;
  }
}

}

void on_create_move(user_cmd* cmd)
{
  static bool was_in_game = false;
  const bool in_game = engine != nullptr && engine->is_in_game();
  if (!in_game && was_in_game)
  {
    reset_motions();
  }
  was_in_game = in_game;

  if (!config.auto_reflect.enabled || !in_game)
  {
    return;
  }

  if (entity_list == nullptr || global_vars == nullptr || cmd == nullptr)
  {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive() || localplayer->get_tf_class() != tf_class::PYRO)
  {
    return;
  }

  auto* weapon = localplayer->get_weapon();
  const int weapon_id = weapon != nullptr ? weapon->get_weapon_id() : 0;
  if (weapon == nullptr ||
      (weapon_id != TF_WEAPON_FLAMETHROWER && weapon_id != TF_WEAPON_FLAME_BALL) ||
      weapon->get_def_id() == Pyro_m_ThePhlogistinator)
  {
    return;
  }
  if (attribute_manager != nullptr &&
      attribute_manager->attrib_hook_value(0.0f, "airblast_disabled", weapon->to_entity()) != 0.0f)
  {
    return;
  }
  if (!weapon->can_secondary_attack())
  {
    return;
  }

  const Vec3 eye_position = localplayer->get_shoot_pos();
  const float blast_radius = airblast_radius(weapon);
  const float range = std::max(std::clamp(config.auto_reflect.range, 40.0f, 400.0f), blast_radius);
  const float lead_time = std::max(backtrack::real_latency_seconds() - 0.03f,
    static_cast<float>(prediction_ticks) * tick_interval());

  Vec3 view_forward{};
  angle_vectors(cmd->view_angles, &view_forward, nullptr, nullptr);

  auto try_projectile = [&](Entity* projectile, enum class_id projectile_class) -> bool
  {
    if (projectile == nullptr ||
        projectile->is_dormant() ||
        is_deflected(projectile) ||
        !type_enabled(projectile, projectile_class) ||
        (projectile_class == class_id::PILL_OR_STICKY && projectile_touched(projectile)))
    {
      return false;
    }

    const tf_team team = effective_team(projectile);
    if (team == localplayer->get_team() ||
        team == tf_team::UNKNOWN ||
        team == tf_team::SPECTATOR)
    {
      return false;
    }

    const Vec3 velocity = estimated_velocity(projectile);
    if (length(velocity) < min_projectile_speed_per_second)
    {
      return false;
    }

    const Vec3 origin = projectile->get_origin();
    const Vec3 predicted = origin + velocity * lead_time;
    if (distance_3d(predicted, eye_position) > range)
    {
      return false;
    }

    const float speed = length(velocity);
    const Vec3 direction = velocity * (1.0f / speed);
    const Vec3 to_eye = eye_position - origin;
    const float to_eye_length = length(to_eye);
    if (to_eye_length <= 0.001f ||
        dot(to_eye, direction) / to_eye_length <= 0.05f)
    {
      return false;
    }

    if (config.auto_reflect.fov_limit > 0.0f)
    {
      const Vec3 to_predicted = predicted - eye_position;
      const float predicted_distance = length(to_predicted);
      if (predicted_distance > 0.001f &&
          dot(view_forward, to_predicted * (1.0f / predicted_distance)) <
            std::cos(config.auto_reflect.fov_limit * pideg))
      {
        return false;
      }
    }

    cmd->view_angles = angles_to_position(eye_position, predicted);
    cmd->buttons |= IN_ATTACK2;
    return true;
  };

  constexpr std::array<enum class_id, 6> projectile_ids{
    class_id::ROCKET,
    class_id::SENTRY_ROCKET,
    class_id::PILL_OR_STICKY,
    class_id::FLARE,
    class_id::ARROW,
    class_id::CROSSBOW_BOLT
  };
  for (const enum class_id projectile_id : projectile_ids)
  {
    for (Entity* projectile : entity_cache_entities(projectile_id))
    {
      if (try_projectile(projectile, projectile_id))
      {
        return;
      }
    }
  }

  if (config.auto_reflect.burning_teammates)
  {
    for (const auto& entry : entity_cache_players())
    {
      if (!entry.alive ||
          entry.dormant ||
          entry.player == nullptr ||
          entry.player == localplayer ||
          entry.team != localplayer->get_team() ||
          !entry.player->in_cond(TF_COND_BURNING))
      {
        continue;
      }

      if (distance_3d(entry.player->get_origin(), eye_position) <= range)
      {
        cmd->buttons |= IN_ATTACK2;
        return;
      }
    }
  }
}

}
