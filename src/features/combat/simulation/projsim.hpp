#ifndef PROJSIM_HPP
#define PROJSIM_HPP
#include <algorithm>
#include <cmath>
#include <vector>
#include "core/math/math.hpp"
#include "core/types.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/vphysics.hpp"

namespace projsim {

struct drag_profile {
  float coefficient = 0.0f;
  Vec3 linear{};
  Vec3 angular{};
  Vec3 spin{};
};

inline drag_profile drag_for_weapon(int weapon_id) {
  drag_profile profile{};
  switch (weapon_id) {
  case TF_WEAPON_GRENADELAUNCHER:
    profile.coefficient = 1.0f;
    profile.linear = {0.003902f, 0.009962f, 0.009962f};
    profile.angular = {0.003618f, 0.001514f, 0.001514f};
    profile.spin = {600.0f, -1200.0f, 0.0f};
    break;
  case TF_WEAPON_PIPEBOMBLAUNCHER:
  case TF_WEAPON_STICKY_BALL_LAUNCHER:
    profile.coefficient = 1.0f;
    profile.linear = {0.007491f, 0.007491f, 0.007306f};
    profile.angular = {0.002777f, 0.002842f, 0.002812f};
    profile.spin = {600.0f, -1200.0f, 0.0f};
    break;
  case TF_WEAPON_CANNON:
    profile.coefficient = 1.0f;
    profile.linear = {0.020971f, 0.019420f, 0.020971f};
    profile.angular = {0.012997f, 0.013496f, 0.013714f};
    profile.spin = {600.0f, -1200.0f, 0.0f};
    break;
  case TF_WEAPON_CLEAVER:
  case TF_WEAPON_GRENADE_CLEAVER:
    profile.coefficient = 1.0f;
    profile.linear = {0.022287f, 0.005208f, 0.110697f};
    profile.angular = {0.013982f, 0.043243f, 0.003465f};
    profile.spin = {0.0f, 500.0f, 0.0f};
    break;
  case TF_WEAPON_BAT_WOOD:
    profile.coefficient = 1.0f;
    profile.linear = {0.009000f, 0.006581f, 0.006710f};
    profile.angular = {0.002233f, 0.002246f, 0.002206f};
    profile.spin = {0.0f, 100.0f, 0.0f};
    break;
  case TF_WEAPON_BAT_GIFTWRAP:
    profile.coefficient = 1.0f;
    profile.linear = {0.013500f, 0.0108727f, 0.010804f};
    profile.angular = {0.002081f, 0.002162f, 0.002069f};
    profile.spin = {0.0f, 100.0f, 0.0f};
    break;
  case TF_WEAPON_JAR:
    profile.coefficient = 1.0f;
    profile.linear = {0.005127f, 0.002925f, 0.004337f};
    profile.angular = {0.000641f, 0.001350f, 0.000717f};
    profile.spin = {300.0f, 0.0f, 0.0f};
    break;
  case TF_WEAPON_JAR_MILK:
    profile.coefficient = 1.0f;
    profile.linear = {0.005514f, 0.002313f, 0.005558f};
    profile.angular = {0.000684f, 0.001439f, 0.000680f};
    profile.spin = {300.0f, 0.0f, 0.0f};
    break;
  case TF_WEAPON_THROWABLE:
    profile.coefficient = 1.0f;
    profile.linear = {0.002208f, 0.001640f, 0.002187f};
    profile.angular = {0.000799f, 0.001515f, 0.000879f};
    profile.spin = {300.0f, 0.0f, 0.0f};
    break;
  case TF_WEAPON_GRENADE_GAS:
    profile.coefficient = 1.0f;
    profile.linear = {0.026360f, 0.021780f, 0.058978f};
    profile.angular = {0.035050f, 0.031199f, 0.022922f};
    profile.spin = {300.0f, 0.0f, 0.0f};
    break;
  default:
    break;
  }
  return profile;
}

struct params {
  Vec3 origin{};
  Vec3 velocity{};
  Vec3 angles{};
  float gravity = 800.0f;
  float drag = 0.0f;
  Vec3 hull{2.0f, 2.0f, 2.0f};
  unsigned int collision_mask = MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_HITBOX;
  trace_filter filter{};
  int weapon_id = 0;
  bool spin = false;
};

struct env_state {
  IPhysicsEnvironment* env = nullptr;
  IPhysicsObject* object = nullptr;
  CPhysCollide* collide = nullptr;
};

inline env_state& shared_env() {
  static env_state state{};
  return state;
}

inline void shutdown() {
  env_state& state = shared_env();
  if (physics != nullptr && state.env != nullptr) {
    if (state.object != nullptr) {
      state.env->DestroyObject(state.object);
    }
    physics->DestroyEnvironment(state.env);
  }
  state = {};
}

inline bool ensure_env() {
  env_state& state = shared_env();
  if (physics == nullptr || physics_collision == nullptr) {
    return false;
  }
  if (state.env == nullptr) {
    state.env = physics->CreateEnvironment();
  }
  if (state.env == nullptr) {
    return false;
  }
  if (state.object == nullptr) {
    state.collide = physics_collision->BBoxToCollide({-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f});
    if (state.collide == nullptr) {
      return false;
    }
    objectparams_t params{};
    params.mass = 1.0f;
    params.inertia = 0.0f;
    params.damping = 0.0f;
    params.rotdamping = 0.0f;
    params.rotInertiaLimit = 0.0f;
    params.pName = "projsim";
    params.dragCoefficient = 0.0f;
    params.enableCollisions = false;
    state.object = state.env->CreatePolyObject(state.collide, 0, {}, {}, &params);
    if (state.object != nullptr) {
      state.object->Wake();
    }
  }
  return state.object != nullptr;
}

struct simulation {
  params p{};
  Vec3 position{};
  Vec3 velocity{};
  int tick = 0;
  std::vector<Vec3> path{};
  trace_t last_trace{};
  bool stopped = false;
  bool physics_mode = false;

