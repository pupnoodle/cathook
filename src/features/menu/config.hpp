/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/menu/config.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef CONFIG_HPP
#define CONFIG_HPP
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_timer.h>
#include <cstdint>
#include <string>
#include <vector>
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/aim_hitboxes.hpp"
#include "core/types.hpp"
#include "features/automation/nographics/nographics.hpp"

struct Aim {
  enum class TargetType {
    FOV,
    DISTANCE,
    LEAST_HEALTH,
    MOST_HEALTH
  };

  enum aim_at_flags : uint32_t {
    aim_at_enemies = 1u << 0,
    aim_at_sentries = 1u << 1,
    aim_at_dispensers = 1u << 2,
    aim_at_teleporters = 1u << 3,
    aim_at_mvm_robots = 1u << 4,
    aim_at_npcs = 1u << 5,
    aim_at_stickies = 1u << 6,
    aim_at_bombs = 1u << 7,
    aim_at_buildings = aim_at_sentries | aim_at_dispensers | aim_at_teleporters,
    aim_at_default = aim_at_enemies | aim_at_mvm_robots,
    aim_at_all = aim_at_enemies | aim_at_buildings | aim_at_mvm_robots | aim_at_npcs |
      aim_at_stickies | aim_at_bombs
  };

  enum class AimMode {
    PLAIN,
    SMOOTH,
    ASSISTIVE,
    PSILENT,
    SNAP
  };

  enum class ProjectilePredictionMode {
    EXTRAPOLATION = 0,
    MOVE_SIM = 1
  };

  enum projectile_modifier_flags : uint32_t {
    projectile_mod_charge_weapon = 1u << 0,
    projectile_mod_cancel_charge = 1u << 1,
    projectile_mod_all = projectile_mod_charge_weapon | projectile_mod_cancel_charge
  };

  enum ignore_player_flags : uint32_t {
    ignore_friends = 1u << 0,
    ignore_ipc_bots = 1u << 1,
    ignore_cloaked = 1u << 2,
    ignore_invulnerable = 1u << 3,
    ignore_party = 1u << 4,
    ignore_unprioritized = 1u << 5,
    ignore_invisible_players = 1u << 6,
    ignore_dead_ringer = 1u << 7,
    ignore_vaccinator = 1u << 8,
    ignore_disguised = 1u << 9,
    ignore_taunting = 1u << 10,
    ignore_team = 1u << 11,
    ignore_sentry_busters = 1u << 12,
    ignore_unsimulated = 1u << 13,
    ignore_default = ignore_friends | ignore_ipc_bots | ignore_invulnerable,
    ignore_all = ignore_friends | ignore_ipc_bots | ignore_cloaked | ignore_invulnerable |
      ignore_party | ignore_unprioritized | ignore_invisible_players | ignore_dead_ringer |
      ignore_vaccinator | ignore_disguised | ignore_taunting | ignore_team |
      ignore_sentry_busters | ignore_unsimulated
  };

  enum hitscan_modifier_flags : uint32_t {
    hitscan_mod_wait_for_headshot = 1u << 0,
    hitscan_mod_wait_for_charge   = 1u << 1,
    hitscan_mod_body_aim_if_lethal = 1u << 2,
    hitscan_mod_scoped_only       = 1u << 3,
    hitscan_mod_tapfire           = 1u << 4,
    hitscan_mod_auto_scope        = 1u << 5,
    hitscan_mod_auto_rev          = 1u << 6,
    hitscan_mod_extinguish_team   = 1u << 7,
    hitscan_mod_prefer_medics     = 1u << 8,
    hitscan_mod_headshot_only     = 1u << 9,
    hitscan_mod_default = hitscan_mod_body_aim_if_lethal,
    hitscan_mod_all = hitscan_mod_wait_for_headshot |
      hitscan_mod_wait_for_charge | hitscan_mod_body_aim_if_lethal |
      hitscan_mod_scoped_only | hitscan_mod_tapfire | hitscan_mod_auto_scope |
      hitscan_mod_auto_rev | hitscan_mod_extinguish_team | hitscan_mod_prefer_medics |
      hitscan_mod_headshot_only
  };

  bool master = true;

