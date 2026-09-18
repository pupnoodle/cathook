#ifndef WORLD_COLLISION_HPP
#define WORLD_COLLISION_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/math/math.hpp"
#include "core/types.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/model_info.hpp"
#include "games/tf2/sdk/interfaces/utl_vector.hpp"
#include "games/tf2/sdk/interfaces/vphysics.hpp"

class IHandleEntity;
class IClientRenderable;
class IPhysicsEnvironment;
class IVPhysicsKeyHandler;

enum SolidType_t {
  SOLID_NONE = 0,
  SOLID_BSP = 1,
  SOLID_BBOX = 2,
  SOLID_OBB = 3,
  SOLID_OBB_YAW = 4,
  SOLID_CUSTOM = 5,
  SOLID_VPHYSICS = 6,
};

constexpr int FSOLID_CUSTOMRAYTEST = 0x0001;
constexpr int FSOLID_CUSTOMBOXTEST = 0x0002;
constexpr int FSOLID_NOT_SOLID = 0x0004;
constexpr int FSOLID_TRIGGER = 0x0008;

enum world_face_type : std::uint8_t {
  face_box_brush = 0,
  face_plane_brush = 1,
  face_displacement = 2,
  face_prop = 3,
  face_entity = 4,
};

struct world_face {
  Vec3 vertices[16]{};
  int vertex_count = 0;
  Vec3 normal{};
  world_face_type type = face_plane_brush;
};

class ICollideable {
public:
  IHandleEntity* get_entity_handle() {
    auto** vtable = *reinterpret_cast<void***>(this);
    auto fn = reinterpret_cast<IHandleEntity* (*)(void*)>(vtable[0]);
    return fn(this);
  }

  const Vec3& obb_mins() const {
    auto** vtable = *reinterpret_cast<void***>(const_cast<ICollideable*>(this));
    auto fn = reinterpret_cast<const Vec3& (*)(const void*)>(vtable[3]);
    return fn(this);
  }

  const Vec3& obb_maxs() const {
    auto** vtable = *reinterpret_cast<void***>(const_cast<ICollideable*>(this));
    auto fn = reinterpret_cast<const Vec3& (*)(const void*)>(vtable[4]);
    return fn(this);
  }

  int get_collision_model_index() {
    auto** vtable = *reinterpret_cast<void***>(this);
    auto fn = reinterpret_cast<int (*)(void*)>(vtable[8]);
    return fn(this);
  }

  const model_t* get_collision_model() {
    auto** vtable = *reinterpret_cast<void***>(this);
    auto fn = reinterpret_cast<const model_t* (*)(void*)>(vtable[9]);
    return fn(this);
  }

  const Vec3& get_collision_origin() const {
    auto** vtable = *reinterpret_cast<void***>(const_cast<ICollideable*>(this));
    auto fn = reinterpret_cast<const Vec3& (*)(const void*)>(vtable[10]);
    return fn(this);
  }

  const Vec3& get_collision_angles() const {
    auto** vtable = *reinterpret_cast<void***>(const_cast<ICollideable*>(this));
    auto fn = reinterpret_cast<const Vec3& (*)(const void*)>(vtable[11]);
    return fn(this);
  }

  const matrix3x4& collision_to_world_transform() const {
    auto** vtable = *reinterpret_cast<void***>(const_cast<ICollideable*>(this));
    auto fn = reinterpret_cast<const matrix3x4& (*)(const void*)>(vtable[12]);
    return fn(this);
  }

  SolidType_t get_solid() const {
    auto** vtable = *reinterpret_cast<void***>(const_cast<ICollideable*>(this));
    auto fn = reinterpret_cast<SolidType_t (*)(const void*)>(vtable[13]);
    return fn(this);
  }

  int get_solid_flags() const {
    auto** vtable = *reinterpret_cast<void***>(const_cast<ICollideable*>(this));
    auto fn = reinterpret_cast<int (*)(const void*)>(vtable[14]);
    return fn(this);
  }

  bool is_solid() const {
    return get_solid() != SOLID_NONE && (get_solid_flags() & FSOLID_NOT_SOLID) == 0;
  }
};

