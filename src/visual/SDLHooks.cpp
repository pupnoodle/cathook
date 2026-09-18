/*
  Created by Jenny White on 29.04.18.
  Copyright (c) 2018 nullworks. All rights reserved.
*/

#include "SDLHooks.hpp"
#include "HookedMethods.hpp"

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core/resolve.hpp"

namespace sdl_hooks
{

SDL_Window *window{ nullptr };

namespace pointers
{
#if ENABLE_CLIP
static hooked_methods::types::SDL_SetClipboardText *SDL_SetClipboardText{ nullptr };
#endif
static hooked_methods::types::SDL_GL_SwapWindow *SDL_GL_SwapWindow{ nullptr };
static hooked_methods::types::SDL_PollEvent *SDL_PollEvent{ nullptr };
} // namespace pointers

static bool write_pointer_slot(void **slot, void *value)
{
    if (!slot)
        return false;
    long ps   = sysconf(_SC_PAGESIZE);
    auto page = reinterpret_cast<void *>(uintptr_t(slot) & ~uintptr_t(ps - 1));
    if (mprotect(page, ps, PROT_READ | PROT_WRITE) != 0)
        return false;
    *slot = value;
    mprotect(page, ps, PROT_READ);
    return true;
}

static void **slot_for(const char *name)
{
    void *fn = dlsym(sharedobj::libsdl().lmap, name);
    if (!fn)
        fn = dlsym(RTLD_DEFAULT, name);
    if (!fn)
    {
        logging::Info("SDL: dlsym(%s) failed", name);
        return nullptr;
    }
    auto *slot = static_cast<void **>(cathook::core::memory::resolve_jmp_slot(fn));
    if (!slot)
        logging::Info("SDL: %s is not a GOT stub (%p)", name, fn);
    return slot;
}

void applySdlHooks()
{
    pointers::SDL_GL_SwapWindow = reinterpret_cast<hooked_methods::types::SDL_GL_SwapWindow *>(slot_for("SDL_GL_SwapWindow"));
    if (!pointers::SDL_GL_SwapWindow)
        return;
    hooked_methods::original::SDL_GL_SwapWindow = *pointers::SDL_GL_SwapWindow;
    write_pointer_slot(reinterpret_cast<void **>(pointers::SDL_GL_SwapWindow), reinterpret_cast<void *>(hooked_methods::methods::SDL_GL_SwapWindow));

    pointers::SDL_PollEvent = reinterpret_cast<hooked_methods::types::SDL_PollEvent *>(slot_for("SDL_PollEvent"));
    if (pointers::SDL_PollEvent)
    {
        hooked_methods::original::SDL_PollEvent = *pointers::SDL_PollEvent;
        write_pointer_slot(reinterpret_cast<void **>(pointers::SDL_PollEvent), reinterpret_cast<void *>(hooked_methods::methods::SDL_PollEvent));
    }
#if ENABLE_CLIP
    pointers::SDL_SetClipboardText = reinterpret_cast<hooked_methods::types::SDL_SetClipboardText *>(slot_for("SDL_SetClipboardText"));
    if (pointers::SDL_SetClipboardText)
    {
        hooked_methods::original::SDL_SetClipboardText = *pointers::SDL_SetClipboardText;
        write_pointer_slot(reinterpret_cast<void **>(pointers::SDL_SetClipboardText), reinterpret_cast<void *>(hooked_methods::methods::SDL_SetClipboardText));
    }
#endif
}

void cleanSdlHooks()
{
    if (pointers::SDL_GL_SwapWindow)
        write_pointer_slot(reinterpret_cast<void **>(pointers::SDL_GL_SwapWindow), reinterpret_cast<void *>(hooked_methods::original::SDL_GL_SwapWindow));
    if (pointers::SDL_PollEvent)
        write_pointer_slot(reinterpret_cast<void **>(pointers::SDL_PollEvent), reinterpret_cast<void *>(hooked_methods::original::SDL_PollEvent));
#if ENABLE_CLIP
    if (pointers::SDL_SetClipboardText)
        write_pointer_slot(reinterpret_cast<void **>(pointers::SDL_SetClipboardText), reinterpret_cast<void *>(hooked_methods::original::SDL_SetClipboardText));
#endif
}
} // namespace sdl_hooks
