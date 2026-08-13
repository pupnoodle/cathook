#include "steam_nographics.hpp"

#include <cstdarg>
#include <dlfcn.h>
#include <link.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace
{

using x_display = void;
using x_window = unsigned long;
using x_bool = int;
using gl_enum = unsigned int;
using gl_sizei = int;
using egl_display = void*;
using egl_surface = void*;
using egl_boolean = unsigned int;
using glx_drawable = unsigned long;
using sdl_window = void;
using sdl_window_flags = std::uint64_t;

constexpr sdl_window_flags sdl_window_hidden = 0x00000008ULL;

template <typename function_type>
function_type next_symbol(const char* name)
{
  return reinterpret_cast<function_type>(dlsym(RTLD_NEXT, name));
}

}

#define CAT_STEAM_NOGRAPHICS_EXPORT extern "C" __attribute__((visibility("default")))

// Source's GetCPUInformation() is a C++ symbol.  Keep this wrapper outside
// steam_nographics so it exports the exact global ABI name _Z17GetCPUInformationv.
__attribute__((visibility("default"))) const void* GetCPUInformation()
{
  return steam_nographics::synthetic_cpu_information_pointer();
}

__attribute__((constructor)) static void steam_nographics_constructor()
{
  steam_nographics::initialize();
}

CAT_STEAM_NOGRAPHICS_EXPORT void* dlopen(const char* path, int flags)
{
  using function_type = void* (*)(const char*, int);
  static const auto original = next_symbol<function_type>("dlopen");
  void* const result = original != nullptr ? original(path, flags) : nullptr;
  if (result != nullptr)
  {
    steam_nographics::on_library_loaded(path, result);
  }
  return result;
}

CAT_STEAM_NOGRAPHICS_EXPORT void* dlmopen(Lmid_t namespace_id, const char* path, int flags)
{
  using function_type = void* (*)(Lmid_t, const char*, int);
  static const auto original = next_symbol<function_type>("dlmopen");
  void* const result = original != nullptr ? original(namespace_id, path, flags) : nullptr;
  if (result != nullptr)
  {
    steam_nographics::on_library_loaded(path, result);
  }
  return result;
}

CAT_STEAM_NOGRAPHICS_EXPORT x_window XCreateWindow(
  x_display* display, x_window parent, int x, int y, unsigned int width, unsigned int height,
  unsigned int border_width, int depth, unsigned int window_class, void* visual,
  unsigned long value_mask, void* attributes)
{
  using function_type = x_window (*)(x_display*, x_window, int, int, unsigned int, unsigned int,
                                     unsigned int, int, unsigned int, void*, unsigned long, void*);
  static const auto original = next_symbol<function_type>("XCreateWindow");
  if (original == nullptr)
  {
    return 0;
  }
  if (steam_nographics::should_hide_windows())
  {
    return original(display, parent, steam_nographics::offscreen_coordinate(), steam_nographics::offscreen_coordinate(),
      steam_nographics::offscreen_extent(), steam_nographics::offscreen_extent(), border_width, depth,
      window_class, visual, value_mask, attributes);
  }
  return original(display, parent, x, y, width, height, border_width, depth, window_class, visual, value_mask, attributes);
}

CAT_STEAM_NOGRAPHICS_EXPORT x_bool XMapRaised(x_display* display, x_window window)
{
  using function_type = x_bool (*)(x_display*, x_window);
  if (steam_nographics::should_hide_windows())
  {
    return 0;
  }
  static const auto original = next_symbol<function_type>("XMapRaised");
  return original != nullptr ? original(display, window) : 0;
}

CAT_STEAM_NOGRAPHICS_EXPORT sdl_window* SDL_CreateWindow(
  const char* title, int width, int height, sdl_window_flags flags)
{
  using function_type = sdl_window* (*)(const char*, int, int, sdl_window_flags);
  static const auto original = next_symbol<function_type>("SDL_CreateWindow");
  if (original == nullptr)
  {
    return nullptr;
  }
  if (steam_nographics::should_hide_windows())
  {
    return original(title, 1, 1, flags | sdl_window_hidden);
  }
  return original(title, width, height, flags);
}

CAT_STEAM_NOGRAPHICS_EXPORT int SDL_GL_SwapWindow(sdl_window* window)
{
  using function_type = int (*)(sdl_window*);
  if (steam_nographics::should_skip_presentation())
  {
    steam_nographics::limit_present_rate();
    return 1;
  }
  static const auto original = next_symbol<function_type>("SDL_GL_SwapWindow");
  return original != nullptr ? original(window) : 0;
}

CAT_STEAM_NOGRAPHICS_EXPORT void glXSwapBuffers(void* display, glx_drawable drawable)
{
  using function_type = void (*)(void*, glx_drawable);
  if (steam_nographics::should_skip_presentation())
  {
    steam_nographics::limit_present_rate();
    return;
  }
  static const auto original = next_symbol<function_type>("glXSwapBuffers");
  if (original != nullptr)
  {
    original(display, drawable);
  }
}

