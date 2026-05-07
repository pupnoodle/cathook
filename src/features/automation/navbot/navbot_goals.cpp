/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_goals.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/navbot/navbot_goals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "core/entity_cache.hpp"
#include "core/math/math.hpp"

#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/menu/config.hpp"

#include "games/tf2/sdk/entities/capture_flag.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/team_objective_resource.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace navbot
{

namespace
{

bool pickup_recently_taken(Entity* pickup, float current_time)
{
  if (pickup == nullptr)
  {
    return true;
  }

  auto pickup_origin = pickup->get_origin();
  for (const auto& pickup_item : pickup_item_cache)
  {
    if (pickup_item.time < current_time)
    {
      continue;
    }

    if (distance_squared_2d(pickup_origin, pickup_item.location) <= 24.0f * 24.0f)
    {
      return true;
    }
  }

  return false;
}

goal_candidate make_candidate(goal_type type, float score, const Vec3& destination, nav_area_id destination_area)
{
  goal_candidate candidate{};
  candidate.type = type;
  candidate.score = score;
  candidate.destination = destination;
  candidate.destination_area = destination_area;
  return candidate;
}

goal_candidate make_entity_candidate(goal_type type, float score, const Vec3& destination, nav_area_id destination_area, int entity_index)
{
  auto candidate = make_candidate(type, score, destination, destination_area);
  candidate.entity_index = entity_index;
  return candidate;
}

void choose_best(goal_candidate& best, const goal_candidate& candidate)
{
  if (!candidate.destination_area.valid())
  {
    return;
  }

  if (candidate.score > best.score)
  {
    best = candidate;
  }
}

float clamp01(float value)
{
  return std::clamp(value, 0.0f, 1.0f);
}

float normalize_yaw(float yaw)
{
  while (yaw > 180.0f)
  {
    yaw -= 360.0f;
  }
  while (yaw < -180.0f)
  {
    yaw += 360.0f;
  }

  return yaw;
}

bool goal_enabled(goal_type type)
{
  if (!goal_type_can_be_excluded(type))
  {
    return true;
  }

  return (config.misc.automation.navbot_excluded_jobs_mask & goal_type_bit(type)) == 0;
}

bool area_is_roam_candidate(const navbot_mesh& mesh, nav_area_id area_id)
{
  auto area = mesh.find_area(area_id);
  if (area == nullptr)
  {
    return false;
  }

  if ((area->flags & (nav_area_flag_blocked | nav_area_flag_setup_gate)) != 0)
  {
    return false;
  }

  return true;
}

float roam_area_score(const nav_area_data& area, const Vec3& local_origin, bool prefer_spawn_exit)
{
  auto score = 0.0f;
  auto distance_sq = distance_squared_2d(local_origin, area.center);

  if (distance_sq < 250.0f * 250.0f)
  {
    score -= 2000.0f;
  }

  score += std::min(distance_sq, 4000.0f * 4000.0f) * 0.00002f;

  if ((area.flags & nav_area_flag_control_point) != 0)
  {
    score += 60.0f;
  }
  if ((area.flags & nav_area_flag_sniper_spot) != 0)
  {
    score += 45.0f;
  }
  if ((area.flags & nav_area_flag_spawn_exit) != 0)
  {
    score += prefer_spawn_exit ? 200.0f : 20.0f;
  }
  if ((area.flags & nav_area_flag_spawn_room) != 0)
  {
    score -= 150.0f;
  }

  return score;
}

goal_candidate make_roam_candidate(const navbot_mesh& mesh, Player* localplayer, nav_area_id area_id, bool prefer_spawn_exit)
{
  auto area = mesh.find_area(area_id);
  if (area == nullptr)
  {
    return {};
  }

  return make_candidate(goal_type::roam, roam_area_score(*area, localplayer->get_origin(), prefer_spawn_exit), area->center, area_id);
}

goal_candidate choose_pickup_goal(const navbot_mesh& mesh, Player* localplayer, class_id cache_id, goal_type type, float base_score)
{
  goal_candidate best{};
  best.score = -1.0f;

  auto local_origin = localplayer->get_origin();
  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  for (auto* entity : entity_cache[cache_id])
  {
    if (entity == nullptr || entity->is_dormant() || pickup_recently_taken(entity, current_time))
    {
      continue;
    }

    auto area_id = mesh.find_closest_area(entity->get_origin());
    if (!area_id.valid())
    {
      continue;
    }

    auto score = base_score - distance_squared_2d(local_origin, entity->get_origin()) * 0.00001f;
    choose_best(best, make_candidate(type, score, entity->get_origin(), area_id));
  }

  return best;
}

goal_candidate choose_pickup_area_goal(const navbot_mesh& mesh, Player* localplayer, const std::vector<nav_area_id>& areas, goal_type type, float base_score)
{
  goal_candidate best{};
  best.score = -1.0f;

  auto local_origin = localplayer->get_origin();
  for (auto area_id : areas)
  {
    auto area = mesh.find_area(area_id);
    if (area == nullptr)
    {
      continue;
    }

    auto score = base_score - distance_squared_2d(local_origin, area->center) * 0.00001f;
    choose_best(best, make_candidate(type, score, area->center, area_id));
  }

  return best;
}

bool weapon_needs_reload(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  auto* weapon = localplayer->get_weapon();
  return weapon != nullptr && weapon->get_clip1() == 0;
}

goal_candidate choose_reload_weapons_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;

  if (localplayer == nullptr)
  {
    return best;
  }

  auto enemy_origins = std::vector<Vec3>{};
  enemy_origins.reserve(entity_cache[class_id::PLAYER].size());
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

    enemy_origins.emplace_back(player->get_origin());
  }

  auto local_origin = localplayer->get_origin();
  auto nearest_enemy_distance = [&enemy_origins](const Vec3& origin)
  {
    if (enemy_origins.empty())
    {
      return 1200.0f;
    }

    auto best_distance_sq = std::numeric_limits<float>::max();
    for (const auto& enemy_origin : enemy_origins)
    {
      best_distance_sq = std::min(best_distance_sq, distance_squared_2d(origin, enemy_origin));
    }

    return std::sqrt(best_distance_sq);
  };

  constexpr float max_reload_route_distance = 1800.0f;
  for (const auto& area : mesh.cache().areas)
  {
    if ((area.flags & (nav_area_flag_blocked | nav_area_flag_setup_gate | nav_area_flag_spawn_room)) != 0)
    {
      continue;
    }

    auto local_distance = std::sqrt(distance_squared_2d(local_origin, area.center));
    if (local_distance > max_reload_route_distance)
    {
      continue;
    }

    auto enemy_distance = nearest_enemy_distance(area.center);
    auto score = 115.0f;
    score -= local_distance * 0.030f;
    score += std::min(enemy_distance, 1400.0f) * 0.060f;

    if (enemy_distance < 350.0f)
    {
      score -= 90.0f;
    }
    if ((area.flags & nav_area_flag_health) != 0)
    {
      score += 10.0f;
    }
    if ((area.flags & nav_area_flag_ammo) != 0)
    {
      score += 8.0f;
    }
    if ((area.flags & nav_area_flag_spawn_exit) != 0)
    {
      score += 16.0f;
    }
    if ((area.flags & (nav_area_flag_control_point | nav_area_flag_sentry_spot)) != 0)
    {
      score -= 12.0f;
    }

    choose_best(best, make_candidate(goal_type::reload_weapons, score, area.center, area.id));
  }

  if (best.destination_area.valid())
  {
    return best;
  }

  auto local_area_id = mesh.find_closest_area(local_origin);
  auto local_area = mesh.find_area(local_area_id);
  if (local_area == nullptr)
  {
    return best;
  }

  return make_candidate(goal_type::reload_weapons, 1.0f, local_area->center, local_area->id);
}

