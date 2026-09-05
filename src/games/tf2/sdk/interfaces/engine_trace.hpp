/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/interfaces/engine_trace.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef ENGINE_TRACE_HPP
#define ENGINE_TRACE_HPP

#include "core/types.hpp"

#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/interfaces/utl_vector.hpp"

class CPhysCollide;
class ICollideable;
class IHandleEntity;

struct vector4d {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 0.0f;
};

class IEntityEnumerator {
public:
  virtual bool enum_entity(IHandleEntity* handle_entity) = 0;
};

enum trace_type_t {
  TRACE_EVERYTHING = 0,
  TRACE_WORLD_ONLY,
  TRACE_ENTITIES_ONLY,
  TRACE_EVERYTHING_FILTER_PROPS,
};

struct ray_t
{
  struct Vec3_aligned start;
  struct Vec3_aligned delta;
  struct Vec3_aligned start_offset;
  struct Vec3_aligned extents;
  bool is_ray;
  bool is_swept;
};

struct trace_filter {
  void** vtable;
  void* skip;
  void* target;
  int skip_team = -1;
  bool ignore_target = false;
};

struct cplane_t {
  Vec3 normal;
  float dist;
  unsigned char type;
  unsigned char signbits;
  unsigned char pad[2];
};

struct csurface_t {
  const char* name;
  short surface_props;
  unsigned short flags;
};

struct trace_t {
  Vec3 startpos;
  Vec3 endpos;
  struct cplane_t plane;
  float fraction;
  int contents;
  unsigned short disp_flags;
  bool all_solid;
  bool start_solid;
  float fraction_left_solid;
  struct csurface_t surface;
  int hit_group;
  short physics_bone;
  void* entity;
  int hitbox;

};

#define	CONTENTS_EMPTY			0

#define	CONTENTS_SOLID			0x1
#define	CONTENTS_WINDOW			0x2
#define	CONTENTS_AUX			0x4
#define	CONTENTS_GRATE			0x8
#define	CONTENTS_SLIME			0x10
#define	CONTENTS_WATER			0x20
#define	CONTENTS_BLOCKLOS		0x40
#define CONTENTS_OPAQUE			0x80
#define	LAST_VISIBLE_CONTENTS	0x80

#define ALL_VISIBLE_CONTENTS (LAST_VISIBLE_CONTENTS | (LAST_VISIBLE_CONTENTS-1))

#define CONTENTS_TESTFOGVOLUME	0x100
#define CONTENTS_UNUSED			0x200

#define CONTENTS_UNUSED6		0x400

#define CONTENTS_TEAM1			0x800
#define CONTENTS_TEAM2			0x1000
#define CONTENTS_REDTEAM		CONTENTS_TEAM1
#define CONTENTS_BLUETEAM		CONTENTS_TEAM2

#define CONTENTS_IGNORE_NODRAW_OPAQUE	0x2000

#define CONTENTS_MOVEABLE		0x4000

#define	CONTENTS_AREAPORTAL		0x8000

#define	CONTENTS_PLAYERCLIP		0x10000
#define	CONTENTS_MONSTERCLIP	0x20000

#define	CONTENTS_CURRENT_0		0x40000
#define	CONTENTS_CURRENT_90		0x80000
#define	CONTENTS_CURRENT_180	0x100000
#define	CONTENTS_CURRENT_270	0x200000
#define	CONTENTS_CURRENT_UP		0x400000
#define	CONTENTS_CURRENT_DOWN	0x800000

#define	CONTENTS_ORIGIN			0x1000000

#define	CONTENTS_MONSTER		0x2000000
#define	CONTENTS_DEBRIS			0x4000000
#define	CONTENTS_DETAIL			0x8000000
#define	CONTENTS_TRANSLUCENT	0x10000000
#define	CONTENTS_LADDER			0x20000000
#define CONTENTS_HITBOX			0x40000000
#define CONTENTS_NOSTARTSOLID	0x80000000

#define	MASK_ALL					(0xFFFFFFFF)

#define	MASK_SOLID					(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_WINDOW|CONTENTS_MONSTER|CONTENTS_GRATE)

#define	MASK_SOLID_BRUSHONLY		(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_WINDOW|CONTENTS_GRATE)

#define	MASK_PLAYERSOLID			(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_PLAYERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER|CONTENTS_GRATE)

#define	MASK_NPCSOLID				(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_MONSTERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER|CONTENTS_GRATE)

