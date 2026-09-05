#include "config.hpp"

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>

#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>

namespace
{

using namespace cat_stm;

using gl_proc = void (*)();
using dlopen_fn = void* (*)(const char*, int);
using dlmopen_fn = void* (*)(long, const char*, int);
using x_display = void;
using x_window = unsigned long;
using x_drawable = unsigned long;
using x_bool = int;

std::atomic<int> g_should_throttle{ -1 };

template <typename fn>
fn next_symbol(fn& slot, const char* name)
{
  if (slot == nullptr)
  {
    slot = reinterpret_cast<fn>(::dlsym(RTLD_NEXT, name));
  }
  return slot;
}

bool path_base_matches(const char* path, const char* name)
{
  if (path == nullptr)
  {
    return false;
  }
  const char* slash = std::strrchr(path, '/');
  const char* base = slash ? slash + 1 : path;
  return std::strcmp(base, name) == 0;
}

bool process_base_is(const char* name)
{
  char path[4096]{};
  const ssize_t length = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
  return length > 0 && path_base_matches(path, name);
}

bool should_throttle_process()
{
  const int cached = g_should_throttle.load(std::memory_order_relaxed);
  if (cached >= 0)
  {
    return cached == 1;
  }

  const bool throttle = process_base_is("steam") || process_base_is("steamwebhelper");
  g_should_throttle.store(throttle ? 1 : 0, std::memory_order_relaxed);
  return throttle;
}

void sleep_microseconds(int microseconds)
{
  if (microseconds <= 0)
  {
    return;
  }

  timespec remaining{};
  remaining.tv_sec = microseconds / 1000000;
  remaining.tv_nsec = static_cast<long>(microseconds % 1000000) * 1000L;
  while (::nanosleep(&remaining, &remaining) == -1 && errno == EINTR)
  {
  }
}

void sleep_loop_if(bool condition)
{
  const config& cfg = settings();
  if (!cfg.disabled && cfg.loop_sleep && cfg.loop_sleep_us > 0 && condition && should_throttle_process())
  {
    sleep_microseconds(cfg.loop_sleep_us);
  }
}

bool should_hide_x11()
{
  const config& cfg = settings();
  return !cfg.disabled && cfg.hide_x11;
}

bool should_drop_gl()
{
  const config& cfg = settings();
  return !cfg.disabled && cfg.no_gl;
}

int swap_interval(int requested)
{
  const config& cfg = settings();
  return (!cfg.disabled && cfg.no_vsync) ? 0 : requested;
}

gl_proc proc_override(const char* name);

}

#define CAT_STM_EXPORT extern "C" __attribute__((visibility("default")))

CAT_STM_EXPORT int XMapWindow(x_display* display, x_window window)
{
  static int (*real)(x_display*, x_window) = nullptr;
  if (should_hide_x11())
  {
    return 0;
  }
  return next_symbol(real, "XMapWindow") != nullptr ? real(display, window) : 0;
}

CAT_STM_EXPORT int XMapRaised(x_display* display, x_window window)
{
  static int (*real)(x_display*, x_window) = nullptr;
  if (should_hide_x11())
  {
    return 0;
  }
  return next_symbol(real, "XMapRaised") != nullptr ? real(display, window) : 0;
}

CAT_STM_EXPORT int XRaiseWindow(x_display* display, x_window window)
{
  static int (*real)(x_display*, x_window) = nullptr;
  if (should_hide_x11())
  {
    return 0;
  }
  return next_symbol(real, "XRaiseWindow") != nullptr ? real(display, window) : 0;
}

CAT_STM_EXPORT x_window XCreateWindow(
  x_display* display,
  x_window parent,
  int x,
  int y,
  unsigned int width,
  unsigned int height,
  unsigned int border_width,
  int depth,
  unsigned int window_class,
  void* visual,
  unsigned long valuemask,
  void* attributes)
{
  static x_window (*real)(x_display*, x_window, int, int, unsigned int, unsigned int, unsigned int, int, unsigned int, void*, unsigned long, void*) = nullptr;
  if (next_symbol(real, "XCreateWindow") == nullptr)
  {
    return 0;
  }
  if (should_hide_x11())
  {
    return real(display, parent, -32000, -32000, 1, 1, border_width, depth, window_class, visual, valuemask, attributes);
  }
  return real(display, parent, x, y, width, height, border_width, depth, window_class, visual, valuemask, attributes);
}

CAT_STM_EXPORT x_window XCreateSimpleWindow(
  x_display* display,
  x_window parent,
  int x,
  int y,
  unsigned int width,
  unsigned int height,
  unsigned int border_width,
  unsigned long border,
  unsigned long background)
{
  static x_window (*real)(x_display*, x_window, int, int, unsigned int, unsigned int, unsigned int, unsigned long, unsigned long) = nullptr;
  if (next_symbol(real, "XCreateSimpleWindow") == nullptr)
  {
    return 0;
  }
  if (should_hide_x11())
  {
    return real(display, parent, -32000, -32000, 1, 1, border_width, border, background);
  }
  return real(display, parent, x, y, width, height, border_width, border, background);
}

