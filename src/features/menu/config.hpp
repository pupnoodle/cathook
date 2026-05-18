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

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/aim_hitboxes.hpp"

#include "core/types.hpp"

struct button {
  enum class mode_type {
    HOLD = 0,
    TOGGLE,
    DOUBLE_CLICK
  };

  int button;
  mode_type mode = mode_type::HOLD;
  bool waiting = false;
  bool active = false;
  bool was_down = false;
  float last_press_time = 0.0f;
};

struct Aim {
  enum class TargetType {
    FOV,
    DISTANCE,
    LEAST_HEALTH,
    MOST_HEALTH
  };

  enum aim_at_flags : uint32_t {
    aim_at_enemies = 1u << 0,
    aim_at_buildings = 1u << 1,
    aim_at_mvm_robots = 1u << 2,
    aim_at_pumpkins = 1u << 3,
    aim_at_stickies = 1u << 4,
    aim_at_default = aim_at_enemies | aim_at_mvm_robots,
    aim_at_all = aim_at_enemies | aim_at_buildings | aim_at_mvm_robots | aim_at_pumpkins | aim_at_stickies
  };

  enum class AimMode {
    PLAIN,
    SMOOTH,
    ASSISTIVE,
    PSILENT
  };

  enum class ProjectileMode {
    DIRECT_ONLY,
    DIRECT_THEN_SPLASH,
    PREFER_SPLASH,
    SPLASH_ONLY
  };

  bool master = true;

  bool auto_shoot = true;
  TargetType target_type = TargetType::FOV;
  uint32_t aim_at = aim_at_default;
  AimMode aim_mode = AimMode::PSILENT;
  
  struct button key = {.button = SDLK_UNKNOWN};
  
  float fov = 45;
  float smooth_factor = 8.0f;
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

  ProjectileMode projectile_mode = ProjectileMode::DIRECT_THEN_SPLASH;
  uint32_t projectile_hitboxes = aim_hitbox_mask_auto;
  bool projectile_wall_splash = true;
  bool projectile_seam_shot = true;
  float projectile_splash_radius_scale = 1.0f;
  int projectile_path_steps = 16;
  int projectile_splash_samples = 18;
  int projectile_prediction_ticks = 360;
  bool projectile_strafe_prediction = true;
  float projectile_strafe_confidence = 55.0f;
  int projectile_trace_interval = 2;
  bool projectile_splash_debug = false;
  float projectile_far_distance_begin = 2200.0f;
  float projectile_far_distance_full = 4200.0f;
  float projectile_far_error_cap_add = 120.0f;
  int projectile_far_splash_budget_percent = 42;
  int projectile_far_path_steps_percent = 55;
  
  bool auto_scope = false;
  bool auto_unscope = false;
  float auto_scope_threshold = 800.0f;
  bool auto_rev = false;
  bool auto_unrev = false;
  float auto_rev_threshold = 450.0f;
  bool scoped_only = false;
  bool wait_for_headshot = false;
  
  bool ignore_friends = true;

  int max_targets = 6;
};

struct random_crits_config {
  bool force_crits = false;
  bool always_melee_crit = false;
  bool save_bucket = true;
  bool respect_bucket = true;
  bool advanced_stats = false;
  int seed_scan = 8192;
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
  bool aimbot = true;
  float fake_latency_ms = 0.0f;
  bool fake_interp = false;
  int window_ms = 200;
  bool visualizer = false;
  int visualizer_ticks = 16;
  visualizer_style visualizer_mode = visualizer_style::projected_boxes;
};

struct ipc_config {
  bool enabled = true;
  bool auto_connect = true;
  bool auto_ignore_local_bots = true;
};

struct Esp {
  enum class box_type {
    OUTLINE = 0,
    CORNER,
    FILLED,
    ROUNDED,
    PROJECTED
  };

  bool master = true;
  bool lerp = false;
  float lerp_speed = 12.0f;

  struct Player {
    enum class mafia_position {
      UNDER_NAME = 0,
      LEFT,
      RIGHT
    };

    RGBA_float enemy_color = {.r = 1, .g = 0.501960784, .b = 0, .a = 1};
    RGBA_float team_color = {.r = 1, .g = 1, .b = 1, .a = 1};
    RGBA_float friend_color = {.r = 0, .g = 0.862745098, .b = 0.31372549, .a = 1};
    
    bool box = true;
    box_type box_style = box_type::CORNER;
    bool health_bar = true;    
    bool name = true;
    bool class_icon = false;
    float class_icon_scale = 2.0f;
    bool class_icon_teammates = false;
    bool head_emoji = false;
    float head_emoji_scale = 2.0f;
    int head_emoji_style = 0;
    bool head_emoji_teammates = false;
    bool mafia_level = true;
    mafia_position mafia_level_position = mafia_position::UNDER_NAME;
    
    struct Flags {
      bool target_indicator = true;
      bool friend_indicator = true;
      bool scoped_indicator = false;
    } flags;

