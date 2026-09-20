/*
 * SkinChanger.hpp
 *
 *  Created on: May 4, 2017
 *      Author: nullifiedcat
 */

#pragma once

#include "common.hpp"

namespace hacks::tf2::skinchanger
{

class CAttributeList;
class CAttribute;

typedef void *ItemSchemaPtr_t;
typedef void *AttributeDefinitionPtr_t;

// FIXME move to separate header

typedef ItemSchemaPtr_t (*ItemSystem_t)(void);
typedef void *(*SetRuntimeAttributeValue_t)(CAttributeList *, AttributeDefinitionPtr_t, float);
typedef AttributeDefinitionPtr_t (*GetAttributeDefinition_t)(ItemSchemaPtr_t, int);
ItemSchemaPtr_t GetItemSchema(void);

extern const char *sig_GetItemSchema;
extern const char *sig_GetAttributeDefinition;
extern const char *sig_SetRuntimeAttributeValue;
extern ItemSystem_t ItemSystemFn;
extern GetAttributeDefinition_t GetAttributeDefinitionFn;
extern SetRuntimeAttributeValue_t SetRuntimeAttributeValueFn;

// TOTALLY NOT A PASTE.
// Seriously tho, it's modified at least.
// Credits: blackfire62

struct attribute_s
{
    uint16_t defidx;
    float value;
};

class CAttribute
{
public:
    CAttribute(uint16_t iAttributeDefinitionIndex, float flValue);

public:
    void *vtable;
    uint16_t defidx;
    float value;
    unsigned int pad01;
};

class CAttributeList
{
public:
    CAttributeList();
    float GetAttribute(int defindex);
    void SetAttribute(int index, float value);
    void RemoveAttribute(int index);

public:
    uint32_t unknown;
    CUtlVector<CAttribute, CUtlMemory<CAttribute>> m_Attributes;
};

enum class Attributes
{
    loot_rarity            = 2022,
    is_australium_item     = 2027,
    item_style_override    = 542,
    sheen                  = 2014,
    killstreak_tier        = 2025,
    set_item_texture_wear  = 725,
    paintkit_proto_defidx  = 834,
    custom_paintkit_seed   = 866,
    custom_paintkit_seed_hi = 867,
    inspect                = 731,
    unusual_weapon_effect  = 370,
    festive                = 2053
};

enum class UnusualEffects
{
    HOT = 701,
    ISOTOPE,
    COOL,
    ENERGY_ORB
};

enum class Sheens
{
    TEAM_SHINE = 1,
    DEADLY_DAFFODIL,
    MANNDARIN,
    MEAN_GREEN,
    AGONIZING_EMERALD,
    VILLAINOUS_VIOLET,
    HOT_ROD
};

struct def_attribute_modifier
{
    bool Default() const;
    void Apply(int entity);
    void Set(int id, float value);
    void Remove(int id);
    bool HasAttr(int id) const;
    int defidx{ 0 };
    int defidx_redirect{ 0 };
    int quality{ -1 };
    std::vector<attribute_s> modifiers{};
};

extern boost::unordered_flat_map<int, def_attribute_modifier> modifier_map;

def_attribute_modifier &GetModifier(int idx);

constexpr unsigned SERIALIZE_VERSION = 2;
void Save(std::string filename);
void Load(std::string filename, bool merge = false);

void FrameStageNotify(int stage);
void DrawText();
} // namespace hacks::tf2::skinchanger