goal_candidate choose_control_point_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;

  for (auto* entity : entity_cache[class_id::OBJECTIVE_RESOURCE])
  {
    auto* objective = reinterpret_cast<TeamObjectiveResource*>(entity);
    if (objective == nullptr)
    {
      continue;
    }

    auto point_count = objective->get_num_control_points();
    for (int point_index = 0; point_index < point_count; ++point_index)
    {
      if (objective->is_locked(point_index))
      {
        continue;
      }
      if (objective->get_owning_team(point_index) == static_cast<int>(localplayer->get_team()))
      {
        continue;
      }
      if (!objective->can_team_capture(point_index, localplayer->get_team()))
      {
        continue;
      }

      auto origin = objective->get_origin(point_index);
      auto area_id = mesh.find_closest_area(origin);
      if (!area_id.valid())
      {
        continue;
      }

      auto score = 40.0f - distance_squared_2d(localplayer->get_origin(), origin) * 0.00001f;
      choose_best(best, make_candidate(goal_type::capture_objective, score, origin, area_id));
    }
  }

  if (best.score < 0.0f)
  {
    best = choose_pickup_area_goal(mesh, localplayer, mesh.cache().control_point_areas, goal_type::capture_objective, 25.0f);
  }

  return best;
}

bool is_payload_model(const char* model_name)
{
  if (model_name == nullptr)
  {
    return false;
  }

  constexpr const char* known_payload_models[] = {
    "models/props_trainyard/bomb_cart.mdl",
    "models/custom/dirty_bomb_cart.mdl",
    "models/lilchewchew/lilchewchew_v3.mdl",
    "models/props_trainyard/bomb_redmond.mdl",
    "models/props_snowycoast/gasoline_bomb_cart.mdl",
    "models/props_xmas/rudy.mdl",
    "models/props_trainyard/bomb_blutarch.mdl",
    "models/props_trainyard/bomb_cart_red.mdl"
  };

  for (auto* known_model : known_payload_models)
  {
    if (std::strcmp(model_name, known_model) == 0)
    {
      return true;
    }
  }

  return false;
}

