/*
 * HUD.cpp
 *
 *  Created on: Jun 4, 2017
 *      Author: nullifiedcat
 */

#include <core/logging.hpp>
#include "sdk/HUD.h"
#include "copypasted/CSignature.h"
#include "core/sigs.hpp"

#include <cstdint>
#include <cstring>
#include <strings.h>

namespace
{
struct HudLayout
{
    uint32_t count  = uint32_t(vtables::hud::element_count);
    uint32_t array  = uint32_t(vtables::hud::element_array);
    uint32_t factor = uint32_t(vtables::hud::mouse_sensitivity_factor);
};

HudLayout &hud_layout()
{
    static HudLayout layout = [] {
        HudLayout out;
        if (auto *p = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::hud_find_element)))
        {
            for (int i = 0; i < 32; ++i)
            {
                if (p[i] == 0x8B && p[i + 1] == 0x47)
                {
                    out.count = p[i + 2];
                    out.array = out.count - 0x10;
                    break;
                }
            }
        }
        if (auto *p = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::hud_mouse_sensitivity)))
        {
            if (p[0] == 0xF3 && p[1] == 0x0F && p[2] == 0x10)
                out.factor = p[4];
        }
        logging::Info("CHud layout array=%x count=%x factor=%x", out.array, out.count, out.factor);
        return out;
    }();
    return layout;
}
} // namespace

CHudElement *CHud::FindElement(const char *name)
{
    if (!this || !name)
        return nullptr;
    const auto &layout = hud_layout();
    auto *bytes        = reinterpret_cast<char *>(this);
    const int count    = *reinterpret_cast<int *>(bytes + layout.count);
    auto **list        = *reinterpret_cast<CHudElement ***>(bytes + layout.array);
    if (!list || count <= 0)
        return nullptr;
    for (int i = 0; i < count; ++i)
    {
        CHudElement *el = list[i];
        if (!el)
            continue;
        const char *elname = vfunc<const char *(*)(CHudElement *)>(el, vtables::hud_element::get_name)(el);
        if (elname && !strcasecmp(elname, name))
            return el;
    }
    return nullptr;
}

float &CHud::GetSensitivityFactor()
{
    return *reinterpret_cast<float *>(reinterpret_cast<char *>(this) + hud_layout().factor);
}