  bool auto_shoot = true;
  TargetType target_type = TargetType::FOV;
  uint32_t aim_at = aim_at_default;
  AimMode aim_mode = AimMode::PSILENT;

  float fov = 45;
  float assist_strength = 25.0f;
  bool draw_fov = false;
  bool shoot_through_glass = false;
  bool spread_compensation = true;
  bool resolver = true;
  int resolver_max_yaws = 12;
  bool debug_overlay = false;
  float debug_overlay_x = 24.0f;
  float debug_overlay_y = 326.0f;

  uint32_t hitscan_hitboxes = aim_hitbox_mask_default_hitscan;
  uint32_t melee_hitboxes = aim_hitbox_mask_default_melee;
  bool melee_walk_to_target = true;
  bool melee_auto_backstab = true;
  bool melee_ignore_razorback = true;

  bool melee_swing_prediction = true;
  int melee_swing_ticks = 13;
  bool melee_swing_predict_lag = true;
  int melee_swing_validate_mode = 0;
  bool sniper_auto_scope = true;
  float sniper_scope_cancel_time = 3.0f;
  bool auto_rev = false;
  bool auto_unrev = false;
  float auto_rev_threshold = 450.0f;
  uint32_t hitscan_modifiers = hitscan_mod_default;
  float tapfire_distance = 1000.0f;
  float ignore_invisible = 50.0f;
  int ignore_unsimulated_ticks = 4;
  float multipoint_scale = 75.0f;
  float bone_size_subtract = 1.0f;
  float bone_size_min_scale = 0.4f;

  bool projectile_active = true;
  uint32_t projectile_modifiers = projectile_mod_all;
  int projectile_mode = 0;
  ProjectilePredictionMode projectile_prediction_mode = ProjectilePredictionMode::EXTRAPOLATION;
  int projectile_aim_pos = 0;
  float projectile_fov = 30.0f;
  float projectile_snap_time = 0.08f;
  bool projectile_snap_smooth = false;
  float projectile_snap_smooth_start = 20.0f;
  float projectile_snap_smooth_end = 10.0f;
  int projectile_max_sim_targets = 1;
  float projectile_max_sim_time = 2.0f;
  int projectile_multipoint_scale = 70;
  int projectile_splash_policy = 1;
  bool projectile_smooth_flamethrowers_active = false;
  float projectile_smooth_flamethrowers = 25.0f;

  uint32_t ignore = ignore_default;

  int max_targets = 6;
};

struct backtrack_config {
  enum class visualizer_style {
    points = 0,
    boxes,
    projected_boxes,
    trail,
    pulse
  };

  bool enabled = true;
  float fake_latency_ms = 0.0f;
  float interp_ms = 0.0f;
  bool fake_interp = false;
  int window_ms = 200;
  bool prefer_on_shot = false;
  bool to_crosshair = false;
  int offset_ticks = 0;
  bool visualizer = false;
  int visualizer_ticks = 16;
  visualizer_style visualizer_mode = visualizer_style::projected_boxes;
};

struct ipc_config {
  bool enabled = true;
  bool auto_connect = true;
  bool auto_ignore_local_bots = true;
};

enum class esp_box_type {
  outline = 0,
  corner,
  filled,
  rounded,
  projected
};

enum class mafia_level_position {
  under_name = 0,
  left,
  right
};

struct group_esp_settings {
  enum draw_flags : uint32_t {
    name = 1u << 0,
    name_background = 1u << 1,
    box = 1u << 2,
    distance = 1u << 3,
    bones = 1u << 4,
    health_bar = 1u << 5,
    health_text = 1u << 6,
    class_icon = 1u << 7,
    class_text = 1u << 8,
    weapon_text = 1u << 9,
    priority = 1u << 10,
    flags = 1u << 11,
    ping = 1u << 12,
    kdr = 1u << 13,
    mafia_level = 1u << 14,
    owner = 1u << 15,
    level = 1u << 16,
    ammo_text = 1u << 17,
    intel_return_time = 1u << 18,
    head_emoji = 1u << 19,
    uber = 1u << 20,
    uber_bar = 1u << 21,
    tags = 1u << 22,
    steamid = 1u << 23,
    conditions = 1u << 24,
    latency = 1u << 25,
    weapon_icon = 1u << 26,
    labels = 1u << 27,
    buffs = 1u << 28,
    debuffs = 1u << 29,
    lag_compensation = 1u << 30,
    ammo_bar = 1u << 31
  };

