/*
/^-----^\   data: 2026-05-15
V  o o  V  file: src/features/combat/aimbot/proj_aim/proj_aim_types.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PROJ_AIM_TYPES_HPP
#define PROJ_AIM_TYPES_HPP

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>
#include <utility>
#include <vector>

#include "features/combat/aimbot/aim_utils.hpp"
#include "features/combat/aimbot/projectile/projectile_types.hpp"
#include "features/combat/aimbot/projectile/splash_trace_cache.hpp"
#include "core/shared/sigs.hpp"
#include "libsigscan/libsigscan.h"

inline bool proj_aim_supports_splash(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
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
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheScorchShot:
    return true;
  default:
    return false;
  }
}

struct proj_aim_hitbox_sample {
  int hitbox = -1;
  int bone = 0;
  int priority = INT_MAX;
  Vec3 offset{};
};

enum class proj_aim_point_kind {
  direct,
  splash
};

struct proj_aim_weapon_profile {
  LocalPredictionProjectileParameters params{};
  float splash_radius = 0.0f;
  float hull_radius = 2.0f;
  float arm_time = 0.0f;
  bool supports_direct = false;
  bool supports_splash = false;
  bool arcing = false;
};

struct proj_aim_direct_point {
  int hitbox = -1;
  int bone = 0;
  int priority = INT_MAX;
  Vec3 offset{};
};

struct proj_aim_solution {
  bool valid = false;
  proj_aim_point_kind kind = proj_aim_point_kind::direct;
  int hitbox = -1;
  int bone = 0;
  int priority = INT_MAX;
  Vec3 aim_position{};
  Vec3 aim_angles{};
  Vec3 explosion_origin{};
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  float intercept_time = 0.0f;
  float splash_radius = 0.0f;
};

struct proj_aim_direct_history {
  Vec3 predicted_origin{};
  proj_aim_direct_point point{};
  LocalPredictionInterceptResult intercept{};
  float fov = FLT_MAX;
};

struct proj_aim_splash_history {
  Vec3 predicted_origin{};
  Vec3 splash_point{};
  Vec3 explosion_origin{};
  LocalPredictionInterceptResult intercept{};
  float time_error = FLT_MAX;
  float fov = FLT_MAX;
};

struct proj_aim_path_sample {
  float time = 0.0f;
  Vec3 position{};
};

struct proj_aim_splash_point {
  Vec3 point{};
};

struct projectile_timing_context {
  float interp_time = 0.0f;
  float outgoing_latency = 0.0f;
  float entity_staleness = 0.0f;
  float unclamped_lead_time = 0.0f;
  float clamped_lead_time = 0.0f;
  int lead_ticks = 0;
};

#endif
