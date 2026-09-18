/*
/^-----^\   data: 2026-09-18
V  o o  V  file: src/core/hooks/recv_proxy_simulation_time.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include "core/print.hpp"

namespace {

struct recv_variant {
  union {
    float m_Float;
    int m_Int;
    const char* m_pString;
    void* m_pData;
    float m_Vector[3];
    long long m_Int64;
  };
  int m_Type;
};

struct recv_proxy_data {
  const tf2_netvars::recv_prop* m_pRecvProp;
  recv_variant m_Value;
  int m_iElement;
  int m_ObjectID;
};

using recv_proxy_fn = void (*)(const recv_proxy_data*, void*, void*);

recv_proxy_fn simulation_time_proxy_original = nullptr;
tf2_netvars::recv_prop* simulation_time_prop = nullptr;

int network_base(int tick, int entity_index) {
  if (global_vars == nullptr || global_vars->nTimestampNetworkingBase <= 0 ||
      global_vars->nTimestampRandomizeWindow <= 0) {
    return tick;
  }
  const int entity_mod = entity_index % global_vars->nTimestampRandomizeWindow;
  return global_vars->nTimestampNetworkingBase *
    ((tick - entity_mod) / global_vars->nTimestampNetworkingBase);
}

void hooked_simulation_time(const recv_proxy_data* data, void* object, void* out) {
  auto* entity = static_cast<Entity*>(object);
  if (entity == nullptr || data == nullptr || entity->get_class_id() != class_id::PLAYER ||
      (engine != nullptr && entity->get_index() == engine->get_localplayer_index())) {
    if (simulation_time_proxy_original != nullptr) {
      simulation_time_proxy_original(data, object, out);
    }
    return;
  }

  if (data->m_Value.m_Int == 0) {
    return;
  }

  int addt = data->m_Value.m_Int;
  int t = network_base(global_vars != nullptr ? global_vars->tickcount : 0, entity->get_index());
  t += addt;
  const int tickcount = global_vars != nullptr ? global_vars->tickcount : t;
  while (t < tickcount - 127) {
    t += 256;
  }
  while (t > tickcount + 127) {
    t -= 256;
  }

  const float sim_time = ticks_to_time(t);
  static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_flSimulationTime"}};
  if (offset > 0) {
    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(entity) +
                              static_cast<uintptr_t>(offset)) = sim_time;
  } else if (out != nullptr) {
    *static_cast<float*>(out) = sim_time;
  }
}

}

namespace simulation_time_proxy {

inline bool install() {
  if (simulation_time_prop != nullptr) {
    return true;
  }
  auto* prop = tf2_netvars::find_prop("DT_BaseEntity", {"m_flSimulationTime"});
  if (prop == nullptr || prop->proxy_fn == nullptr) {
    print("m_flSimulationTime recv proxy missing\n");
    return false;
  }
  simulation_time_prop = prop;
  simulation_time_proxy_original = reinterpret_cast<recv_proxy_fn>(prop->proxy_fn);
  prop->proxy_fn = reinterpret_cast<void*>(hooked_simulation_time);
  return true;
}

inline void restore() {
  if (simulation_time_prop != nullptr && simulation_time_proxy_original != nullptr) {
    simulation_time_prop->proxy_fn = reinterpret_cast<void*>(simulation_time_proxy_original);
  }
  simulation_time_prop = nullptr;
  simulation_time_proxy_original = nullptr;
}

}