CAT_STEAM_NOGRAPHICS_EXPORT egl_boolean eglSwapBuffers(egl_display display, egl_surface surface)
{
  using function_type = egl_boolean (*)(egl_display, egl_surface);
  if (steam_nographics::should_skip_presentation())
  {
    steam_nographics::limit_present_rate();
    return 1;
  }
  static const auto original = next_symbol<function_type>("eglSwapBuffers");
  return original != nullptr ? original(display, surface) : 0;
}

CAT_STEAM_NOGRAPHICS_EXPORT void glClear(gl_enum mask)
{
  using function_type = void (*)(gl_enum);
  if (steam_nographics::should_skip_draw_calls())
  {
    return;
  }
  static const auto original = next_symbol<function_type>("glClear");
  if (original != nullptr)
  {
    original(mask);
  }
}

CAT_STEAM_NOGRAPHICS_EXPORT void glDrawArrays(gl_enum mode, int first, gl_sizei count)
{
  using function_type = void (*)(gl_enum, int, gl_sizei);
  if (steam_nographics::should_skip_draw_calls())
  {
    return;
  }
  static const auto original = next_symbol<function_type>("glDrawArrays");
  if (original != nullptr)
  {
    original(mode, first, count);
  }
}

CAT_STEAM_NOGRAPHICS_EXPORT void glDrawElements(gl_enum mode, gl_sizei count, gl_enum type, const void* indices)
{
  using function_type = void (*)(gl_enum, gl_sizei, gl_enum, const void*);
  if (steam_nographics::should_skip_draw_calls())
  {
    return;
  }
  static const auto original = next_symbol<function_type>("glDrawElements");
  if (original != nullptr)
  {
    original(mode, count, type, indices);
  }
}