    bool enemy = true;
    bool team = false;
    bool friends = true;
  } player;

  struct Pickup {
    bool box = false;    
    box_type box_style = box_type::OUTLINE;
    bool name = true;
  } pickup;

  struct Intelligence {
    bool box = true;    
    box_type box_style = box_type::CORNER;
    bool name = true;
  } intelligence;
  
  struct Buildings {
    bool box = true;
    box_type box_style = box_type::CORNER;
    bool health_bar = true;    
    bool name = true;

    bool team = false;
  } buildings;
};

struct Chams {
  bool master = true;

  struct Player {
    bool enemy = true;

    enum class material_type {
      none,
      flat,
      flat_wireframe,
      shaded,
      shaded_wireframe,
      fresnel,
      fresnel_wireframe,
      glossy,
      glossy_wireframe,
      additive,
      additive_wireframe
    };

    struct ChamFlags {      
      bool ignore_z = true;
    };
    
    RGBA_float enemy_color = {.r = .8, .g = 0.701960784, .b = .1, .a = 1};
    material_type enemy_material_type = material_type::flat;
    RGBA_float enemy_color_z = {.r = 1, .g = 0.2, .b = .2, .a = 1};
    material_type enemy_material_z_type = material_type::flat;
    ChamFlags enemy_flags;
    RGBA_float enemy_overlay_color = {.r = .8, .g = 0.701960784, .b = .1, .a = 1};
    material_type enemy_overlay_material_type = material_type::none;
    RGBA_float enemy_overlay_color_z = {.r = 1, .g = 0.2, .b = .2, .a = 1};
    material_type enemy_overlay_material_z_type = material_type::none;
    ChamFlags enemy_overlay_flags;

    bool team = false;    
    RGBA_float team_color = {.r = 0, .g = 1, .b = 0, .a = 1};
    material_type team_material_type = material_type::shaded;
    RGBA_float team_color_z = {.r = 0, .g = 0.25, .b = 1, .a = 1};    
    material_type team_material_z_type = material_type::shaded;
    ChamFlags team_flags;
    
    bool friends = true;
    RGBA_float friend_color = {.r = 0, .g = 0.632745098, .b = 0.31372549, .a = 1};
    material_type friend_material_type = material_type::flat;
    RGBA_float friend_color_z = {.r = 0, .g = 1, .b = 0.20272549, .a = 1};
    material_type friend_material_z_type = material_type::flat;
    ChamFlags friends_flags;

    bool local = false;
    RGBA_float local_color = {.r = 0, .g = 0.8, .b = 0.35, .a = 1};
    material_type local_material_type = material_type::shaded;

    bool backtrack = false;
    RGBA_float backtrack_color = {.r = 0.12f, .g = 0.95f, .b = 0.75f, .a = 0.42f};
    RGBA_float backtrack_color_z = {.r = 0.95f, .g = 0.18f, .b = 0.55f, .a = 0.32f};
    material_type backtrack_material_type = material_type::fresnel;
    material_type backtrack_material_z_type = material_type::flat;
    ChamFlags backtrack_flags;
    int backtrack_ticks = 8;
  } player;
};

struct glow_config {
  bool master = false;
  int outline_scale = 4;
  float blur_scale = 2.0f;
  float start = 0.0f;
  float end = 8192.0f;
  bool smooth_alpha = true;
  bool filled_body = false;

  struct player_config {
    bool enemy = true;
    bool team = false;
    bool friends = true;
    bool local = false;

    RGBA_float enemy_color = {.r = 1, .g = 0.501960784, .b = 0, .a = 1};
    RGBA_float enemy_color_z = {.r = 1, .g = 0.501960784, .b = 0, .a = 1};
    RGBA_float team_color = {.r = 1, .g = 1, .b = 1, .a = 1};
    RGBA_float team_color_z = {.r = 1, .g = 1, .b = 1, .a = 1};
    RGBA_float friend_color = {.r = 0, .g = 0.862745098, .b = 0.31372549, .a = 1};
    RGBA_float friend_color_z = {.r = 0, .g = 0.862745098, .b = 0.31372549, .a = 1};
    RGBA_float local_color = {.r = 0, .g = 0.8, .b = 0.35, .a = 1};
  } player;
};

struct Visuals {
  struct Indicators {
    enum item_flags : uint32_t {
      legacy_ticks = 1u << 0,
      random_crits = 1u << 1,
      spectators = 1u << 2,
      keybinds = 1u << 3,
      tickbase = 1u << 4
    };

    uint32_t enabled_mask = random_crits | spectators | keybinds | tickbase;
    float x = 24.0f;
    float y = 140.0f;
    float legacy_ticks_x = 24.0f;
    float legacy_ticks_y = 140.0f;
    float random_crits_x = 24.0f;
    float random_crits_y = 186.0f;
    float keybinds_x = 24.0f;
    float keybinds_y = 232.0f;
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

  bool override_fov = false;
  float custom_fov = 90;

