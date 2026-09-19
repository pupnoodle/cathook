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
#include <cstring>
#include <cmath>
#include <cstdint>
#include <sys/mman.h>
#include "core/memory/resolve.hpp"
#include "core/types.hpp"
#include "games/tf2/sdk/base_handle.hpp"
#include "games/tf2/sdk/datamap.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/model_info.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include "games/tf2/sdk/combat_offsets.hpp"

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
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_hOwnerEntity"}};
    return offset > 0
      ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset))
      : 0;
  }

  Entity* get_owner_entity(void) {
    if (entity_list == nullptr) {
      return nullptr;
    }

    return entity_list->entity_from_handle(this->get_owner_entity_handle());
  }

  static bool origin_usable(const Vec3& origin) {
    return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z);
  }

  static bool origin_is_world_zero(const Vec3& origin) {
    return origin.x == 0.0f && origin.y == 0.0f && origin.z == 0.0f;
  }

  Vec3 get_network_origin(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_vecOrigin"}};
    if (offset <= 0) {
      return {};
    }
    return *reinterpret_cast<Vec3*>(reinterpret_cast<std::uintptr_t>(this) +
                                    static_cast<std::uintptr_t>(offset));
  }

  void set_network_origin(const Vec3& origin) {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_vecOrigin"}};
    if (offset > 0) {
      *reinterpret_cast<Vec3*>(reinterpret_cast<std::uintptr_t>(this) +
                               static_cast<std::uintptr_t>(offset)) = origin;
    }
  }

  Vec3 get_origin(void) {
    const Vec3 network = get_network_origin();
    if (origin_usable(network) && !origin_is_world_zero(network)) {
      return network;
    }
    return get_abs_origin();
  }

  void set_origin(const Vec3& origin) {
    set_abs_origin(origin);
  }

  float get_gravity(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_flGravity"}};
    return offset > 0
      ? *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset))
      : 0.0f;
  }

  void set_gravity(float value) {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_flGravity"}};
    if (offset > 0) {
      *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset)) = value;
    }
  }

  static int move_type_offset() {
    static int offset = 0;
    if (offset <= 0) {
      if (const auto* prop = tf2_netvars::find_prop("DT_BaseEntity", {"movetype"});
          prop != nullptr && prop->proxy_fn != nullptr) {
        offset = tf2_netvars::proxy_store_disp(prop->proxy_fn);
        if (offset <= 0 || offset > 0x2000) {
          offset = 0;
        }
      }
    }
    return offset;
  }

  int get_move_type(void) {
    const int offset = move_type_offset();
    return offset != 0 ? static_cast<int>(*reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset))) : MOVETYPE_WALK;
  }

  void set_move_type(int value) {
    const int offset = move_type_offset();
    if (offset != 0) {
      *reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset)) = static_cast<unsigned char>(value);
    }
  }

  Vec3 get_abs_origin(void) {
    if (this == nullptr) {
      return {};
    }

    Vec3 abs{};
    using get_abs_origin_fn = const Vec3& (*)(void*);
    if (auto* const direct = reinterpret_cast<get_abs_origin_fn>(tf2_combat::get().get_abs_origin_fn)) {
      abs = direct(this);
    } else {
      void** vtable = *(void***)this;
      const std::size_t slot = tf2_combat::player::get_abs_origin();
      if (vtable != nullptr && slot != 0 && vtable[slot] != nullptr) {
        abs = reinterpret_cast<get_abs_origin_fn>(vtable[slot])(this);
      }
    }

    const Vec3 network = get_network_origin();
    if (origin_usable(abs) &&
        !(origin_is_world_zero(abs) && origin_usable(network) && !origin_is_world_zero(network))) {
      return abs;
    }
    if (origin_usable(network) && !origin_is_world_zero(network)) {
      return network;
    }
    return origin_usable(abs) ? abs : network;
  }

  void set_abs_origin(const Vec3& origin) {
    if (this == nullptr || !origin_usable(origin)) {
      return;
    }
    const Vec3 network = get_network_origin();
    if (origin_is_world_zero(origin) && origin_usable(network) && !origin_is_world_zero(network)) {
      return;
    }
    using set_abs_origin_fn = void (*)(void*, const Vec3*);
    if (auto* const direct = reinterpret_cast<set_abs_origin_fn>(tf2_combat::get().set_abs_origin_fn)) {
      direct(this, &origin);
      return;
    }
    const int offset = tf2_combat::entity::abs_origin();
    if (offset <= 0) {
      return;
    }
    *reinterpret_cast<Vec3*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset)) =
      origin;
  }

  void mark_abs_transform_dirty() {
    const int offset = tf2_combat::entity::eflags();
    if (this == nullptr || offset <= 0) {
      return;
    }
    *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset)) |=
      (1 << 11);
  }

  Vec3 get_abs_angles(void) {
    if (this == nullptr) {
      return {};
    }
    using get_abs_angles_fn = const Vec3& (*)(void*);
    if (auto* const direct = reinterpret_cast<get_abs_angles_fn>(tf2_combat::get().get_abs_angles_fn)) {
      return direct(this);
    }
    void** vtable = *(void***)this;
    const std::size_t slot = tf2_combat::player::get_abs_angles();
    if (vtable == nullptr || slot == 0 || vtable[slot] == nullptr) {
      return {};
    }
    return reinterpret_cast<get_abs_angles_fn>(vtable[slot])(this);
  }

  void set_abs_angles(const Vec3& angles) {
    if (this == nullptr || !origin_usable(angles)) {
      return;
    }
    using set_abs_angles_fn = void (*)(void*, const Vec3*);
    if (auto* const direct = reinterpret_cast<set_abs_angles_fn>(tf2_combat::get().set_abs_angles_fn)) {
      direct(this, &angles);
      return;
    }
    const int offset = tf2_combat::entity::abs_angles();
    if (offset <= 0) {
      return;
    }
    *reinterpret_cast<Vec3*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset)) =
      angles;
  }

  Vec3 get_render_origin(void) {
    void* renderable = this->get_renderable();
    if (renderable == nullptr) {
      return get_abs_origin();
    }

    void** vtable = *(void***)renderable;
    if (vtable == nullptr || vtable[1] == nullptr) {
      return get_abs_origin();
    }
    const Vec3& (*get_render_origin_fn)(void*) = (const Vec3& (*)(void*))vtable[1];
    return get_render_origin_fn(renderable);
  }

  int get_ent_flags(void) {
    static tf2_netvars::lazy_offset offset{"DT_BasePlayer", {"m_fFlags"}};
    return offset > 0 ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset)) : 0;
  }

  bool is_engine_static_prop() {
    return this != nullptr && get_ref_ehandle().GetSerialNumber() == (1 << 15);
  }

  bool is_listed_base_entity() {
    if (this == nullptr || entity_list == nullptr || is_engine_static_prop()) {
      return false;
    }
    const int entry = get_ref_ehandle().GetEntryIndex();
    if (entry <= 0 || entry >= 8192) {
      return false;
    }
    return entity_list->entity_from_index(static_cast<unsigned int>(entry)) == this;
  }

  void* get_networkable(void) {
    if (this == nullptr || is_engine_static_prop()) {
      return nullptr;
    }
    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this) + 16);
  }

  void* get_renderable(void) {
    return this == nullptr ? nullptr : reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this) + 8);
  }

  bool setup_bones(matrix_3x4* bone_to_world, int max_bones, int bone_mask, float current_time) {
    if (bone_to_world == nullptr || max_bones <= 0 || max_bones > 128) {
      return false;
    }

    using setup_bones_fn = bool (*)(void*, matrix_3x4*, int, int, float);
    if (auto* const direct = reinterpret_cast<setup_bones_fn>(tf2_combat::get().setup_bones_fn)) {
      return direct(this, bone_to_world, max_bones, bone_mask, current_time);
    }

    void** vtable = *reinterpret_cast<void***>(this);
    const std::size_t slot = tf2_combat::player::setup_bones();
    if (vtable == nullptr || slot == 0 || vtable[slot] == nullptr) {
      return false;
    }
    return reinterpret_cast<setup_bones_fn>(vtable[slot])(
      this, bone_to_world, max_bones, bone_mask, current_time);
  }

  void* get_client_unknown() {
    void* networkable = get_networkable();
    if (!networkable) return nullptr;
    void** vtable = *(void***)networkable;
    if (vtable == nullptr || vtable[0] == nullptr) return nullptr;

    void* (*get_client_unknown_fn)(void*) = (void* (*)(void*))vtable[0];
    return get_client_unknown_fn(networkable);
  }

  int collision_property_offset() const {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_Collision"}};
    return offset;
  }

  int collision_mins_offset() const {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_Collision", "m_vecMins"}};
    return offset;
  }

  int collision_maxs_offset() const {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_Collision", "m_vecMaxs"}};
    return offset;
  }

  void* get_collideable() {
    const int offset = collision_property_offset();
    return offset > 0
      ? reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset))
      : nullptr;
  }

  Vec3& collideable_mins() {
    const int offset = collision_mins_offset();
    static Vec3 fallback{};
    return offset > 0
      ? *reinterpret_cast<Vec3*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset))
      : fallback;
  }

  Vec3& collideable_maxs() {
    const int offset = collision_maxs_offset();
    static Vec3 fallback{};
    return offset > 0
      ? *reinterpret_cast<Vec3*>(reinterpret_cast<std::uintptr_t>(this) + static_cast<std::uintptr_t>(offset))
      : fallback;
  }

  Vec3 get_collideable_mins() {
    return collideable_mins();
  }

  Vec3 get_collideable_maxs() {
    return collideable_maxs();
  }

  Vec3 get_collision_origin() {
    return get_origin();
  }

  enum tf_team get_team(void)  {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_iTeamNum"}};
    return offset > 0 ? static_cast<enum tf_team>(*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset))) : tf_team::UNKNOWN;
  }

  int get_index(void) {
    void* networkable = get_networkable();
    if (networkable == nullptr) return -1;
    void** vtable = *(void***)networkable;
    if (vtable == nullptr || vtable[9] == nullptr) return -1;

    int (*get_index_fn)(void*) = (int (*)(void*))vtable[9];

    return get_index_fn(networkable);
  }

  const CBaseHandle& get_ref_ehandle(void) {
    static const CBaseHandle invalid_handle{invalid_ehandle_index};
    if (this == nullptr) return invalid_handle;
    void** vtable = *(void***)this;
    if (vtable == nullptr || vtable[3] == nullptr) return invalid_handle;
    const CBaseHandle& (*get_ref_ehandle_fn)(void*) = (const CBaseHandle& (*)(void*))vtable[3];
    return get_ref_ehandle_fn(this);
  }

  int get_ref_handle(void) {
    return get_ref_ehandle().ToInt();
  }

  const char* get_model_name(void) {
    const model_t* model = get_model();
    if (model == nullptr || model->name == nullptr) return "";

    return model->name;
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

  static int moveparent_field_offset() {
    static int offset = 0;
    if (offset <= 0) {
      static tf2_netvars::lazy_offset moveparent{"DT_BaseEntity", {"moveparent"}};
      offset = moveparent;
    }
    return offset;
  }

  Entity* first_move_child(void) {
    const int base = moveparent_field_offset();
    const int move_child_offset = base > 12 ? base - 12 : 0;
    if (move_child_offset == 0 || entity_list == nullptr) {
      return nullptr;
    }

    const int handle = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(this) + move_child_offset);
    return entity_list->entity_from_handle(handle);
  }

  Entity* next_move_peer(void) {
    const int base = moveparent_field_offset();
    const int move_peer_offset = base > 8 ? base - 8 : 0;
    if (move_peer_offset == 0 || entity_list == nullptr) {
      return nullptr;
    }

    const int handle = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(this) + move_peer_offset);
    return entity_list->entity_from_handle(handle);
  }

  Entity* move_parent(void) {
    const int base = moveparent_field_offset();
    const int move_parent_offset = base > 16 ? base - 16 : 0;
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
    if (networkable == nullptr) return nullptr;
    void** vtable = *(void ***)networkable;
    if (vtable == nullptr || vtable[2] == nullptr) return nullptr;

    void* (*get_client_class_fn)(void*) = (void* (*)(void*))vtable[2];

    void* client_class = get_client_class_fn(networkable);
    if (client_class == nullptr) return nullptr;
    const auto client_class_address = reinterpret_cast<std::uintptr_t>(client_class);
    if (client_class_address < 0x10000 || client_class_address >= 0x0000800000000000ULL) return nullptr;
    const auto* typed_client_class = reinterpret_cast<const tf2_netvars::client_class*>(client_class);
    if (typed_client_class->class_id < 0 || typed_client_class->class_id > 512 ||
        typed_client_class->network_name == nullptr || typed_client_class->network_name[0] == '\0') return nullptr;
    return client_class;
  }

  const tf2_netvars::client_class* get_typed_client_class(void) {
    return reinterpret_cast<const tf2_netvars::client_class*>(get_client_class());
  }

  const char* get_network_name(void) {
    const auto* client_class = get_typed_client_class();
    if (client_class == nullptr || client_class->network_name == nullptr) {
      return "";
    }
    return client_class->network_name;
  }

  bool is_network_class(const char* network_name) {
    return network_name != nullptr && strcmp(get_network_name(), network_name) == 0;
  }

  class_id get_class_id(void) {
    const auto* client_class = get_typed_client_class();
    return client_class == nullptr ? static_cast<class_id>(-1) : static_cast<class_id>(client_class->class_id);
  }

  int get_tickbase(void) {
    static tf2_netvars::lazy_offset offset{"DT_BasePlayer", {"m_nTickBase"}};
    return offset > 0 ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset)) : 0;
  }

  void set_tickbase(int tickbase) {
    static tf2_netvars::lazy_offset offset{"DT_BasePlayer", {"m_nTickBase"}};
    if (offset > 0) {
      *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset)) = tickbase;
    }
  }

  float get_simulation_time(void) {
    static tf2_netvars::lazy_offset offset{"DT_BaseEntity", {"m_flSimulationTime"}};
    return offset > 0 ? *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset)) : 0.0f;
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

  datamap_t* get_pred_desc_map() {
    void** vtable = *reinterpret_cast<void***>(this);
    if (vtable == nullptr || vtable[18] == nullptr) {
      return nullptr;
    }

    const auto readable = [](const void* address, std::size_t bytes) -> bool {
      if (address == nullptr) {
        return false;
      }
      const int protection = puphook::core::memory::protection_at(address);
      if (protection < 0 || (protection & PROT_READ) == 0) {
        return false;
      }
      if (bytes <= 1) {
        return true;
      }
      const auto* start = static_cast<const std::uint8_t*>(address);
      return puphook::core::memory::protection_at(start + bytes - 1) >= 0;
    };

    const auto looks_like_pred_map = [&](const datamap_t* map) -> bool {
      if (!readable(map, sizeof(datamap_t)) || map->dataDesc == nullptr ||
          !readable(map->dataDesc, 16)) {
        return false;
      }
      if (map->dataNumFields <= 0 || map->dataNumFields > 1024) {
        return false;
      }
      if (map->packed_size < 0 || map->packed_size > 1 << 20) {
        return false;
      }
      if (!readable(map->dataClassName, 8)) {
        return false;
      }
      const char* name = map->dataClassName;
      std::size_t length = 0;
      while (length < 64 && name[length] != '\0') {
        ++length;
      }
      return length >= 3 && length < 64 && std::strstr(name, "Player") != nullptr;
    };

    using get_map_fn = datamap_t* (*)(void*);
    datamap_t* map = reinterpret_cast<get_map_fn>(vtable[18])(this);
    return looks_like_pred_map(map) ? map : nullptr;
  }

};

inline Player* EntityList::player_from_index(unsigned int index) {
  Entity* entity = entity_from_index(index);
  if (entity == nullptr || entity->get_class_id() != class_id::PLAYER) {
    return nullptr;
  }
  return reinterpret_cast<Player*>(entity);
}

inline Entity* EntityList::get_game_rules_proxy() {
  static int cached_index = -1;
  if (cached_index != -1) {
    Entity* ent = entity_from_index(cached_index);
    if (ent != nullptr && ent->is_network_class("CTFGameRulesProxy")) {
      return ent;
    }
    cached_index = -1;
  }
  for (unsigned int i = 1; i < get_max_entities(); ++i) {
    Entity* ent = entity_from_index(i);
    if (ent != nullptr && ent->is_network_class("CTFGameRulesProxy")) {
      cached_index = i;
      return ent;
    }
  }
  return nullptr;
}
#endif
