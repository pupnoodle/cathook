#pragma once

#include <stdint.h>

inline void *e8call(void *address)
{
    if (!address)
        return nullptr;
    auto rel = *reinterpret_cast<int32_t *>(address);
    return reinterpret_cast<void *>(uintptr_t(address) + 4 + rel);
}
inline uintptr_t e8call(uintptr_t address)
{
    return uintptr_t(e8call(reinterpret_cast<void *>(address)));
}
inline uintptr_t e8call_direct(uintptr_t address)
{
    if (!address)
        return 0;
    return e8call(address + 1);
}