#define	MASK_WATER					(CONTENTS_WATER|CONTENTS_MOVEABLE|CONTENTS_SLIME)

#define	MASK_OPAQUE					(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_OPAQUE)

#define MASK_OPAQUE_AND_NPCS		(MASK_OPAQUE|CONTENTS_MONSTER)

#define MASK_BLOCKLOS				(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_BLOCKLOS)

#define MASK_BLOCKLOS_AND_NPCS		(MASK_BLOCKLOS|CONTENTS_MONSTER)

#define	MASK_VISIBLE					(MASK_OPAQUE|CONTENTS_IGNORE_NODRAW_OPAQUE)

#define MASK_VISIBLE_AND_NPCS		(MASK_OPAQUE_AND_NPCS|CONTENTS_IGNORE_NODRAW_OPAQUE)

#define	MASK_SHOT					(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_MONSTER|CONTENTS_WINDOW|CONTENTS_DEBRIS|CONTENTS_HITBOX)

inline bool should_hit_entity(struct trace_filter* interface, Entity* entity, int contents_mask) {
  if (entity == nullptr) return false;
  if (entity->get_class_id() == class_id::RESPAWN_ROOM_VISUALIZER) return false;
  return interface == nullptr || entity != interface->skip;
}

inline enum trace_type_t get_type(struct trace_filter* interface) {
  return TRACE_EVERYTHING;
}

static void* trace_filter_vtable[2] = { (void*)should_hit_entity, (void*)get_type };

inline bool trace_filter_same_entity(Entity* entity, void* other) {
  if (entity == nullptr || other == nullptr) {
    return false;
  }
  if (entity == other) {
    return true;
  }

  Entity* other_entity = static_cast<Entity*>(other);
  return entity->get_index() == other_entity->get_index();
}

inline bool hitscan_trace_should_hit_entity(struct trace_filter* interface, Entity* entity, int contents_mask) {
  (void)contents_mask;
  if (entity == nullptr) {
    return false;
  }
  if (interface != nullptr && trace_filter_same_entity(entity, interface->skip)) {
    return false;
  }
  if (interface != nullptr && trace_filter_same_entity(entity, interface->target)) {
    return !interface->ignore_target;
  }

  const class_id cid = entity->get_class_id();
  if (interface != nullptr &&
      interface->skip_team >= 0 &&
      static_cast<int>(entity->get_team()) == interface->skip_team &&
      cid == class_id::PLAYER) {
    return false;
  }

  return cid != class_id::RESPAWN_ROOM_VISUALIZER &&
    cid != class_id::AMMO_OR_HEALTH_PACK &&
    cid != class_id::CAPTURE_FLAG &&
    cid != class_id::OBJECTIVE_RESOURCE &&
    cid != class_id::PLAYER_RESOURCE &&
    cid != class_id::SNIPER_DOT &&
    cid != class_id::ROCKET &&
    cid != class_id::FLARE &&
    cid != class_id::CROSSBOW_BOLT &&
    cid != class_id::ARROW &&
    !entity->is_wearable();
}

inline enum trace_type_t hitscan_trace_get_type(struct trace_filter*) {
  return TRACE_EVERYTHING;
}

static void* trace_filter_hitscan_vtable[2] = { (void*)hitscan_trace_should_hit_entity, (void*)hitscan_trace_get_type };

inline bool world_trace_should_hit_entity(struct trace_filter*, Entity*, int) {
  return false;
}

inline enum trace_type_t world_trace_get_type(struct trace_filter*) {
  return TRACE_WORLD_ONLY;
}

static void* trace_filter_world_vtable[2] = { (void*)world_trace_should_hit_entity, (void*)world_trace_get_type };

inline enum trace_type_t world_and_props_trace_get_type(struct trace_filter*) {
  return TRACE_EVERYTHING;
}

static void* trace_filter_world_and_props_vtable[2] = {
  (void*)world_trace_should_hit_entity,
  (void*)world_and_props_trace_get_type
};

inline bool melee_trace_should_hit_entity(struct trace_filter* interface, Entity* entity, int contents_mask) {
  (void)contents_mask;
  if (entity == nullptr || (interface != nullptr && trace_filter_same_entity(entity, interface->skip))) {
    return false;
  }
  if (interface != nullptr && trace_filter_same_entity(entity, interface->target)) {
    return true;
  }
  if (interface != nullptr && interface->skip_team >= 0 &&
      static_cast<int>(entity->get_team()) == interface->skip_team &&
      entity->get_class_id() == class_id::PLAYER) {
    return false;
  }
  return true;
}

