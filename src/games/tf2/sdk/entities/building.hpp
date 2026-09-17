/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/entities/building.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef BUILDING_HPP
#define BUILDING_HPP
#include "entity.hpp"

class Building : public Entity {
public:
  int get_object_mode(void) {
    static tf2_netvars::lazy_offset object_mode_offset{"DT_BaseObject", {"m_iObjectMode"}};
    return object_mode_offset > 0
      ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + object_mode_offset)
      : -1;
  }

  int get_health(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_iHealth"}};
    return offset > 0 ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int())) : 0;
  }

  int get_max_health(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_iMaxHealth"}};
    return offset > 0 ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int())) : 0;
  }

  bool is_sapped(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_bHasSapper"}};
    return offset > 0 && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int()));
  }

  bool is_carried(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_bCarried"}};
    return (offset > 0 && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int()))) || this->is_carried_deploy();
  }

  bool is_placing(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_bPlacing"}};
    return offset > 0 && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int()));
  }

  bool is_disabled(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_bDisabled"}};
    return offset > 0 && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int()));
  }

  bool is_carried_deploy(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_bCarryDeploy"}};
    return offset > 0 && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int()));
  }

  bool is_mini_sentry(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_bMiniBuilding"}};
    return offset > 0 && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int()));
  }

  int get_building_level(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseObject", {"m_iUpgradeLevel"}};
    return offset > 0 ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int())) : 1;
  }

  int get_sentry_ammo_shells(void) {
    static tf2_netvars::lazy_offset offset{"DT_ObjectSentrygun", {"m_iAmmoShells"}};
    return offset > 0 ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset) : 0;
  }

  int get_sentry_ammo_rockets(void) {
    static tf2_netvars::lazy_offset offset{"DT_ObjectSentrygun", {"m_iAmmoRockets"}};
    return offset > 0 ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset) : 0;
  }

  int get_sentry_max_ammo_shells(void) {
    return is_mini_sentry() || get_building_level() == 1 ? 150 : 200;
  }

  int get_sentry_max_ammo_rockets(void) {
    return is_mini_sentry() || get_building_level() < 3 ? 0 : 20;
  }
};
#endif
