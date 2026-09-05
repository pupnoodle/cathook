/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/entities/entity.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef ENTITY_HPP
#define ENTITY_HPP
#include <string>
#include <string.h>
#include <cstdint>
#include "core/types.hpp"
#include "games/tf2/sdk/base_handle.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/model_info.hpp"
#include "games/tf2/sdk/netvars.hpp"

enum class_id {
  AMMO_OR_HEALTH_PACK = 1,
  OBJECT_CART_DISPENSER = 85,
  DISPENSER = 86,
  SENTRY = 88,
  TELEPORTER = 89,
  ARROW = 122,
  PLAYER = 247,
  PLAYER_RESOURCE = 249,
  ROCKET = 264,
  SENTRY_ROCKET = 271,
  PILL_OR_STICKY = 217,
  FLARE = 257,
  CROSSBOW_BOLT = 259,
  SNIPER_DOT = 118,
  CAPTURE_FLAG = 26,
  OBJECTIVE_RESOURCE = 235,
  WEARABLE_VM = 343,
  WEARABLE = 336,
  WEARABLE_ITEM = 339,
  WEARABLE_ECON = 35,
  WEARABLE_ROBOT_ARM = 342,
  WEARABLE_RAZORBACK = 341,
  WEARABLE_DEMO_SHIELD = 338,
  WEARABLE_LEVELABLE_ITEM = 340,
  WEARABLE_CAMPAIGN_ITEM = 337,
  RESPAWN_ROOM_VISUALIZER = 64,
  BASE_DOOR = 6,
  BASE_PROP_DOOR = 15,

  PUMPKIN = -3,
  AMMO = -2,
  HEALTH_PACK = -1,
  MVM_CURRENCY = -4,
  MVM_UPGRADE_STATION = -5,
};

enum class tf_team {
  UNKNOWN = 0,
  SPECTATOR,
  RED,
  BLU
};

enum pickup_type {
  UNKNOWN = 0,
  MEDKIT,
  AMMOPACK,
};

enum entity_flags {
    FL_ONGROUND = (1 << 0),
    FL_DUCKING = (1 << 1),
    FL_WATERJUMP = (1 << 2),
    FL_ONTRAIN = (1 << 3),
    FL_INRAIN = (1 << 4),
    FL_FROZEN = (1 << 5),
    FL_ATCONTROLS = (1 << 6),
    FL_CLIENT = (1 << 7),
    FL_FAKECLIENT = (1 << 8),
    FL_INWATER = (1 << 9),
    FL_FLY = (1 << 10),
    FL_SWIM = (1 << 11),
    FL_CONVEYOR = (1 << 12),
    FL_NPC = (1 << 13),
    FL_GODMODE = (1 << 14),
    FL_NOTARGET = (1 << 15),
    FL_AIMTARGET = (1 << 16),
    FL_PARTIALGROUND = (1 << 17),
    FL_STATICPROP = (1 << 18),
    FL_GRAPHED = (1 << 19),
    FL_GRENADE = (1 << 20),
    FL_STEPMOVEMENT = (1 << 21),
    FL_DONTTOUCH = (1 << 22),
    FL_BASEVELOCITY = (1 << 23),
    FL_WORLDBRUSH = (1 << 24),
    FL_OBJECT = (1 << 25),
    FL_KILLME = (1 << 26),
    FL_ONFIRE = (1 << 27),
    FL_DISSOLVING = (1 << 28),
    FL_TRANSRAGDOLL = (1 << 29),
    FL_UNBLOCKABLE_BY_PLAYER = (1 << 30)
};

enum move_type {
    MOVETYPE_NONE = 0,
    MOVETYPE_ISOMETRIC,
    MOVETYPE_WALK,
    MOVETYPE_STEP,
    MOVETYPE_FLY,
    MOVETYPE_FLYGRAVITY,
    MOVETYPE_VPHYSICS,
    MOVETYPE_PUSH,
    MOVETYPE_NOCLIP,
    MOVETYPE_LADDER,
    MOVETYPE_OBSERVER,
    MOVETYPE_CUSTOM
};
#define STUDIO_NONE 0x00000000
#define STUDIO_RENDER 0x00000001
#define STUDIO_VIEWXFORMATTACHMENTS 0x00000002
#define STUDIO_DRAWTRANSLUCENTSUBMODELS 0x00000004
#define STUDIO_TWOPASS 0x00000008
#define STUDIO_STATIC_LIGHTING 0x00000010
#define STUDIO_WIREFRAME 0x00000020
#define STUDIO_ITEM_BLINK 0x00000040
#define STUDIO_NOSHADOWS 0x00000080
#define STUDIO_WIREFRAME_VCOLLIDE 0x00000100
#define STUDIO_NO_OVERRIDE_FOR_ATTACH 0x00000200
#define STUDIO_GENERATE_STATS 0x01000000
#define STUDIO_SSAODEPTHTEXTURE 0x08000000
#define STUDIO_SHADOWDEPTHTEXTURE 0x40000000
#define STUDIO_TRANSPARENCY 0x80000000

