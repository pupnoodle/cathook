#include "skin_changer.hpp"

#include "core/hooks/equip_region_unlock.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"

#include <cstring>

namespace skin_changer {

namespace {

enum paint_family {
  family_none,
  family_scattergun,
  family_pistol,
  family_rocket,
  family_shotgun,
  family_flame,
  family_grenade,
  family_sticky,
  family_minigun,
  family_wrench,
  family_medigun,
  family_sniper,
  family_smg,
  family_knife,
  family_revolver,
  family_unique
};

struct kit_entry {
  int id;
  const char* name;
};

constexpr kit_entry kits[] = {
  { 0, "None" },
  { 102, "Wrapped Reviver Mk.II" },
  { 104, "Carpet Bomber Mk.II" },
  { 105, "Masked Mender Mk.II" },
  { 106, "Woodland Warrior Mk.II" },
  { 109, "Forest Fire Mk.II" },
  { 112, "Backwoods Boomstick Mk.II" },
  { 113, "Woodsy Widowmaker Mk.II" },
  { 114, "Night Owl Mk.II" },
  { 120, "Iron Wood Mk.II" },
  { 122, "Plaid Potshotter Mk.II" },
  { 130, "Bovine Blazemaker Mk.II" },
  { 139, "Civil Servant Mk.II" },
  { 143, "Smalltown Bringdown Mk.II" },
  { 144, "Civic Duty Mk.II" },
  { 151, "Dead Reckoner Mk.II" },
  { 160, "Autumn Mk.II" },
  { 161, "Nutcracker Mk.II" },
  { 163, "Macabre Web Mk.II" },
  { 200, "Bloom Buffed" },
  { 201, "Quack Canvassed" },
  { 202, "Bank Rolled" },
  { 203, "Merc Stained" },
  { 204, "Kill Covered" },
  { 205, "Fire Glazed" },
  { 206, "Pizza Polished" },
  { 207, "Bonk Varnished" },
  { 208, "Star Crossed" },
  { 209, "Clover Camo'd" },
  { 210, "Freedom Wrapped" },
  { 211, "Cardboard Boxed" },
  { 212, "Dream Piped" },
  { 213, "Miami Element" },
  { 214, "Neo Tokyo" },
  { 215, "Geometrical Teams" },
  { 217, "Bomber Soul" },
  { 218, "Uranium" },
  { 220, "Cabin Fevered" },
  { 221, "Polar Surprise" },
  { 223, "Hana" },
  { 224, "Dovetailed" },
  { 225, "Cosmic Calamity" },
  { 226, "Hazard Warning" },
  { 228, "Mosaic" },
  { 230, "Jazzy" },
  { 232, "Alien Tech" },
  { 234, "Damascus and Mahogany" },
  { 235, "Skull Study" },
  { 236, "Haunted Ghosts" },
  { 237, "Spectral Shimmered" },
  { 238, "Spirit of Halloween" },
  { 239, "Horror Holiday" },
  { 240, "Totally Boned" },
  { 241, "Electroshocked" },
  { 242, "Ghost Town" },
  { 243, "Tumor Toasted" },
  { 244, "Calavera Canvas" },
  { 245, "Snow Covered" },
  { 246, "Frost Ornamented" },
  { 247, "Smissmas Village" },
  { 248, "Igloo" },
  { 249, "Seriously Snowed" },
  { 250, "Smissmas Camo" },
  { 251, "Sleighin' Style" },
  { 252, "Alpine" },
  { 253, "Gift Wrapped" },
  { 254, "Winterland Wrapped" },
  { 255, "Helldriver" },
  { 256, "Organ-ically Hellraised" },
  { 257, "Spectrum Splattered" },
  { 258, "Candy Coated" },
  { 259, "Pumpkin Pied" },
  { 260, "Sweet Toothed" },
  { 261, "Crawlspace Critters" },
  { 262, "Portal Plastered" },
  { 263, "Death Deluxe" },
  { 264, "Raving Dead" },
  { 265, "Eyestalker" },
  { 266, "Spider's Cluster" },
  { 267, "Gourdy Green" },
  { 268, "Mummified Mimic" },
  { 269, "Spider Season" },
  { 270, "Gingerbread Winner" },
  { 271, "Saccharine Striped" },
  { 272, "Elfin Enamel" },
  { 273, "Peppermint Swirl" },
  { 275, "Snow Globalization" },
  { 276, "Gifting Mann's Wrapping Paper" },
  { 277, "Snowflake Swirled" },
  { 278, "Smissmas Spycrabs" },
  { 279, "Frozen Aurora" },
  { 280, "Starlight Serenity" },
  { 281, "Frosty Delivery" },
  { 282, "Glacial Glazed" },
  { 283, "Cookie Fortress" },
  { 284, "Sarsaparilla Sprayed" },
  { 285, "Swashbuckled" },
  { 286, "Skull Cracked" },
  { 287, "Misfortunate" },
  { 289, "Neon-ween" },
  { 290, "Simple Spirits" },
  { 291, "Broken Bones" },
  { 292, "Potent Poison" },
  { 293, "Searing Souls" },
  { 294, "Party Phantoms" },
  { 295, "Polter-Guised" },
  { 296, "Kiln and Conquer" },
  { 297, "Necromanced" },
  { 300, "Yeti Coated" },
  { 301, "Park Pigmented" },
  { 302, "Mannana Peeled" },
  { 303, "Macaw Masked" },
  { 304, "Sax Waxed" },
  { 305, "Anodized Aloha" },
  { 306, "Bamboo Brushed" },
  { 307, "Tiger Buffed" },
  { 308, "Croc Dusted" },
  { 309, "Pina Polished" },
  { 310, "Leopard Printed" },
  { 390, "Dragon Slayer" },
  { 391, "Smissmas Sweater" },
  { 400, "Ghoul Blaster" },
  { 401, "Cream Corned" },
  { 402, "Sunriser" },
  { 403, "Sacred Slayer" },
  { 404, "Metalized Soul" },
  { 405, "Bonzo Gnawed" },
  { 406, "Health and Hell" },
  { 407, "Health and Hell (Green)" },
  { 408, "Hypergon" },
  { 409, "Pumpkin Plastered" },
  { 410, "Chilly Autumn" },
  { 411, "Steel Brushed" },
  { 412, "Secretly Serviced" },
  { 413, "Sky Stallion" },
  { 414, "Bomb Carrier" },
  { 415, "Business Class" },
  { 416, "Deadly Dragon" },
  { 417, "Team Serviced" },
  { 418, "Warborn" },
  { 419, "Pacific Peacemaker" },
  { 420, "Mechanized Monster" },
  { 421, "Stardust" },
  { 422, "Team Detail" },
  { 423, "Gobi Glazed" },
  { 424, "Sleek Greek" },
  { 425, "Graphite Gripped" },
  { 426, "Stealth Specialist" },
  { 427, "Piranha Mania" },
  { 428, "Team Charged" },
  { 429, "Brawler's Iron" },
  { 430, "Necropolish" },
  { 431, "Blackout" },
  { 432, "Broken Record" },
  { 433, "Sandwich Diner" },
  { 434, "Beachy Boy" },
  { 435, "Army Guns" },
  { 436, "Taxi Cabbed" },
  { 437, "Ocean Mapped" },
  { 438, "Krak-coated" },
  { 439, "Team Union" },
  { 440, "Sideshow" },
  { 441, "Storage War" },
  { 442, "Die'n Dasher" },
};

constexpr int attribute_paintkit = 834;
constexpr int attribute_wear = 725;
constexpr int attribute_seed = 866;
constexpr int attribute_seed_hi = 867;
constexpr int attribute_inspect = 731;
constexpr int attribute_unusual_weapon = 370;
constexpr int attribute_festive = 2053;
constexpr int attribute_australium = 2027;
constexpr int attribute_loot_rarity = 2022;
constexpr int attribute_style_override = 542;
constexpr int attribute_killstreak_tier = 2025;
constexpr int attribute_killstreak_sheen = 2014;

constexpr int quality_unusual = 5;
constexpr int quality_strange = 11;
constexpr int quality_decorated = 15;

constexpr int unusual_hot = 701;
constexpr int unusual_isotope = 702;
constexpr int unusual_cool = 703;
constexpr int unusual_energy_orb = 704;

int redirect_index(int definition) {
  switch (definition) {
  case Soldier_m_RocketLauncher: return Soldier_m_RocketLauncherR;
  case Scout_m_Scattergun: return Scout_m_ScattergunR;
  case Pyro_m_FlameThrower: return Pyro_m_FlameThrowerR;
  case Demoman_m_GrenadeLauncher: return Demoman_m_GrenadeLauncherR;
  case Demoman_s_StickybombLauncher: return Demoman_s_StickybombLauncherR;
  case Heavy_m_Minigun: return Heavy_m_MinigunR;
  case Engi_t_Wrench: return Engi_t_WrenchR;
  case Medic_s_MediGun: return Medic_s_MediGunR;
  case Sniper_m_SniperRifle: return Sniper_m_SniperRifleR;
  case Sniper_s_SMG: return Sniper_s_SMGR;
  case Spy_t_Knife: return Spy_t_KnifeR;
  case Spy_m_Revolver: return Spy_m_RevolverR;
  case Scout_s_ScoutsPistol:
  case Engi_s_EngineersPistol: return Engi_s_PistolR;
  case Soldier_s_SoldiersShotgun:
  case Pyro_s_PyrosShotgun:
  case Heavy_s_HeavysShotgun:
  case Engi_m_EngineersShotgun: return Soldier_s_ShotgunR;
  case Scout_t_Bat: return Scout_t_BatR;
  case Soldier_t_Shovel: return Soldier_t_ShovelR;
  case Pyro_t_FireAxe: return Pyro_t_FireAxeR;
  case Demoman_t_Bottle: return Demoman_t_BottleR;
  case Medic_t_Bonesaw: return Medic_t_BonesawR;
  case Sniper_t_Kukri: return Sniper_t_KukriR;
  default: return definition;
  }
}

int mk2_family(int kit) {
  switch (kit) {
  case 102:
  case 105:
  case 139: return family_medigun;
  case 104: return family_sticky;
  case 106:
  case 143: return family_rocket;
  case 109:
  case 130: return family_flame;
  case 112:
  case 144: return family_shotgun;
  case 113:
  case 122: return family_smg;
  case 114: return family_sniper;
  case 120: return family_minigun;
  case 151: return family_revolver;
  default: return family_none;
  }
}

int paint_family_of(int definition) {
  switch (redirect_index(definition)) {
  case Scout_m_ScattergunR: return family_scattergun;
  case Scout_s_PistolR: return family_pistol;
  case Soldier_m_RocketLauncherR: return family_rocket;
  case Soldier_s_ShotgunR: return family_shotgun;
  case Pyro_m_FlameThrowerR: return family_flame;
  case Demoman_m_GrenadeLauncherR: return family_grenade;
  case Demoman_s_StickybombLauncherR: return family_sticky;
  case Heavy_m_MinigunR: return family_minigun;
  case Engi_t_WrenchR: return family_wrench;
  case Medic_s_MediGunR: return family_medigun;
  case Sniper_m_SniperRifleR: return family_sniper;
  case Sniper_s_SMGR: return family_smg;
  case Spy_t_KnifeR: return family_knife;
  case Spy_m_RevolverR: return family_revolver;
  default: break;
  }

  switch (definition) {
  case Scout_m_FestiveScattergun:
  case Scout_m_SilverBotkillerScattergunMkI:
  case Scout_m_GoldBotkillerScattergunMkI:
  case Scout_m_RustBotkillerScattergunMkI:
  case Scout_m_BloodBotkillerScattergunMkI:
  case Scout_m_CarbonadoBotkillerScattergunMkI:
  case Scout_m_DiamondBotkillerScattergunMkI:
  case Scout_m_SilverBotkillerScattergunMkII:
  case Scout_m_GoldBotkillerScattergunMkII:
  case Scout_m_NightTerror:
  case Scout_m_TartanTorpedo:
  case Scout_m_CountryCrusher:
  case Scout_m_BackcountryBlaster:
  case Scout_m_SpruceDeuce:
  case Scout_m_CurrentEvent:
  case Scout_m_MacabreWeb:
  case Scout_m_Nutcracker:
  case Scout_m_BlueMew:
  case Scout_m_FlowerPower:
  case Scout_m_ShottoHell:
  case Scout_m_CoffinNail:
  case Scout_m_KillerBee:
  case Scout_m_Corsair:
    return family_scattergun;

  case Scout_s_RedRockRoscoe:
  case Scout_s_HomemadeHeater:
  case Scout_s_HickoryHolepuncher:
  case Scout_s_LocalHero:
  case Scout_s_BlackDahlia:
  case Scout_s_SandstoneSpecial:
  case Scout_s_MacabreWeb:
  case Scout_s_Nutcracker:
  case Scout_s_BlueMew:
  case Scout_s_BrainCandy:
  case Scout_s_ShottoHell:
  case Scout_s_DressedToKill:
  case Scout_s_Blitzkrieg:
    return family_pistol;

  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_SilverBotkillerRocketLauncherMkI:
  case Soldier_m_GoldBotkillerRocketLauncherMkI:
  case Soldier_m_RustBotkillerRocketLauncherMkI:
  case Soldier_m_BloodBotkillerRocketLauncherMkI:
  case Soldier_m_CarbonadoBotkillerRocketLauncherMkI:
  case Soldier_m_DiamondBotkillerRocketLauncherMkI:
  case Soldier_m_SilverBotkillerRocketLauncherMkII:
  case Soldier_m_GoldBotkillerRocketLauncherMkII:
  case Soldier_m_WoodlandWarrior:
  case Soldier_m_SandCannon:
  case Soldier_m_AmericanPastoral:
  case Soldier_m_SmalltownBringdown:
  case Soldier_m_ShellShocker:
  case Soldier_m_AquaMarine:
  case Soldier_m_Autumn:
  case Soldier_m_BlueMew:
  case Soldier_m_BrainCandy:
  case Soldier_m_CoffinNail:
  case Soldier_m_HighRollers:
  case Soldier_m_Warhawk:
    return family_rocket;

  case Soldier_s_FestiveShotgun:
  case Soldier_s_BackwoodsBoomstick:
  case Soldier_s_RusticRuiner:
  case Soldier_s_CivicDuty:
  case Soldier_s_LightningRod:
  case Soldier_s_Autumn:
  case Soldier_s_FlowerPower:
  case Soldier_s_CoffinNail:
  case Soldier_s_DressedtoKill:
  case Soldier_s_RedBear:
    return family_shotgun;

  case Pyro_m_FestiveFlameThrower:
  case Pyro_m_SilverBotkillerFlameThrowerMkI:
  case Pyro_m_GoldBotkillerFlameThrowerMkI:
  case Pyro_m_RustBotkillerFlameThrowerMkI:
  case Pyro_m_BloodBotkillerFlameThrowerMkI:
  case Pyro_m_CarbonadoBotkillerFlameThrowerMkI:
  case Pyro_m_DiamondBotkillerFlameThrowerMkI:
  case Pyro_m_SilverBotkillerFlameThrowerMkII:
  case Pyro_m_GoldBotkillerFlameThrowerMkII:
  case Pyro_m_ForestFire:
  case Pyro_m_BarnBurner:
  case Pyro_m_BovineBlazemaker:
  case Pyro_m_EarthSkyandFire:
  case Pyro_m_FlashFryer:
  case Pyro_m_TurbineTorcher:
  case Pyro_m_Autumn:
  case Pyro_m_PumpkinPatch:
  case Pyro_m_Nutcracker:
  case Pyro_m_Balloonicorn:
  case Pyro_m_Rainbow:
  case Pyro_m_CoffinNail:
  case Pyro_m_Warhawk:
    return family_flame;

  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_Autumn:
  case Demoman_m_MacabreWeb:
  case Demoman_m_Rainbow:
  case Demoman_m_SweetDreams:
  case Demoman_m_CoffinNail:
  case Demoman_m_TopShelf:
  case Demoman_m_Warhawk:
  case Demoman_m_ButcherBird:
    return family_grenade;

  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_SilverBotkillerStickybombLauncherMkI:
  case Demoman_s_GoldBotkillerStickybombLauncherMkI:
  case Demoman_s_RustBotkillerStickybombLauncherMkI:
  case Demoman_s_BloodBotkillerStickybombLauncherMkI:
  case Demoman_s_CarbonadoBotkillerStickybombLauncherMkI:
  case Demoman_s_DiamondBotkillerStickybombLauncherMkI:
  case Demoman_s_SilverBotkillerStickybombLauncherMkII:
  case Demoman_s_GoldBotkillerStickybombLauncherMkII:
  case Demoman_s_SuddenFlurry:
  case Demoman_s_CarpetBomber:
  case Demoman_s_BlastedBombardier:
  case Demoman_s_RooftopWrangler:
  case Demoman_s_LiquidAsset:
  case Demoman_s_PinkElephant:
  case Demoman_s_Autumn:
  case Demoman_s_PumpkinPatch:
  case Demoman_s_MacabreWeb:
  case Demoman_s_SweetDreams:
  case Demoman_s_CoffinNail:
  case Demoman_s_DressedtoKill:
  case Demoman_s_Blitzkrieg:
    return family_sticky;

  case Heavy_m_FestiveMinigun:
  case Heavy_m_IronCurtain:
  case Heavy_m_SilverBotkillerMinigunMkI:
  case Heavy_m_GoldBotkillerMinigunMkI:
  case Heavy_m_RustBotkillerMinigunMkI:
  case Heavy_m_BloodBotkillerMinigunMkI:
  case Heavy_m_CarbonadoBotkillerMinigunMkI:
  case Heavy_m_DiamondBotkillerMinigunMkI:
  case Heavy_m_SilverBotkillerMinigunMkII:
  case Heavy_m_GoldBotkillerMinigunMkII:
  case Heavy_m_KingoftheJungle:
  case Heavy_m_IronWood:
  case Heavy_m_AntiqueAnnihilator:
  case Heavy_m_WarRoom:
  case Heavy_m_CitizenPain:
  case Heavy_m_BrickHouse:
  case Heavy_m_MacabreWeb:
  case Heavy_m_PumpkinPatch:
  case Heavy_m_Nutcracker:
  case Heavy_m_BrainCandy:
  case Heavy_m_MisterCuddles:
  case Heavy_m_CoffinNail:
  case Heavy_m_DressedtoKill:
  case Heavy_m_TopShelf:
  case Heavy_m_ButcherBird:
    return family_minigun;

  case Engi_t_FestiveWrench:
  case Engi_t_SilverBotkillerWrenchMkI:
  case Engi_t_GoldBotkillerWrenchMkI:
  case Engi_t_RustBotkillerWrenchMkI:
  case Engi_t_BloodBotkillerWrenchMkI:
  case Engi_t_CarbonadoBotkillerWrenchMkI:
  case Engi_t_DiamondBotkillerWrenchMkI:
  case Engi_t_SilverBotkillerWrenchMkII:
  case Engi_t_GoldBotkillerWrenchMkII:
  case Engi_t_Nutcracker:
  case Engi_t_Autumn:
  case Engi_t_Boneyard:
  case Engi_t_DressedtoKill:
  case Engi_t_TopShelf:
  case Engi_t_TorquedtoHell:
  case Engi_t_Airwolf:
    return family_wrench;

  case Medic_s_FestiveMediGun:
  case Medic_s_SilverBotkillerMediGunMkI:
  case Medic_s_GoldBotkillerMediGunMkI:
  case Medic_s_RustBotkillerMediGunMkI:
  case Medic_s_BloodBotkillerMediGunMkI:
  case Medic_s_CarbonadoBotkillerMediGunMkI:
  case Medic_s_DiamondBotkillerMediGunMkI:
  case Medic_s_SilverBotkillerMediGunMkII:
  case Medic_s_GoldBotkillerMediGunMkII:
  case Medic_s_MaskedMender:
  case Medic_s_WrappedReviver:
  case Medic_s_ReclaimedReanimator:
  case Medic_s_CivilServant:
  case Medic_s_SparkofLife:
  case Medic_s_Wildwood:
  case Medic_s_FlowerPower:
  case Medic_s_DressedToKill:
  case Medic_s_HighRollers:
  case Medic_s_Blitzkrieg:
  case Medic_s_Corsair:
    return family_medigun;

  case Sniper_m_FestiveSniperRifle:
  case Sniper_m_SilverBotkillerSniperRifleMkI:
  case Sniper_m_GoldBotkillerSniperRifleMkI:
  case Sniper_m_RustBotkillerSniperRifleMkI:
  case Sniper_m_BloodBotkillerSniperRifleMkI:
  case Sniper_m_CarbonadoBotkillerSniperRifleMkI:
  case Sniper_m_DiamondBotkillerSniperRifleMkI:
  case Sniper_m_SilverBotkillerSniperRifleMkII:
  case Sniper_m_GoldBotkillerSniperRifleMkII:
  case Sniper_m_NightOwl:
  case Sniper_m_PurpleRange:
  case Sniper_m_LumberFromDownUnder:
  case Sniper_m_ShotintheDark:
  case Sniper_m_Bogtrotter:
  case Sniper_m_Thunderbolt:
  case Sniper_m_PumpkinPatch:
  case Sniper_m_Boneyard:
  case Sniper_m_Wildwood:
  case Sniper_m_Balloonicorn:
  case Sniper_m_Rainbow:
  case Sniper_m_CoffinNail:
  case Sniper_m_DressedtoKill:
  case Sniper_m_Airwolf:
    return family_sniper;

  case Sniper_s_FestiveSMG:
  case Sniper_s_WoodsyWidowmaker:
  case Sniper_s_PlaidPotshotter:
  case Sniper_s_TreadplateTormenter:
  case Sniper_s_TeamSprayer:
  case Sniper_s_LowProfile:
  case Sniper_s_Wildwood:
  case Sniper_s_BlueMew:
  case Sniper_s_HighRollers:
  case Sniper_s_Blitzkrieg:
    return family_smg;

  case Spy_t_FestiveKnife:
  case Spy_t_SilverBotkillerKnifeMkI:
  case Spy_t_GoldBotkillerKnifeMkI:
  case Spy_t_RustBotkillerKnifeMkI:
  case Spy_t_BloodBotkillerKnifeMkI:
  case Spy_t_CarbonadoBotkillerKnifeMkI:
  case Spy_t_DiamondBotkillerKnifeMkI:
  case Spy_t_SilverBotkillerKnifeMkII:
  case Spy_t_GoldBotkillerKnifeMkII:
  case Spy_t_Boneyard:
  case Spy_t_BlueMew:
  case Spy_t_BrainCandy:
  case Spy_t_StabbedtoHell:
  case Spy_t_DressedtoKill:
  case Spy_t_TopShelf:
  case Spy_t_Blitzkrieg:
  case Spy_t_Airwolf:
    return family_knife;

  case Spy_m_FestiveRevolver:
  case Spy_m_PsychedelicSlugger:
  case Spy_m_OldCountry:
  case Spy_m_Mayor:
  case Spy_m_DeadReckoner:
  case Spy_m_Wildwood:
  case Spy_m_MacabreWeb:
  case Spy_m_FlowerPower:
  case Spy_m_TopShelf:
  case Spy_m_Blitzkrieg:
    return family_revolver;

  case Scout_m_ForceANature:
  case Scout_m_FestiveForceANature:
  case Scout_m_TheShortstop:
  case Scout_m_TheSodaPopper:
  case Scout_m_TheBackScatter:
  case Scout_m_BabyFacesBlaster:
  case Scout_s_TheWinger:
  case Scout_t_TheHolyMackerel:
  case Scout_t_FestiveHolyMackerel:
  case Soldier_m_TheBlackBox:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
  case Soldier_s_TheReserveShooter:
  case Soldier_s_PanicAttack:
  case Soldier_t_TheDisciplinaryAction:
  case Pyro_m_TheDegreaser:
  case Pyro_m_TheBackburner:
  case Pyro_m_FestiveBackburner:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheScorchShot:
  case Pyro_t_ThePowerjack:
  case Pyro_t_TheBackScratcher:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_t_TheScotsmansSkullcutter:
  case Demoman_t_HorselessHeadlessHorsemannsHeadtaker:
  case Demoman_t_TheClaidheamhMor:
  case Demoman_t_ThePersianPersuader:
  case Heavy_m_TheBrassBeast:
  case Heavy_m_Tomislav:
  case Heavy_s_TheFamilyBusiness:
  case Engi_m_TheRescueRanger:
  case Engi_t_TheJag:
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Medic_t_TheUbersaw:
  case Medic_t_FestiveUbersaw:
  case Medic_t_Amputator:
  case Sniper_m_TheBazaarBargain:
  case Sniper_t_TheShahanshah:
    return family_unique;

  default:
    return family_none;
  }
}

int unusual_particle(int unusual) {
  switch (unusual) {
  case 1: return unusual_hot;
  case 2: return unusual_isotope;
  case 3: return unusual_cool;
  case 4: return unusual_energy_orb;
  default: return 0;
  }
}

float int_bits(int value) {
  float result;
  static_assert(sizeof(result) == sizeof(value));
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

bool functions_ready() {
  return attribute_definition_lookup != nullptr &&
      attribute_list_set_runtime_value != nullptr &&
      item_schema_lookup_map_original != nullptr;
}

void set_attribute(void* list, int index, float value) {
  const std::uintptr_t schema = item_schema_lookup_map_original();
  if (schema == 0) {
    return;
  }
  void* definition = attribute_definition_lookup(schema, index);
  if (definition == nullptr) {
    return;
  }
  attribute_list_set_runtime_value(list, definition, value);
}

void set_attribute_int(void* list, int index, int value) {
  set_attribute(list, index, int_bits(value));
}

int resolved_quality(const Skin& skin) {
  if (skin.quality >= 0) {
    return skin.quality;
  }
  if (skin.unusual != 0) {
    return quality_unusual;
  }
  if (skin.australium) {
    return quality_strange;
  }
  if (skin.paintkit != 0) {
    return quality_decorated;
  }
  return -1;
}

void apply_skin(Weapon* weapon, const Skin& skin, bool reskin) {
  if (weapon == nullptr) {
    return;
  }

  if (reskin || skin.paintkit != 0 || skin.australium) {
    weapon->set_def_id(static_cast<short>(redirect_index(weapon->get_def_id())));
  }

  const int quality = resolved_quality(skin);
  if (quality >= 0) {
    weapon->set_entity_quality(quality);
  }
  weapon->set_item_initialized(true);

  void* list = weapon->get_attribute_list();
  if (list == nullptr) {
    return;
  }

  if (skin.paintkit != 0) {
    set_attribute_int(list, attribute_paintkit, skin.paintkit);
    set_attribute(list, attribute_wear, skin.wear);
    set_attribute(list, attribute_inspect, 1.0f);
    set_attribute_int(list, attribute_seed, skin.seed);
    set_attribute_int(list, attribute_seed_hi, 0);
  }
  if (skin.australium) {
    set_attribute_int(list, attribute_australium, 1);
    set_attribute_int(list, attribute_loot_rarity, 1);
    set_attribute(list, attribute_style_override, 1.0f);
  }
  if (skin.festive) {
    set_attribute(list, attribute_festive, 1.0f);
  }
  if (skin.killstreak != 0) {
    set_attribute(list, attribute_killstreak_tier, static_cast<float>(skin.killstreak));
  }
  int sheen = skin.sheen;
  if (sheen == 0 && skin.killstreak != 0) {
    sheen = 1;
  }
  if (sheen != 0) {
    set_attribute(list, attribute_killstreak_sheen, static_cast<float>(sheen));
  }
  if (const int unusual = unusual_particle(skin.unusual); unusual != 0) {
    set_attribute(list, attribute_unusual_weapon, static_cast<float>(unusual));
  }
}

void mix(unsigned int& hash, unsigned int value) {
  hash = (hash ^ value) * 16777619u;
}

void mix_skin(unsigned int& hash, const Skin& skin) {
  mix(hash, static_cast<unsigned int>(skin.paintkit));
  mix(hash, static_cast<unsigned int>(skin.wear * 10000.0f));
  mix(hash, static_cast<unsigned int>(skin.seed));
  mix(hash, static_cast<unsigned int>(skin.quality + 1));
  mix(hash, skin.festive ? 1u : 0u);
  mix(hash, skin.australium ? 1u : 0u);
  mix(hash, static_cast<unsigned int>(skin.killstreak));
  mix(hash, static_cast<unsigned int>(skin.sheen));
  mix(hash, static_cast<unsigned int>(skin.unusual));
}

unsigned int config_fingerprint() {
  const auto& changer = config.visuals.skin_changer;
  unsigned int hash = 2166136261u;
  mix(hash, changer.enabled ? 1u : 0u);
  mix(hash, changer.reskin ? 1u : 0u);
  mix_skin(hash, changer.defaults);
  for (const auto& [skin_key, skin] : changer.weapons) {
    mix(hash, static_cast<unsigned int>(skin_key));
    mix_skin(hash, skin);
  }
  return hash;
}

unsigned int last_fingerprint = 0;
bool was_enabled = false;

void force_full_update() {
  if (client_state == nullptr) {
    return;
  }
  if (client_state_force_full_update != nullptr) {
    if (client_state->m_nDeltaTick == -1) {
      client_state->m_nDeltaTick = 0;
    }
    client_state_force_full_update(client_state);
    return;
  }
  client_state->m_nDeltaTick = -1;
}

Skin skin_for_weapon(int definition) {
  const int skin_key = key(definition);
  const auto found = config.visuals.skin_changer.weapons.find(skin_key);
  if (found != config.visuals.skin_changer.weapons.end() && !found->second.empty()) {
    return found->second;
  }
  return config.visuals.skin_changer.defaults;
}

}

int key(int definition) {
  return redirect_index(definition);
}

Skin get(int skin_key) {
  const auto found = config.visuals.skin_changer.weapons.find(skin_key);
  return found != config.visuals.skin_changer.weapons.end() ? found->second : Skin{};
}

void set(int skin_key, const Skin& skin) {
  if (skin_key < 0) {
    return;
  }
  if (skin.empty()) {
    config.visuals.skin_changer.weapons.erase(skin_key);
  } else {
    config.visuals.skin_changer.weapons[skin_key] = skin;
  }
}

void get_kits(int skin_key, std::vector<const char*>& names, std::vector<int>& ids) {
  names = { "None" };
  ids = { 0 };

  const int family = skin_key < 0 ? family_unique : paint_family_of(skin_key);
  if (skin_key >= 0 && family == family_none) {
    return;
  }

  for (const kit_entry& kit : kits) {
    if (kit.id == 0) {
      continue;
    }
    const int mk2 = mk2_family(kit.id);
    if (skin_key >= 0 && mk2 != family_none && (family == family_unique || mk2 != family)) {
      continue;
    }
    names.push_back(kit.name);
    ids.push_back(kit.id);
  }
}

const char* weapon_label(int skin_key) {
  switch (paint_family_of(skin_key)) {
  case family_scattergun: return "Scattergun";
  case family_pistol: return "Pistol";
  case family_rocket: return "Rocket launcher";
  case family_shotgun: return "Shotgun";
  case family_flame: return "Flame thrower";
  case family_grenade: return "Grenade launcher";
  case family_sticky: return "Stickybomb launcher";
  case family_minigun: return "Minigun";
  case family_wrench: return "Wrench";
  case family_medigun: return "Medigun";
  case family_sniper: return "Sniper rifle";
  case family_smg: return "SMG";
  case family_knife: return "Knife";
  case family_revolver: return "Revolver";
  case family_unique: return "This weapon";
  default: return "This weapon (no warpaints)";
  }
}

void apply() {
  if (!config.visuals.skin_changer.enabled || !functions_ready()) {
    if (was_enabled) {
      was_enabled = false;
      last_fingerprint = 0;
      force_full_update();
    }
    return;
  }
  was_enabled = true;

  const unsigned int fingerprint = config_fingerprint();
  if (fingerprint != last_fingerprint) {
    last_fingerprint = fingerprint;
    force_full_update();
  }

  if (engine == nullptr || entity_list == nullptr || !engine->is_in_game()) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  const bool reskin = config.visuals.skin_changer.reskin;
  for (int i = 0; i < Player::max_weapon_count; ++i) {
    Weapon* weapon = localplayer->get_weapon_at(i);
    if (weapon == nullptr) {
      continue;
    }
    const Skin skin = skin_for_weapon(weapon->get_def_id());
    if (skin.empty() && !reskin) {
      continue;
    }
    apply_skin(weapon, skin, reskin);
  }
}

void invalidate() {
  last_fingerprint = 0;
  was_enabled = false;
}

}
