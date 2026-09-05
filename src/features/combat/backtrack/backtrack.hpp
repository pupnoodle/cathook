#ifndef BACKTRACK_HPP
#define BACKTRACK_HPP
#include <array>
#include "core/types.hpp"
#include "games/tf2/sdk/interfaces/model_info.hpp"

class Player;
class Weapon;
struct aimbot_candidate;
struct user_cmd;

namespace backtrack
{

constexpr int max_entities = 65;
constexpr int max_records = 96;
constexpr int max_bones = 128;
constexpr int max_hitboxes = 32;

struct backtrack_bounds {
  bool valid = false;
  Vec3 mins{};
  Vec3 maxs{};
};

struct backtrack_hitbox {
  bool valid = false;
  int bone = -1;
  int hitbox = -1;
  int studio_hitbox = -1;
  int group = 0;
  Vec3 center{};
  Vec3 mins{};
  Vec3 maxs{};
};

struct backtrack_settings {
  bool exact_server_math = true;
  bool snap_origin = true;
};

inline backtrack_settings settings{};

struct backtrack_interp_pose {
  bool valid = false;
  Vec3 origin{};
  float yaw = 0.0f;
  Vec3 mins{};
  Vec3 maxs{};
  float cycle = 0.0f;
};

struct backtrack_record {
  bool valid = false;
  bool invalid = false;
  bool teleport = false;
  bool on_shot = false;
  Player* player = nullptr;
  int ent_index = 0;
  float sim_time = 0.0f;
  float receive_time = 0.0f;

  float sample_gap = 0.0f;
  float valid_delta = 0.0f;
  Vec3 origin{};
  Vec3 mins{};
  Vec3 maxs{};
  Vec3 velocity{};
  int choked_ticks = 0;
  std::array<matrix_3x4, max_bones> bones{};
  int bone_count = 0;
  std::array<backtrack_hitbox, max_hitboxes> hitboxes{};
  int hitbox_count = 0;

  float yaw_angle = 0.0f;
  Vec3 snapped_origin{};
  int sequence = -1;
  float cycle = 0.0f;
  bool dormant = false;
};

struct backtrack_history {
  int ent_index = 0;
  int record_count = 0;
  std::array<backtrack_record, max_records> records{};
};

struct backtrack_timing {
  bool valid = false;
  float outgoing_latency = 0.0f;
  float incoming_latency = 0.0f;
  float fake_latency = 0.0f;
  float fake_interp = 0.0f;
  float correct = 0.0f;
  float window = 0.0f;
  float max_unlag = 1.0f;
  float frame_gap = 0.0f;
  float selection_slack = 0.0f;
  int server_tick = 0;
  int lerp_ticks = 0;
};

struct backtrack_record_view {
  std::array<const backtrack_record*, max_records> records{};
  int count = 0;
};

constexpr float dormant_shoot_seconds = 5.0f;
constexpr float dormant_keep_seconds = 30.0f;
constexpr float dormant_ghost_max_distance_hu = 8192.0f;

void on_create_move(user_cmd* user_cmd);
void record_player(Player* player);
void report_shot(Player* player);
void store();
void clear();

[[nodiscard]] bool is_enabled();
[[nodiscard]] float lerp_seconds();
[[nodiscard]] float correct_seconds();
[[nodiscard]] int rewind_tick_for(int cmd_tick_count);
[[nodiscard]] bool command_tick_for_rewind(float sim_time, int* tick_count);
[[nodiscard]] bool interpolated_pose(const backtrack_record& newer,
  const backtrack_record& older,
  float target_time,
  backtrack_interp_pose* out);
void mark_stale(Player* player);
[[nodiscard]] float fake_latency_seconds();
[[nodiscard]] float interpolation_time();
[[nodiscard]] backtrack_timing current_timing();
[[nodiscard]] bool command_tick_for_current_pose(float simulation_time, int* tick_count);
[[nodiscard]] bool command_tick_for_record(const backtrack_record& record, Player* player, int* tick_count);
[[nodiscard]] bool implicit_rewind_position(Player* player, Vec3* position);
[[nodiscard]] const backtrack_history* records_for_player(Player* player);
[[nodiscard]] backtrack_record_view valid_records(Player* player);
[[nodiscard]] backtrack_record_view visual_records(Player* player);
[[nodiscard]] bool is_record_valid(const backtrack_record& record, Player* player);
[[nodiscard]] bool selected_position(Vec3* position);
void backtrack_to_crosshair(user_cmd* user_cmd, Player* localplayer, Weapon* weapon);
[[nodiscard]] aimbot_candidate find_hitscan_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& original_view_angles,
  bool preferred);
[[nodiscard]] bool dormant_ghost_valid(Player* player);
[[nodiscard]] bool dormant_origin(Player* player, Vec3* out);
[[nodiscard]] bool dormant_can_shoot(Player* player);
[[nodiscard]] Vec3 dormant_extrapolated_origin(const backtrack_record& record);
[[nodiscard]] backtrack_record dormant_extrapolated_record(const backtrack_record& record);
[[nodiscard]] const backtrack_record* latest_dormant_record(Player* player);

void install_net_channel_hook();
void restore_net_channel_hook();

}
#endif