class IStaticPropMgrClient {
public:
  bool is_static_prop(IHandleEntity* handle_entity) const {
    auto** vtable = *reinterpret_cast<void***>(const_cast<IStaticPropMgrClient*>(this));
    auto fn = reinterpret_cast<bool (*)(const void*, IHandleEntity*)>(vtable[2]);
    return fn(this, handle_entity);
  }

  ICollideable* get_static_prop_by_index(int prop_index) {
    auto** vtable = *reinterpret_cast<void***>(this);
    auto fn = reinterpret_cast<ICollideable* (*)(void*, int)>(vtable[4]);
    return fn(this, prop_index);
  }

  void get_all_static_props_in_aabb(const Vec3& mins, const Vec3& maxs, CUtlVector<ICollideable*>* output) {
    auto** vtable = *reinterpret_cast<void***>(this);
    auto fn = reinterpret_cast<void (*)(void*, const Vec3&, const Vec3&, CUtlVector<ICollideable*>*)>(vtable[11]);
    fn(this, mins, maxs, output);
  }
};

class ISpatialPartition {
public:
  void enumerate_elements_in_box(int list_mask, const Vec3& mins, const Vec3& maxs, bool coarse,
                                 void* enumerator) {
    auto** vtable = *reinterpret_cast<void***>(this);
    auto fn = reinterpret_cast<void (*)(void*, int, const Vec3&, const Vec3&, bool, void*)>(vtable[14]);
    fn(this, list_mask, mins, maxs, coarse, enumerator);
  }
};

inline static IStaticPropMgrClient* static_prop_mgr = nullptr;
inline static ISpatialPartition* spatial_partition = nullptr;

