/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/entities/player.hpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "games/tf2/sdk/interfaces/steam_friends.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/model_info.hpp"
#include "games/tf2/sdk/netvars.hpp"

#include "core/entity_cache.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/player_manager.hpp"

#include "entity.hpp"
#include "weapon.hpp"

#include "core/types.hpp"
#include "core/print.hpp"

#include <algorithm>
#include <cstdint>

struct user_cmd;

#define	FL_ONGROUND (1<<0)
#define FL_DUCKING (1<<1)
#define	FL_WATERJUMP (1<<2)
#define FL_ONTRAIN (1<<3)
#define FL_INRAIN (1<<4)
#define FL_FROZEN (1<<5)
#define FL_ATCONTROLS (1<<6)
#define	FL_CLIENT (1<<7)
#define FL_FAKECLIENT (1<<8)
#define	FL_INWATER (1<<9)
#define PLAYER_FLAG_BITS 32

enum class tf_class {
  UNDEFINED = 0,
  SCOUT,
  SNIPER,
  SOLDIER,
  DEMOMAN,
  MEDIC,
  HEAVYWEAPONS,
  PYRO,
  SPY,
  ENGINEER
};

enum class observer_mode {
  none = 0,
  deathcam = 1,
  freezecam = 2,
  fixed = 3,
  in_eye = 4,
  chase = 5,
  roaming = 6
};

enum class life_state : uint8_t {
  alive = 0,
  dying = 1,
  dead = 2
};

struct player_info {
  char name[32];
  int user_id;
  char guid[33];
  unsigned long friends_id;
  char friends_name[32];
  bool fakeplayer;
  bool ishltv;
  unsigned long custom_files[4];
  unsigned char files_downloaded;
};