  uint32_t draw_mask = name | box | health_bar;
  bool override_color = false;
  RGBA_float color = {.r = 1.0f, .g = 0.501960784f, .b = 0.0f, .a = 1.0f};
  esp_box_type box_style = esp_box_type::corner;
  float start = 0.0f;
  float end = 8192.0f;
  bool smooth_alpha = false;
  uint8_t background_alpha = 180;
  float class_icon_scale = 2.0f;
  float head_emoji_scale = 2.0f;
  int head_emoji_style = 0;
  ::mafia_level_position mafia_level_position = ::mafia_level_position::under_name;
};

struct chams_layer {
  std::string material = "Original";
  RGBA_float color{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f};
  float start = 0.0f;
  float end = 8192.0f;
  bool smooth_alpha = false;

  bool operator==(const chams_layer& other) const {
    return material == other.material && color.r == other.color.r && color.g == other.color.g &&
      color.b == other.color.b && color.a == other.color.a && color.rainbow == other.color.rainbow && start == other.start &&
      end == other.end && smooth_alpha == other.smooth_alpha;
  }
};

struct chams_settings {
  std::vector<chams_layer> visible{chams_layer{}};
  std::vector<chams_layer> occluded{};

  [[nodiscard]] bool active() const {
    return visible != std::vector<chams_layer>{chams_layer{}} || !occluded.empty();
  }
};

struct glow_settings {
  RGBA_float color{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f};
  int stencil = 0;
  float blur = 0.0f;
  float start = 0.0f;
  float end = 8192.0f;
  bool smooth_alpha = true;

  [[nodiscard]] bool active() const {
    return stencil > 0 || blur > 0.0f;
  }

  bool operator==(const glow_settings& other) const {
    return color.r == other.color.r && color.g == other.color.g && color.b == other.color.b &&
      color.a == other.color.a && color.rainbow == other.color.rainbow && stencil == other.stencil && blur == other.blur &&
      start == other.start && end == other.end && smooth_alpha == other.smooth_alpha;
  }
};

struct visual_group_backtrack_settings {
  bool enabled = false;
  chams_settings chams{};
  glow_settings glow{};
  int record_mode = 0;
  bool ignore_z = false;

  [[nodiscard]] bool active() const {
    return enabled && (chams.active() || glow.active());
  }
};

struct visual_group {
  enum target_flags : uint32_t {
    target_players = 1u << 0,
    target_buildings = 1u << 1,
    target_projectiles = 1u << 2,
    target_ragdolls = 1u << 3,
    target_objective = 1u << 4,
    target_npcs = 1u << 5,
    target_health = 1u << 6,
    target_ammo = 1u << 7,
    target_money = 1u << 8,
    target_powerups = 1u << 9,
    target_spellbook = 1u << 10,
    target_bombs = 1u << 11,
    target_gargoyle = 1u << 12,
    target_fake_angle = 1u << 13,
    target_viewmodel_weapon = 1u << 14,
    target_viewmodel_hands = 1u << 15
  };

  enum condition_flags : uint32_t {
    condition_enemy = 1u << 0,
    condition_team = 1u << 1,
    condition_blu = 1u << 2,
    condition_red = 1u << 3,
    condition_local = 1u << 4,
    condition_friends = 1u << 5,
    condition_party = 1u << 6,
    condition_priority = 1u << 7,
    condition_target = 1u << 8,
    condition_dormant = 1u << 9,
    condition_cat = 1u << 10,
    condition_ignored = 1u << 11
  };