bool is_payload_cart(Entity* entity)
{
  if (entity == nullptr)
  {
    return false;
  }

  return entity->get_class_id() == class_id::OBJECT_CART_DISPENSER || is_payload_model(entity->get_model_name());
}

Vec3 payload_origin(Entity* entity)
{
  if (entity == nullptr)
  {
    return {};
  }

  if (entity->get_class_id() == class_id::OBJECT_CART_DISPENSER)
  {
    return entity->get_abs_origin();
  }

  return entity->get_origin();
}

float payload_distance_score(float base_score, const Vec3& local_origin, const Vec3& cart_origin)
{
  return base_score - std::sqrt(distance_squared_2d(local_origin, cart_origin)) * 0.006f;
}

Vec3 choose_payload_push_destination(const navbot_mesh& mesh, Player* localplayer, const Vec3& cart_origin, nav_area_id cart_area_id)
{
  auto destination = mesh.get_nearest_point(cart_area_id, cart_origin);
  if (localplayer == nullptr)
  {
    return destination;
  }

  auto local_origin = localplayer->get_origin();
  if (std::sqrt(distance_squared_2d(local_origin, cart_origin)) <= 150.0f)
  {
    destination.z = local_origin.z;
  }

  return destination;
}

bool payload_push_destination_in_range(const Vec3& destination, const Vec3& cart_origin)
{
  constexpr float max_push_destination_distance = 110.0f;
  return distance_squared_2d(destination, cart_origin) <= max_push_destination_distance * max_push_destination_distance;
}

goal_candidate choose_payload_defend_destination(const navbot_mesh& mesh, Player* localplayer, const Vec3& cart_origin, nav_area_id cart_area_id, float base_score)
{
  goal_candidate best{};
  best.score = -1.0f;

  if (localplayer == nullptr)
  {
    return best;
  }

  constexpr float preferred_defend_distance = 430.0f;
  constexpr float min_defend_distance = 225.0f;
  constexpr float max_defend_distance = 725.0f;

  auto local_origin = localplayer->get_origin();
  for (const auto& area : mesh.cache().areas)
  {
    if ((area.flags & (nav_area_flag_blocked | nav_area_flag_setup_gate | nav_area_flag_spawn_room)) != 0)
    {
      continue;
    }

    auto cart_distance = std::sqrt(distance_squared_2d(area.center, cart_origin));
    if (cart_distance < min_defend_distance || cart_distance > max_defend_distance)
    {
      continue;
    }

    auto distance_error = std::fabs(cart_distance - preferred_defend_distance);
    auto score = base_score;
    score -= std::sqrt(distance_squared_2d(local_origin, area.center)) * 0.004f;
    score -= distance_error * 0.035f;

    if ((area.flags & nav_area_flag_control_point) != 0)
    {
      score += 5.0f;
    }
    if ((area.flags & nav_area_flag_sentry_spot) != 0)
    {
      score += 4.0f;
    }
    if ((area.flags & nav_area_flag_sniper_spot) != 0)
    {
      score += 3.0f;
    }

    choose_best(best, make_candidate(goal_type::defend_payload, score, area.center, area.id));
  }

  if (best.destination_area.valid())
  {
    return best;
  }

  auto fallback_area = mesh.find_area(cart_area_id);
  if (fallback_area == nullptr)
  {
    return best;
  }

  return make_candidate(goal_type::defend_payload, base_score - 20.0f, fallback_area->center, cart_area_id);
}