enum tf_cond {
  TF_COND_INVALID                          = -1,
  TF_COND_AIMING                           = 0, // Sniper aiming, Heavy minigun.
  TF_COND_ZOOMED                           = 1,
  TF_COND_DISGUISING                       = 2,
  TF_COND_DISGUISED                        = 3,
  TF_COND_STEALTHED                        = 4, // Spy specific
  TF_COND_INVULNERABLE                     = 5,
  TF_COND_TELEPORTED                       = 6,
  TF_COND_TAUNTING                         = 7,
  TF_COND_INVULNERABLE_WEARINGOFF          = 8,
  TF_COND_STEALTHED_BLINK                  = 9,
  TF_COND_SELECTED_TO_TELEPORT             = 10,
  TF_COND_CRITBOOSTED                      = 11, // DO NOT RE-USE THIS -- THIS IS FOR KRITZKRIEG AND REVENGE CRITS ONLY
  TF_COND_TMPDAMAGEBONUS                   = 12,
  TF_COND_FEIGN_DEATH                      = 13,
  TF_COND_PHASE                            = 14,
  TF_COND_STUNNED                          = 15, // Any type of stun. Check iStunFlags for more info.
  TF_COND_OFFENSEBUFF                      = 16,
  TF_COND_SHIELD_CHARGE                    = 17,
  TF_COND_DEMO_BUFF                        = 18,
  TF_COND_ENERGY_BUFF                      = 19,
  TF_COND_RADIUSHEAL                       = 20,
  TF_COND_HEALTH_BUFF                      = 21,
  TF_COND_BURNING                          = 22,
  TF_COND_HEALTH_OVERHEALED                = 23,
  TF_COND_URINE                            = 24,
  TF_COND_BLEEDING                         = 25,
  TF_COND_DEFENSEBUFF                      = 26, // 35% defense! No crit damage.
  TF_COND_MAD_MILK                         = 27,
  TF_COND_MEGAHEAL                         = 28,
  TF_COND_REGENONDAMAGEBUFF                = 29,
  TF_COND_MARKEDFORDEATH                   = 30,
  TF_COND_NOHEALINGDAMAGEBUFF              = 31,
  TF_COND_SPEED_BOOST                      = 32, // = 32
  TF_COND_CRITBOOSTED_PUMPKIN              = 33, // Brandon hates bits
  TF_COND_CRITBOOSTED_USER_BUFF            = 34,
  TF_COND_CRITBOOSTED_DEMO_CHARGE          = 35,
  TF_COND_SODAPOPPER_HYPE                  = 36,
  TF_COND_CRITBOOSTED_FIRST_BLOOD          = 37, // arena mode first blood
  TF_COND_CRITBOOSTED_BONUS_TIME           = 38,
  TF_COND_CRITBOOSTED_CTF_CAPTURE          = 39,
  TF_COND_CRITBOOSTED_ON_KILL              = 40, // =40. KGB, etc.
  TF_COND_CANNOT_SWITCH_FROM_MELEE         = 41,
  TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK        = 42, // 35% defense! Still damaged by crits.
  TF_COND_REPROGRAMMED                     = 43, // Bots only
  TF_COND_CRITBOOSTED_RAGE_BUFF            = 44,
  TF_COND_DEFENSEBUFF_HIGH                 = 45, // 75% defense! Still damaged by crits.
  TF_COND_SNIPERCHARGE_RAGE_BUFF           = 46, // Sniper Rage - Charge time speed up
  TF_COND_DISGUISE_WEARINGOFF              = 47, // Applied for half-second post-disguise
  TF_COND_MARKEDFORDEATH_SILENT            = 48, // Sans sound
  TF_COND_DISGUISED_AS_DISPENSER           = 49,
  TF_COND_SAPPED                           = 50, // =50. Bots only
  TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED = 51,
  TF_COND_INVULNERABLE_USER_BUFF           = 52,
  TF_COND_HALLOWEEN_BOMB_HEAD              = 53,
  TF_COND_HALLOWEEN_THRILLER               = 54,
  TF_COND_RADIUSHEAL_ON_DAMAGE             = 55,
  TF_COND_CRITBOOSTED_CARD_EFFECT          = 56,
  TF_COND_INVULNERABLE_CARD_EFFECT         = 57,
  TF_COND_MEDIGUN_UBER_BULLET_RESIST       = 58,
  TF_COND_MEDIGUN_UBER_BLAST_RESIST        = 59,
  TF_COND_MEDIGUN_UBER_FIRE_RESIST         = 60, // =60
  TF_COND_MEDIGUN_SMALL_BULLET_RESIST      = 61,
  TF_COND_MEDIGUN_SMALL_BLAST_RESIST       = 62,
  TF_COND_MEDIGUN_SMALL_FIRE_RESIST        = 63,
  TF_COND_STEALTHED_USER_BUFF              = 64, // Any class can have this
  TF_COND_MEDIGUN_DEBUFF                   = 65,
  TF_COND_STEALTHED_USER_BUFF_FADING       = 66,
  TF_COND_BULLET_IMMUNE                    = 67,
  TF_COND_BLAST_IMMUNE                     = 68,
  TF_COND_FIRE_IMMUNE                      = 69,
  TF_COND_PREVENT_DEATH                    = 70, // =70
  TF_COND_MVM_BOT_STUN_RADIOWAVE           = 71, // Bots only
  TF_COND_HALLOWEEN_SPEED_BOOST            = 72,
  TF_COND_HALLOWEEN_QUICK_HEAL             = 73,
  TF_COND_HALLOWEEN_GIANT                  = 74,
  TF_COND_HALLOWEEN_TINY                   = 75,
  TF_COND_HALLOWEEN_IN_HELL                = 76,
  TF_COND_HALLOWEEN_GHOST_MODE             = 77, // =77
  TF_COND_MINICRITBOOSTED_ON_KILL          = 78,
  TF_COND_OBSCURED_SMOKE                   = 79,
  TF_COND_PARACHUTE_ACTIVE                 = 80, // actively being used (not retracted)
  TF_COND_BLASTJUMPING                     = 81,
  TF_COND_HALLOWEEN_KART                   = 82,
  TF_COND_HALLOWEEN_KART_DASH              = 83,
  TF_COND_BALLOON_HEAD                     = 84, // =84 larger head, lower-gravity-feeling jumps
  TF_COND_MELEE_ONLY                       = 85, // =85 melee only
  TF_COND_SWIMMING_CURSE                   = 86, // player movement become swimming movement
  TF_COND_FREEZE_INPUT                     = 87, // freezes player input
  TF_COND_HALLOWEEN_KART_CAGE              = 88, // attach cage model to player while in kart
  TF_COND_DONOTUSE_0                       = 89,
  TF_COND_RUNE_STRENGTH                    = 90,
  TF_COND_RUNE_HASTE                       = 91,
  TF_COND_RUNE_REGEN                       = 92,
  TF_COND_RUNE_RESIST                      = 93,
  TF_COND_RUNE_VAMPIRE                     = 94,
  TF_COND_RUNE_REFLECT                     = 95,
  TF_COND_RUNE_PRECISION                   = 96,
  TF_COND_RUNE_AGILITY                     = 97,
  TF_COND_GRAPPLINGHOOK                    = 98,
  TF_COND_GRAPPLINGHOOK_SAFEFALL           = 99,
  TF_COND_GRAPPLINGHOOK_LATCHED            = 100,
  TF_COND_GRAPPLINGHOOK_BLEEDING           = 101,
  TF_COND_AFTERBURN_IMMUNE                 = 102,
  TF_COND_RUNE_KNOCKOUT                    = 103,
  TF_COND_RUNE_IMBALANCE                   = 104,
  TF_COND_CRITBOOSTED_RUNE_TEMP            = 105,
  TF_COND_PASSTIME_INTERCEPTION            = 106,
  TF_COND_SWIMMING_NO_EFFECTS              = 107, // =107_DNOC_FT
  TF_COND_PURGATORY                        = 108,
  TF_COND_RUNE_KING                        = 109,
  TF_COND_RUNE_PLAGUE                      = 110,
  TF_COND_RUNE_SUPERNOVA                   = 111,
  TF_COND_PLAGUE                           = 112,
  TF_COND_KING_BUFFED                      = 113,
  TF_COND_TEAM_GLOWS                       = 114, // used to show team glows to living players
  TF_COND_KNOCKED_INTO_AIR                 = 115,
  TF_COND_COMPETITIVE_WINNER               = 116,
  TF_COND_COMPETITIVE_LOSER                = 117,
  TF_COND_HEALING_DEBUFF                   = 118,
  TF_COND_PASSTIME_PENALTY_DEBUFF          = 119, // when carrying the ball without any teammates nearby
  TF_COND_GRAPPLED_TO_PLAYER               = 120,
  TF_COND_GRAPPLED_BY_PLAYER               = 121,
  TF_COND_PARACHUTE_DEPLOYED               = 122, // activated at least once while player's been airborne, but not does mean it's active now (see TF_COND_PARACHUTE_ACTIVE)
  TF_COND_GAS                              = 123,
  TF_COND_BURNING_PYRO                     = 124,
  TF_COND_ROCKETPACK                       = 125,
  // Players who lose their footing have lessened friction and don't re-stick to the ground unless they're below a
  // tf_movement_lost_footing_restick speed
  TF_COND_LOST_FOOTING                     = 126,
  // When in the air, slide up/along surfaces with momentum as if caught up in a... blast of air of some sort.
  // Reduces air control as well.  See tf_movement_aircurrent convars.  Removed upon touching ground.
  TF_COND_AIR_CURRENT                      = 127,
  TF_COND_HALLOWEEN_HELL_HEAL              = 128,
  TF_COND_POWERUPMODE_DOMINANT		   = 129,
  TF_COND_IMMUNE_TO_PUSHBACK		   = 130,
  TF_COND_LAST
};