  enum player_flags : uint32_t {
    player_scout = 1u << 0,
    player_soldier = 1u << 1,
    player_pyro = 1u << 2,
    player_demoman = 1u << 3,
    player_heavy = 1u << 4,
    player_engineer = 1u << 5,
    player_medic = 1u << 6,
    player_sniper = 1u << 7,
    player_spy = 1u << 8,
    player_invulnerable = 1u << 9,
    player_crits = 1u << 10,
    player_invisible = 1u << 11,
    player_disguise = 1u << 12,
    player_hurt = 1u << 13,
    player_not_invisible = 1u << 14,
    player_classes = player_scout | player_soldier | player_pyro | player_demoman | player_heavy | player_engineer | player_medic | player_sniper | player_spy,
    player_conditions = player_invulnerable | player_crits | player_invisible | player_disguise | player_hurt | player_not_invisible
  };

  enum building_flags : uint32_t {
    building_sentry = 1u << 0,
    building_dispenser = 1u << 1,
    building_teleporter = 1u << 2,
    building_hurt = 1u << 3,
    building_classes = building_sentry | building_dispenser | building_teleporter,
    building_conditions = building_hurt
  };

  enum projectile_flags : uint32_t {
    projectile_rocket = 1u << 0,
    projectile_sticky = 1u << 1,
    projectile_pipe = 1u << 2,
    projectile_arrow = 1u << 3,
    projectile_heal = 1u << 4,
    projectile_flare = 1u << 5,
    projectile_fire = 1u << 6,
    projectile_repair = 1u << 7,
    projectile_cleaver = 1u << 8,
    projectile_milk = 1u << 9,
    projectile_jarate = 1u << 10,
    projectile_gas = 1u << 11,
    projectile_bauble = 1u << 12,
    projectile_baseball = 1u << 13,
    projectile_energy = 1u << 14,
    projectile_short_circuit = 1u << 15,
    projectile_meteor_shower = 1u << 16,
    projectile_lightning = 1u << 17,
    projectile_fireball = 1u << 18,
    projectile_bomb = 1u << 19,
    projectile_bats = 1u << 20,
    projectile_pumpkin = 1u << 21,
    projectile_monoculus = 1u << 22,
    projectile_skeleton = 1u << 23,
    projectile_misc = 1u << 24,
    projectile_crit = 1u << 25,
    projectile_minicrit = 1u << 26,
    projectile_classes = projectile_rocket | projectile_sticky | projectile_pipe | projectile_arrow | projectile_heal | projectile_flare | projectile_fire | projectile_repair | projectile_cleaver | projectile_milk | projectile_jarate | projectile_gas | projectile_bauble | projectile_baseball | projectile_energy | projectile_short_circuit | projectile_meteor_shower | projectile_lightning | projectile_fireball | projectile_bomb | projectile_bats | projectile_pumpkin | projectile_monoculus | projectile_skeleton | projectile_misc,
    projectile_conditions = projectile_crit | projectile_minicrit
  };

  enum backtrack_flags : uint32_t {
    backtrack_enabled = 1u << 0,
    backtrack_ignore_z = 1u << 1,
    backtrack_last = 1u << 2,
    backtrack_first = 1u << 3,
    backtrack_always = 1u << 4
  };

  enum trajectory_flags : uint32_t {
    trajectory_enabled = 1u << 0,
    trajectory_ignore_z = 1u << 1,
    trajectory_predict = 1u << 2,
    trajectory_radius = 1u << 3,
    trajectory_trace = 1u << 4,
    trajectory_sphere = 1u << 5,
    trajectory_path = 1u << 6
  };

  enum sightline_flags : uint32_t {
    sightline_enabled = 1u << 0,
    sightline_ignore_z = 1u << 1
  };

  std::string name = "Group";
  RGBA_float color = {.r = 1.0f, .g = 0.501960784f, .b = 0.0f, .a = 1.0f};
  bool tags_override_color = true;
  uint32_t targets = 0;
  uint32_t conditions = condition_enemy | condition_team | condition_blu | condition_red;

  std::vector<int> roles{};
  uint32_t players = 0;
  uint32_t buildings = 0;
  uint32_t projectiles = 0;
  group_esp_settings esp{};
  chams_settings chams{};
  glow_settings glow{};
  visual_group_backtrack_settings backtrack_visuals{};
  bool offscreen_arrows = false;
  int offscreen_arrows_offset = 100;
  float offscreen_arrows_max_distance = 1000.0f;
  bool pickup_timer = false;
  uint32_t backtrack = 0;
  uint32_t trajectory = 0;
  uint32_t sightlines = sightline_ignore_z;
};