bool goal_is_objective(goal_type type)
{
  return type == goal_type::capture_objective
    || type == goal_type::push_payload
    || type == goal_type::defend_payload;
}

bool enemy_close_to_payload_cart(Player* localplayer)
{
  if (localplayer == nullptr || entity_list == nullptr)
  {
    return false;
  }

  constexpr float payload_threat_distance = 325.0f;
  auto payload_threat_distance_sq = payload_threat_distance * payload_threat_distance;
  auto max_entities = entity_list->get_max_entities();

  for (int entity_index = 1; entity_index <= max_entities; ++entity_index)
  {
    auto* entity = entity_list->entity_from_index(entity_index);
    if (entity == nullptr || entity->is_dormant() || !is_payload_cart(entity))
    {
      continue;
    }

    auto cart_origin = payload_origin(entity);
    for (auto* player_entity : entity_cache[class_id::PLAYER])
    {
      auto* player = reinterpret_cast<Player*>(player_entity);
      if (player == nullptr || player == localplayer || player->is_dormant())
      {
        continue;
      }
      if (player->get_team() == localplayer->get_team() || player->get_health() <= 0)
      {
        continue;
      }

      if (distance_squared_2d(player->get_origin(), cart_origin) <= payload_threat_distance_sq)
      {
        return true;
      }
    }
  }

  return false;
}

goal_candidate choose_payload_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;

  if (entity_list == nullptr)
  {
    return best;
  }

  auto local_origin = localplayer->get_origin();
  auto max_entities = entity_list->get_max_entities();
  for (int entity_index = 1; entity_index <= max_entities; ++entity_index)
  {
    auto* entity = entity_list->entity_from_index(entity_index);
    if (entity == nullptr || entity->is_dormant())
    {
      continue;
    }

    if (!is_payload_cart(entity))
    {
      continue;
    }

    auto origin = payload_origin(entity);
    auto area_id = mesh.find_closest_area(origin);
    if (!area_id.valid())
    {
      continue;
    }

    auto goal = entity->get_team() == localplayer->get_team()
      ? goal_type::push_payload
      : goal_type::defend_payload;
    if (!goal_enabled(goal))
    {
      continue;
    }
    if (goal == goal_type::push_payload)
    {
      auto destination = choose_payload_push_destination(mesh, localplayer, origin, area_id);
      if (!payload_push_destination_in_range(destination, origin))
      {
        continue;
      }

      choose_best(best, make_candidate(goal, payload_distance_score(65.0f, local_origin, origin), destination, area_id));
      continue;
    }

    auto defend_goal = choose_payload_defend_destination(mesh, localplayer, origin, area_id, payload_distance_score(48.0f, local_origin, origin));
    choose_best(best, defend_goal);
  }

  return best;
}

struct enemy_range_profile
{
  float preferred_distance = 325.0f;
  float low_health_distance = 225.0f;
  float min_distance = 125.0f;
  float max_distance = 525.0f;
  float side_bias = 1.0f;
};

