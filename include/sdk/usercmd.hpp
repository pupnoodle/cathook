#pragma once

#include <stdint.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <mathlib/vector.h>
#include "checksum_crc.h"
#include "core/code_scan.hpp"

class CUserCmd
{
public:
    CUserCmd()
    {
        Reset();
    }

    void Reset()
    {
        command_number = 0;
        tick_count     = 0;
        viewangles.Init();
        forwardmove   = 0.0f;
        sidemove      = 0.0f;
        upmove        = 0.0f;
        buttons       = 0;
        impulse       = 0;
        weaponselect  = 0;
        weaponsubtype = 0;
        random_seed   = 0;
        mousedx       = 0;
        mousedy       = 0;

        hasbeenpredicted = false;
    }

    CUserCmd &operator=(const CUserCmd &src)
    {
        if (this == &src)
            return *this;

        command_number = src.command_number;
        tick_count     = src.tick_count;
        viewangles     = src.viewangles;
        forwardmove    = src.forwardmove;
        sidemove       = src.sidemove;
        upmove         = src.upmove;
        buttons        = src.buttons;
        impulse        = src.impulse;
        weaponselect   = src.weaponselect;
        weaponsubtype  = src.weaponsubtype;
        random_seed    = src.random_seed;
        mousedx        = src.mousedx;
        mousedy        = src.mousedy;

        hasbeenpredicted = src.hasbeenpredicted;

        return *this;
    }

    CUserCmd(const CUserCmd &src)
    {
        *this = src;
    }

    CRC32_t GetChecksum(void) const
    {
        CRC32_t crc;

        CRC32_Init(&crc);
        CRC32_ProcessBuffer(&crc, &command_number, sizeof(command_number));
        CRC32_ProcessBuffer(&crc, &tick_count, sizeof(tick_count));
        CRC32_ProcessBuffer(&crc, &viewangles, sizeof(viewangles));
        CRC32_ProcessBuffer(&crc, &forwardmove, sizeof(forwardmove));
        CRC32_ProcessBuffer(&crc, &sidemove, sizeof(sidemove));
        CRC32_ProcessBuffer(&crc, &upmove, sizeof(upmove));
        CRC32_ProcessBuffer(&crc, &buttons, sizeof(buttons));
        CRC32_ProcessBuffer(&crc, &impulse, sizeof(impulse));
        CRC32_ProcessBuffer(&crc, &weaponselect, sizeof(weaponselect));
        CRC32_ProcessBuffer(&crc, &weaponsubtype, sizeof(weaponsubtype));
        CRC32_ProcessBuffer(&crc, &random_seed, sizeof(random_seed));
        CRC32_ProcessBuffer(&crc, &mousedx, sizeof(mousedx));
        CRC32_ProcessBuffer(&crc, &mousedy, sizeof(mousedy));
        CRC32_Final(&crc);

        return crc;
    }

    void MakeInert(void)
    {
        viewangles.Init();
        forwardmove = 0.f;
        sidemove    = 0.f;
        upmove      = 0.f;
        buttons     = 0;
        impulse     = 0;
    }

    void *vmt;
    int command_number;
    int tick_count;
    Vector viewangles;
    float forwardmove;
    float sidemove;
    float upmove;
    int buttons;
    byte impulse;
    int weaponselect;
    int weaponsubtype;
    int random_seed;
    short mousedx;
    short mousedy;
    bool hasbeenpredicted;
};

class CVerifiedUserCmd
{
public:
    CUserCmd m_cmd;
    CRC32_t m_crc;
};

static_assert(sizeof(CUserCmd) == 0x48, "CUserCmd layout mismatch");
static_assert(sizeof(CVerifiedUserCmd) == 0x50, "CVerifiedUserCmd layout mismatch");

inline CRC32_t GetChecksum(CUserCmd *cmd)
{
    return cmd->GetChecksum();
}

inline std::ptrdiff_t InputCommandsOffset(void *iinput)
{
    static std::ptrdiff_t off = 0;
    if (off > 0)
        return off;
    if (iinput)
    {
        auto **vtable = *reinterpret_cast<void ***>(iinput);
        if (vtable && vtable[8])
        {
            auto *fn = static_cast<const std::uint8_t *>(vtable[8]);
            for (int i = 0; i < 80; ++i)
            {
                if (fn[i] == 0x48 && fn[i + 1] == 0x8B && fn[i + 2] == 0x87)
                {
                    std::uint32_t disp = 0;
                    std::memcpy(&disp, fn + i + 3, 4);
                    if (disp >= 0x40 && disp < 0x400)
                    {
                        off = std::ptrdiff_t(disp);
                        break;
                    }
                }
            }
        }
        if (off <= 0 && vtable && vtable[0])
        {
            auto *fn = static_cast<const std::uint8_t *>(vtable[0]);
            off      = cathook::core::memory::member_store_after_alloc(fn, fn + 0x800, 90 * sizeof(CUserCmd) + 8);
        }
    }
    return off;
}

inline std::ptrdiff_t InputVerifiedOffset(void *iinput)
{
    static std::ptrdiff_t off = 0;
    if (off > 0)
        return off;
    if (iinput)
    {
        auto **vtable = *reinterpret_cast<void ***>(iinput);
        if (vtable && vtable[0])
        {
            auto *fn = static_cast<const std::uint8_t *>(vtable[0]);
            off      = cathook::core::memory::member_store_after_alloc(fn, fn + 0x800, 90 * sizeof(CVerifiedUserCmd) + 8);
        }
    }
    if (off <= 0)
        off = InputCommandsOffset(iinput) + static_cast<std::ptrdiff_t>(sizeof(void *));
    return off;
}

inline CUserCmd *GetCmds(void *iinput)
{
    auto off = InputCommandsOffset(iinput);
    if (off <= 0)
        return nullptr;
    return *reinterpret_cast<CUserCmd **>(uintptr_t(iinput) + off);
}

inline CVerifiedUserCmd *GetVerifiedCmds(void *iinput)
{
    auto off = InputVerifiedOffset(iinput);
    if (off <= 0)
        return nullptr;
    return *reinterpret_cast<CVerifiedUserCmd **>(uintptr_t(iinput) + off);
}