struct visual_group_config {
  static constexpr std::size_t max_groups = 32;

  uint32_t active_group_mask = 0;
  std::vector<visual_group> groups{};
};

struct Visuals {
  struct CasualMedal {
    bool guaranteed_flip = false;
    bool changer = false;
    int rank = 1;
  } casual_medal;

  struct Indicators {
    enum item_flags : uint32_t {
      legacy_ticks = 1u << 0,
      spectators = 1u << 2,
      keybinds = 1u << 3,
      tickbase = 1u << 4,
      crit_hack = 1u << 5,
      nospread = 1u << 6
    };

    uint32_t enabled_mask = spectators | keybinds | tickbase | crit_hack | nospread;
    float x = 24.0f;
    float y = 140.0f;
    float legacy_ticks_x = 24.0f;
    float legacy_ticks_y = 140.0f;
    float keybinds_x = 24.0f;
    float keybinds_y = 232.0f;
    float crit_hack_x = 24.0f;
    float crit_hack_y = 320.0f;
    float nospread_x = 24.0f;
    float nospread_y = 356.0f;
    RGBA_float tickbase_bar_color = {.r = 0.39215687f, .g = 0.8627451f, .b = 0.50980395f, .a = 1.0f};
    RGBA_float crit_hack_bar_color = {.r = 0.39215687f, .g = 0.8627451f, .b = 0.50980395f, .a = 1.0f};
  } indicators;

  struct Removals {
    bool scope = false;
    bool zoom = false;
  } removals;

  struct Thirdperson {
    bool enabled = false;
    bool crosshair = false;
    float distance = 150.0f;
    float right = 0.0f;
    float up = 0.0f;
    bool scale = true;
    bool collision = true;
  } thirdperson;

  struct Hitmarker {
    bool enabled = true;
    bool damage_text = true;
    float duration = 0.80f;
    float size = 8.0f;
    RGBA_float color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f};
    RGBA_float crit_color = {.r = 1.0f, .g = 0.57f, .b = 0.14f, .a = 1.0f};
    RGBA_float headshot_color = {.r = 0.35f, .g = 0.82f, .b = 1.0f, .a = 1.0f};
  } hitmarker;

  struct SpectatorList {
    bool enabled = true;
    bool show_target = true;
    bool show_modes = true;
    bool highlight_firstperson = true;
    float x = 24.0f;
    float y = 240.0f;
    RGBA_float firstperson_color = {.r = 0.95f, .g = 0.82f, .b = 0.24f, .a = 1.0f};
  } spectator_list;

  struct Radar {
    bool enabled = false;

    float x = 100.0f;
    float y = 100.0f;

    int size = 300;

    float zoom = 10.0f;
    int icon_size = 20;
    bool show_teammates = true;
    bool show_enemies = true;

    bool use_icons = true;

    bool axis_lines = true;

    int range_rings = 2;
  } radar;

  bool override_fov = false;
  float custom_fov = 90;

  bool override_zoom_fov = false;
  float custom_zoom_fov = 20;
  bool flat_zoom_sensitivity = false;

  bool override_viewmodel_fov = false;
  float custom_viewmodel_fov = 70;
  bool esp_lerp = false;
  bool dormant_esp = false;
};

struct Misc {

  struct InventorySlot {
    int item = 0;
    int paintkit = 0;
    int wear = 0;
    int seed = 0;
    int style = 0;
    int sheen = 0;
    int killstreak = 0;
    int unusual = 0;
  };

  struct InventoryChanger {
    bool enabled = false;
    bool apply_to_all = false;
    bool debug = false;

    std::string redirects;

    std::string boxes;

    std::string attributes;
    InventorySlot primary;
    InventorySlot secondary;
    InventorySlot melee;
    InventorySlot hat1;
    InventorySlot hat2;
    InventorySlot hat3;
    int taunt1_unusual = 0;
    int crate = 0;
    int key = 0;
  } inventory_changer;

  struct Movement {
    enum class auto_strafe_mode {
      OFF = 0,
      LEGIT,
      DIRECTIONAL
    };

    enum class auto_edgebug_mode {
      OFF = 0,
      LEGIT,
      STRAFE,
      STRAFE_SILENT
    };

