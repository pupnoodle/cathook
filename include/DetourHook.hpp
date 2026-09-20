#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "bytepatch.hpp"
#include "funchook.h"

#define foffset(p, i) ((unsigned char *) &p)[i]

class DetourHook
{
    funchook_t *fh{ nullptr };
    void *orig_func{ nullptr };
    void *hook_fn{ nullptr };

    static std::vector<DetourHook *> &registry()
    {
        static std::vector<DetourHook *> hooks;
        return hooks;
    }

    static std::mutex &registryMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

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
            return;
        }
        std::lock_guard<std::mutex> lock(registryMutex());
        auto &hooks = registry();
        if (std::find(hooks.begin(), hooks.end(), this) == hooks.end())
            hooks.push_back(this);
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

    static void ShutdownAll()
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        for (DetourHook *hook : registry())
            hook->Shutdown();
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