enemy_range_profile build_enemy_range_profile(tf_class class_type)
{
  switch (class_type)
  {
    case tf_class::SCOUT:
      return enemy_range_profile{185.0f, 115.0f, 80.0f, 280.0f, 0.55f};
    case tf_class::SNIPER:
      return enemy_range_profile{950.0f, 700.0f, 450.0f, 1500.0f, 1.25f};
    case tf_class::SOLDIER:
      return enemy_range_profile{430.0f, 260.0f, 140.0f, 650.0f, 1.05f};
    case tf_class::DEMOMAN:
      return enemy_range_profile{500.0f, 300.0f, 160.0f, 725.0f, 1.10f};
    case tf_class::MEDIC:
      return enemy_range_profile{360.0f, 230.0f, 140.0f, 525.0f, 1.00f};
    case tf_class::HEAVYWEAPONS:
      return enemy_range_profile{240.0f, 165.0f, 100.0f, 360.0f, 0.70f};
    case tf_class::PYRO:
      return enemy_range_profile{170.0f, 100.0f, 75.0f, 260.0f, 0.45f};
    case tf_class::SPY:
      return enemy_range_profile{150.0f, 95.0f, 60.0f, 220.0f, 0.35f};
    case tf_class::ENGINEER:
      return enemy_range_profile{315.0f, 210.0f, 115.0f, 440.0f, 0.85f};
    case tf_class::UNDEFINED:
    default:
      return enemy_range_profile{};
  }
}

float choose_enemy_distance(const enemy_range_profile& profile, Player* player)
{
  if (player == nullptr)
  {
    return profile.preferred_distance;
  }

  auto max_health = player->get_max_health();
  if (max_health <= 0)
  {
    return profile.preferred_distance;
  }

  auto health_ratio = clamp01(static_cast<float>(player->get_health()) / static_cast<float>(max_health));
  auto aggression = clamp01((0.55f - health_ratio) / 0.55f);
  return profile.preferred_distance + (profile.low_health_distance - profile.preferred_distance) * aggression;
}

float choose_enemy_orbit_phase(float current_time)
{
  auto phase = std::fmod(current_time * 110.0f, 360.0f);
  if (phase < 0.0f)
  {
    phase += 360.0f;
  }

  return phase;
}

float enemy_goal_score(const nav_area_data& area, const Vec3& local_origin, const Vec3& enemy_origin, float desired_distance, const enemy_range_profile& profile)
{
  auto local_distance = std::sqrt(distance_squared_2d(local_origin, area.center));
  auto enemy_distance = std::sqrt(distance_squared_2d(enemy_origin, area.center));
  auto distance_error = std::fabs(enemy_distance - desired_distance);
  auto score = 55.0f;

  score -= distance_squared_2d(local_origin, area.center) * 0.00001f;
  score -= distance_error * 0.075f;

  if (enemy_distance < profile.min_distance)
  {
    score -= (profile.min_distance - enemy_distance) * 0.30f;
  }
  if (enemy_distance > profile.max_distance)
  {
    score -= (enemy_distance - profile.max_distance) * 0.12f;
  }

  auto enemy_to_area_yaw = std::atan2(area.center.y - enemy_origin.y, area.center.x - enemy_origin.x) * radpi;
  auto enemy_to_local_yaw = std::atan2(local_origin.y - enemy_origin.y, local_origin.x - enemy_origin.x) * radpi;
  auto yaw_delta = std::fabs(normalize_yaw(enemy_to_area_yaw - enemy_to_local_yaw));
  auto side_amount = clamp01(1.0f - std::fabs(yaw_delta - 90.0f) / 90.0f);
  score += side_amount * 12.0f * profile.side_bias;

  if ((area.flags & nav_area_flag_sniper_spot) != 0)
  {
    score += profile.preferred_distance >= 700.0f ? 18.0f : 3.0f;
  }
  if ((area.flags & nav_area_flag_spawn_room) != 0)
  {
    score -= 50.0f;
  }
  if ((area.flags & nav_area_flag_blocked) != 0)
  {
    score -= 250.0f;
  }

  if (local_distance < 90.0f)
  {
    score -= 8.0f;
  }

  return score;
}