    bool bhop = true;
    auto_strafe_mode auto_strafe = auto_strafe_mode::DIRECTIONAL;
    float auto_strafe_turn_scale = 0.5f;
    float auto_strafe_max_delta = 180.0f;
    bool edge_jump = false;
    bool jumpbug = false;
    bool duck_jump = false;
    bool break_jump = false;
    bool auto_reverse_jump = false;
    bool fast_stop = false;
    bool fast_accelerate = false;
    auto_edgebug_mode auto_edgebug = auto_edgebug_mode::OFF;
    bool no_push = false;
    bool taunt_slide = false;
    bool moonwalk = false;
    bool moonwalk_forward = false;
    bool moonwalk_navbot_compat = false;
  } movement;

  struct Exploits {
    enum class anti_aim_pitch_mode {
      off = 0,
      up,
      down,
      zero,
      half_up,
      half_down,
      jitter,
      random
    };

    enum class anti_aim_yaw_base {
      view = 0,
      target
    };

    enum class anti_aim_yaw_mode {
      off = 0,
      forward,
      left,
      right,
      backwards,
      jitter,
      spin,
      random,
      sideways
    };

    bool bypasspure = true;
    bool pure_bypass = true;
    bool cheats_bypass = false;
    bool vac_bypass = false;
    bool network_fix = false;
    bool tickbase = false;
    bool tickbase_recharge = true;
    bool doubletap = true;
    int doubletap_ticks = 14;
    bool warp = false;
    int warp_ticks = 14;
    bool fakelag = false;
    int fakelag_ticks = 6;
    bool anti_aim = false;
    anti_aim_pitch_mode anti_aim_real_pitch = anti_aim_pitch_mode::off;
    anti_aim_pitch_mode anti_aim_fake_pitch = anti_aim_pitch_mode::off;
    anti_aim_yaw_base anti_aim_real_yaw_base = anti_aim_yaw_base::view;
    anti_aim_yaw_base anti_aim_fake_yaw_base = anti_aim_yaw_base::view;
    anti_aim_yaw_mode anti_aim_real_yaw = anti_aim_yaw_mode::backwards;
    anti_aim_yaw_mode anti_aim_fake_yaw = anti_aim_yaw_mode::forward;
    float anti_aim_real_yaw_offset = 0.0f;
    float anti_aim_fake_yaw_offset = 0.0f;
    float anti_aim_spin_speed = 30.0f;
    bool anti_aim_anti_overlap = true;
    bool antiwarp = true;
    bool equip_region_unlock = false;
    bool ping_reducer = false;
    int ping_target = 60;
    bool no_engine_sleep = false;
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

    bool null_graphics = true;
#else

    bool null_graphics = false;
#endif

    bool legacy_tickbase_indicator = true;
    bool keybind_indicator = true;
    float legacy_tickbase_indicator_x = 24.0f;
    float legacy_tickbase_indicator_y = 140.0f;
    float keybind_indicator_x = 24.0f;
    float keybind_indicator_y = 186.0f;
  } exploits;

  struct Automation {
    enum medic_heal_target_flags : uint32_t {
      medic_heal_target_friends = 1u << 0,
      medic_heal_target_ignored = 1u << 1,
      medic_heal_target_ipc_bots = 1u << 2,
      medic_heal_target_default = medic_heal_target_friends | medic_heal_target_ignored | medic_heal_target_ipc_bots
    };

    enum class chatspam_source {
      OFF = 0,
      CATHOOK,
      LMAOBOX,
      CUSTOM
    };

    enum class killsay_mode {
      OFF = 0,
      CATHOOK,
      MLG,
      CUSTOM
    };

    enum class requeue_action_mode {
      QUEUE_ONLY = 0,
      LEAVE_AND_REQUEUE = 1
    };

    enum class queueing_mode {
      NORMAL = 0,
      BOOST = 1
    };

    enum class boost_queue_mode {
      WAIT = 0,
      INSTANT = 1
    };

    enum class navbot_enemy_stalk_mode {
      DEFAULT = 0,
      YOLO = 1
    };
    enum class navbot_mode {
      DEFAULT = 0,
      COMPLETE_MVM_SNIPER = 1
    };