CAT_STM_EXPORT int XConfigureWindow(x_display* display, x_window window, unsigned int value_mask, void* values)
{
  static int (*real)(x_display*, x_window, unsigned int, void*) = nullptr;
  if (should_hide_x11())
  {
    return 0;
  }
  return next_symbol(real, "XConfigureWindow") != nullptr ? real(display, window, value_mask, values) : 0;
}

CAT_STM_EXPORT int XMoveResizeWindow(x_display* display, x_window window, int x, int y, unsigned int width, unsigned int height)
{
  static int (*real)(x_display*, x_window, int, int, unsigned int, unsigned int) = nullptr;
  if (next_symbol(real, "XMoveResizeWindow") == nullptr)
  {
    return 0;
  }
  if (should_hide_x11())
  {
    return real(display, window, -32000, -32000, 1, 1);
  }
  return real(display, window, x, y, width, height);
}

CAT_STM_EXPORT int XResizeWindow(x_display* display, x_window window, unsigned int width, unsigned int height)
{
  static int (*real)(x_display*, x_window, unsigned int, unsigned int) = nullptr;
  if (next_symbol(real, "XResizeWindow") == nullptr)
  {
    return 0;
  }
  return should_hide_x11() ? real(display, window, 1, 1) : real(display, window, width, height);
}

CAT_STM_EXPORT int XIconifyWindow(x_display* display, x_window window, int screen)
{
  static int (*real)(x_display*, x_window, int) = nullptr;
  if (should_hide_x11())
  {
    return 1;
  }
  return next_symbol(real, "XIconifyWindow") != nullptr ? real(display, window, screen) : 0;
}

CAT_STM_EXPORT void glClear(unsigned int mask)
{
  static void (*real)(unsigned int) = nullptr;
  if (!should_drop_gl() && next_symbol(real, "glClear") != nullptr)
  {
    real(mask);
  }
}

CAT_STM_EXPORT void glDrawArrays(unsigned int mode, int first, int count)
{
  static void (*real)(unsigned int, int, int) = nullptr;
  if (!should_drop_gl() && next_symbol(real, "glDrawArrays") != nullptr)
  {
    real(mode, first, count);
  }
}

CAT_STM_EXPORT void glDrawElements(unsigned int mode, int count, unsigned int type, const void* indices)
{
  static void (*real)(unsigned int, int, unsigned int, const void*) = nullptr;
  if (!should_drop_gl() && next_symbol(real, "glDrawElements") != nullptr)
  {
    real(mode, count, type, indices);
  }
}

CAT_STM_EXPORT void glDrawRangeElements(unsigned int mode, unsigned int start, unsigned int end, int count, unsigned int type, const void* indices)
{
  static void (*real)(unsigned int, unsigned int, unsigned int, int, unsigned int, const void*) = nullptr;
  if (!should_drop_gl() && next_symbol(real, "glDrawRangeElements") != nullptr)
  {
    real(mode, start, end, count, type, indices);
  }
}

CAT_STM_EXPORT void glFlush()
{
  static void (*real)() = nullptr;
  if (!should_drop_gl() && next_symbol(real, "glFlush") != nullptr)
  {
    real();
  }
}

CAT_STM_EXPORT void glFinish()
{
  static void (*real)() = nullptr;
  if (!should_drop_gl() && next_symbol(real, "glFinish") != nullptr)
  {
    real();
  }
}

CAT_STM_EXPORT void glXSwapBuffers(void* dpy, x_drawable drawable)
{
  sleep_loop_if(should_drop_gl());
  static void (*real)(void*, x_drawable) = nullptr;
  if (!should_drop_gl() && next_symbol(real, "glXSwapBuffers") != nullptr)
  {
    real(dpy, drawable);
  }
}

CAT_STM_EXPORT unsigned int eglSwapBuffers(void* dpy, void* surface)
{
  sleep_loop_if(should_drop_gl());
  static unsigned int (*real)(void*, void*) = nullptr;
  if (!should_drop_gl() && next_symbol(real, "eglSwapBuffers") != nullptr)
  {
    return real(dpy, surface);
  }
  return 1u;
}

CAT_STM_EXPORT void SDL_GL_SwapWindow(void* window)
{
  sleep_loop_if(should_drop_gl());
  static void (*real)(void*) = nullptr;
  if (!should_drop_gl() && next_symbol(real, "SDL_GL_SwapWindow") != nullptr)
  {
    real(window);
  }
}

