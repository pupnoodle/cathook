#include "common.hpp"
#include "copypasted/Netvar.h"
#include "sdk/datamap.hpp"
#include "sdk/dt_recv_redef.h"
#include "core/resolve.hpp"
#include "core/sigs.hpp"
#include "core/maps.hpp"
#include <chrono>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <unordered_map>
#include <vector>

NetVars netvar;

static_assert(offsetof(RecvPropRedef, m_ProxyFn) == 0x30, "RecvProp::m_ProxyFn");
static_assert(offsetof(RecvPropRedef, m_Offset) == 0x48, "RecvProp::m_Offset");

static int proxy_store_disp(const void *fn)
{
    const auto *code = static_cast<const std::uint8_t *>(fn);
    if (!code)
        return 0;
    for (std::size_t i = 0; i < 64; ++i)
    {
        std::size_t j = i;
        bool rex_b    = false;
        while (code[j] >= 0x40 && code[j] <= 0x4F && j - i < 3)
        {
            rex_b = rex_b || (code[j] & 1) != 0;
            ++j;
        }
        if (code[j] != 0x88 && code[j] != 0x89)
            continue;
        const std::uint8_t modrm = code[j + 1];
        const int mod            = modrm >> 6;
        const int rm             = modrm & 7;
        if (rm != 6 || mod == 0 || mod == 3 || rex_b)
            continue;
        if (mod == 1)
            return code[j + 2];
        return cathook::core::memory::read_disp32(code + j, 2);
    }
    return 0;
}

static bool readable(const void *address, std::size_t bytes)
{
    if (!address)
        return false;
    const int protection = cathook::core::memory::protection_at(address);
    if (protection < 0 || (protection & PROT_READ) == 0)
        return false;
    if (bytes <= 1)
        return true;
    const auto *start = static_cast<const std::uint8_t *>(address);
    return cathook::core::memory::protection_at(start + bytes - 1) >= 0;
}

static bool looks_like_datamap(const datamap_t *map)
{
    if (!readable(map, sizeof(datamap_t)))
        return false;
    if (map->dataNumFields <= 0 || map->dataNumFields > 1024)
        return false;
    if (map->packed_size < 0 || map->packed_size > (1 << 20))
        return false;
    if (!readable(map->dataClassName, 8))
        return false;
    const char *name = map->dataClassName;
    std::size_t length = 0;
    while (length < 64 && name[length] != '\0')
        ++length;
    if (length < 3 || length >= 64)
        return false;
    return std::strstr(name, "Player") || std::strstr(name, "Entity") || std::strstr(name, "Animating") || std::strstr(name, "Weapon") || std::strstr(name, "Object");
}

static datamap_t *decode_map(void *function)
{
    if (!readable(function, 16))
        return nullptr;
    auto *code = static_cast<std::uint8_t *>(function);
    if (code[0] == 0xF3 && code[1] == 0x0F && code[2] == 0x1E && code[3] == 0xFA)
        code += 4;
    if (code[0] == 0x55)
        code += (code[1] == 0x48 && code[2] == 0x89 && code[3] == 0xE5) ? 4 : 1;
    if (code[0] == 0x48 && code[1] == 0x8D && code[2] == 0x05)
    {
        auto *map = static_cast<datamap_t *>(cathook::core::memory::resolve_rip_relative(code, 3, 7));
        return looks_like_datamap(map) ? map : nullptr;
    }
    if (code[0] == 0x48 && code[1] == 0x8B && code[2] == 0x05)
    {
        auto **slot = static_cast<void **>(cathook::core::memory::resolve_rip_relative(code, 3, 7));
        if (!readable(slot, sizeof(void *)))
            return nullptr;
        auto *map = static_cast<datamap_t *>(*slot);
        return looks_like_datamap(map) ? map : nullptr;
    }
    return nullptr;
}

datamap_t *PredDescMap(void *entity)
{
    if (!entity)
        return nullptr;
    void **vtable = *reinterpret_cast<void ***>(entity);
    if (!vtable)
        return nullptr;

    static std::unordered_map<void **, int> index_cache;
    if (auto found = index_cache.find(vtable); found != index_cache.end())
    {
        if (found->second < 0)
            return nullptr;
        if (vtable[found->second])
            if (datamap_t *map = decode_map(vtable[found->second]))
                return map;
    }

    int best_index     = -1;
    datamap_t *best_map = nullptr;
    int best_score     = -1;
    for (int index = 12; index <= 22; ++index)
    {
        if (!vtable[index])
            continue;
        datamap_t *map = decode_map(vtable[index]);
        if (!map || !map->dataClassName)
            continue;
        int score = index;
        if (std::strstr(map->dataClassName, "Player"))
            score += 100;
        else if (std::strstr(map->dataClassName, "Weapon"))
            score += 50;
        else if (std::strstr(map->dataClassName, "Animating"))
            score += 10;
        if (score >= best_score)
        {
            best_score = score;
            best_index = index;
            best_map   = map;
        }
    }
    index_cache[vtable] = best_index;
    return best_map;
}