    enum followbot_target_flags : uint32_t {
      followbot_teammates = 1u << 0,
      followbot_enemies = 1u << 1
    };

    enum class followbot_prefer {
      OFF = 0,
      FRIENDS = 1,
      PARTY = 2
    };

    enum class followbot_nav_mode {
      OFF = 0,
      NORMAL = 1,
      DORMANT = 2
    };

    enum class followbot_look_mode {
      OFF = 0,
      PATH = 1,
      COPY_TARGET = 2,
      AT_TARGET = 3
    };

    enum class voice_command_spam_mode {
      off = 0,
      random,
      medic,
      thanks,
      nice_shot,
      cheers,
      jeers,
      go_go_go,
      move_up,
      go_left,
      go_right,
      yes,
      no,
      incoming,
      spy,
      sentry,
      need_teleporter,
      pootis,
      need_sentry,
      activate_charge,
      help,
      battle_cry
    };

    bool auto_class_select = false;
    enum tf_class class_selected = tf_class::SNIPER;
    bool auto_class_dont_join_during_warmup = false;
    bool anti_afk = false;
    bool anti_autobalance = false;
    bool anti_motd = false;
    bool anti_motd_dont_close_during_warmup = false;
    bool auto_report = false;
    bool auto_vote_map = false;
    int auto_vote_map_option = 2;
    bool noisemaker_spam = false;
    voice_command_spam_mode voice_command_spam = voice_command_spam_mode::off;
    bool micspam = false;
    int micspam_interval_on_seconds = 3;
    int micspam_interval_off_seconds = 60;
    bool micspam_from_file = false;
    bool auto_item = false;
    int auto_item_interval_ms = 30000;
    bool auto_item_weapons = false;
    std::string auto_item_primary = "-1";
    std::string auto_item_secondary = "-1";
    std::string auto_item_melee = "-1";
    bool auto_item_hats = false;
    std::string auto_item_hat1 = "940";
    std::string auto_item_hat2 = "941";
    std::string auto_item_hat3 = "302";
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

    bool auto_item_noisemaker = true;
#else

    bool auto_item_noisemaker = false;
#endif

    bool auto_item_debug = false;
    bool autotaunt = false;
    float autotaunt_chance = 100.0f;
    float autotaunt_safety_distance = 1000.0f;
    int autotaunt_weapon_slot = 0;
    chatspam_source chatspam = chatspam_source::OFF;
    bool chatspam_random = false;
    bool chatspam_team = false;
    int chatspam_delay_ms = 800;
    std::string chatspam_file = "spam.txt";
    killsay_mode killsay = killsay_mode::OFF;
    int killsay_delay_ms = 100;
    std::string killsay_file = "killsays.txt";
    bool custom_announcer = false;
    bool mvm_instant_respawn = false;
    bool mvm_instant_revive = false;
    bool allow_mvm_inspect = false;
    bool auto_mvm_ready_up = false;
    bool auto_mvm_abandon_mannup = false;
    bool mvm_buybot = false;
    int mvm_buybot_max_cash = 15000;
    bool mvm_buybot_auto_class = false;
    tf_class mvm_buybot_class = tf_class::HEAVYWEAPONS;
    bool medic_autoheal = false;
    bool medic_autovacc = false;
    bool medic_autouber = false;
    uint32_t medic_heal_targets_mask = medic_heal_target_default;
    bool auto_queue = false;
    bool auto_requeue = false;
    bool requeue_on_kick = false;
    queueing_mode queue_mode = queueing_mode::NORMAL;
    bool boost_queue_enabled = false;
    boost_queue_mode boost_queue = boost_queue_mode::WAIT;
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

    bool auto_casual_join = true;
#else

    bool auto_casual_join = false;
#endif

    int auto_queue_mode = 7;
    int rq_if_players_lte = 0;
    int rq_if_players_gte = 0;
    int rq_if_ipc_bots_gt = 0;
    bool rq_if_no_navmesh = false;
    bool rq_ignore_friends = true;
    requeue_action_mode requeue_action = requeue_action_mode::QUEUE_ONLY;
    bool region_selector = false;

