/*
/^-----^\   data: 2026-05-15
V  o o  V  file: src/features/combat/aimbot/proj_aim/proj_aim_weapon.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PROJ_AIM_WEAPON_HPP
#define PROJ_AIM_WEAPON_HPP

#include "features/combat/aimbot/projectile/projectile_live_data.hpp"
#include "proj_aim_debug.hpp"

inline bool proj_aim_mode_allows_direct(Weapon* weapon) {
  if (!proj_aim_supports_splash(weapon)) {
    return true;
  }

  return config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_ONLY ||
    config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_THEN_SPLASH ||
    config.aimbot.projectile_mode == Aim::ProjectileMode::PREFER_SPLASH;
}

inline bool proj_aim_mode_allows_splash(Weapon* weapon) {
  if (!proj_aim_supports_splash(weapon)) {
    return false;
  }

  return config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_THEN_SPLASH ||
    config.aimbot.projectile_mode == Aim::ProjectileMode::PREFER_SPLASH ||
    config.aimbot.projectile_mode == Aim::ProjectileMode::SPLASH_ONLY;
}

inline float proj_aim_splash_radius_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  float radius = 0.0f;
  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
    radius = 170.0f;
    break;
  case Soldier_m_TheAirStrike:
    radius = 130.0f;
    break;
  case Soldier_m_TheDirectHit:
    radius = 70.0f;
    break;
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
    radius = 170.0f;
    break;
  case Pyro_s_TheDetonator:
  case Pyro_s_TheScorchShot:
    radius = weapon->get_def_id() == Pyro_s_TheScorchShot ? 150.0f : 110.0f;
    break;
  default:
    break;
  }

  if (radius > 0.0f && attribute_manager != nullptr) {
    radius = attribute_manager->attrib_hook_value(radius, "mult_explosion_radius", weapon->to_entity());
  }

  radius *= std::clamp(config.aimbot.projectile_splash_radius_scale, 0.25f, 2.0f);
  return radius;
}

inline float proj_aim_hull_radius_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return 2.0f;
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
  case Soldier_s_TheRighteousBison:
    return 4.0f;
  case Engi_m_ThePomson6000:
  case Pyro_m_DragonsFury:
    return 3.0f;
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
    return 6.0f;
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return 4.0f;
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return 5.0f;
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Engi_m_TheRescueRanger:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    return 2.0f;
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
    return 1.0f;
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheManmelter:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
    return 3.0f;
  default:
    return 2.0f;
  }
}

inline bool proj_aim_is_stickybomb_launcher(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return true;
  default:
    return false;
  }
}

inline float proj_aim_arm_time_for_weapon(Weapon* weapon) {
  if (!proj_aim_is_stickybomb_launcher(weapon)) {
    return 0.0f;
  }

  const float fallback_arm_time = weapon->get_def_id() == Demoman_s_TheQuickiebombLauncher ? 0.55f : 0.8f;
  const float live_arm_time = projectile_sticky_arm_time_live(weapon, fallback_arm_time);
  if (live_arm_time >= 0.0f && live_arm_time <= 5.0f) {
    return live_arm_time;
  }

  return fallback_arm_time;
}

inline proj_aim_weapon_profile proj_aim_profile_for_weapon(Weapon* weapon) {
  proj_aim_weapon_profile profile{};
  profile.params = local_prediction_projectile_parameters_for_weapon(weapon);
  profile.splash_radius = proj_aim_splash_radius_for_weapon(weapon);
  profile.hull_radius = proj_aim_hull_radius_for_weapon(weapon);
  profile.arm_time = proj_aim_arm_time_for_weapon(weapon);
  profile.supports_direct = profile.params.speed > 0.0f && proj_aim_mode_allows_direct(weapon);
  profile.supports_splash = profile.params.speed > 0.0f && proj_aim_mode_allows_splash(weapon) && profile.splash_radius > 0.0f;
  profile.arcing = profile.params.gravity > 0.0f;
  profile.params.max_time = std::min(
    profile.params.max_time,
    static_cast<float>(std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420)) * static_cast<float>(TICK_INTERVAL));
  profile.params.time_step = std::max(profile.params.time_step, static_cast<float>(TICK_INTERVAL));
  return profile;
}

#endif
