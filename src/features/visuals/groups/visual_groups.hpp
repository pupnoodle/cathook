#ifndef VISUAL_GROUPS_HPP
#define VISUAL_GROUPS_HPP
#include "features/menu/config.hpp"
#include <memory>

class Entity;
class Player;

namespace visual_groups
{

struct visual_group_snapshot;

struct visual_group_match
{
  std::shared_ptr<const visual_group_snapshot> snapshot{};
  const visual_group* group = nullptr;

  [[nodiscard]] explicit operator bool() const
  {
    return group != nullptr;
  }

  [[nodiscard]] const visual_group& operator*() const
  {
    return *group;
  }

  [[nodiscard]] const visual_group* operator->() const
  {
    return group;
  }
};

void ensure_defaults();
[[nodiscard]] std::uint32_t allocate_group_id();
void store(Player* localplayer);
void begin_fake_angle_model();
void end_fake_angle_model();
void begin_viewmodel_model();
void end_viewmodel_model();
[[nodiscard]] visual_group_match group_for_entity(Entity* entity, bool models = true);
[[nodiscard]] bool groups_active();
[[nodiscard]] bool groups_need_screen_overlay();
[[nodiscard]] bool groups_need_model_effects();
[[nodiscard]] bool groups_need_backtrack_visualizer();
[[nodiscard]] bool groups_need_pickup_timers();
[[nodiscard]] bool groups_need_head_emojis();
void move_group(int from, int to);
[[nodiscard]] RGBA_float color_for_entity(Entity* entity, const visual_group& group);
[[nodiscard]] RGBA_float resolve_color(Entity* entity, const visual_group& group, bool override_enabled, const RGBA_float& override_color);
[[nodiscard]] float alpha_for_entity(Entity* entity, float start, float end, bool smooth_alpha);
[[nodiscard]] const char* label_for_entity(Entity* entity);

}
#endif