    std::uint64_t region_selector_allowed_mask = 0xe0995ff6c3feull;
    bool navbot_enabled = false;
    navbot_mode navbot_behavior = navbot_mode::DEFAULT;
    bool navbot_draw_path = true;
    bool navbot_draw_path_boxes = true;
    RGBA_float navbot_path_color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f};
    bool navbot_dont_path_during_warmup = false;
    bool navbot_look_at_path = false;
    bool navbot_look_at_path_silent = false;
    bool navbot_auto_weapon = true;
    navbot_enemy_stalk_mode enemy_stalk_mode = navbot_enemy_stalk_mode::DEFAULT;
    float navbot_melee_target_range = 600.0f;
    int navbot_look_at_path_speed = 5;
    int navbot_look_at_path_spin_chance = 25;
    float navbot_crumb_blacklist_seconds = 50.0f;
    bool navbot_debug_text = true;
    uint32_t navbot_excluded_jobs_mask = 0;
    bool followbot_enabled = false;
    followbot_nav_mode followbot_use_nav = followbot_nav_mode::OFF;
    uint32_t followbot_targets = followbot_teammates;
    followbot_prefer followbot_preference = followbot_prefer::OFF;
    followbot_look_mode followbot_look = followbot_look_mode::OFF;
    bool followbot_look_no_snap = false;
    bool followbot_ignore_afk = true;
    int followbot_min_priority = 0;
    int followbot_max_nodes = 300;
    float followbot_activation_distance = 300.0f;
    float followbot_follow_distance = 60.0f;
    float followbot_abandon_distance = 1500.0f;
    float followbot_nav_abandon_distance = 1500.0f;
  } automation;

  struct Menu {
    bool enabled = true;
    std::string text = "I Use Arch BTW!!!";
    bool use_custom_font = false;
    std::string custom_font = "Verdana.ttf";
    int dpi_scale = 2;
    RGBA_float theme_color = {.r = 0.267f, .g = 0.392f, .b = 0.596f, .a = 1.0f};
  } menu;
};

struct Debug {
  int font_height = 14;
  int font_weight = 400;
  bool debug_render_all_entities = false;
  bool show_active_flag_ids_of_players = false;
  bool insider_settings_unlocked = false;
};

struct crit_hack_config {
  bool enabled = false;
  bool force_crits = true;
  bool always_melee = false;
  bool avoid_random = false;
};

struct Config {
  Aim aimbot;
  backtrack_config backtrack;
  ipc_config ipc;
  visual_group_config visual_groups;
  Visuals visuals;
  Misc misc;
  Debug debug;
  crit_hack_config crithack;
};
#if defined(__GNUC__) || defined(__clang__)
#define cathook_EARLY_INIT __attribute__((init_priority(101)))
#else
#define cathook_EARLY_INIT
#endif

inline static Config config cathook_EARLY_INIT;
#undef cathook_EARLY_INIT

inline void enforce_insider_settings_lock(Config& cfg)
{
  if (cfg.debug.insider_settings_unlocked) {
    return;
  }

}

inline void reset_insider_settings_session(Config& cfg)
{
  cfg.debug.insider_settings_unlocked = false;
  enforce_insider_settings_lock(cfg);
}

static std::string get_button_name(const int button_code) {
  if (button_code == SDLK_UNKNOWN) {
    return "Not bound";
  }

  if (button_code >= 0) {
    const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(button_code)));
    return name != nullptr && name[0] != '\0' ? std::string(name) : "Unknown";
  }

  switch (-button_code) {
  case SDL_BUTTON_LEFT:
    return "Mouse Left";
  case SDL_BUTTON_RIGHT:
    return "Mouse Right";
  case SDL_BUTTON_MIDDLE:
    return "Mouse Middle";
  case SDL_BUTTON_X1:
    return "Mouse X1";
  case SDL_BUTTON_X2:
    return "Mouse X2";
  default:
    return "Mouse Button " + std::to_string(-button_code);
  }
}

static constexpr bool textmode_binds_disabled() {
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

  return true;
#else

  return false;
#endif

}

inline bool are_binds_disabled() {
  if constexpr (textmode_binds_disabled()) {
    return true;
  }

  return nographics::is_enabled();
}
#endif