inline enum trace_type_t melee_trace_get_type(struct trace_filter*) {
  return TRACE_EVERYTHING;
}

static void* trace_filter_melee_vtable[2] = {
  (void*)melee_trace_should_hit_entity,
  (void*)melee_trace_get_type
};

inline bool projectile_trace_should_hit_entity(struct trace_filter* interface, Entity* entity, int contents_mask) {
  (void)contents_mask;
  if (entity == nullptr || (interface != nullptr && trace_filter_same_entity(entity, interface->skip))) {
    return false;
  }
  if (interface != nullptr && trace_filter_same_entity(entity, interface->target)) {
    return !interface->ignore_target;
  }
  if (interface != nullptr && interface->skip_team >= 0 &&
      entity->get_class_id() == class_id::PLAYER &&
      static_cast<int>(entity->get_team()) == interface->skip_team) {
    return false;
  }
  return entity->get_class_id() != class_id::RESPAWN_ROOM_VISUALIZER;
}

inline enum trace_type_t projectile_trace_get_type(struct trace_filter*) {
  return TRACE_EVERYTHING;
}

static void* trace_filter_projectile_vtable[2] = {
  (void*)projectile_trace_should_hit_entity,
  (void*)projectile_trace_get_type
};

class EngineTrace {
public:
  struct Vec3_aligned Vec3_aligned_subtract(Vec3* a, Vec3* b) {
    struct Vec3_aligned result = {
      .x = a->x - b->x,
      .y = a->y - b->y,
      .z = a->z - b->z
    };

    return result;
  }

  struct Vec3_aligned Vec3_aligned_add(Vec3* a, Vec3* b) {
    struct Vec3_aligned result = {
      .x = a->x + b->x,
      .y = a->y + b->y,
      .z = a->z + b->z
    };

    return result;
  }

  struct Vec3_aligned Vec3_aligned_add(Vec3* a, Vec3_aligned* b) {
    struct Vec3_aligned result = {
      .x = a->x + b->x,
      .y = a->y + b->y,
      .z = a->z + b->z
    };

    return result;
  }

  struct ray_t init_ray(Vec3* start, Vec3* end) {
    struct Vec3_aligned delta = Vec3_aligned_subtract(end, start);
    bool is_swept = (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f);

    struct ray_t ray = {
      .start = { start->x, start->y, start->z },
      .delta = { delta.x, delta.y, delta.z },
      .start_offset = { 0.0f, 0.0f, 0.0f },
      .extents = { 0.0f, 0.0f, 0.0f },
      .is_ray = true,
      .is_swept = is_swept
    };

    return ray;
  }

  struct ray_t init_ray(Vec3* start, Vec3* end, Vec3* mins, Vec3* maxs) {
    struct ray_t ray;

    ray.delta = Vec3_aligned_subtract(end, start);
    ray.is_swept = (ray.delta.x * ray.delta.x + ray.delta.y * ray.delta.y + ray.delta.z * ray.delta.z) != 0;
    ray.extents = Vec3_aligned_subtract(maxs, mins);
    ray.extents.x *= 0.5f;
    ray.extents.y *= 0.5f;
    ray.extents.z *= 0.5f;

    ray.is_ray = (ray.extents.x * ray.extents.x + ray.extents.y * ray.extents.y + ray.extents.z * ray.extents.z) == 0;
    ray.start_offset = Vec3_aligned_add(mins, maxs);
    ray.start_offset.x *= 0.5f;
    ray.start_offset.y *= 0.5f;
    ray.start_offset.z *= 0.5f;

    ray.start = Vec3_aligned_add(start, &ray.start_offset);
    ray.start_offset.x *= -1.0f;
    ray.start_offset.y *= -1.0f;
    ray.start_offset.z *= -1.0f;

    return ray;
  }

  void init_trace_filter(struct trace_filter* filter, void* skip) {
    filter->vtable = trace_filter_vtable;
    filter->skip = skip;
    filter->target = nullptr;
    filter->skip_team = -1;
    filter->ignore_target = false;
  }

  void init_hitscan_trace_filter(struct trace_filter* filter, Entity* skip_entity, Entity* target_entity = nullptr) {
    filter->vtable = trace_filter_hitscan_vtable;
    filter->skip = skip_entity;
    filter->target = target_entity;
    filter->skip_team = skip_entity != nullptr ? static_cast<int>(skip_entity->get_team()) : -1;
    filter->ignore_target = false;
  }