//Original of a hooked class function
static bool (*in_cond_original)(void*, int);

//TODO/NOTE: Something useful would be a network variable helper.
//           There are a lot of magical offsets to specific structures, and those offsets have a string key associated with them.

class Player : public Entity {
public:
  static constexpr int weapon_inventory_offset = 0x1110;
  static constexpr int max_weapon_count = 48;


  int get_weapon_handle(void) {
    return *(int*)(this + 0x11D0);
  }

  Weapon* get_weapon(void) {
    auto* entity = entity_list->entity_from_handle(get_weapon_handle());
    if (entity == nullptr) {
      return nullptr;
    }

    return reinterpret_cast<Weapon*>(entity);
  }

  int get_weapon_handle_at(int index) {
    if (index < 0 || index >= max_weapon_count) {
      return 0;
    }

    auto* weapon_handles = reinterpret_cast<int*>(this + weapon_inventory_offset);
    return weapon_handles[index];
  }

  Weapon* get_weapon_at(int index) {
    auto* entity = entity_list->entity_from_handle(get_weapon_handle_at(index));
    if (entity == nullptr) {
      return nullptr;
    }

    return reinterpret_cast<Weapon*>(entity);
  }
   
  bool is_friend(void) {
    player_info pinfo;
    if (engine->get_player_info(this->get_index(), &pinfo) && pinfo.friends_id != 0 && pinfo.fakeplayer != true) { 
      const auto account_id = static_cast<std::uint32_t>(pinfo.friends_id);
      if (cathook::core::players::is_friendly(account_id) || cat_ipc::client::is_local_ipc_friend(account_id)) {
        return true;
      }

      return friend_cache[pinfo.friends_id];
    }

    return false;
  }

