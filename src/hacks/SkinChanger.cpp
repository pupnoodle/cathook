/*
 * SkinChanger.cpp
 *
 *  Created on: May 4, 2017
 *      Author: nullifiedcat
 *
 *  Modern skin fields (warpaints, quality, killstreak, sheen, unusual
 *  effects, stock->decorated reskins) ported from puphook.
 */

#include "common.hpp"
#include "MiscTemporary.hpp"

#include <sys/dir.h>
#include <sys/stat.h>
#include <hacks/SkinChanger.hpp>
#include <hacks/SkinChangerDefids.hpp>
#include <settings/Bool.hpp>
#include <boost/functional/hash.hpp>

namespace hacks::tf2::skinchanger
{
static settings::Boolean enable{ "skinchanger.enable", "false" };
static settings::Boolean reskin{ "skinchanger.reskin", "false" };
static settings::Boolean debug{ "skinchanger.debug", "false" };

// Because fuck you, that's why.
const char *sig_GetAttributeDefinition   = sigs::attribute_definition_lookup;
const char *sig_SetRuntimeAttributeValue = sigs::attribute_list_set_runtime_value;
const char *sig_GetItemSchema            = sigs::item_schema_lookup_map;

ItemSystem_t ItemSystem{ nullptr };
GetAttributeDefinition_t GetAttributeDefinitionFn{ nullptr };
SetRuntimeAttributeValue_t SetRuntimeAttributeValueFn{ nullptr };
using GetItemDefinition_t = void *(*)(void *, unsigned);
static GetItemDefinition_t GetItemDefinitionFn{ nullptr };
using ForceFullUpdate_t = void (*)(CBaseClientState *);
static ForceFullUpdate_t ForceFullUpdateFn{ nullptr };

static bool force_item_update = false;
static bool menu_dirty = true;

static void request_update()
{
    force_item_update = true;
    menu_dirty        = true;
}

ItemSchemaPtr_t GetItemSchema(void)
{
    static void *schema = nullptr;
    if (!schema)
    {
        auto insn = gSignatures.GetClientSignature(sigs::item_schema_lookup_map);
        if (!insn)
            return nullptr;
        auto **slot = reinterpret_cast<void **>(cathook::core::memory::resolve_rip_relative(reinterpret_cast<void *>(insn), 3, 7));
        schema      = slot ? *slot : nullptr;
    }
    return schema;
}

CAttribute::CAttribute(uint16_t iAttributeDefinitionIndex, float flValue)
{
    defidx = iAttributeDefinitionIndex;
    value  = flValue;
}

float CAttributeList::GetAttribute(int defindex)
{
    for (int i = 0; i < m_Attributes.Count(); ++i)
    {
        const auto &a = m_Attributes[i];
        if (a.defidx == defindex)
        {
            return a.value;
        }
    }
    return 0.0f;
}

void CAttributeList::RemoveAttribute(int index)
{
    for (int i = 0; i < m_Attributes.Count(); ++i)
    {
        const auto &a = m_Attributes[i];
        if (a.defidx == index)
        {
            m_Attributes.Remove(i);
            return;
        }
    }
}

CAttributeList::CAttributeList()
{
}

void CAttributeList::SetAttribute(int index, float value)
{
    ItemSchemaPtr_t schema = GetItemSchema();
    if (!schema || !GetAttributeDefinitionFn || !SetRuntimeAttributeValueFn)
        return;
    AttributeDefinitionPtr_t attrib = GetAttributeDefinitionFn(schema, index);
    if (!attrib)
        return;
    SetRuntimeAttributeValueFn(this, attrib, value);
}

constexpr int attribute_paintkit          = 834;
constexpr int attribute_wear              = 725;
constexpr int attribute_seed              = 866;
constexpr int attribute_seed_hi           = 867;
constexpr int attribute_inspect           = 731;
constexpr int attribute_unusual_weapon    = 370;
constexpr int attribute_festive           = 2053;
constexpr int attribute_australium        = 2027;
constexpr int attribute_loot_rarity       = 2022;
constexpr int attribute_style_override    = 542;
constexpr int attribute_killstreak_tier   = 2025;
constexpr int attribute_killstreak_sheen  = 2014;

constexpr int quality_unusual   = 5;
constexpr int quality_strange   = 11;
constexpr int quality_decorated = 15;

constexpr int unusual_hot        = 701;
constexpr int unusual_isotope    = 702;
constexpr int unusual_cool       = 703;
constexpr int unusual_energy_orb = 704;

enum paint_family
{
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

struct kit_entry
{
    int id;
    const char *name;
};

constexpr kit_entry kits[] = {
    { 0, "None" },            { 102, "Wrapped Reviver Mk.II" },   { 104, "Carpet Bomber Mk.II" },
    { 105, "Masked Mender Mk.II" },     { 106, "Woodland Warrior Mk.II" }, { 109, "Forest Fire Mk.II" },
    { 112, "Backwoods Boomstick Mk.II" },   { 113, "Woodsy Widowmaker Mk.II" }, { 114, "Night Owl Mk.II" },
    { 120, "Iron Wood Mk.II" },       { 122, "Plaid Potshotter Mk.II" },   { 130, "Bovine Blazemaker Mk.II" },
    { 139, "Civil Servant Mk.II" },   { 143, "Smalltown Bringdown Mk.II" }, { 144, "Civic Duty Mk.II" },
    { 151, "Dead Reckoner Mk.II" },   { 160, "Autumn Mk.II" },       { 161, "Nutcracker Mk.II" },
    { 163, "Macabre Web Mk.II" },     { 200, "Bloom Buffed" },       { 201, "Quack Canvassed" },
    { 202, "Bank Rolled" },           { 203, "Merc Stained" },       { 204, "Kill Covered" },
    { 205, "Fire Glazed" },           { 206, "Pizza Polished" },     { 207, "Bonk Varnished" },
    { 208, "Star Crossed" },          { 209, "Clover Camo'd" },      { 210, "Freedom Wrapped" },
    { 211, "Cardboard Boxed" },       { 212, "Dream Piped" },        { 213, "Miami Element" },
    { 214, "Neo Tokyo" },             { 215, "Geometrical Teams" },  { 217, "Bomber Soul" },
    { 218, "Uranium" },               { 220, "Cabin Fevered" },      { 221, "Polar Surprise" },
    { 223, "Hana" },                  { 224, "Dovetailed" },         { 225, "Cosmic Calamity" },
    { 226, "Hazard Warning" },        { 228, "Mosaic" },             { 230, "Jazzy" },
    { 232, "Alien Tech" },            { 234, "Damascus and Mahogany" }, { 235, "Skull Study" },
    { 236, "Haunted Ghosts" },        { 237, "Spectral Shimmered" }, { 238, "Spirit of Halloween" },
    { 239, "Horror Holiday" },        { 240, "Totally Boned" },      { 241, "Electroshocked" },
    { 242, "Ghost Town" },            { 243, "Tumor Toasted" },      { 244, "Calavera Canvas" },
    { 245, "Snow Covered" },          { 246, "Frost Ornamented" },   { 247, "Smissmas Village" },
    { 248, "Igloo" },                 { 249, "Seriously Snowed" },   { 250, "Smissmas Camo" },
    { 251, "Sleighin' Style" },       { 252, "Alpine" },             { 253, "Gift Wrapped" },
    { 254, "Winterland Wrapped" },    { 255, "Helldriver" },         { 256, "Organ-ically Hellraised" },
    { 257, "Spectrum Splattered" },   { 258, "Candy Coated" },       { 259, "Pumpkin Pied" },
    { 260, "Sweet Toothed" },         { 261, "Crawlspace Critters" }, { 262, "Portal Plastered" },
    { 263, "Death Deluxe" },          { 264, "Raving Dead" },        { 265, "Eyestalker" },
    { 266, "Spider's Cluster" },      { 267, "Gourdy Green" },       { 268, "Mummified Mimic" },
    { 269, "Spider Season" },         { 270, "Gingerbread Winner" }, { 271, "Saccharine Striped" },
    { 272, "Elfin Enamel" },          { 273, "Peppermint Swirl" },   { 275, "Snow Globalization" },
    { 276, "Gifting Mann's Wrapping Paper" }, { 277, "Snowflake Swirled" }, { 278, "Smissmas Spycrabs" },
    { 279, "Frozen Aurora" },         { 280, "Starlight Serenity" }, { 281, "Frosty Delivery" },
    { 282, "Glacial Glazed" },        { 283, "Cookie Fortress" },    { 284, "Sarsaparilla Sprayed" },
    { 285, "Swashbuckled" },          { 286, "Skull Cracked" },      { 287, "Misfortunate" },
    { 289, "Neon-ween" },             { 290, "Simple Spirits" },     { 291, "Broken Bones" },
    { 292, "Potent Poison" },         { 293, "Searing Souls" },      { 294, "Party Phantoms" },
    { 295, "Polter-Guised" },         { 296, "Kiln and Conquer" },   { 297, "Necromanced" },
    { 300, "Yeti Coated" },           { 301, "Park Pigmented" },     { 302, "Mannana Peeled" },
    { 303, "Macaw Masked" },          { 304, "Sax Waxed" },          { 305, "Anodized Aloha" },
    { 306, "Bamboo Brushed" },        { 307, "Tiger Buffed" },       { 308, "Croc Dusted" },
    { 309, "Pina Polished" },         { 310, "Leopard Printed" },    { 390, "Dragon Slayer" },
    { 391, "Smissmas Sweater" },      { 400, "Ghoul Blaster" },      { 401, "Cream Corned" },
    { 402, "Sunriser" },              { 403, "Sacred Slayer" },      { 404, "Metalized Soul" },
    { 405, "Bonzo Gnawed" },          { 406, "Health and Hell" },    { 407, "Health and Hell (Green)" },
    { 408, "Hypergon" },              { 409, "Pumpkin Plastered" },  { 410, "Chilly Autumn" },
    { 411, "Steel Brushed" },         { 412, "Secretly Serviced" },  { 413, "Sky Stallion" },
    { 414, "Bomb Carrier" },          { 415, "Business Class" },     { 416, "Deadly Dragon" },
    { 417, "Team Serviced" },         { 418, "Warborn" },            { 419, "Pacific Peacemaker" },
    { 420, "Mechanized Monster" },    { 421, "Stardust" },           { 422, "Team Detail" },
    { 423, "Gobi Glazed" },           { 424, "Sleek Greek" },        { 425, "Graphite Gripped" },
    { 426, "Stealth Specialist" },    { 427, "Piranha Mania" },      { 428, "Team Charged" },
    { 429, "Brawler's Iron" },        { 430, "Necropolish" },        { 431, "Blackout" },
    { 432, "Broken Record" },         { 433, "Sandwich Diner" },     { 434, "Beachy Boy" },
    { 435, "Army Guns" },             { 436, "Taxi Cabbed" },        { 437, "Ocean Mapped" },
    { 438, "Krak-coated" },           { 439, "Team Union" },         { 440, "Sideshow" },
    { 441, "Storage War" },           { 442, "Die'n Dasher" },
};

int redirect_index(int definition)
{
    switch (definition)
    {
    case Soldier_m_RocketLauncher:
        return Soldier_m_RocketLauncherR;
    case Scout_m_Scattergun:
        return Scout_m_ScattergunR;
    case Pyro_m_FlameThrower:
        return Pyro_m_FlameThrowerR;
    case Demoman_m_GrenadeLauncher:
        return Demoman_m_GrenadeLauncherR;
    case Demoman_s_StickybombLauncher:
        return Demoman_s_StickybombLauncherR;
    case Heavy_m_Minigun:
        return Heavy_m_MinigunR;
    case Engi_t_Wrench:
        return Engi_t_WrenchR;
    case Medic_s_MediGun:
        return Medic_s_MediGunR;
    case Sniper_m_SniperRifle:
        return Sniper_m_SniperRifleR;
    case Sniper_s_SMG:
        return Sniper_s_SMGR;
    case Spy_t_Knife:
        return Spy_t_KnifeR;
    case Spy_m_Revolver:
        return Spy_m_RevolverR;
    case Scout_s_ScoutsPistol:
    case Engi_s_EngineersPistol:
        return Engi_s_PistolR;
    case Soldier_s_SoldiersShotgun:
    case Pyro_s_PyrosShotgun:
    case Heavy_s_HeavysShotgun:
    case Engi_m_EngineersShotgun:
        return Soldier_s_ShotgunR;
    case Scout_t_Bat:
        return Scout_t_BatR;
    case Soldier_t_Shovel:
        return Soldier_t_ShovelR;
    case Pyro_t_FireAxe:
        return Pyro_t_FireAxeR;
    case Demoman_t_Bottle:
        return Demoman_t_BottleR;
    case Medic_t_Bonesaw:
        return Medic_t_BonesawR;
    case Sniper_t_Kukri:
        return Sniper_t_KukriR;
    default:
        return definition;
    }
}

int mk2_family(int kit)
{
    switch (kit)
    {
    case 102:
    case 105:
    case 139:
        return family_medigun;
    case 104:
        return family_sticky;
    case 106:
    case 143:
        return family_rocket;
    case 109:
    case 130:
        return family_flame;
    case 112:
    case 144:
        return family_shotgun;
    case 113:
    case 122:
        return family_smg;
    case 114:
        return family_sniper;
    case 120:
        return family_minigun;
    case 151:
        return family_revolver;
    default:
        return family_none;
    }
}

int paint_family_of(int definition)
{
    switch (redirect_index(definition))
    {
    case Scout_m_ScattergunR:
        return family_scattergun;
    case Scout_s_PistolR:
        return family_pistol;
    case Soldier_m_RocketLauncherR:
        return family_rocket;
    case Soldier_s_ShotgunR:
        return family_shotgun;
    case Pyro_m_FlameThrowerR:
        return family_flame;
    case Demoman_m_GrenadeLauncherR:
        return family_grenade;
    case Demoman_s_StickybombLauncherR:
        return family_sticky;
    case Heavy_m_MinigunR:
        return family_minigun;
    case Engi_t_WrenchR:
        return family_wrench;
    case Medic_s_MediGunR:
        return family_medigun;
    case Sniper_m_SniperRifleR:
        return family_sniper;
    case Sniper_s_SMGR:
        return family_smg;
    case Spy_t_KnifeR:
        return family_knife;
    case Spy_m_RevolverR:
        return family_revolver;
    default:
        break;
    }

    switch (definition)
    {
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

int unusual_particle(int unusual)
{
    switch (unusual)
    {
    case 1:
        return unusual_hot;
    case 2:
        return unusual_isotope;
    case 3:
        return unusual_cool;
    case 4:
        return unusual_energy_orb;
    default:
        return 0;
    }
}

float int_bits(int value)
{
    float result;
    static_assert(sizeof(result) == sizeof(value));
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

const char *weapon_label(int skin_key)
{
    if (skin_key == defaults_key)
        return "All weapons";
    switch (paint_family_of(skin_key))
    {
    case family_scattergun:
        return "Scattergun";
    case family_pistol:
        return "Pistol";
    case family_rocket:
        return "Rocket launcher";
    case family_shotgun:
        return "Shotgun";
    case family_flame:
        return "Flame thrower";
    case family_grenade:
        return "Grenade launcher";
    case family_sticky:
        return "Stickybomb launcher";
    case family_minigun:
        return "Minigun";
    case family_wrench:
        return "Wrench";
    case family_medigun:
        return "Medigun";
    case family_sniper:
        return "Sniper rifle";
    case family_smg:
        return "SMG";
    case family_knife:
        return "Knife";
    case family_revolver:
        return "Revolver";
    case family_unique:
        return "This weapon";
    default:
        return "This weapon (no warpaints)";
    }
}

void get_kits(int skin_key, std::vector<const char *> &names, std::vector<int> &ids)
{
    names = { "None" };
    ids   = { 0 };

    const int family = skin_key < 0 ? family_unique : paint_family_of(skin_key);
    if (skin_key >= 0 && family == family_none)
        return;

    for (const kit_entry &kit : kits)
    {
        if (kit.id == 0)
            continue;
        const int mk2 = mk2_family(kit.id);
        if (skin_key >= 0 && mk2 != family_none && (family == family_unique || mk2 != family))
            continue;
        names.push_back(kit.name);
        ids.push_back(kit.id);
    }
}

static void force_full_update()
{
    if (!g_IBaseClientState)
        return;
    if (ForceFullUpdateFn)
    {
        if (g_IBaseClientState->m_nDeltaTick() == -1)
            g_IBaseClientState->m_nDeltaTick() = 0;
        ForceFullUpdateFn(g_IBaseClientState);
        return;
    }
    g_IBaseClientState->m_nDeltaTick() = -1;
}

static int skin_key(int defidx)
{
    return redirect_index(defidx);
}

static int active_weapon_defidx()
{
    if (CE_BAD(LOCAL_W))
        return -1;
    return NET_VAR(RAW_ENT(LOCAL_W), netvar.iItemDefinitionIndex, unsigned short);
}

static int active_skin_key()
{
    const int defidx = active_weapon_defidx();
    return defidx < 0 ? -1 : skin_key(defidx);
}

static int resolved_quality(const def_attribute_modifier &mod)
{
    if (mod.quality >= 0)
        return mod.quality;
    if (mod.HasAttr(attribute_unusual_weapon))
        return quality_unusual;
    if (mod.HasAttr(attribute_australium))
        return quality_strange;
    if (mod.HasAttr(attribute_paintkit))
        return quality_decorated;
    return -1;
}

static std::array<int, 46> australium_table{ 4, 7, 13, 14, 15, 16, 18, 19, 20, 21, 29, 36, 38, 45, 61, 132, 141, 194, 197, 200, 201, 202, 203, 205, 206, 207, 208, 211, 228, 424, 654, 658, 659, 662, 663, 664, 665, 669, 1000, 1004, 1006, 1007, 1078, 1082, 1085, 1149 };
static std::array<std::pair<int, int>, 12> redirects{ std::pair{ 264, 1071 }, std::pair{ 18, 205 }, std::pair{ 13, 200 }, std::pair{ 21, 208 }, std::pair{ 19, 206 }, std::pair{ 20, 207 }, std::pair{ 15, 202 }, std::pair{ 7, 197 }, std::pair{ 29, 211 }, std::pair{ 14, 201 }, std::pair{ 16, 203 }, std::pair{ 4, 194 } };

static CatCommand australize("australize", "Make everything australium",
                             []()
                             {
                                 enable = true;
                                 for (auto i : redirects)
                                     GetModifier(i.first).defidx_redirect = i.second;
                                 for (auto i : australium_table)
                                 {
                                     auto &mod = GetModifier(i);
                                     mod.Set(2027, int_bits(1));
                                     mod.Set(2022, int_bits(1));
                                     mod.Set(542, 1.0f);
                                 }
                                 request_update();
                             });
static CatCommand set_attr("skinchanger_set", "Set attribute on active weapon. Format: <attr defindex> <attr value>",
                           [](const CCommand &args)
                           {
                               if (args.ArgC() < 2)
                               {
                                   g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                   return;
                               }
                               enable = true;
                               try
                               {
                                   const int key = active_skin_key();
                                   if (key < 0)
                                       return;
                                   unsigned attrid = std::strtoul(args.Arg(1), nullptr, 10);
                                   float attrv     = std::strtof(args.Arg(2), nullptr);
                                   GetModifier(key).Set(attrid, attrv);
                                   request_update();
                               }
                               catch (const std::invalid_argument &)
                               {
                                   g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please pass a valid int\n");
                               }
                           });
static CatCommand remove_attr("skinchanger_remove", "Remove attribute",
                              [](const CCommand &args)
                              {
                                  if (args.ArgC() < 2)
                                  {
                                      g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                      return;
                                  }
                                  try
                                  {
                                      const int key = active_skin_key();
                                      if (key < 0)
                                          return;
                                      enable          = true;
                                      unsigned attrid = std::strtoul(args.Arg(1), nullptr, 10);
                                      GetModifier(key).Remove(attrid);
                                      request_update();
                                  }
                                  catch (const std::invalid_argument &)
                                  {
                                      g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please pass a valid int\n");
                                  }
                              });
static CatCommand set_redirect("skinchanger_redirect", "Set Redirect",
                               [](const CCommand &args)
                               {
                                   if (args.ArgC() < 2)
                                   {
                                       g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                       return;
                                   }
                                   try
                                   {
                                       const int defidx = active_weapon_defidx();
                                       if (defidx < 0)
                                           return;
                                       enable                                        = true;
                                       unsigned redirect                             = std::strtoul(args.Arg(1), nullptr, 10);
                                       GetModifier(defidx).defidx_redirect           = redirect;
                                       request_update();
                                   }
                                   catch (const std::invalid_argument &)
                                   {
                                       g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please pass a valid int\n");
                                   }
                               });
static CatCommand dump_attrs("skinchanger_debug_attrs", "Dump attributes",
                             []()
                             {
                                 CAttributeList *list = (CAttributeList *) ((uintptr_t) (RAW_ENT(LOCAL_W)) + netvar.AttributeList);
                                 logging::Info("ATTRIBUTE LIST: %i", list->m_Attributes.Size());
                                 for (int i = 0; i < list->m_Attributes.Size(); ++i)
                                 {
                                     logging::Info("%i %.2f", list->m_Attributes[i].defidx, list->m_Attributes[i].value);
                                 }
                             });

static CatCommand list_redirects("skinchanger_redirect_list", "Dump redirects",
                                 []()
                                 {
                                     for (const auto &mod : modifier_map)
                                     {
                                         if (mod.second.defidx_redirect)
                                         {
                                             g_ICvar->ConsoleColorPrintf(MENU_COLOR, "%d -> %d\n", mod.first, mod.second.defidx_redirect);
                                         }
                                     }
                                 });
static CatCommand save("skinchanger_save", "Save",
                       [](const CCommand &args)
                       {
                           std::string filename = "skinchanger";
                           if (args.ArgC() > 1)
                           {
                               filename = args.Arg(1);
                           }
                           Save(filename);
                       });
static CatCommand load("skinchanger_load", "Load",
                       [](const CCommand &args)
                       {
                           enable               = true;
                           std::string filename = "skinchanger";
                           if (args.ArgC() > 1)
                           {
                               filename = args.Arg(1);
                           }
                           Load(filename);
                           request_update();
                       });
static CatCommand load_merge("skinchanger_load_merge", "Load with merge",
                             [](const CCommand &args)
                             {
                                 enable               = true;
                                 std::string filename = "skinchanger";
                                 if (args.ArgC() > 1)
                                 {
                                     filename = args.Arg(1);
                                 }
                                 Load(filename, true);
                                 request_update();
                             });
static CatCommand remove_redirect("skinchanger_remove_redirect", "Remove redirect",
                                  [](const CCommand &args)
                                  {
                                      if (args.ArgC() < 2)
                                      {
                                          g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an argument\n");
                                      }
                                      try
                                      {
                                          if (CE_BAD(LOCAL_W))
                                              return;
                                          enable                                  = true;
                                          unsigned redirectid                     = std::strtoul(args.Arg(1), nullptr, 10);
                                          GetModifier(redirectid).defidx_redirect = 0;
                                          logging::Info("Redirect removed");
                                          request_update();
                                      }
                                      catch (const std::invalid_argument &)
                                      {
                                          g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please supply a valid int\n");
                                      }
                                  });
static CatCommand reset("skinchanger_reset", "Reset",
                        []()
                        {
                            modifier_map.clear();
                            request_update();
                        });

static CatCommand dump_itemdef("skinchanger_dump_def", "Dump CEconItemDefinition for the active weapon",
                               []()
                               {
                                   if (!GetItemDefinitionFn)
                                       GetItemDefinitionFn = GetItemDefinition_t(gSignatures.GetClientSignature(sigs::item_definition_lookup));
                                   void *schema = GetItemSchema();
                                   if (CE_BAD(LOCAL_W) || !schema || !GetItemDefinitionFn)
                                   {
                                       logging::Info("item definition lookup unavailable");
                                       return;
                                   }
                                   unsigned def = unsigned(active_weapon_defidx());
                                   logging::Info("item definition %u -> %p", def, GetItemDefinitionFn(schema, def));
                               });

static void set_attr_int(def_attribute_modifier &mod, int index, int value)
{
    mod.Set(index, int_bits(value));
}

static bool name_contains(const char *name, const char *needle)
{
    std::string n = name, s = needle;
    for (auto &c : n)
        c = char(std::tolower((unsigned char) c));
    for (auto &c : s)
        c = char(std::tolower((unsigned char) c));
    return n.find(s) != std::string::npos;
}

static CatCommand set_paint("skinchanger_paint", "Set warpaint on active weapon. Format: <kit id|name|none>",
                            [](const CCommand &args)
                            {
                                const int key = active_skin_key();
                                if (key < 0)
                                    return;
                                if (args.ArgC() < 2)
                                {
                                    g_ICvar->ConsoleColorPrintf(MENU_COLOR, "%s kits:\n", weapon_label(key));
                                    std::vector<const char *> names;
                                    std::vector<int> ids;
                                    get_kits(key, names, ids);
                                    for (size_t i = 0; i < ids.size(); ++i)
                                        g_ICvar->ConsoleColorPrintf(MENU_COLOR, "  %4d  %s\n", ids[i], names[i]);
                                    return;
                                }
                                int kit         = -1;
                                const char *arg = args.ArgS();
                                kit             = std::atoi(arg);
                                if (!kit && std::strcmp(arg, "0") && !name_contains("none", arg))
                                {
                                    for (const auto &k : kits)
                                    {
                                        if (k.id && name_contains(k.name, arg))
                                        {
                                            kit = k.id;
                                            break;
                                        }
                                    }
                                }
                                if (kit < 0)
                                {
                                    g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Unknown kit '%s'\n", arg);
                                    return;
                                }
                                enable       = true;
                                auto &mod    = GetModifier(key);
                                if (!kit)
                                {
                                    mod.Remove(attribute_paintkit);
                                    mod.Remove(attribute_wear);
                                    mod.Remove(attribute_inspect);
                                    mod.Remove(attribute_seed);
                                    mod.Remove(attribute_seed_hi);
                                    logging::Info("Warpaint removed from %i", key);
                                }
                                else
                                {
                                    const int mk2 = mk2_family(kit);
                                    const int fam = paint_family_of(key);
                                    if (mk2 != family_none && fam != mk2)
                                        g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Warning: kit %d is not for %s\n", kit, weapon_label(key));
                                    set_attr_int(mod, attribute_paintkit, kit);
                                    mod.Set(attribute_inspect, 1.0f);
                                    set_attr_int(mod, attribute_seed_hi, 0);
                                    if (!mod.HasAttr(attribute_wear))
                                        mod.Set(attribute_wear, 0.0f);
                                    if (!mod.HasAttr(attribute_seed))
                                        set_attr_int(mod, attribute_seed, 0);
                                    logging::Info("Set kit %i on %s (%i)", kit, weapon_label(key), key);
                                }
                                request_update();
                            });
static CatCommand set_wear("skinchanger_wear", "Set paint wear on active weapon (0.0 - 1.0)",
                           [](const CCommand &args)
                           {
                               const int key = active_skin_key();
                               if (key < 0)
                                   return;
                               if (args.ArgC() < 2)
                               {
                                   g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                   return;
                               }
                               enable = true;
                               GetModifier(key).Set(attribute_wear, std::clamp(std::strtof(args.Arg(1), nullptr), 0.0f, 1.0f));
                               request_update();
                           });
static CatCommand set_seed("skinchanger_seed", "Set paint seed on active weapon",
                           [](const CCommand &args)
                           {
                               const int key = active_skin_key();
                               if (key < 0)
                                   return;
                               if (args.ArgC() < 2)
                               {
                                   g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                   return;
                               }
                               enable       = true;
                               auto &mod    = GetModifier(key);
                               set_attr_int(mod, attribute_seed, std::atoi(args.Arg(1)));
                               set_attr_int(mod, attribute_seed_hi, 0);
                               request_update();
                           });
static CatCommand set_quality("skinchanger_quality", "Set item quality on active weapon (-1 = automatic)",
                              [](const CCommand &args)
                              {
                                  const int key = active_skin_key();
                                  if (key < 0)
                                      return;
                                  if (args.ArgC() < 2)
                                  {
                                      g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                      return;
                                  }
                                  enable                    = true;
                                  GetModifier(key).quality  = std::atoi(args.Arg(1));
                                  request_update();
                              });
static CatCommand set_australium("skinchanger_australium", "Toggle australium on active weapon",
                                 []()
                                 {
                                     const int key = active_skin_key();
                                     if (key < 0)
                                         return;
                                     enable    = true;
                                     auto &mod = GetModifier(key);
                                     if (mod.HasAttr(attribute_australium))
                                     {
                                         mod.Remove(attribute_australium);
                                         mod.Remove(attribute_loot_rarity);
                                         mod.Remove(attribute_style_override);
                                     }
                                     else
                                     {
                                         set_attr_int(mod, attribute_australium, 1);
                                         set_attr_int(mod, attribute_loot_rarity, 1);
                                         mod.Set(attribute_style_override, 1.0f);
                                     }
                                     request_update();
                                 });
static CatCommand set_festive("skinchanger_festive", "Toggle festivizer on active weapon",
                              []()
                              {
                                  const int key = active_skin_key();
                                  if (key < 0)
                                      return;
                                  enable    = true;
                                  auto &mod = GetModifier(key);
                                  if (mod.HasAttr(attribute_festive))
                                      mod.Remove(attribute_festive);
                                  else
                                      mod.Set(attribute_festive, 1.0f);
                                  request_update();
                              });
static CatCommand set_unusual("skinchanger_unusual", "Set unusual weapon effect on active weapon (0 = off, 1 = hot, 2 = isotope, 3 = cool, 4 = energy orb)",
                              [](const CCommand &args)
                              {
                                  const int key = active_skin_key();
                                  if (key < 0)
                                      return;
                                  if (args.ArgC() < 2)
                                  {
                                      g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                      return;
                                  }
                                  enable       = true;
                                  auto &mod    = GetModifier(key);
                                  const int fx = unusual_particle(std::atoi(args.Arg(1)));
                                  if (fx)
                                      mod.Set(attribute_unusual_weapon, float(fx));
                                  else
                                      mod.Remove(attribute_unusual_weapon);
                                  request_update();
                              });
static CatCommand set_killstreak("skinchanger_killstreak", "Set killstreak tier on active weapon (0 - 3)",
                                 [](const CCommand &args)
                                 {
                                     const int key = active_skin_key();
                                     if (key < 0)
                                         return;
                                     if (args.ArgC() < 2)
                                     {
                                         g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                         return;
                                     }
                                     enable         = true;
                                     auto &mod      = GetModifier(key);
                                     const int tier = std::atoi(args.Arg(1));
                                     if (tier > 0)
                                     {
                                         mod.Set(attribute_killstreak_tier, float(tier));
                                         if (!mod.HasAttr(attribute_killstreak_sheen))
                                             mod.Set(attribute_killstreak_sheen, 1.0f);
                                     }
                                     else
                                     {
                                         mod.Remove(attribute_killstreak_tier);
                                         mod.Remove(attribute_killstreak_sheen);
                                     }
                                     request_update();
                                 });
static CatCommand set_sheen("skinchanger_sheen", "Set killstreak sheen on active weapon (0 - 7)",
                            [](const CCommand &args)
                            {
                                const int key = active_skin_key();
                                if (key < 0)
                                    return;
                                if (args.ArgC() < 2)
                                {
                                    g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please Provide an Argument\n");
                                    return;
                                }
                                enable          = true;
                                auto &mod       = GetModifier(key);
                                const int sheen = std::atoi(args.Arg(1));
                                if (sheen > 0)
                                    mod.Set(attribute_killstreak_sheen, float(sheen));
                                else
                                    mod.Remove(attribute_killstreak_sheen);
                                request_update();
                            });
static CatCommand list_kits("skinchanger_kits", "List warpaints for the active weapon",
                            []()
                            {
                                const int key = active_skin_key();
                                if (key < 0)
                                    return;
                                std::vector<const char *> names;
                                std::vector<int> ids;
                                get_kits(key, names, ids);
                                g_ICvar->ConsoleColorPrintf(MENU_COLOR, "%s (%d):\n", weapon_label(key), key);
                                for (size_t i = 0; i < ids.size(); ++i)
                                    g_ICvar->ConsoleColorPrintf(MENU_COLOR, "  %4d  %s\n", ids[i], names[i]);
                            });
static CatCommand list_modifiers("skinchanger_list", "List skinchanger modifiers",
                                 []()
                                 {
                                     for (const auto &entry : modifier_map)
                                     {
                                         const auto &mod = entry.second;
                                         if (mod.Default())
                                             continue;
                                         std::string attrs;
                                         for (const auto &a : mod.modifiers)
                                             attrs += format(a.defidx, '=', a.value, ' ');
                                         g_ICvar->ConsoleColorPrintf(MENU_COLOR, "%d (%s): redirect=%d quality=%d attrs: %s\n", entry.first, weapon_label(entry.first), mod.defidx_redirect, mod.quality, attrs.c_str());
                                     }
                                 });

void FrameStageNotify(int stage)
{
    static int my_weapon, handle, eid, *weapon_list;
    static IClientEntity *entity, *my_weapon_ptr;
    static bool was_enabled = false, was_reskin = false;

    if (stage != FRAME_NET_UPDATE_POSTDATAUPDATE_START)
        return;

    if (bool(enable) != was_enabled || bool(reskin) != was_reskin)
    {
        was_enabled       = bool(enable);
        was_reskin        = bool(reskin);
        request_update();
    }
    if (force_item_update)
    {
        force_item_update = false;
        force_full_update();
    }
    if (!enable)
        return;
    if (CE_BAD(LOCAL_E) || !netvar.hMyWeapons)
        return;

    if (!SetRuntimeAttributeValueFn)
    {
        SetRuntimeAttributeValueFn = (SetRuntimeAttributeValue_t) (gSignatures.GetClientSignature((char *) sig_SetRuntimeAttributeValue));
        logging::Info("SetRuntimeAttributeValue: %p", SetRuntimeAttributeValueFn);
    }
    if (!GetAttributeDefinitionFn)
    {
        GetAttributeDefinitionFn = (GetAttributeDefinition_t) (gSignatures.GetClientSignature((char *) sig_GetAttributeDefinition));
        logging::Info("GetAttributeDefinition: %p", GetAttributeDefinitionFn);
    }
    if (!GetItemDefinitionFn)
    {
        GetItemDefinitionFn = GetItemDefinition_t(gSignatures.GetClientSignature(sigs::item_definition_lookup));
        logging::Info("GetItemDefinition: %p", GetItemDefinitionFn);
    }
    if (!ForceFullUpdateFn)
    {
        ForceFullUpdateFn = ForceFullUpdate_t(gSignatures.GetEngineSignature(sigs::client_state_force_full_update));
        logging::Info("ForceFullUpdate: %p", ForceFullUpdateFn);
    }

    weapon_list   = (int *) ((uintptr_t) (RAW_ENT(LOCAL_E)) + netvar.hMyWeapons);
    my_weapon     = CE_INT(g_pLocalPlayer->entity, netvar.hActiveWeapon);
    my_weapon_ptr = g_IEntityList->GetClientEntity(HandleToIDX(my_weapon));
    if (!my_weapon_ptr)
        return;
    if (!re::C_BaseCombatWeapon::IsBaseCombatWeapon(my_weapon_ptr))
        return;
    for (int i = 0; i < 48; ++i)
    {
        handle = weapon_list[i];
        eid    = HandleToIDX(handle);
        if (eid <= MAX_PLAYERS || eid > HIGHEST_ENTITY)
            continue;
        entity = g_IEntityList->GetClientEntity(eid);
        if (!entity)
            continue;
        if (!re::C_BaseCombatWeapon::IsBaseCombatWeapon(entity))
            continue;
        const int defidx = NET_VAR(entity, netvar.iItemDefinitionIndex, unsigned short);
        if (!defidx)
            continue;
        auto it = modifier_map.find(defidx);
        if (it == modifier_map.end())
        {
            const int key = skin_key(defidx);
            if (key != defidx)
                it = modifier_map.find(key);
        }
        if (it == modifier_map.end() || it->second.Default())
            it = modifier_map.find(defaults_key);
        if (it == modifier_map.end() || it->second.Default())
        {
            if (reskin)
            {
                const int redirected = redirect_index(defidx);
                if (redirected != defidx)
                    NET_VAR(entity, netvar.iItemDefinitionIndex, unsigned short) = static_cast<unsigned short>(redirected);
            }
            continue;
        }
        it->second.Apply(eid);
    }
}

void DrawText()
{
    CAttributeList *list;

    if (!enable)
        return;
    if (!debug)
        return;
    if (CE_GOOD(LOCAL_W))
    {
        AddSideString(format("dIDX: ", active_weapon_defidx(), " (", weapon_label(active_skin_key()), ")"));
        list = (CAttributeList *) ((uintptr_t) (RAW_ENT(LOCAL_W)) + netvar.AttributeList);
        for (int i = 0; i < list->m_Attributes.Size(); ++i)
        {
            AddSideString(format('[', i, "] ", list->m_Attributes[i].defidx, ": ", list->m_Attributes[i].value));
        }
    }
}

#define BINARY_FILE_WRITE(handle, data) handle.write(reinterpret_cast<const char *>(&data), sizeof(data))
#define BINARY_FILE_READ(handle, data) handle.read(reinterpret_cast<char *>(&data), sizeof(data))

void Save(std::string filename)
{
    DIR *cathook_directory = opendir(paths::getDataPath("/skinchanger").c_str());
    if (!cathook_directory)
    {
        logging::Info("Skinchanger directory doesn't exist, creating one!");
        mkdir(paths::getDataPath("/skinchanger").c_str(), S_IRWXU | S_IRWXG);
    }
    else
        closedir(cathook_directory);
    try
    {
        std::ofstream file(paths::getDataPath("/skinchanger/" + filename), std::ios::out | std::ios::binary);
        BINARY_FILE_WRITE(file, SERIALIZE_VERSION);
        size_t size = modifier_map.size();
        BINARY_FILE_WRITE(file, size);
        for (const auto &item : modifier_map)
        {
            BINARY_FILE_WRITE(file, item.first);
            // modifier data isn't a POD (it contains a vector), we can't
            // BINARY_WRITE it completely.
            BINARY_FILE_WRITE(file, item.second.defidx_redirect);
            BINARY_FILE_WRITE(file, item.second.quality);
            const auto &modifiers = item.second.modifiers;
            size_t modifier_count = modifiers.size();
            BINARY_FILE_WRITE(file, modifier_count);
            // this code is a bit tricky - I'm treating vector as an array
            if (modifier_count)
            {
                file.write(reinterpret_cast<const char *>(modifiers.data()), modifier_count * sizeof(attribute_s));
            }
        }
        file.close();
        logging::Info("Writing successful");
    }
    catch (std::exception &e)
    {
        logging::Info("Writing unsuccessful: %s", e.what());
    }
}

void Load(std::string filename, bool merge)
{
    DIR *cathook_directory = opendir(paths::getDataPath("/skinchanger").c_str());
    if (!cathook_directory)
    {
        logging::Info("Skinchanger directory doesn't exist, creating one!");
        mkdir(paths::getDataPath("/skinchanger").c_str(), S_IRWXU | S_IRWXG);
    }
    else
        closedir(cathook_directory);
    try
    {
        std::ifstream file(paths::getDataPath("/skinchanger/" + filename), std::ios::in | std::ios::binary);
        unsigned file_serialize = 0;
        BINARY_FILE_READ(file, file_serialize);
        if (file_serialize != SERIALIZE_VERSION && file_serialize != 1)
        {
            logging::Info("Outdated/corrupted SkinChanger file! Cannot load this.");
            file.close();
            return;
        }
        size_t size = 0;
        BINARY_FILE_READ(file, size);
        logging::Info("Reading %i entries...", size);
        if (!merge)
            modifier_map.clear();
        for (int i = 0; i < size; ++i)
        {
            int defindex;
            BINARY_FILE_READ(file, defindex);
            size_t count;
            def_attribute_modifier modifier;
            BINARY_FILE_READ(file, modifier.defidx_redirect);
            if (file_serialize >= 2)
                BINARY_FILE_READ(file, modifier.quality);
            BINARY_FILE_READ(file, count);
            modifier.modifiers.resize(count);
            file.read(reinterpret_cast<char *>(modifier.modifiers.data()), sizeof(attribute_s) * count);
            if (!merge)
            {
                modifier_map.insert(std::make_pair(defindex, std::move(modifier)));
            }
            else
            {
                if (!modifier.Default())
                {
                    modifier_map[defindex] = modifier;
                }
            }
        }
        file.close();
        logging::Info("Reading successful! Result: %i entries.", modifier_map.size());
    }
    catch (std::exception &e)
    {
        logging::Info("Reading unsuccessful: %s", e.what());
    }
}

void def_attribute_modifier::Set(int id, float value)
{
    for (auto &i : modifiers)
    {
        if (i.defidx == id)
        {
            i.value = value;
            return;
        }
    }
    if (modifiers.size() > 13)
    {
        logging::Info("Woah there, that's too many! Remove some.");
        return;
    }
    modifiers.push_back(attribute_s{ (uint16_t) id, value });
    logging::Info("Added new attribute: %i %.2f (%i)", id, value, modifiers.size());
}

void def_attribute_modifier::Remove(int id)
{
    auto it = modifiers.begin();
    while (it != modifiers.end())
    {
        if ((*it).defidx == id)
        {
            it = modifiers.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool def_attribute_modifier::HasAttr(int id) const
{
    for (const auto &i : modifiers)
        if (i.defidx == id)
            return true;
    return false;
}

bool def_attribute_modifier::Default() const
{
    return defidx_redirect == 0 && quality < 0 && modifiers.empty();
}

void def_attribute_modifier::Apply(int entity)
{
    IClientEntity *ent;
    CAttributeList *list;

    ent = g_IEntityList->GetClientEntity(entity);
    if (!ent)
        return;
    if (!re::C_BaseCombatWeapon::IsBaseCombatWeapon(ent))
        return;
    const int defidx = NET_VAR(ent, netvar.iItemDefinitionIndex, unsigned short);
    int target       = defidx;
    if (defidx_redirect)
        target = defidx_redirect;
    else if (reskin || HasAttr(attribute_paintkit) || HasAttr(attribute_australium))
        target = redirect_index(defidx);
    if (target != defidx)
    {
        NET_VAR(ent, netvar.iItemDefinitionIndex, unsigned short) = static_cast<unsigned short>(target);
        if (debug)
            logging::Info("Redirect -> %i", target);
    }
    const int item_quality = resolved_quality(*this);
    if (item_quality >= 0)
        NET_VAR(ent, netvar.iEntityQuality, int) = item_quality;
    NET_VAR(ent, netvar.bInitialized, unsigned char) = 1;
    list = (CAttributeList *) ((uintptr_t) ent + netvar.AttributeList);
    for (const auto &mod : modifiers)
    {
        if (mod.defidx)
        {
            list->SetAttribute(mod.defidx, mod.value);
        }
    }
    if (defidx_redirect && target != defidx)
    {
        auto it = modifier_map.find(defidx_redirect);
        if (it != modifier_map.end() && !it->second.Default())
            it->second.Apply(entity);
    }
}

def_attribute_modifier &GetModifier(int idx)
{
    try
    {
        return modifier_map.at(idx);
    }
    catch (std::out_of_range &oor)
    {
        modifier_map.emplace(idx, def_attribute_modifier{});
        return modifier_map.at(idx);
    }
}
// A map that maps an Item Definition Index to a modifier
boost::unordered_flat_map<int, def_attribute_modifier> modifier_map{};

static float attr_flt(const def_attribute_modifier &mod, int id)
{
    for (const auto &a : mod.modifiers)
        if (a.defidx == id)
            return a.value;
    return 0.0f;
}

static int attr_int(const def_attribute_modifier &mod, int id)
{
    const float f = attr_flt(mod, id);
    int i;
    std::memcpy(&i, &f, sizeof(i));
    return i;
}

const char *kit_name(int id)
{
    for (const auto &k : kits)
        if (k.id == id)
            return k.name;
    return nullptr;
}

int ActiveSkinKey()
{
    return active_skin_key();
}

bool ConsumeMenuDirty()
{
    const bool dirty = menu_dirty;
    menu_dirty       = false;
    return dirty;
}

SkinConfig GetSkinConfig(int key)
{
    SkinConfig s;
    const auto it = modifier_map.find(key);
    if (it == modifier_map.end())
        return s;
    const auto &mod = it->second;
    s.paintkit     = attr_int(mod, attribute_paintkit);
    s.wear         = attr_flt(mod, attribute_wear);
    s.seed         = attr_int(mod, attribute_seed);
    s.quality      = mod.quality;
    s.festive      = mod.HasAttr(attribute_festive);
    s.australium   = mod.HasAttr(attribute_australium);
    s.killstreak   = int(attr_flt(mod, attribute_killstreak_tier));
    s.sheen        = int(attr_flt(mod, attribute_killstreak_sheen));
    switch (int(attr_flt(mod, attribute_unusual_weapon)))
    {
    case unusual_hot:
        s.unusual = 1;
        break;
    case unusual_isotope:
        s.unusual = 2;
        break;
    case unusual_cool:
        s.unusual = 3;
        break;
    case unusual_energy_orb:
        s.unusual = 4;
        break;
    }
    return s;
}

void SetSkinConfig(int key, const SkinConfig &s)
{
    auto &mod = GetModifier(key);
    if (s.paintkit)
    {
        set_attr_int(mod, attribute_paintkit, s.paintkit);
        mod.Set(attribute_inspect, 1.0f);
        set_attr_int(mod, attribute_seed_hi, 0);
        mod.Set(attribute_wear, std::clamp(s.wear, 0.0f, 1.0f));
        set_attr_int(mod, attribute_seed, s.seed);
    }
    else
    {
        mod.Remove(attribute_paintkit);
        mod.Remove(attribute_wear);
        mod.Remove(attribute_inspect);
        mod.Remove(attribute_seed);
        mod.Remove(attribute_seed_hi);
    }
    if (s.australium)
    {
        set_attr_int(mod, attribute_australium, 1);
        set_attr_int(mod, attribute_loot_rarity, 1);
        mod.Set(attribute_style_override, 1.0f);
    }
    else
    {
        mod.Remove(attribute_australium);
        mod.Remove(attribute_loot_rarity);
        mod.Remove(attribute_style_override);
    }
    if (s.festive)
        mod.Set(attribute_festive, 1.0f);
    else
        mod.Remove(attribute_festive);
    if (s.killstreak > 0)
        mod.Set(attribute_killstreak_tier, float(s.killstreak));
    else
        mod.Remove(attribute_killstreak_tier);
    const int sheen = s.sheen ? s.sheen : (s.killstreak > 0 ? 1 : 0);
    if (sheen)
        mod.Set(attribute_killstreak_sheen, float(sheen));
    else
        mod.Remove(attribute_killstreak_sheen);
    if (const int fx = unusual_particle(s.unusual))
        mod.Set(attribute_unusual_weapon, float(fx));
    else
        mod.Remove(attribute_unusual_weapon);
    mod.quality = s.quality;

    enable = true;
    request_update();
}
} // namespace hacks::tf2::skinchanger