CAT_STM_EXPORT void* SDL_CreateWindow(const char* title, int x, int y, int w, int h, unsigned int flags)
{
  static void* (*real)(const char*, int, int, int, int, unsigned int) = nullptr;
  if (next_symbol(real, "SDL_CreateWindow") == nullptr)
  {
    return nullptr;
  }
  if (should_hide_x11())
  {
    return real(title, -32000, -32000, 1, 1, flags);
  }
  return real(title, x, y, w, h, flags);
}

CAT_STM_EXPORT void SDL_ShowWindow(void* window)
{
  static void (*real)(void*) = nullptr;
  if (!should_hide_x11() && next_symbol(real, "SDL_ShowWindow") != nullptr)
  {
    real(window);
  }
}

CAT_STM_EXPORT void SDL_RaiseWindow(void* window)
{
  static void (*real)(void*) = nullptr;
  if (!should_hide_x11() && next_symbol(real, "SDL_RaiseWindow") != nullptr)
  {
    real(window);
  }
}

CAT_STM_EXPORT void SDL_SetWindowPosition(void* window, int x, int y)
{
  static void (*real)(void*, int, int) = nullptr;
  if (next_symbol(real, "SDL_SetWindowPosition") == nullptr)
  {
    return;
  }
  if (should_hide_x11())
  {
    real(window, -32000, -32000);
    return;
  }
  real(window, x, y);
}

CAT_STM_EXPORT void SDL_SetWindowSize(void* window, int w, int h)
{
  static void (*real)(void*, int, int) = nullptr;
  if (next_symbol(real, "SDL_SetWindowSize") == nullptr)
  {
    return;
  }
  if (should_hide_x11())
  {
    real(window, 1, 1);
    return;
  }
  real(window, w, h);
}

CAT_STM_EXPORT void SDL_MaximizeWindow(void* window)
{
  static void (*real)(void*) = nullptr;
  if (!should_hide_x11() && next_symbol(real, "SDL_MaximizeWindow") != nullptr)
  {
    real(window);
  }
}

CAT_STM_EXPORT void SDL_RestoreWindow(void* window)
{
  static void (*real)(void*) = nullptr;
  if (!should_hide_x11() && next_symbol(real, "SDL_RestoreWindow") != nullptr)
  {
    real(window);
  }
}

CAT_STM_EXPORT unsigned int eglSwapInterval(void* dpy, int interval)
{
  static unsigned int (*real)(void*, int) = nullptr;
  if (next_symbol(real, "eglSwapInterval") == nullptr)
  {
    return 1u;
  }
  return real(dpy, swap_interval(interval));
}

CAT_STM_EXPORT void glXSwapIntervalEXT(void* dpy, x_drawable drawable, int interval)
{
  static void (*real)(void*, x_drawable, int) = nullptr;
  if (next_symbol(real, "glXSwapIntervalEXT") != nullptr)
  {
    real(dpy, drawable, swap_interval(interval));
  }
}

CAT_STM_EXPORT int glXSwapIntervalSGI(int interval)
{
  static int (*real)(int) = nullptr;
  return next_symbol(real, "glXSwapIntervalSGI") != nullptr ? real(swap_interval(interval)) : 0;
}

CAT_STM_EXPORT int glXSwapIntervalMESA(unsigned int interval)
{
  static int (*real)(unsigned int) = nullptr;
  const unsigned int value = static_cast<unsigned int>(swap_interval(static_cast<int>(interval)));
  return next_symbol(real, "glXSwapIntervalMESA") != nullptr ? real(value) : 0;
}

CAT_STM_EXPORT int SDL_GL_SetSwapInterval(int interval)
{
  static int (*real)(int) = nullptr;
  return next_symbol(real, "SDL_GL_SetSwapInterval") != nullptr ? real(swap_interval(interval)) : 0;
}

namespace
{

gl_proc proc_override(const char* name)
{
  if (name == nullptr || !should_drop_gl())
  {
    return nullptr;
  }

  struct entry
  {
    const char* name;
    gl_proc proc;
  };

  static const entry table[] = {
    { "glClear", reinterpret_cast<gl_proc>(&glClear) },
    { "glDrawArrays", reinterpret_cast<gl_proc>(&glDrawArrays) },
    { "glDrawElements", reinterpret_cast<gl_proc>(&glDrawElements) },
    { "glDrawRangeElements", reinterpret_cast<gl_proc>(&glDrawRangeElements) },
    { "glFlush", reinterpret_cast<gl_proc>(&glFlush) },
    { "glFinish", reinterpret_cast<gl_proc>(&glFinish) },
    { "glXSwapBuffers", reinterpret_cast<gl_proc>(&glXSwapBuffers) },
    { "eglSwapBuffers", reinterpret_cast<gl_proc>(&eglSwapBuffers) },
    { "eglSwapInterval", reinterpret_cast<gl_proc>(&eglSwapInterval) },
    { "glXSwapIntervalEXT", reinterpret_cast<gl_proc>(&glXSwapIntervalEXT) },
    { "glXSwapIntervalSGI", reinterpret_cast<gl_proc>(&glXSwapIntervalSGI) },
    { "glXSwapIntervalMESA", reinterpret_cast<gl_proc>(&glXSwapIntervalMESA) },
  };

  for (const entry& item : table)
  {
    if (std::strcmp(item.name, name) == 0)
    {
      return item.proc;
    }
  }
  return nullptr;
}

}