goal_candidate choose_enemy_goal(const navbot_mesh& mesh, Player* localplayer, float current_time)
{
  goal_candidate best{};
  best.score = -1.0f;
  auto local_origin = localplayer->get_origin();
  auto profile = build_enemy_range_profile(localplayer->get_tf_class());
  auto orbit_phase = choose_enemy_orbit_phase(current_time);
  constexpr float orbit_offsets[] = {0.0f, 60.0f, -60.0f, 120.0f, -120.0f, 180.0f};

  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player == localplayer || player->is_dormant())
    {
      continue;
    }
    if (player->get_team() == localplayer->get_team() || player->get_health() <= 0)
    {
      continue;
    }

    auto enemy_origin = player->get_origin();
    auto desired_distance = choose_enemy_distance(profile, player);
    auto fallback_area_id = mesh.find_closest_area(enemy_origin);
    auto fallback_area = mesh.find_area(fallback_area_id);
    if (fallback_area != nullptr)
    {
      auto fallback_score = enemy_goal_score(*fallback_area, local_origin, enemy_origin, desired_distance, profile) - 6.0f;
      choose_best(best, make_candidate(goal_type::hold_range_on_enemy, fallback_score, fallback_area->center, fallback_area_id));
    }

    for (auto orbit_offset : orbit_offsets)
    {
      auto angle = (orbit_phase + orbit_offset) * pideg;
      auto orbit_point = Vec3{
        enemy_origin.x + std::cos(angle) * desired_distance,
        enemy_origin.y + std::sin(angle) * desired_distance,
        enemy_origin.z
      };

      auto area_id = mesh.find_closest_area(orbit_point);
      if (!area_id.valid())
      {
        continue;
      }

      auto area = mesh.find_area(area_id);
      if (area == nullptr)
      {
        continue;
      }

      auto destination = mesh.get_nearest_point(area_id, orbit_point);
      auto score = enemy_goal_score(*area, local_origin, enemy_origin, desired_distance, profile);
      score -= distance_squared_2d(destination, orbit_point) * 0.00002f;
      choose_best(best, make_candidate(goal_type::hold_range_on_enemy, score, destination, area_id));
    }
  }

  return best;
}

goal_candidate choose_heal_follow_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (localplayer == nullptr || localplayer->get_tf_class() != tf_class::MEDIC)
  {
    return best;
  }

  auto* target = medic_automation::controller().heal_target();
  if (target == nullptr || target->is_dormant() || !target->is_alive() || target->get_team() != localplayer->get_team())
  {
    return best;
  }

  auto destination = target->get_origin();
  auto area_id = mesh.find_closest_area(destination);
  if (!area_id.valid())
  {
    return best;
  }

  auto score = 88.0f;
  if (target->get_max_health() > 0 && target->get_health() < target->get_max_health())
  {
    score += (1.0f - std::clamp(static_cast<float>(target->get_health()) / static_cast<float>(target->get_max_health()), 0.0f, 1.0f)) * 18.0f;
  }
  if (medic_automation::controller().wants_crossbow())
  {
    score += 4.0f;
  }

  return make_entity_candidate(goal_type::heal_follow, score, destination, area_id, target->get_index());
}

} // namespace

void navbot_goals::reset_flag_home_cache()
{
  red_flag_home_ = {};
  blu_flag_home_ = {};
}

void navbot_goals::update_flag_home_cache(tf_team team, const Vec3& origin)
{
  switch (team)
  {
    case tf_team::RED:
      red_flag_home_.valid = true;
      red_flag_home_.origin = origin;
      break;
    case tf_team::BLU:
      blu_flag_home_.valid = true;
      blu_flag_home_.origin = origin;
      break;
    default:
      break;
  }
}

navbot_goals::cached_flag_home navbot_goals::flag_home_for_team(tf_team team) const
{
  switch (team)
  {
    case tf_team::RED:
      return red_flag_home_;
    case tf_team::BLU:
      return blu_flag_home_;
    default:
      return {};
  }
}