  bool is_ignored(void) {
    player_info pinfo;
    if (engine->get_player_info(this->get_index(), &pinfo) && pinfo.friends_id != 0 && pinfo.fakeplayer != true) {
      const auto account_id = static_cast<std::uint32_t>(pinfo.friends_id);
      return cathook::core::players::is_ignored(account_id) || cat_ipc::client::is_local_ipc_friend(account_id);
    }

    return false;
  }

  int get_health(void) {
    return *(int*)(this + 0xD4);
  }

  int get_max_health(void) {
    return *(int*)(this + 0x1DF8);
  }
  
  int get_default_fov(void) {
    return *(int*)(this + 0x15E4);
  }

  int get_fov(void) {
    return *(int*)(this + 0x15DC);
  }

  
  unsigned int get_player_name(wchar_t name[32]) {
    player_info pinfo;
    if (!engine->get_player_info(this->get_index(), &pinfo)) return 0;

    size_t len = mbstowcs(name, pinfo.name, 32);
    if (len == (size_t)-1) return 0;

    return len;
  }
  
  void set_head_size(float size) {
    *(float*)(this + 0x39D8) = size;
  }

  void set_torso_length(float length) {
    *(float*)(this + 0x39DC) = length;
  }

  void set_taunt_cam(bool value) {
    *(int*)(this + 0x2414) = value ? 1 : 0;
  }

  int get_force_taunt_cam(void) {
    return *(int*)(this + 0x2414);
  }