CAT_STM_EXPORT gl_proc eglGetProcAddress(const char* name)
{
  static gl_proc (*real)(const char*) = nullptr;
  if (gl_proc override = proc_override(name))
  {
    return override;
  }
  return next_symbol(real, "eglGetProcAddress") != nullptr ? real(name) : nullptr;
}

CAT_STM_EXPORT gl_proc glXGetProcAddressARB(const unsigned char* name)
{
  static gl_proc (*real)(const unsigned char*) = nullptr;
  if (gl_proc override = proc_override(reinterpret_cast<const char*>(name)))
  {
    return override;
  }
  return next_symbol(real, "glXGetProcAddressARB") != nullptr ? real(name) : nullptr;
}

CAT_STM_EXPORT gl_proc glXGetProcAddress(const unsigned char* name)
{
  static gl_proc (*real)(const unsigned char*) = nullptr;
  if (gl_proc override = proc_override(reinterpret_cast<const char*>(name)))
  {
    return override;
  }
  return next_symbol(real, "glXGetProcAddress") != nullptr ? real(name) : nullptr;
}

CAT_STM_EXPORT void* dlopen(const char* filename, int flags)
{
  static dlopen_fn real = nullptr;
  return next_symbol(real, "dlopen") != nullptr ? real(filename, flags) : nullptr;
}

CAT_STM_EXPORT void* dlmopen(long namespace_id, const char* filename, int flags)
{
  static dlmopen_fn real = nullptr;
  return next_symbol(real, "dlmopen") != nullptr ? real(namespace_id, filename, flags) : nullptr;
}

CAT_STM_EXPORT void* alcOpenDevice(const char* device_name)
{
  static void* (*real)(const char*) = nullptr;
  if (!settings().disabled && settings().no_audio)
  {
    return nullptr;
  }
  return next_symbol(real, "alcOpenDevice") != nullptr ? real(device_name) : nullptr;
}

CAT_STM_EXPORT int snd_pcm_open(void** pcm, const char* name, int stream, int mode)
{
  static int (*real)(void**, const char*, int, int) = nullptr;
  if (!settings().disabled && settings().no_audio)
  {
    return -2;
  }
  return next_symbol(real, "snd_pcm_open") != nullptr ? real(pcm, name, stream, mode) : -2;
}


namespace
{

__attribute__((constructor)) void cat_steamtxtmode_init()
{
  config& cfg = settings();
  cfg.log = env_flag("CAT_STM_LOG", true);
  cfg.disabled = !env_flag("CAT_STEAM_TXTMODE", true) || env_flag("CAT_STM_DISABLE", false);
  cfg.hide_x11 = env_flag("CAT_STM_HIDE_X11", env_flag("CAT_STEAM_TXTMODE_HIDE_WINDOWS", true));
  cfg.no_gl = env_flag("CAT_STM_NO_GL", env_flag("CAT_STEAM_TXTMODE_DROP_DRAWS", true));
  cfg.no_vsync = env_flag("CAT_STM_NO_VSYNC", true);
  cfg.no_audio = env_flag("CAT_STM_NO_AUDIO", true);
  cfg.webhelper_trim = env_flag("CAT_STM_WEBHELPER_TRIM", env_flag("CAT_STEAM_TXTMODE_TRIM_WEBHELPER", true));
  cfg.webhelper_single = env_flag("CAT_STM_WEBHELPER_SINGLE", false);
  cfg.loop_sleep = env_flag("CAT_STM_LOOP_SLEEP", true);
  cfg.loop_sleep_us = std::clamp(env_int("CAT_STM_LOOP_SLEEP_US", env_int("CAT_STEAM_TXTMODE_FRAME_INTERVAL_US", 100000)), 0, 1000000);

  if (cfg.disabled)
  {
    log_line("disabled via CAT_STM_DISABLE");
    return;
  }

  log_line("loaded (%d-bit): hide_x11=%d no_gl=%d no_vsync=%d no_audio=%d webhelper_trim=%d webhelper_single=%d loop_sleep=%d loop_sleep_us=%d",
           static_cast<int>(sizeof(void*) * 8), cfg.hide_x11, cfg.no_gl, cfg.no_vsync,
           cfg.no_audio, cfg.webhelper_trim, cfg.webhelper_single, cfg.loop_sleep, cfg.loop_sleep_us);
}

}
