/*
  Created by Jenny White on 29.04.18.
  Copyright (c) 2018 nullworks. All rights reserved.
*/

#include <MiscTemporary.hpp>
#include <visual/SDLHooks.hpp>
#include "HookedMethods.hpp"
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <exception>
#include <menu/menu/Menu.hpp>
#if ENABLE_CLIP
#include "clip.h"
#endif
#if ENABLE_VISUALS
#include "drawmgr.hpp"
#endif

static bool swapwindow_init{ false };
static bool overlay_failed{ false };

namespace hooked_methods
{
#if ENABLE_CLIP
DEFINE_HOOKED_METHOD(SDL_SetClipboardText, int, const char *text)
{
    if (original::SDL_SetClipboardText)
        original::SDL_SetClipboardText(text);
    clip::set_text(text);
    return 0;
}
#endif

DEFINE_HOOKED_METHOD(SDL_GL_SwapWindow, void, SDL_Window *window)
{
    static thread_local bool reentrant = false;
    auto *orig                         = original::SDL_GL_SwapWindow;
    if (!orig)
        return;

    if (reentrant)
    {
        orig(window);
        return;
    }
    reentrant = true;

    if (!sdl_hooks::window)
        sdl_hooks::window = window;

    if (isHackActive() && !disable_visuals && !overlay_failed)
    {
        try
        {
            int w = 0, h = 0;
            SDL_GetWindowSize(window, &w, &h);
            if (w > 0 && h > 0)
            {
                draw::width  = w;
                draw::height = h;
            }

            static int prev_width = 0, prev_height = 0;
            if (!swapwindow_init)
            {
                const char *gl_ver = reinterpret_cast<const char *>(glGetString(GL_VERSION));
                logging::Info("SDL overlay: GL_VERSION=%s ctx=%p", gl_ver ? gl_ver : "?", SDL_GL_GetCurrentContext());
                draw::InitGL();
                swapwindow_init = true;
            }
            else if (zerokernel::Menu::instance && (draw::width != prev_width || draw::height != prev_height))
                zerokernel::Menu::instance->resize(draw::width, draw::height);
            prev_width  = draw::width;
            prev_height = draw::height;

            draw::BeginGL();
#if ENABLE_IMGUI_DRAWING
            render_cheat_visuals();
#else
            DrawCache();
#endif
            draw::EndGL();
        }
        catch (const std::exception &e)
        {
            logging::Info("SDL overlay failed: %s — drawing disabled", e.what());
            overlay_failed = true;
        }
        catch (...)
        {
            logging::Info("SDL overlay failed — drawing disabled");
            overlay_failed = true;
        }
    }

    orig(window);
    reentrant = false;
}
} // namespace hooked_methods