static int walk_datamap(datamap_t *map, const char *name, int depth = 0)
{
    while (map && depth < 32)
    {
        if (!readable(map, sizeof(datamap_t)) || !map->dataDesc || map->dataNumFields <= 0 || map->dataNumFields > 1024)
            break;
        for (int i = 0; i < map->dataNumFields; ++i)
        {
            typedescription_t &field = map->dataDesc[i];
            if (field.fieldName && !std::strcmp(field.fieldName, name))
            {
                int off = field.fieldOffset[TD_OFFSET_NORMAL];
                if (off > 0)
                    return off;
            }
            if (field.td)
            {
                int nested = walk_datamap(field.td, name, depth + 1);
                if (nested > 0)
                    return nested;
            }
        }
        map = map->baseMap;
        ++depth;
    }
    return 0;
}

int DatamapField(void *entity, const char *name)
{
    return walk_datamap(PredDescMap(entity), name);
}

int DatamapFieldAny(const char *name)
{
    static std::unordered_map<std::string, int> cache;
    if (auto found = cache.find(name); found != cache.end())
        return found->second;
    if (!g_IEntityList)
        return 0;
    const int highest = g_IEntityList->GetHighestEntityIndex();
    for (int i = 0; i <= highest; ++i)
    {
        IClientEntity *ent = g_IEntityList->GetClientEntity(i);
        if (!ent)
            continue;
        int off = DatamapField(ent, name);
        if (off > 0)
        {
            cache.emplace(name, off);
            return off;
        }
    }
    return 0;
}

int lazy_netvar::get() const
{
    int current = cached.load(std::memory_order_acquire);
    if (current >= 0)
        return current;
    int off = 0;
    switch (argc)
    {
    case 2:
        off = gNetvars.get_offset(n0, n1);
        break;
    case 3:
        off = gNetvars.get_offset(n0, n1, n2);
        break;
    case 4:
        off = gNetvars.get_offset(n0, n1, n2, n3);
        break;
    case 5:
        off = gNetvars.get_offset(n0, n1, n2, n3, n4);
        break;
    default:
        break;
    }
    cached.store(off, std::memory_order_release);
    return off;
}

int lazy_proxy::get() const
{
    int current = cached.load(std::memory_order_acquire);
    if (current >= 0)
        return current;
    int off = 0;
    if (RecvProp *recv = gNetvars.get_prop(table, this->prop))
    {
        auto *live = reinterpret_cast<RecvPropRedef *>(recv);
        if (live->m_ProxyFn)
            off = proxy_store_disp(reinterpret_cast<const void *>(live->m_ProxyFn));
        if (off <= 0 || off > 0x4000)
            off = live->m_Offset;
    }
    cached.store(off, std::memory_order_release);
    return off;
}

int lazy_datamap::get() const
{
    int current = cached.load(std::memory_order_acquire);
    if (current >= 0)
        return current;
    // Fields that don't exist in any datamap must not re-walk every entity's
    // map on every call. Failed lookups retry at a low rate instead.
    const auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now < next_retry.load(std::memory_order_acquire))
        return 0;
    int off = DatamapFieldAny(name);
    if (off <= 0 && name && !std::strcmp(name, "m_pIk"))
        off = DatamapFieldAny("m_pIK");
    if (off <= 0 && name && !std::strcmp(name, "m_pCurrentCommand"))
    {
        const int constraint = netvar.m_hConstraintEntity;
        if (constraint > int(sizeof(void *)))
            off = constraint - int(sizeof(void *));
    }
    if (off <= 0 && name && !std::strcmp(name, "m_iEFlags"))
    {
        if (uintptr_t insn = gSignatures.GetClientSignature(sigs::base_animating_add_eflags))
            off = *reinterpret_cast<int *>(insn + 2);
    }
    if (off <= 0 && name && !std::strcmp(name, "m_pStudioHdr"))
    {
        if (uintptr_t insn = gSignatures.GetClientSignature(sigs::base_animating_studio_hdr))
            off = *reinterpret_cast<int *>(insn + 3);
    }
    if (off <= 0 && name && !std::strcmp(name, "m_AnimOverlay"))
    {
        if (uintptr_t insn = gSignatures.GetClientSignature(sigs::base_animating_anim_overlay))
            off = *reinterpret_cast<int *>(insn + 9);
    }
    if (off > 0)
        cached.store(off, std::memory_order_release);
    else
        next_retry.store(now + 2000, std::memory_order_release);
    return off;
}

void NetVars::Init()
{
    logging::Info("NetVars gamerules round=%x win=%x mvm=%x halloween=%x spells=%x", int(m_iRoundState), int(m_iWinningTeam), int(m_bPlayingMannVsMachine), int(m_halloweenScenario), int(m_bIsUsingSpells));
    logging::Info("NetVars punch=%x tauntcam=%x deadflag=%x weapons=%x attr=%x", int(vecPunchAngle), int(nForceTauntCam), int(deadflag), int(hMyWeapons), int(AttributeList));
    logging::Info("NetVars overlay=%x ik=%x studiohdr=%x", int(m_AnimOverlay), int(m_pIk), int(m_pStudioHdr));
}

void InitNetVars()
{
    netvar.Init();
}