  bool allow_move_during_taunt() {
    static const int allow_move_during_taunt_offset = tf2_netvars::find_offset("DT_TFPlayer", {"m_bAllowMoveDuringTaunt"});
    if (allow_move_during_taunt_offset <= 0) {
      return false;
    }

    return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + allow_move_during_taunt_offset);
  }
  
  life_state get_lifestate(void) {
    static const int life_state_offset = tf2_netvars::find_offset("DT_BasePlayer", {"m_lifeState"});
    if (life_state_offset <= 0) {
      return life_state::dead;
    }

    const auto value = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(this) + life_state_offset);
    return static_cast<life_state>(value);
  }

  bool is_alive(void) {
    return get_lifestate() == life_state::alive && get_health() > 0;
  }

  observer_mode get_observer_mode() {
    static const int observer_mode_offset = tf2_netvars::find_offset("DT_BasePlayer", {"m_iObserverMode"});
    if (observer_mode_offset <= 0) {
      return observer_mode::none;
    }

    return static_cast<observer_mode>(*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + observer_mode_offset));
  }

  Entity* get_observer_target() {
    static const int observer_target_offset = tf2_netvars::find_offset("DT_BasePlayer", {"m_hObserverTarget"});
    if (observer_target_offset <= 0) {
      return nullptr;
    }

    const int handle = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + observer_target_offset);
    return entity_list->entity_from_handle(handle);
  }
    
  Vec3 get_shoot_pos(void) {
    void** vtable = *(void ***)this;

    Vec3 (*get_shoot_pos_fn)(void*) = (Vec3 (*)(void*))vtable[303];

    return get_shoot_pos_fn(this);
  }

  Vec3 get_punch_angles(void) {
    return *(Vec3*)(this + 0x74);
  }

  static int get_eye_pitch_offset(void) {
    static const int offset = tf2_netvars::find_offset("DT_TFPlayer", {"m_angEyeAngles[0]"});
    return offset;
  }

  static int get_eye_yaw_offset(void) {
    static const int offset = tf2_netvars::find_offset("DT_TFPlayer", {"m_angEyeAngles[1]"});
    return offset;
  }

  Vec3 get_eye_angles(void) {
    const int pitch_offset = get_eye_pitch_offset();
    const int yaw_offset = get_eye_yaw_offset();
    if (pitch_offset <= 0 || yaw_offset <= 0) {
      return Vec3{};
    }

    const auto base = reinterpret_cast<std::uintptr_t>(this);
    return Vec3{
      *reinterpret_cast<float*>(base + static_cast<std::uintptr_t>(pitch_offset)),
      *reinterpret_cast<float*>(base + static_cast<std::uintptr_t>(yaw_offset)),
      0.0f
    };
  }

  void set_eye_angles(const Vec3& angles) {
    const int pitch_offset = get_eye_pitch_offset();
    const int yaw_offset = get_eye_yaw_offset();
    if (pitch_offset <= 0 || yaw_offset <= 0) {
      return;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(this);
    *reinterpret_cast<float*>(base + static_cast<std::uintptr_t>(pitch_offset)) = angles.x;
    *reinterpret_cast<float*>(base + static_cast<std::uintptr_t>(yaw_offset)) = angles.y;
  }

  enum tf_class get_tf_class(void) {
    return (enum tf_class)*(int*)(this + 0x1BA0);
  }

  bool in_upgrade_zone() {
    static const int in_upgrade_zone_offset = tf2_netvars::find_offset("DT_TFPlayer", {"m_bInUpgradeZone"});
    if (in_upgrade_zone_offset <= 0) {
      return false;
    }

    return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + in_upgrade_zone_offset);
  }

  int get_currency() {
    static const int currency_offset = tf2_netvars::find_offset("DT_TFPlayer", {"m_nCurrency"});
    if (currency_offset <= 0) {
      return 0;
    }

    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + currency_offset);
  }
  
  Vec3 get_bone_pos(int bone_num) {
    // 128 bones, 3x4 matrix
    float bone_to_world_out[128][3][4];
    if (this->setup_bones(bone_to_world_out, 128, 0x100, this->get_simulation_time())) {
      // Saw this in the source leak, don't know how it works
      return (Vec3){bone_to_world_out[bone_num][0][3], bone_to_world_out[bone_num][1][3], bone_to_world_out[bone_num][2][3]};
    }

    return Vec3{0.0f, 0.0f, 0.0f};
  }

  int get_hitbox_set(void) {
    static const int offset = tf2_netvars::find_offset("DT_BaseAnimating", {"m_nHitboxSet"});
    if (offset <= 0) {
      return 0;
    }

    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset));
  }

  bool get_hitbox_center(int hitbox_id, Vec3* center_out, int* bone_out = nullptr) {
    if (center_out == nullptr) {
      return false;
    }

    const model_t* model = get_model();
    if (model == nullptr || model_info == nullptr) {
      return false;
    }

    studio_hdr* hdr = model_info->get_studio_model(model);
    if (hdr == nullptr) {
      return false;
    }

    studio_hitbox_set* hitbox_set = hdr->hitbox_set(get_hitbox_set());
    if (hitbox_set == nullptr || hitbox_id < 0 || hitbox_id >= hitbox_set->num_hitboxes) {
      return false;
    }

    studio_box* hitbox = hitbox_set->hitbox(hitbox_id);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
      return false;
    }

    matrix_3x4 bone_to_world[128]{};
    if (!setup_bones(bone_to_world, 128, 0x100, get_simulation_time())) {
      return false;
    }

    const Vec3 local_center = (hitbox->bbmin + hitbox->bbmax) * 0.5f;
    const matrix_3x4& matrix = bone_to_world[hitbox->bone];

    center_out->x = (local_center.x * matrix.mat[0][0]) + (local_center.y * matrix.mat[0][1]) + (local_center.z * matrix.mat[0][2]) + matrix.mat[0][3];
    center_out->y = (local_center.x * matrix.mat[1][0]) + (local_center.y * matrix.mat[1][1]) + (local_center.z * matrix.mat[1][2]) + matrix.mat[1][3];
    center_out->z = (local_center.x * matrix.mat[2][0]) + (local_center.y * matrix.mat[2][1]) + (local_center.z * matrix.mat[2][2]) + matrix.mat[2][3];

    if (bone_out != nullptr) {
      *bone_out = hitbox->bone;
    }

    return true;
  }
  
  int get_head_bone(void) {
    switch (this->get_tf_class()) {
    case tf_class::SCOUT:
    case tf_class::PYRO:
    case tf_class::SPY:
    case tf_class::MEDIC:
    case tf_class::HEAVYWEAPONS:
    case tf_class::SNIPER:
    case tf_class::SOLDIER:
      return 6;
    case tf_class::DEMOMAN:
      return 16;
    case tf_class::ENGINEER:
      return 8;
      
    default:
      return 0;
    }

    return 0;
  }
  
  int setup_bones(void* bone_to_world_out, int max_bones, int bone_mask, float current_time) {
    void** vtable = *(void***)this;
    
    int (*setup_bones_fn)(void*, void*, int, int, float) = (int (*)(void*, void*, int, int, float))vtable[96];

    return setup_bones_fn(this, bone_to_world_out, max_bones, bone_mask, current_time);
  }
  
  void set_thirdperson(bool value) {
    *(bool*)(this + 0x240C) = value;
  }
  
  void switch_thirdperson(void) {
    void** vtable = *(void ***)this;

    void (*switch_thirdperson_fn)(void*) = (void (*)(void*))vtable[256];
    
    return switch_thirdperson_fn(this);    
  }
  
  bool is_thirdperson(void) {
    return *(bool*)(this + 0x240C);
  }  

  float get_model_scale(void) {
    return *(float*)(this + 0x914);
  }

  int get_stun_flags(void) {
    return *(int*)(this + 0x1E78 + 0x3B4);
  }
  
  void* get_shared(void) {
    return (void*)(this + 0x1E78);
  }

  static bool has_in_cond_original() {
    return in_cond_original != nullptr;
  }

  int get_crit_mult_raw() {
    static const int offset = tf2_netvars::find_offset("DT_TFPlayerShared", {"m_iCritMult"});
    if (offset <= 0) {
      return 0;
    }

    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(get_shared()) + offset);
  }

  float get_crit_mult() {
    const float raw_mult = static_cast<float>(std::clamp(get_crit_mult_raw(), 0, 255));
    return 1.0f + ((raw_mult / 255.0f) * 3.0f);
  }


  float get_invisibility(void) {
    return *(float*)(this + 0x178);
  }

  void set_invisibility(float value) {
    *(float*)(this + 0x178) = value;
  }
  
  bool in_cond(tf_cond condition) {
    if (!has_in_cond_original()) {
      return false;
    }

    return in_cond_original(get_shared(), condition);
  }
  
  bool is_scoped(void) {
    return in_cond(TF_COND_ZOOMED);
  }

  bool is_heavy_revved(void) {
    return get_tf_class() == tf_class::HEAVYWEAPONS && in_cond(TF_COND_AIMING);
  }

  bool is_ducking(void) {
    return this->get_flags() & FL_DUCKING;
  }

  bool is_on_ground(void) {
    return (this->get_flags() & FL_ONGROUND) != 0;
  }

  int get_water_level(void) {
    static int offset = tf2_netvars::find_offset("DT_TFPlayer", {"m_nWaterLevel"});
    if (offset == 0) {
      offset = tf2_netvars::find_offset("DT_LocalPlayerExclusive", {"m_nWaterLevel"});
    }
    return offset != 0 ? static_cast<int>(*reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset))) : 0;
  }

  float get_max_speed(void) {
    static int offset = tf2_netvars::find_offset("DT_BasePlayer", {"m_flMaxspeed"});
    return offset != 0 ? *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset)) : 0.0f;
  }

  Vec3 get_player_mins(bool ducked = false) {
      Vec3 mins = this->get_collideable_mins();
      // TF2 player mins are typically invariant between standing and ducking,
      // but if the physics box is somehow different, we return the real dynamically-scaled base.
      return mins;
    }

    Vec3 get_player_maxs(bool ducked = false) {
      Vec3 maxs = this->get_collideable_maxs();
      bool currently_ducking = this->is_ducking();

      if (ducked && !currently_ducking) {
        // We are standing but want hypothetical duck bounds.
        // Default standing max_z is 82, duck is 62.
        maxs.z = maxs.z * (62.0f / 82.0f);
      } else if (!ducked && currently_ducking) {
        // We are ducking but want hypothetical standing bounds.
        maxs.z = maxs.z * (82.0f / 62.0f);
      }

      return maxs;
    }

  bool is_invulnerable(void) {
    if (this->in_cond(TF_COND_INVULNERABLE)                     ||
	this->in_cond(TF_COND_INVULNERABLE_USER_BUFF)           ||
	//this->in_cond(TF_COND_INVULNERABLE_WEARINGOFF)          ||
	this->in_cond(TF_COND_INVULNERABLE_CARD_EFFECT)         ||
	this->in_cond(TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED) ||
	this->in_cond(TF_COND_PHASE))
      {
	return true;
      }

    return false;
  }

  bool is_crit_boosted(void) {
    return in_cond(TF_COND_CRITBOOSTED) ||
           in_cond(TF_COND_CRITBOOSTED_PUMPKIN) ||
           in_cond(TF_COND_CRITBOOSTED_USER_BUFF) ||
           in_cond(TF_COND_CRITBOOSTED_DEMO_CHARGE) ||
           in_cond(TF_COND_CRITBOOSTED_FIRST_BLOOD) ||
           in_cond(TF_COND_CRITBOOSTED_BONUS_TIME) ||
           in_cond(TF_COND_CRITBOOSTED_CTF_CAPTURE) ||
           in_cond(TF_COND_CRITBOOSTED_ON_KILL) ||
           in_cond(TF_COND_CRITBOOSTED_RAGE_BUFF) ||
           in_cond(TF_COND_CRITBOOSTED_CARD_EFFECT) ||
           in_cond(TF_COND_CRITBOOSTED_RUNE_TEMP);
  }
  
  int get_ground_entity_handle(void) {
    return *(int*)(this + 0x31C);
  }

  Entity* get_ground_entity(void) {
    return entity_list->entity_from_handle(this->get_ground_entity_handle());
  }

  void set_ground_entity_handle(int handle) {
    *(int*)(this + 0x31C) = handle;
  }

  int get_flags(void) {
    return *(int*)(this + 0x460);
  }

  void set_flags(int flags) {
    *(int*)(this + 0x460) = flags;
  }

  Vec3 get_velocity(void) {
    return *(Vec3*)(this + 0x168);
  }

  void set_velocity(const Vec3& velocity) {
    *(Vec3*)(this + 0x168) = velocity;
  }

  Vec3 get_base_velocity(void) {
    return *(Vec3*)(this + 0x1F0);
  }

  void set_base_velocity(const Vec3& velocity) {
    *(Vec3*)(this + 0x1F0) = velocity;
  }

  bool get_ducked(void) {
    return *(bool*)(this + 0x1288);
  }

  void set_ducked(bool value) {
    *(bool*)(this + 0x1288) = value;
  }

  bool get_ducking_state(void) {
    return *(bool*)(this + 0x1289);
  }

  void set_ducking_state(bool value) {
    *(bool*)(this + 0x1289) = value;
  }

  bool get_in_duck_jump(void) {
    return *(bool*)(this + 0x128A);
  }

  void set_in_duck_jump(bool value) {
    *(bool*)(this + 0x128A) = value;
  }

  float get_duck_time(void) {
    return *(float*)(this + 0x128C);
  }

  void set_duck_time(float value) {
    *(float*)(this + 0x128C) = value;
  }

  float get_duck_jump_time(void) {
    return *(float*)(this + 0x1290);
  }

  void set_duck_jump_time(float value) {
    *(float*)(this + 0x1290) = value;
  }

  float get_fall_velocity(void) {
    return *(float*)(this + 0x129C);
  }

  void set_fall_velocity(float value) {
    *(float*)(this + 0x129C) = value;
  }

  Vec3 get_view_offset(void) {
    return *(Vec3*)(this + 0x144);
  }

  void set_view_offset(const Vec3& value) {
    *(Vec3*)(this + 0x144) = value;
  }

  float get_fov_time(void) {
    return *(float*)(this + 0x15E0);
  }

  float get_next_attack(void) {
    return *(float*)(this + 0x1088);
  }
  
  bool can_shoot(Entity* target_entity) {
    (void)target_entity;

    Weapon* weapon = this->get_weapon();
    if (!weapon)
      return false;
    
    if (attribute_manager->attrib_hook_value(0, "no_attack", this))
      return false;

    if (!this->is_scoped() && weapon->get_def_id() == Sniper_m_TheMachina)
      return false;
    
    return true;
  }
  
  user_cmd* get_current_cmd(void) {
    return (user_cmd*)*(void**)(this + (1628 - 8));
  }

  void set_current_cmd(user_cmd* user_cmd) {
    *(void**)(this + (1628 - 8)) = user_cmd;
  }
  
  Entity* to_entity(void) {
    return (Entity*)this;
  }
};

#endif