class Entity {
public:
  int get_owner_entity_handle(void) {
    static const int netvar_offset = tf2_netvars::find_offset("DT_BaseEntity", {"m_hOwnerEntity"});
    const auto offset = netvar_offset > 0 ? netvar_offset : 0x754;
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset));
  }

  Entity* get_owner_entity(void) {
    if (entity_list == nullptr) {
      return nullptr;
    }

    return entity_list->entity_from_handle(this->get_owner_entity_handle());
  }

  Vec3 get_origin(void) {

    return *(Vec3*)(this + 0x328);
  }

  void set_origin(const Vec3& origin) {
    *(Vec3*)(this + 0x328) = origin;
  }

  float get_gravity(void) {
    return *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(this) + 0x1FC);
  }

  void set_gravity(float value) {
    *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(this) + 0x1FC) = value;
  }

  int get_move_type(void) {
    static const int offset = [] {
      int netvar_offset = tf2_netvars::find_offset("DT_BaseEntity", {"movetype"});
      if (netvar_offset == 0) {
        netvar_offset = tf2_netvars::find_offset("DT_BaseEntity", {"m_MoveType"});
      }
      if (netvar_offset == 0) {
        const int water_level_offset = tf2_netvars::find_offset("DT_BaseEntity", {"m_nWaterLevel"});
        if (water_level_offset > 4) {
          netvar_offset = water_level_offset - 4;
        }
      }
      return netvar_offset;
    }();
    return offset != 0 ? static_cast<int>(*reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset))) : MOVETYPE_WALK;
  }

  void set_move_type(int value) {
    static const int offset = [] {
      int netvar_offset = tf2_netvars::find_offset("DT_BaseEntity", {"movetype"});
      if (netvar_offset == 0) {
        netvar_offset = tf2_netvars::find_offset("DT_BaseEntity", {"m_MoveType"});
      }
      if (netvar_offset == 0) {
        const int water_level_offset = tf2_netvars::find_offset("DT_BaseEntity", {"m_nWaterLevel"});
        if (water_level_offset > 4) {
          netvar_offset = water_level_offset - 4;
        }
      }
      return netvar_offset;
    }();
    if (offset != 0) {
      *reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset)) = static_cast<unsigned char>(value);
    }
  }

  Vec3 get_abs_origin(void) {
    void** vtable = *(void***)this;
    const Vec3& (*get_abs_origin_fn)(void*) = (const Vec3& (*)(void*))vtable[11];
    return get_abs_origin_fn(this);
  }

  void set_abs_origin(const Vec3& origin) {
    void** vtable = *(void***)this;
    const Vec3& (*get_abs_origin_fn)(void*) = (const Vec3& (*)(void*))vtable[11];
    const_cast<Vec3&>(get_abs_origin_fn(this)) = origin;
  }

  Vec3 get_render_origin(void) {
    void* renderable = this->get_renderable();
    if (renderable == nullptr) {
      return get_abs_origin();
    }

    void** vtable = *(void***)renderable;
    const Vec3& (*get_render_origin_fn)(void*) = (const Vec3& (*)(void*))vtable[1];
    return get_render_origin_fn(renderable);
  }

  int get_ent_flags(void) {
    return *(int*)(this + 0x460);
  }

  void* get_networkable(void) {
    return (void*)(this + 0x10);
  }

  void* get_renderable(void) {
    return (void*)(this + 0x8);
  }

  bool setup_bones(matrix_3x4* bone_to_world, int max_bones, int bone_mask, float current_time) {
    if (bone_to_world == nullptr || max_bones <= 0 || max_bones > 128) {
      return false;
    }

    void* renderable = get_renderable();
    if (renderable == nullptr) {
      return false;
    }

    void** vtable = *reinterpret_cast<void***>(renderable);
    constexpr std::size_t setup_bones_index = 16;
    if (vtable == nullptr || vtable[setup_bones_index] == nullptr) {
      return false;
    }

    using setup_bones_fn = bool (*)(void*, matrix_3x4*, int, int, float);
    auto setup_bones_call = reinterpret_cast<setup_bones_fn>(vtable[setup_bones_index]);
    return setup_bones_call(renderable, bone_to_world, max_bones, bone_mask, current_time);
  }

  void* get_client_unknown() {
    void* networkable = get_networkable();
    if (!networkable) return nullptr;
    void** vtable = *(void***)networkable;
    void* (*get_client_unknown_fn)(void*) = (void* (*)(void*))vtable[0];
    return get_client_unknown_fn(networkable);
  }

  void* get_collideable() {
    void* unknown = get_client_unknown();
    if (!unknown) return nullptr;
    void** vtable = *(void***)unknown;
    void* (*get_collideable_fn)(void*) = (void* (*)(void*))vtable[4];
    return get_collideable_fn(unknown);
  }

  Vec3 get_collideable_mins() {
    void* collideable = get_collideable();
    if (!collideable) return Vec3{0, 0, 0};
    void** vtable = *(void***)collideable;
    Vec3& (*get_obb_mins_fn)(void*) = (Vec3& (*)(void*))vtable[3];
    return get_obb_mins_fn(collideable);
  }

  Vec3 get_collideable_maxs() {
    void* collideable = get_collideable();
    if (!collideable) return Vec3{0, 0, 0};
    void** vtable = *(void***)collideable;
    Vec3& (*get_obb_maxs_fn)(void*) = (Vec3& (*)(void*))vtable[4];
    return get_obb_maxs_fn(collideable);
  }

  Vec3 get_collision_origin() {
    void* collideable = get_collideable();
    if (!collideable) return get_abs_origin();
    void** vtable = *(void***)collideable;
    const Vec3& (*get_collision_origin_fn)(void*) = (const Vec3& (*)(void*))vtable[10];
    return get_collision_origin_fn(collideable);
  }

  enum tf_team get_team(void)  {
    return (enum tf_team)*(int*)(this + 0xDC);
  }

  int get_index(void) {
    void* networkable = get_networkable();
    void** vtable = *(void***)networkable;

    int (*get_index_fn)(void*) = (int (*)(void*))vtable[9];

    return get_index_fn(networkable);
  }

  const CBaseHandle& get_ref_ehandle(void) {
    void** vtable = *(void***)this;
    const CBaseHandle& (*get_ref_ehandle_fn)(void*) = (const CBaseHandle& (*)(void*))vtable[3];
    return get_ref_ehandle_fn(this);
  }

  int get_ref_handle(void) {
    return get_ref_ehandle().ToInt();
  }

  const char* get_model_name(void) {
    uintptr_t base_class = *(uintptr_t*)(this + 0x88);
    if (base_class == 0) return "";

    return (const char*)*(unsigned long*)(base_class + 0x8);
  }

  const model_t* get_model(void) {
    void* renderable = this->get_renderable();
    if (renderable == nullptr) {
      return nullptr;
    }

    void** vtable = *(void***)renderable;
    const model_t* (*get_model_fn)(void*) = (const model_t* (*)(void*))vtable[9];
    return get_model_fn(renderable);
  }

  bool should_draw(void) {
    void* renderable = this->get_renderable();
    if (renderable == nullptr) {
      return false;
    }

    void** vtable = *(void***)renderable;
    auto should_draw_fn = (bool (*)(void*))vtable[3];
    return should_draw_fn(renderable);
  }

  int draw_model(int flags) {
    void* renderable = this->get_renderable();
    if (renderable == nullptr) {
      return 0;
    }

    void** vtable = *(void***)renderable;

    int (*draw_model_fn)(void*, int) = (int (*)(void*, int))vtable[10];

    return draw_model_fn(renderable, flags);
  }

  Entity* first_move_child(void) {
    static const int move_child_offset = [] {
      const int moveparent_offset = tf2_netvars::find_offset("DT_BaseEntity", { "moveparent" });
      return moveparent_offset > 12 ? moveparent_offset - 12 : 0;
    }();
    if (move_child_offset == 0 || entity_list == nullptr) {
      return nullptr;
    }

    const int handle = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(this) + move_child_offset);
    return entity_list->entity_from_handle(handle);
  }

  Entity* next_move_peer(void) {
    static const int move_peer_offset = [] {
      const int moveparent_offset = tf2_netvars::find_offset("DT_BaseEntity", { "moveparent" });
      return moveparent_offset > 8 ? moveparent_offset - 8 : 0;
    }();
    if (move_peer_offset == 0 || entity_list == nullptr) {
      return nullptr;
    }

    const int handle = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(this) + move_peer_offset);
    return entity_list->entity_from_handle(handle);
  }

  Entity* move_parent(void) {
    static const int move_parent_offset = [] {
      const int moveparent_offset = tf2_netvars::find_offset("DT_BaseEntity", { "moveparent" });
      return moveparent_offset > 16 ? moveparent_offset - 16 : 0;
    }();
    if (move_parent_offset == 0 || entity_list == nullptr) {
      return nullptr;
    }

    const int handle = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(this) + move_parent_offset);
    return entity_list->entity_from_handle(handle);
  }

  bool is_dormant(void) {
    void* networkable = get_networkable();
    if (networkable == nullptr) {
      return false;
    }

    void** vtable = *(void ***)networkable;
    if (vtable == nullptr || vtable[8] == nullptr) {
      return false;
    }

    bool (*is_dormant_fn)(void*) = (bool (*)(void*))vtable[8];

    return is_dormant_fn(networkable);
  }

  void* get_client_class(void) {
    void* networkable = get_networkable();
    void** vtable = *(void ***)networkable;

    void* (*get_client_class_fn)(void*) = (void* (*)(void*))vtable[2];

    return get_client_class_fn(networkable);
  }

  const char* get_network_name(void) {
    void* client_class = get_client_class();
    if (client_class == nullptr) {
      return "";
    }

    const char* network_name = *(const char**)((unsigned long)(client_class) + 0x10);
    return network_name != nullptr ? network_name : "";
  }

  bool is_network_class(const char* network_name) {
    return network_name != nullptr && strcmp(get_network_name(), network_name) == 0;
  }

  class_id get_class_id(void) {
    void* client_class = get_client_class();
    return *(class_id*)((unsigned long)(client_class) + 0x28);
  }

  int get_tickbase(void) {
    return *(int*)(this + 0x1718);
  }

  void set_tickbase(int tickbase) {
    *(int*)(this + 0x1718) = tickbase;
  }

  float get_simulation_time(void) {
    return *(float*)(this + 0x98);
  }

  bool is_building(void) {
    switch (this->get_class_id()) {
    case class_id::SENTRY:
    case class_id::OBJECT_CART_DISPENSER:
    case class_id::DISPENSER:
    case class_id::TELEPORTER:
      return true;
    }

    return false;
  }

  bool is_wearable(void) {
    switch (this->get_class_id()) {
    case class_id::WEARABLE:
    case class_id::WEARABLE_CAMPAIGN_ITEM:
    case class_id::WEARABLE_DEMO_SHIELD:
    case class_id::WEARABLE_ECON:
    case class_id::WEARABLE_ITEM:
    case class_id::WEARABLE_RAZORBACK:
    case class_id::WEARABLE_VM:
    case class_id::WEARABLE_LEVELABLE_ITEM:
      return true;
    }

    return false;
  }

  enum pickup_type get_pickup_type(void) {
    const char* model_name = get_model_name();

    if (strstr(model_name, "models/items/ammopack")) {
      return pickup_type::AMMOPACK;
    }

    if (strstr(model_name, "models/items/medkit")                     ||
	strstr(model_name, "models/props_medieval/medieval_meat.mdl") ||
	strstr(model_name, "models/props_halloween/halloween_medkit")
	)
      {
	return pickup_type::MEDKIT;
      }

    return pickup_type::UNKNOWN;
  }

  bool is_base_combat_weapon(void) {
    void** vtable = *(void***)this;

    bool (*is_base_combat_weapon_fn)(void*) = (bool (*)(void*))vtable[138];

    return is_base_combat_weapon_fn(this);
  }

};

inline Entity* EntityList::get_game_rules_proxy() {
  static int cached_index = -1;
  if (cached_index != -1) {
    Entity* ent = entity_from_index(cached_index);
    if (ent != nullptr && ent->is_network_class("CTFGameRulesProxy")) {
      return ent;
    }
    cached_index = -1;
  }
  for (unsigned int i = 1; i <= get_max_entities(); ++i) {
    Entity* ent = entity_from_index(i);
    if (ent != nullptr && ent->is_network_class("CTFGameRulesProxy")) {
      cached_index = i;
      return ent;
    }
  }
  return nullptr;
}
#endif