  void reset(const params& value) {
    p = value;
    position = p.origin;
    velocity = p.velocity;
    tick = 0;
    stopped = false;
    path.clear();
    last_trace = {};

    const drag_profile drag = drag_for_weapon(p.weapon_id);
    physics_mode = drag.coefficient > 0.0f && length_squared(drag.linear) > 0.0f && ensure_env();
    const float dt = tick_interval();

    if (physics_mode) {
      env_state& state = shared_env();
      float coefficient = drag.coefficient;
      state.object->SetDragCoefficient(&coefficient, &coefficient);
      state.object->m_dragBasis = drag.linear;
      state.object->m_angDragBasis = drag.angular;
      state.object->EnableGravity(true);
      state.object->EnableDrag(true);
      state.object->EnableCollisions(false);

      physics_performanceparams_t performance{};
      performance.Defaults();
      performance.maxVelocity = 1000000.0f;
      performance.maxAngularVelocity = 1000000.0f;
      if (p.weapon_id == TF_WEAPON_GRENADELAUNCHER || p.weapon_id == TF_WEAPON_PIPEBOMBLAUNCHER ||
          p.weapon_id == TF_WEAPON_STICKY_BALL_LAUNCHER || p.weapon_id == TF_WEAPON_CANNON ||
          p.weapon_id == TF_WEAPON_CLEAVER || p.weapon_id == TF_WEAPON_GRENADE_CLEAVER ||
          p.weapon_id == TF_WEAPON_BAT_WOOD || p.weapon_id == TF_WEAPON_BAT_GIFTWRAP) {
        performance.maxVelocity = k_flMaxVelocity;
        performance.maxAngularVelocity = k_flMaxAngularVelocity;
      }
      state.env->SetPerformanceSettings(&performance);
      state.env->SetAirDensity(AIR_DENSITY);
      state.env->SetGravity({0.0f, 0.0f, -p.gravity});
      state.env->ResetSimulationClock();

      Vec3 angular = p.spin ? drag.spin : Vec3{};
      state.object->SetPosition(p.origin, p.angles, true);
      state.object->SetVelocity(&velocity, &angular);
      state.object->Wake();
      state.env->Simulate(dt);
      state.object->GetPosition(&position, nullptr);
      state.object->GetVelocity(&velocity, nullptr);
    } else if (p.gravity != 0.0f) {
      velocity.z += p.gravity * dt * 0.5f;
    }
    path.push_back(position);
  }

  bool step() {
    const float dt = tick_interval();
    if (physics_mode) {
      env_state& state = shared_env();
      if (state.env == nullptr || state.object == nullptr) {
        stopped = true;
        return false;
      }
      state.env->Simulate(dt);
      state.object->GetPosition(&position, nullptr);
      state.object->GetVelocity(&velocity, nullptr);
    } else {
      if (p.gravity != 0.0f) {
        velocity.z -= p.gravity * dt;
      }
      position = position + velocity * dt;
    }

    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
      stopped = true;
      return false;
    }
    ++tick;
    path.push_back(position);
    return true;
  }
};

}

#endif