  bool override_viewmodel_fov = false;
  float custom_viewmodel_fov = 70;
};

struct Misc {

  struct Movement {
    enum class auto_strafe_mode {
      OFF = 0,
      LEGIT,
      DIRECTIONAL
    };

    bool bhop = true;
    auto_strafe_mode auto_strafe = auto_strafe_mode::DIRECTIONAL;
    float auto_strafe_turn_scale = 0.5f;
    float auto_strafe_max_delta = 180.0f;
    bool no_push = false;
    bool taunt_slide = false;
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
    button doubletap_key = {.button = SDLK_UNKNOWN};
    int doubletap_ticks = 14;
    bool warp = false;
    button warp_key = {.button = SDLK_UNKNOWN};
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
    bool setup_bones_optimization = false;
    bool equip_region_unlock = false;
    bool ping_reducer = false;
    int ping_target = 1;
    bool no_engine_sleep = false;
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
    bool null_graphics = true;
    bool null_graphics_render_stubs = true;
#else
    bool null_graphics = false;
    bool null_graphics_render_stubs = false;
#endif
    bool experimental_nographic_hooks = false;
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
    bool anti_afk = false;
    bool anti_autobalance = false;
    bool anti_motd = false;
    bool auto_report = false;
    bool auto_vote_map = false;
    int auto_vote_map_option = 2;
    bool noisemaker_spam = false;
    voice_command_spam_mode voice_command_spam = voice_command_spam_mode::off;
    bool micspam = false;
    int micspam_interval_on_seconds = 3;
    int micspam_interval_off_seconds = 60;
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
    bool mvm_buybot = false;
    int mvm_buybot_max_cash = 0;
    bool medic_autoheal = false;
    bool medic_autovacc = false;
    bool medic_autouber = false;
    bool medic_auto_crossbow = false;
    uint32_t medic_heal_targets_mask = medic_heal_target_default;
    bool medic_heal_only = false;
    bool auto_queue = false;
    bool auto_requeue = false;
    bool requeue_on_kick = false;
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
    std::uint64_t region_selector_allowed_mask = (1ull << 47) - 1ull;
    bool navbot_enabled = false;
    bool navbot_draw_path = true;
    bool navbot_dont_path_during_warmup = false;
    bool navbot_dont_path_unless_match_started = false;
    bool navbot_warmup_only_blu_cp_pl = false;
    bool navbot_look_at_path = false;
    bool navbot_auto_weapon = true;
    float navbot_look_at_path_speed = 360.0f;
    float navbot_crumb_blacklist_seconds = 50.0f;
    bool navbot_debug_text = true;
    uint32_t navbot_excluded_jobs_mask = 0;
  } automation;

  
  struct Menu {
    bool enabled = true;
    std::string text = "I Use Arch BTW!!!";
    bool use_custom_font = false;
    std::string custom_font = "Verdana.ttf";
  } menu;
};

struct Debug {
  int font_height = 14;
  int font_weight = 400;
  bool debug_render_all_entities = false;
  bool show_active_flag_ids_of_players = false;
  bool disable_friend_checks = true;
  bool insider_settings_unlocked = false;
};

struct Config {
  Aim aimbot;
  backtrack_config backtrack;
  ipc_config ipc;
  random_crits_config random_crits;
  Esp esp;
  Chams chams;
  glow_config glow;
  Visuals visuals;
  Misc misc;
  Debug debug;
};

#if defined(__GNUC__) || defined(__clang__)
#define cathook_EARLY_INIT __attribute__((init_priority(101)))
#else
#define cathook_EARLY_INIT
#endif

inline static Config config cathook_EARLY_INIT;

#undef cathook_EARLY_INIT


static bool is_button_raw_down(const struct button& button) {
  if (button.button == SDLK_UNKNOWN) {
    return false;
  }

  if (button.button >= 0) {
    const uint8_t* keys = SDL_GetKeyboardState(NULL);
    return keys[button.button] == 1;
  }

  Uint32 mouse_state = SDL_GetMouseState(NULL, NULL);
  return (mouse_state & SDL_BUTTON(-button.button)) != 0;
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

static bool is_button_active(struct button& button) {
  const bool is_down = is_button_raw_down(button);
  const bool was_pressed = is_down && !button.was_down;

  switch (button.mode) {
  case button::mode_type::HOLD:
    button.active = is_down;
    break;
  case button::mode_type::TOGGLE:
    if (was_pressed) {
      button.active = !button.active;
    }
    break;
  case button::mode_type::DOUBLE_CLICK:
    if (was_pressed) {
      const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
      const float delta = now - button.last_press_time;
      if (delta > 0.0f && delta <= 0.25f) {
        button.active = !button.active;
      }
      button.last_press_time = now;
    }
    break;
  }

  button.was_down = is_down;
  return button.active;
}

static void reset_button_state(struct button& button) {
  button.waiting = false;
  button.active = false;
  button.was_down = false;
  button.last_press_time = 0.0f;
}

#endif