namespace world_faces {
namespace detail {

constexpr int k_max_convexes = 64;
constexpr float k_coplanar = 0.999f;

inline Vec3 cross(const Vec3& left, const Vec3& right) {
  return {
    left.y * right.z - left.z * right.y,
    left.z * right.x - left.x * right.z,
    left.x * right.y - left.y * right.x
  };
}

inline Vec3 vector_transform(const Vec3& in, const matrix3x4& matrix) {
  return {
    in.x * matrix.m[0][0] + in.y * matrix.m[0][1] + in.z * matrix.m[0][2] + matrix.m[0][3],
    in.x * matrix.m[1][0] + in.y * matrix.m[1][1] + in.z * matrix.m[1][2] + matrix.m[1][3],
    in.x * matrix.m[2][0] + in.y * matrix.m[2][1] + in.z * matrix.m[2][2] + matrix.m[2][3]
  };
}

inline bool boxes_overlap(const Vec3& min_a, const Vec3& max_a, const Vec3& min_b, const Vec3& max_b) {
  return min_a.x <= max_b.x && max_a.x >= min_b.x && min_a.y <= max_b.y && max_a.y >= min_b.y &&
    min_a.z <= max_b.z && max_a.z >= min_b.z;
}

inline bool sky_face(const Vec3* vertices, int count, const Vec3& normal) {
  if (engine_trace == nullptr || vertices == nullptr || count < 3) {
    return false;
  }
  Vec3 center{};
  for (int index = 0; index < count; ++index) {
    center += vertices[index];
  }
  center = center * (1.0f / static_cast<float>(count));
  const Vec3 unit = normalized(normal);
  Vec3 start = center + unit * 1.0f;
  Vec3 end = center - unit * 4.0f;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_world_trace_filter(&filter);
  trace_t trace{};
  engine_trace->trace_ray(&ray, MASK_SOLID, &filter, &trace);
  return (trace.surface.flags & SURF_SKY) != 0u;
}

inline bool append_face(const std::vector<Vec3>& vertices, const Vec3& normal, world_face_type type,
                        const Vec3& mins, const Vec3& maxs, bool clamp_box, world_face* out, int& count,
                        int capacity) {
  if (out == nullptr || count >= capacity || vertices.size() < 3 || length_squared(normal) <= 0.0001f) {
    return false;
  }
  Vec3 face_min = vertices[0];
  Vec3 face_max = vertices[0];
  for (const Vec3& vertex : vertices) {
    face_min.x = std::min(face_min.x, vertex.x);
    face_min.y = std::min(face_min.y, vertex.y);
    face_min.z = std::min(face_min.z, vertex.z);
    face_max.x = std::max(face_max.x, vertex.x);
    face_max.y = std::max(face_max.y, vertex.y);
    face_max.z = std::max(face_max.z, vertex.z);
  }
  if (!boxes_overlap(face_min, face_max, mins, maxs) ||
      sky_face(vertices.data(), static_cast<int>(vertices.size()), normal)) {
    return false;
  }

  world_face& face = out[count];
  face.vertex_count = std::min(static_cast<int>(vertices.size()), 16);
  face.normal = normalized(normal);
  face.type = type;
  for (int index = 0; index < face.vertex_count; ++index) {
    Vec3 vertex = vertices[static_cast<std::size_t>(index)];
    if (clamp_box) {
      vertex.x = std::clamp(vertex.x, mins.x, maxs.x);
      vertex.y = std::clamp(vertex.y, mins.y, maxs.y);
      vertex.z = std::clamp(vertex.z, mins.z, maxs.z);
    }
    face.vertices[index] = vertex;
  }
  ++count;
  return true;
}

inline void faces_from_convex(CPhysConvex* convex, world_face_type type, const Vec3& mins, const Vec3& maxs,
                              bool clamp_box, world_face* out, int& count, int capacity) {
  if (physics_collision == nullptr || convex == nullptr || out == nullptr || count >= capacity) {
    return;
  }
  CPolyhedron* polyhedron = physics_collision->PolyhedronFromConvex(convex, true);
  if (polyhedron == nullptr || polyhedron->pVertices == nullptr || polyhedron->pPolygons == nullptr ||
      polyhedron->pIndices == nullptr || polyhedron->pLines == nullptr) {
    return;
  }

  struct grouped {
    std::vector<int> indices;
    Vec3 normal{};
  };
  std::vector<grouped> groups;
  groups.reserve(polyhedron->iPolygonCount);
  for (int polygon = 0; polygon < polyhedron->iPolygonCount; ++polygon) {
    const Polyhedron_IndexedPolygon_t& poly = polyhedron->pPolygons[polygon];
    grouped* group = nullptr;
    for (grouped& existing : groups) {
      if (dot(poly.polyNormal, existing.normal) > k_coplanar) {
        group = &existing;
        break;
      }
    }
    if (group == nullptr) {
      groups.push_back({{}, poly.polyNormal});
      group = &groups.back();
    }
    for (int ref = 0; ref < poly.iIndexCount; ++ref) {
      const Polyhedron_IndexedLineReference_t& line_ref =
        polyhedron->pIndices[poly.iFirstIndex + ref];
      const Polyhedron_IndexedLine_t& line = polyhedron->pLines[line_ref.iLineIndex];
      const int vertex = line.iPointIndices[line_ref.iEndPointIndex];
      if (std::find(group->indices.begin(), group->indices.end(), vertex) == group->indices.end()) {
        group->indices.push_back(vertex);
      }
    }
  }

  for (grouped& group : groups) {
    if (group.indices.size() < 3) {
      continue;
    }
    std::vector<Vec3> vertices;
    vertices.reserve(group.indices.size());
    for (int index : group.indices) {
      vertices.push_back(polyhedron->pVertices[index]);
    }
    if (vertices.size() > 3) {
      Vec3 center{};
      for (const Vec3& vertex : vertices) {
        center += vertex;
      }
      center = center * (1.0f / static_cast<float>(vertices.size()));
      const Vec3 dir1 = normalized(vertices.front() - center);
      const Vec3 dir2 = cross(dir1, group.normal);
      std::sort(vertices.begin(), vertices.end(), [&](const Vec3& left, const Vec3& right) {
        const Vec3 a = left - center;
        const Vec3 b = right - center;
        return std::atan2(dot(dir2, a), dot(dir1, a)) < std::atan2(dot(dir2, b), dot(dir1, b));
      });
    }
    append_face(vertices, group.normal, type, mins, maxs, clamp_box, out, count, capacity);
  }
  polyhedron->Release();
}

inline void faces_from_collision(CPhysCollide* collide, world_face_type type, const Vec3& mins,
                                 const Vec3& maxs, bool clamp_box, world_face* out, int& count,
                                 int capacity) {
  if (physics_collision == nullptr || collide == nullptr) {
    return;
  }
  CPhysConvex* convexes[k_max_convexes]{};
  const int used = physics_collision->GetConvexesUsedInCollideable(collide, convexes, k_max_convexes);
  for (int index = 0; index < used && count < capacity; ++index) {
    faces_from_convex(convexes[index], type, mins, maxs, clamp_box, out, count, capacity);
  }
}

inline void faces_from_planes(vector4d* planes, int plane_count, world_face_type type, const Vec3& mins,
                              const Vec3& maxs, bool clamp_box, world_face* out, int& count,
                              int capacity) {
  if (physics_collision == nullptr || planes == nullptr || plane_count < 4) {
    return;
  }
  CPhysConvex* convex =
    physics_collision->ConvexFromPlanes(reinterpret_cast<float*>(planes), plane_count, 0.0f);
  if (convex == nullptr) {
    return;
  }
  faces_from_convex(convex, type, mins, maxs, clamp_box, out, count, capacity);
  physics_collision->ConvexFree(convex);
}

inline void transform_local_faces(const world_face* local, int local_count, ICollideable* collideable,
                                  world_face_type type, const Vec3& mins, const Vec3& maxs,
                                  world_face* out, int& count, int capacity) {
  if (collideable == nullptr) {
    return;
  }
  const Vec3 origin = collideable->get_collision_origin();
  const Vec3 angles = collideable->get_collision_angles();
  const bool rotated = length_squared(angles) > 0.0001f;
  const matrix3x4& transform = collideable->collision_to_world_transform();
  for (int index = 0; index < local_count && count < capacity; ++index) {
    std::vector<Vec3> vertices;
    vertices.reserve(static_cast<std::size_t>(local[index].vertex_count));
    for (int vertex = 0; vertex < local[index].vertex_count; ++vertex) {
      vertices.push_back(rotated ? vector_transform(local[index].vertices[vertex], transform)
                                 : local[index].vertices[vertex] + origin);
    }
    Vec3 normal = local[index].normal;
    if (rotated && vertices.size() >= 3) {
      normal = normalized(cross(vertices[0] - vertices[2], vertices[0] - vertices[1]));
    }
    append_face(vertices, normal, type, mins, maxs, false, out, count, capacity);
  }
}

inline bool skip_entity(Entity* entity) {
  if (entity == nullptr || entity->is_wearable()) {
    return true;
  }
  switch (entity->get_class_id()) {
  case class_id::PLAYER:
  case class_id::ROCKET:
  case class_id::SENTRY_ROCKET:
  case class_id::FLARE:
  case class_id::CROSSBOW_BOLT:
  case class_id::ARROW:
  case class_id::PILL_OR_STICKY:
    return true;
  default:
    return false;
  }
}

struct partition_enumerator {
  void** vtable = nullptr;
  IHandleEntity* entities[256]{};
  int count = 0;
};

inline int enum_element(partition_enumerator* self, IHandleEntity* entity) {
  if (self != nullptr && entity != nullptr && self->count < 256) {
    self->entities[self->count++] = entity;
  }
  return 0;
}

}  // namespace detail

inline int get_faces_in_aabb(const Vec3& mins, const Vec3& maxs, int mask, world_face* out, int capacity) {
  int count = 0;
  if (out == nullptr || capacity <= 0 || engine_trace == nullptr) {
    return 0;
  }

  int brush_storage[2048]{};
  CUtlVector<int> brushes(brush_storage, 2048);
  engine_trace->get_brushes_in_aabb(mins, maxs, &brushes, mask);
  for (int index = 0; index < brushes.Count() && count < capacity; ++index) {
    vector4d plane_storage[64]{};
    CUtlVector<vector4d> planes(plane_storage, 64);
    int contents = 0;
    if (!engine_trace->get_brush_info(brushes[index], &planes, &contents) || planes.Count() < 4) {
      continue;
    }
    bool axis_box = planes.Count() == 6;
    if (axis_box) {
      for (int plane = 0; plane < planes.Count(); ++plane) {
        const Vec3 normal{planes[plane].x, planes[plane].y, planes[plane].z};
        const float axis = std::fabs(normal.x) + std::fabs(normal.y) + std::fabs(normal.z);
        if (std::fabs(axis - 1.0f) > 0.01f) {
          axis_box = false;
          break;
        }
      }
    }
    detail::faces_from_planes(planes.Base(), planes.Count(),
                              axis_box ? face_box_brush : face_plane_brush, mins, maxs, axis_box, out,
                              count, capacity);
  }

  if (CPhysCollide* displacements =
        engine_trace->get_collidable_from_displacements_in_aabb(mins, maxs)) {
    detail::faces_from_collision(displacements, face_displacement, mins, maxs, false, out, count,
                                 capacity);
  }

  if (static_prop_mgr != nullptr && model_info != nullptr) {
    ICollideable* prop_storage[256]{};
    CUtlVector<ICollideable*> props(prop_storage, 256);
    static_prop_mgr->get_all_static_props_in_aabb(mins, maxs, &props);
    for (int index = 0; index < props.Count() && count < capacity; ++index) {
      ICollideable* collideable = props[index];
      if (collideable == nullptr || !collideable->is_solid()) {
        continue;
      }
      vcollide_t* collide = model_info->get_vcollide(collideable->get_collision_model());
      if (collide == nullptr || collide->solids == nullptr) {
        continue;
      }
      world_face local[64]{};
      int local_count = 0;
      for (int solid = 0; solid < collide->solidCount && local_count < 64; ++solid) {
        detail::faces_from_collision(collide->solids[solid], face_prop, Vec3{-1.0e8f, -1.0e8f, -1.0e8f},
                                     Vec3{1.0e8f, 1.0e8f, 1.0e8f}, false, local, local_count, 64);
      }
      detail::transform_local_faces(local, local_count, collideable, face_prop, mins, maxs, out, count,
                                    capacity);
    }
  }

  if (spatial_partition != nullptr && model_info != nullptr) {
    static void* enumerator_vtable[1] = {reinterpret_cast<void*>(detail::enum_element)};
    detail::partition_enumerator enumerator{};
    enumerator.vtable = enumerator_vtable;
    spatial_partition->enumerate_elements_in_box(engine_trace->spatial_partition_mask(), mins, maxs,
                                                 false, &enumerator);
    for (int index = 0; index < enumerator.count && count < capacity; ++index) {
      IHandleEntity* handle = enumerator.entities[index];
      if (handle == nullptr || (static_prop_mgr != nullptr && static_prop_mgr->is_static_prop(handle))) {
        continue;
      }
      Entity* entity = reinterpret_cast<Entity*>(handle);
      if (detail::skip_entity(entity)) {
        continue;
      }
      ICollideable* collideable = reinterpret_cast<ICollideable*>(entity->get_collideable());
      if (collideable == nullptr || !collideable->is_solid()) {
        continue;
      }
      vcollide_t* collide = model_info->get_vcollide(collideable->get_collision_model());
      if (collide == nullptr || collide->solids == nullptr) {
        continue;
      }
      world_face local[64]{};
      int local_count = 0;
      for (int solid = 0; solid < collide->solidCount && local_count < 64; ++solid) {
        detail::faces_from_collision(collide->solids[solid], face_entity,
                                     Vec3{-1.0e8f, -1.0e8f, -1.0e8f},
                                     Vec3{1.0e8f, 1.0e8f, 1.0e8f}, false, local, local_count, 64);
      }
      detail::transform_local_faces(local, local_count, collideable, face_entity, mins, maxs, out,
                                    count, capacity);
    }
  }

  return count;
}

}  // namespace world_faces

#endif