CAT_STEAM_NOGRAPHICS_EXPORT int open(const char* path, int flags, ...)
{
  va_list arguments;
  va_start(arguments, flags);
  const bool has_mode = (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
  const unsigned int mode = has_mode ? va_arg(arguments, unsigned int) : 0;
  va_end(arguments);
  return steam_nographics::synthetic_open(path, flags, has_mode, mode);
}

CAT_STEAM_NOGRAPHICS_EXPORT int open64(const char* path, int flags, ...)
{
  va_list arguments;
  va_start(arguments, flags);
  const bool has_mode = (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
  const unsigned int mode = has_mode ? va_arg(arguments, unsigned int) : 0;
  va_end(arguments);
  return steam_nographics::synthetic_open(path, flags, has_mode, mode);
}

CAT_STEAM_NOGRAPHICS_EXPORT int openat(int directory_fd, const char* path, int flags, ...)
{
  va_list arguments;
  va_start(arguments, flags);
  const bool has_mode = (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
  const unsigned int mode = has_mode ? va_arg(arguments, unsigned int) : 0;
  va_end(arguments);
  return steam_nographics::synthetic_openat(directory_fd, path, flags, has_mode, mode);
}

CAT_STEAM_NOGRAPHICS_EXPORT int openat64(int directory_fd, const char* path, int flags, ...)
{
  va_list arguments;
  va_start(arguments, flags);
  const bool has_mode = (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
  const unsigned int mode = has_mode ? va_arg(arguments, unsigned int) : 0;
  va_end(arguments);
  return steam_nographics::synthetic_openat(directory_fd, path, flags, has_mode, mode);
}

CAT_STEAM_NOGRAPHICS_EXPORT FILE* fopen(const char* path, const char* mode)
{
  return steam_nographics::synthetic_fopen(path, mode);
}

CAT_STEAM_NOGRAPHICS_EXPORT FILE* fopen64(const char* path, const char* mode)
{
  return steam_nographics::synthetic_fopen(path, mode);
}

CAT_STEAM_NOGRAPHICS_EXPORT int fclose(FILE* stream)
{
  return steam_nographics::synthetic_fclose(stream);
}

CAT_STEAM_NOGRAPHICS_EXPORT ssize_t read(int file_descriptor, void* buffer, size_t count)
{
  return steam_nographics::synthetic_read(file_descriptor, buffer, count);
}

CAT_STEAM_NOGRAPHICS_EXPORT ssize_t pread64(int file_descriptor, void* buffer, size_t count, off64_t offset)
{
  return steam_nographics::synthetic_pread(file_descriptor, buffer, count, offset);
}

CAT_STEAM_NOGRAPHICS_EXPORT int close(int file_descriptor)
{
  return steam_nographics::synthetic_close(file_descriptor);
}

CAT_STEAM_NOGRAPHICS_EXPORT off_t lseek(int file_descriptor, off_t offset, int whence)
{
  return steam_nographics::synthetic_lseek(file_descriptor, offset, whence);
}

CAT_STEAM_NOGRAPHICS_EXPORT off64_t lseek64(int file_descriptor, off64_t offset, int whence)
{
  return steam_nographics::synthetic_lseek64(file_descriptor, offset, whence);
}

CAT_STEAM_NOGRAPHICS_EXPORT int ioctl(int file_descriptor, unsigned long request, ...)
{
  va_list arguments;
  va_start(arguments, request);
  void* argument = va_arg(arguments, void*);
  va_end(arguments);
  return steam_nographics::synthetic_ioctl(file_descriptor, request, argument);
}

CAT_STEAM_NOGRAPHICS_EXPORT int uname(struct utsname* name)
{
  return steam_nographics::synthetic_uname(name);
}

CAT_STEAM_NOGRAPHICS_EXPORT int getifaddrs(struct ifaddrs** ifaddrs)
{
  return steam_nographics::synthetic_getifaddrs(ifaddrs);
}

CAT_STEAM_NOGRAPHICS_EXPORT void freeifaddrs(struct ifaddrs* ifaddrs)
{
  steam_nographics::synthetic_freeifaddrs(ifaddrs);
}

CAT_STEAM_NOGRAPHICS_EXPORT const unsigned char* glGetString(gl_enum name)
{
  return steam_nographics::synthetic_gl_get_string(name);
}

CAT_STEAM_NOGRAPHICS_EXPORT void vkGetPhysicalDeviceProperties(void* physical_device, void* properties)
{
  steam_nographics::synthetic_vk_get_physical_device_properties(physical_device, properties);
}

CAT_STEAM_NOGRAPHICS_EXPORT void vkGetPhysicalDeviceProperties2(void* physical_device, void* properties2)
{
  steam_nographics::synthetic_vk_get_physical_device_properties2(physical_device, properties2);
}

CAT_STEAM_NOGRAPHICS_EXPORT void vkGetPhysicalDeviceProperties2KHR(void* physical_device, void* properties2)
{
  steam_nographics::synthetic_vk_get_physical_device_properties2(physical_device, properties2);
}

CAT_STEAM_NOGRAPHICS_EXPORT const char* udev_device_get_sysattr_value(void* device, const char* attribute)
{
  return steam_nographics::synthetic_udev_sysattr_value(device, attribute);
}

CAT_STEAM_NOGRAPHICS_EXPORT const char* udev_device_get_property_value(void* device, const char* property)
{
  return steam_nographics::synthetic_udev_property_value(device, property);
}

CAT_STEAM_NOGRAPHICS_EXPORT const char* nm_device_wifi_get_hw_address(void* device)
{
  return steam_nographics::synthetic_nm_hw_address(device, "nm_device_wifi_get_hw_address");
}

CAT_STEAM_NOGRAPHICS_EXPORT const char* nm_device_wifi_get_permanent_hw_address(void* device)
{
  return steam_nographics::synthetic_nm_hw_address(device, "nm_device_wifi_get_permanent_hw_address");
}

CAT_STEAM_NOGRAPHICS_EXPORT const char* nm_device_ethernet_get_hw_address(void* device)
{
  return steam_nographics::synthetic_nm_hw_address(device, "nm_device_ethernet_get_hw_address");
}

CAT_STEAM_NOGRAPHICS_EXPORT const char* nm_device_get_vendor(void* device)
{
  return steam_nographics::synthetic_nm_device_string(device, "nm_device_get_vendor");
}

CAT_STEAM_NOGRAPHICS_EXPORT const char* nm_device_get_product(void* device)
{
  return steam_nographics::synthetic_nm_device_string(device, "nm_device_get_product");
}

CAT_STEAM_NOGRAPHICS_EXPORT const char* nm_device_get_udi(void* device)
{
  return steam_nographics::synthetic_nm_device_string(device, "nm_device_get_udi");
}

CAT_STEAM_NOGRAPHICS_EXPORT int execve(const char* path, char* const argv[], char* const envp[])
{
  using function_type = int (*)(const char*, char* const[], char* const[]);
  static const auto original = next_symbol<function_type>("execve");
  char** const rewritten = steam_nographics::rewrite_webhelper_argv(path, argv);
  return original != nullptr ? original(path, rewritten != nullptr ? rewritten : argv, envp) : -1;
}

CAT_STEAM_NOGRAPHICS_EXPORT int execvp(const char* file, char* const argv[])
{
  using function_type = int (*)(const char*, char* const[]);
  static const auto original = next_symbol<function_type>("execvp");
  char** const rewritten = steam_nographics::rewrite_webhelper_argv(file, argv);
  return original != nullptr ? original(file, rewritten != nullptr ? rewritten : argv) : -1;
}

CAT_STEAM_NOGRAPHICS_EXPORT int posix_spawn(pid_t* pid, const char* path, const void* actions,
  const void* attributes, char* const argv[], char* const envp[])
{
  using function_type = int (*)(pid_t*, const char*, const void*, const void*, char* const[], char* const[]);
  static const auto original = next_symbol<function_type>("posix_spawn");
  char** const rewritten = steam_nographics::rewrite_webhelper_argv(path, argv);
  return original != nullptr ? original(pid, path, actions, attributes, rewritten != nullptr ? rewritten : argv, envp) : -1;
}
