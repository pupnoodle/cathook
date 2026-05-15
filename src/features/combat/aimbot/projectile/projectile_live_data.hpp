/*
/^-----^\   data: 2026-05-16
V  o o  V  file: src/features/combat/aimbot/projectile/projectile_live_data.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef PROJECTILE_LIVE_DATA_HPP
#define PROJECTILE_LIVE_DATA_HPP

#include <algorithm>
#include <cmath>

#include "core/shared/sigs.hpp"
#include "libsigscan/libsigscan.h"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"

inline float projectile_attr_float(Weapon* weapon, float base_value, const char* attribute_name) {
  if (weapon == nullptr || attribute_manager == nullptr || attribute_name == nullptr) {
    return base_value;
  }

  const float value = attribute_manager->attrib_hook_value(base_value, attribute_name, weapon->to_entity());
  return std::isfinite(value) ? value : base_value;
}

inline float projectile_weapon_data_speed_or(Weapon* weapon, float fallback_speed) {
  if (std::isfinite(fallback_speed) && fallback_speed > 1.0f) {
    return fallback_speed;
  }
  if (weapon == nullptr) {
    return fallback_speed;
  }

  const float data_speed = weapon->get_projectile_speed_from_data();
  return data_speed > 1.0f ? data_speed : fallback_speed;
}

inline float projectile_speed_attr(Weapon* weapon, float base_speed) {
  return projectile_attr_float(weapon, base_speed, "mult_projectile_speed");
}

inline float projectile_range_speed_attr(Weapon* weapon, float base_speed) {
  return projectile_attr_float(weapon, projectile_speed_attr(weapon, base_speed), "mult_projectile_range");
}

inline float projectile_sticky_arm_time_live(Weapon* weapon, float fallback_arm_time = 0.8f) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  using sticky_arm_time_fn = float (*)(Weapon*);
  static sticky_arm_time_fn sticky_arm_time = nullptr;
  static bool sticky_arm_time_initialized = false;
  if (!sticky_arm_time_initialized) {
    sticky_arm_time_initialized = true;
    sticky_arm_time = reinterpret_cast<sticky_arm_time_fn>(sigscan_module("client.so", sigs::tf_projectile_sticky_arm_time));
    if (sticky_arm_time == nullptr) {
      sticky_arm_time = reinterpret_cast<sticky_arm_time_fn>(sigscan_module("server.so", sigs::tf_projectile_sticky_arm_time));
    }
  }

  if (sticky_arm_time != nullptr) {
    const float arm_time = sticky_arm_time(weapon);
    if (std::isfinite(arm_time) && arm_time >= 0.0f && arm_time <= 5.0f) {
      return arm_time;
    }
  }

  return projectile_attr_float(weapon, fallback_arm_time, "sticky_arm_time");
}

#endif