  void init_world_trace_filter(struct trace_filter* filter) {
    filter->vtable = trace_filter_world_vtable;
    filter->skip = nullptr;
    filter->target = nullptr;
    filter->skip_team = -1;
    filter->ignore_target = false;
  }

  void init_world_and_props_trace_filter(struct trace_filter* filter) {
    filter->vtable = trace_filter_world_and_props_vtable;
    filter->skip = nullptr;
    filter->target = nullptr;
    filter->skip_team = -1;
    filter->ignore_target = false;
  }

  void init_melee_trace_filter(struct trace_filter* filter, Entity* skip_entity, Entity* target_entity) {
    filter->vtable = trace_filter_melee_vtable;
    filter->skip = skip_entity;
    filter->target = target_entity;
    filter->skip_team = skip_entity != nullptr ? static_cast<int>(skip_entity->get_team()) : -1;
    filter->ignore_target = false;
  }

  void init_projectile_trace_filter(struct trace_filter* filter, Entity* skip_entity,
    Entity* target_entity = nullptr, bool ignore_target = false) {
    filter->vtable = trace_filter_projectile_vtable;
    filter->skip = skip_entity;
    filter->target = target_entity;
    filter->skip_team = skip_entity != nullptr ? static_cast<int>(skip_entity->get_team()) : -1;
    filter->ignore_target = ignore_target;
  }

  void trace_ray(struct ray_t* ray, unsigned int f_mask, struct trace_filter* p_trace_filter, struct trace_t* p_trace) {
    void** vtable = *(void ***)this;
    void (*trace_ray_fn)(void*, struct ray_t*, unsigned int, struct trace_filter*, struct trace_t*) =
      (void (*)(void*, struct ray_t*, unsigned int, struct trace_filter*, struct trace_t*))vtable[4];

    trace_ray_fn(this, ray, f_mask, p_trace_filter, p_trace);
  }

  int get_point_contents(const Vec3& point, IHandleEntity** entity = nullptr) {
    void** vtable = *(void***)this;
    auto fn = reinterpret_cast<int (*)(void*, const Vec3&, IHandleEntity**)>(vtable[0]);
    return fn(this, point, entity);
  }

  void enumerate_entities(const Vec3& mins, const Vec3& maxs, IEntityEnumerator* enumerator) {
    void** vtable = *(void***)this;
    auto fn = reinterpret_cast<void (*)(void*, const Vec3&, const Vec3&, IEntityEnumerator*)>(vtable[10]);
    fn(this, mins, maxs, enumerator);
  }

  ICollideable* get_collideable(IHandleEntity* entity) {
    void** vtable = *(void***)this;
    auto fn = reinterpret_cast<ICollideable* (*)(void*, IHandleEntity*)>(vtable[11]);
    return fn(this, entity);
  }

  void get_brushes_in_aabb(const Vec3& mins, const Vec3& maxs, CUtlVector<int>* output, int contents_mask = MASK_ALL) {
    void** vtable = *(void***)this;
    auto fn = reinterpret_cast<void (*)(void*, const Vec3&, const Vec3&, CUtlVector<int>*, int)>(vtable[13]);
    fn(this, mins, maxs, output, contents_mask);
  }

  CPhysCollide* get_collidable_from_displacements_in_aabb(const Vec3& mins, const Vec3& maxs) {
    void** vtable = *(void***)this;
    auto fn = reinterpret_cast<CPhysCollide* (*)(void*, const Vec3&, const Vec3&)>(vtable[14]);
    return fn(this, mins, maxs);
  }

  bool get_brush_info(int brush, CUtlVector<vector4d>* planes_out, int* contents_out) {
    void** vtable = *(void***)this;
    auto fn = reinterpret_cast<bool (*)(void*, int, CUtlVector<vector4d>*, int*)>(vtable[15]);
    return fn(this, brush, planes_out, contents_out);
  }

  void trace_hull(Vec3* start, Vec3* end, Vec3* hull_min, Vec3* hull_max, unsigned int mask, struct trace_t* trace) {
    struct ray_t ray = this->init_ray(start, end, hull_min, hull_max);
    struct trace_filter filter;
    Player* localplayer = entity_list->get_localplayer();
    this->init_trace_filter(&filter, localplayer);

    this->trace_ray(&ray, mask, &filter, trace);
  }

};

inline static EngineTrace* engine_trace;

#endif