goal_candidate navbot_goals::choose_flag_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  auto* localplayer_entity = localplayer->to_entity();
  CaptureFlag* own_flag = nullptr;
  CaptureFlag* enemy_flag = nullptr;

  for (auto* entity : entity_cache[class_id::CAPTURE_FLAG])
  {
    auto* flag = reinterpret_cast<CaptureFlag*>(entity);
    if (flag == nullptr)
    {
      continue;
    }

    if (flag->get_status() == flag_status::HOME)
    {
      update_flag_home_cache(flag->get_team(), flag->get_origin());
    }

    if (flag->get_team() == localplayer->get_team())
    {
      own_flag = flag;
    }
    else
    {
      enemy_flag = flag;
    }
  }

  if (enemy_flag != nullptr && enemy_flag->get_owner_entity() == localplayer_entity && goal_enabled(goal_type::return_flag))
  {
    auto own_home = flag_home_for_team(localplayer->get_team());
    auto own_base_origin = own_home.valid
      ? own_home.origin
      : (own_flag != nullptr ? own_flag->get_origin() : localplayer->get_origin());
    auto own_base_area = mesh.find_closest_area(own_base_origin);
    if (own_base_area.valid())
    {
      auto score = 95.0f - distance_squared_2d(localplayer->get_origin(), own_base_origin) * 0.00001f;
      choose_best(best, make_candidate(goal_type::return_flag, score, own_base_origin, own_base_area));
    }
  }

  if (own_flag != nullptr && own_flag->get_status() == flag_status::DROPPED && goal_enabled(goal_type::return_flag))
  {
    auto origin = own_flag->get_origin();
    auto area_id = mesh.find_closest_area(origin);
    if (area_id.valid())
    {
      auto score = 90.0f - distance_squared_2d(localplayer->get_origin(), origin) * 0.00001f;
      choose_best(best, make_candidate(goal_type::return_flag, score, origin, area_id));
    }
  }

  if (enemy_flag != nullptr && enemy_flag->get_owner_entity() != localplayer_entity && goal_enabled(goal_type::get_flag))
  {
    auto origin = enemy_flag->get_origin();
    auto area_id = mesh.find_closest_area(origin);
    if (area_id.valid())
    {
      auto score = 60.0f - distance_squared_2d(localplayer->get_origin(), origin) * 0.00001f;
      if (enemy_flag->get_status() == flag_status::DROPPED)
      {
        score += 10.0f;
      }
      choose_best(best, make_candidate(goal_type::get_flag, score, origin, area_id));
    }
  }

  return best;
}

goal_candidate navbot_goals::choose_roam_goal(const navbot_mesh& mesh, Player* localplayer, float current_time)
{
  goal_candidate best{};
  best.score = -1.0f;

  auto local_area_id = mesh.find_closest_area(localplayer->get_origin());
  auto local_area = mesh.find_area(local_area_id);
  if (local_area == nullptr)
  {
    return best;
  }

  auto prefer_spawn_exit = (local_area->flags & nav_area_flag_spawn_room) != 0;

  if (last_roam_area_.valid() && current_time < next_roam_refresh_time_)
  {
    auto persisted_area = mesh.find_area(last_roam_area_);
    if (persisted_area != nullptr
      && area_is_roam_candidate(mesh, last_roam_area_)
      && distance_squared_2d(localplayer->get_origin(), persisted_area->center) > 250.0f * 250.0f)
    {
      return make_roam_candidate(mesh, localplayer, last_roam_area_, prefer_spawn_exit);
    }
  }

  auto candidate_ids = std::vector<nav_area_id>{};
  candidate_ids.reserve(mesh.cache().areas.size());

  auto append_candidates = [&candidate_ids, &mesh, local_area_id](const std::vector<nav_area_id>& areas, bool skip_spawn_room)
  {
    for (auto area_id : areas)
    {
      if (!area_id.valid() || area_id.value == local_area_id.value || !area_is_roam_candidate(mesh, area_id))
      {
        continue;
      }

      auto area = mesh.find_area(area_id);
      if (area == nullptr)
      {
        continue;
      }
      if (skip_spawn_room && (area->flags & nav_area_flag_spawn_room) != 0)
      {
        continue;
      }

      auto duplicate = std::find_if(candidate_ids.begin(), candidate_ids.end(), [area_id](nav_area_id existing)
      {
        return existing.value == area_id.value;
      });
      if (duplicate == candidate_ids.end())
      {
        candidate_ids.emplace_back(area_id);
      }
    }
  };

  if (prefer_spawn_exit)
  {
    append_candidates(mesh.cache().spawn_exit_areas, false);
  }

  append_candidates(mesh.cache().control_point_areas, true);
  append_candidates(mesh.cache().sniper_spot_areas, true);

  if (candidate_ids.empty())
  {
    for (const auto& area : mesh.cache().areas)
    {
      if (area.id.value == local_area_id.value
        || !area_is_roam_candidate(mesh, area.id)
        || (area.flags & nav_area_flag_spawn_room) != 0)
      {
        continue;
      }

      candidate_ids.emplace_back(area.id);
    }
  }

  if (candidate_ids.empty())
  {
    best = make_candidate(goal_type::roam, 1.0f, local_area->center, local_area->id);
    last_roam_area_ = best.destination_area;
    next_roam_refresh_time_ = current_time + 2.0f;
    return best;
  }

  std::sort(candidate_ids.begin(), candidate_ids.end(), [&mesh, localplayer, prefer_spawn_exit](nav_area_id left, nav_area_id right)
  {
    auto left_area = mesh.find_area(left);
    auto right_area = mesh.find_area(right);
    if (left_area == nullptr || right_area == nullptr)
    {
      return left.value < right.value;
    }

    return roam_area_score(*left_area, localplayer->get_origin(), prefer_spawn_exit)
      > roam_area_score(*right_area, localplayer->get_origin(), prefer_spawn_exit);
  });

  auto candidate_limit = std::min<size_t>(candidate_ids.size(), 4);
  auto selected_index = roam_cursor_ % candidate_limit;
  ++roam_cursor_;

  best = make_roam_candidate(mesh, localplayer, candidate_ids[selected_index], prefer_spawn_exit);
  last_roam_area_ = best.destination_area;
  next_roam_refresh_time_ = current_time + 2.0f;
  return best;
}

