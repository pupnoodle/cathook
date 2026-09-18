#pragma once

#include <cstdint>
#include <memory>

#include "bytepatch.hpp"
#include "funchook.h"

#define foffset(p, i) ((unsigned char *) &p)[i]

class DetourHook
{
    funchook_t *fh{ nullptr };
    void *orig_func{ nullptr };
    void *hook_fn{ nullptr };

public:
    DetourHook() = default;

    DetourHook(uintptr_t original, void *hook_func)
    {
        Init(original, hook_func);
    }

    void Init(uintptr_t original, void *hook_func)
    {
        Shutdown();
        if (!original || !hook_func)
            return;
        orig_func = reinterpret_cast<void *>(original);
        hook_fn   = hook_func;
        fh        = funchook_create();
        if (!fh)
            return;
        if (funchook_prepare(fh, &orig_func, hook_func) != 0)
        {
            funchook_destroy(fh);
            fh        = nullptr;
            orig_func = nullptr;
            return;
        }
        if (funchook_install(fh, 0) != 0)
        {
            funchook_destroy(fh);
            fh        = nullptr;
            orig_func = nullptr;
        }
    }

    void InitBytepatch() {}

    void Shutdown()
    {
        if (!fh)
            return;
        funchook_uninstall(fh, 0);
        funchook_destroy(fh);
        fh        = nullptr;
        orig_func = nullptr;
    }

    ~DetourHook()
    {
        Shutdown();
    }

    void *GetOriginalFunc() const
    {
        return orig_func;
    }

    inline void RestorePatch() {}
};