navbot_goal_state navbot_goals::select_goal(const navbot_mesh& mesh, Player* localplayer, float current_time)
{
  navbot_goal_state state{};
  if (!mesh.is_ready() || localplayer == nullptr)
  {
    return state;
  }

  if (cached_map_name_ != mesh.map_name())
  {
    cached_map_name_ = mesh.map_name();
    reset_flag_home_cache();
  }

  goal_candidate best{};
  best.score = -1.0f;

  auto max_health = localplayer->get_max_health();
  auto health_ratio = max_health > 0 ? static_cast<float>(localplayer->get_health()) / static_cast<float>(max_health) : 1.0f;

  if (health_ratio < 0.70f)
  {
    if (goal_enabled(goal_type::get_health))
    {
      choose_best(best, choose_pickup_goal(mesh, localplayer, class_id::HEALTH_PACK, goal_type::get_health, 100.0f));
      choose_best(best, choose_pickup_area_goal(mesh, localplayer, mesh.cache().health_areas, goal_type::get_health, 60.0f));
    }
  }

  if (weapon_needs_reload(localplayer) && goal_enabled(goal_type::reload_weapons))
  {
    choose_best(best, choose_reload_weapons_goal(mesh, localplayer));
  }

  auto weapon = localplayer->get_weapon();
  if (weapon != nullptr && weapon->get_clip1() <= 0 && goal_enabled(goal_type::get_ammo))
  {
    choose_best(best, choose_pickup_goal(mesh, localplayer, class_id::AMMO, goal_type::get_ammo, 80.0f));
    choose_best(best, choose_pickup_area_goal(mesh, localplayer, mesh.cache().ammo_areas, goal_type::get_ammo, 50.0f));
  }

  if (goal_enabled(goal_type::get_flag) || goal_enabled(goal_type::return_flag))
  {
    choose_best(best, choose_flag_goal(mesh, localplayer));
  }

  auto best_before_objectives = best;
  if (goal_enabled(goal_type::push_payload) || goal_enabled(goal_type::defend_payload))
  {
    choose_best(best, choose_payload_goal(mesh, localplayer));
  }
  if (goal_enabled(goal_type::capture_objective))
  {
    choose_best(best, choose_control_point_goal(mesh, localplayer));
  }

  if (goal_enabled(goal_type::heal_follow))
  {
    choose_best(best, choose_heal_follow_goal(mesh, localplayer));
  }

  auto have_priority_objective = goal_is_objective(best.type) && best.score > best_before_objectives.score;
  auto allow_enemy_override = !have_priority_objective || enemy_close_to_payload_cart(localplayer);

  if (goal_enabled(goal_type::hold_range_on_enemy) && allow_enemy_override)
  {
    choose_best(best, choose_enemy_goal(mesh, localplayer, current_time));
  }

  if (best.score < 0.0f)
  {
    choose_best(best, choose_roam_goal(mesh, localplayer, current_time));
  }

  if (best.score >= 0.0f && best.destination_area.valid())
  {
    state.valid = true;
    state.score = best.score;
    state.goal = best;
  }

  return state;
}

} // namespace navbot
