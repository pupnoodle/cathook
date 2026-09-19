#include "steam_nographics.hpp"
#include "config.hpp"
#include <cstdarg>
#include <fcntl.h>
#include <ifaddrs.h>
#include <sys/utsname.h>

namespace steam_nographics {
bool is_enabled() {
  return !pup_stm::settings().disabled && pup_stm::env_flag("PUP_STEAM_TXTMODE", true);
}
}

#define PUP_STEAM_NOGRAPHICS_EXPORT extern "C" __attribute__((visibility("default")))

__attribute__((visibility("default"))) const void* GetCPUInformation() {
  return steam_nographics::synthetic_cpu_information_pointer();
}

PUP_STEAM_NOGRAPHICS_EXPORT int open(const char* path, int flags, ...)
{
  va_list arguments;
  va_start(arguments, flags);
  const bool has_mode = (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
  const unsigned int mode = has_mode ? va_arg(arguments, unsigned int) : 0;
  va_end(arguments);
  return steam_nographics::synthetic_open(path, flags, has_mode, mode);
}

PUP_STEAM_NOGRAPHICS_EXPORT int open64(const char* path, int flags, ...)
{
  va_list arguments;
  va_start(arguments, flags);
  const bool has_mode = (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
  const unsigned int mode = has_mode ? va_arg(arguments, unsigned int) : 0;
  va_end(arguments);
  return steam_nographics::synthetic_open(path, flags, has_mode, mode);
}

PUP_STEAM_NOGRAPHICS_EXPORT int openat(int directory_fd, const char* path, int flags, ...)
{
  va_list arguments;
  va_start(arguments, flags);
  const bool has_mode = (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
  const unsigned int mode = has_mode ? va_arg(arguments, unsigned int) : 0;
  va_end(arguments);
  return steam_nographics::synthetic_openat(directory_fd, path, flags, has_mode, mode);
}

PUP_STEAM_NOGRAPHICS_EXPORT int openat64(int directory_fd, const char* path, int flags, ...)
{
  va_list arguments;
  va_start(arguments, flags);
  const bool has_mode = (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
  const unsigned int mode = has_mode ? va_arg(arguments, unsigned int) : 0;
  va_end(arguments);
  return steam_nographics::synthetic_openat(directory_fd, path, flags, has_mode, mode);
}

PUP_STEAM_NOGRAPHICS_EXPORT FILE* fopen(const char* path, const char* mode)
{
  return steam_nographics::synthetic_fopen(path, mode);
}

PUP_STEAM_NOGRAPHICS_EXPORT FILE* fopen64(const char* path, const char* mode)
{
  return steam_nographics::synthetic_fopen(path, mode);
}

PUP_STEAM_NOGRAPHICS_EXPORT int fclose(FILE* stream)
{
  return steam_nographics::synthetic_fclose(stream);
}

PUP_STEAM_NOGRAPHICS_EXPORT ssize_t read(int file_descriptor, void* buffer, size_t count)
{
  return steam_nographics::synthetic_read(file_descriptor, buffer, count);
}

PUP_STEAM_NOGRAPHICS_EXPORT ssize_t pread64(int file_descriptor, void* buffer, size_t count, off64_t offset)
{
  return steam_nographics::synthetic_pread(file_descriptor, buffer, count, offset);
}

PUP_STEAM_NOGRAPHICS_EXPORT int close(int file_descriptor)
{
  return steam_nographics::synthetic_close(file_descriptor);
}

PUP_STEAM_NOGRAPHICS_EXPORT off_t lseek(int file_descriptor, off_t offset, int whence)
{
  return steam_nographics::synthetic_lseek(file_descriptor, offset, whence);
}

PUP_STEAM_NOGRAPHICS_EXPORT off64_t lseek64(int file_descriptor, off64_t offset, int whence)
{
  return steam_nographics::synthetic_lseek64(file_descriptor, offset, whence);
}

PUP_STEAM_NOGRAPHICS_EXPORT int ioctl(int file_descriptor, unsigned long request, ...)
{
  va_list arguments;
  va_start(arguments, request);
  void* argument = va_arg(arguments, void*);
  va_end(arguments);
  return steam_nographics::synthetic_ioctl(file_descriptor, request, argument);
}

PUP_STEAM_NOGRAPHICS_EXPORT int uname(struct utsname* name)
{
  return steam_nographics::synthetic_uname(name);
}

PUP_STEAM_NOGRAPHICS_EXPORT int getifaddrs(struct ifaddrs** ifaddrs)
{
  return steam_nographics::synthetic_getifaddrs(ifaddrs);
}

PUP_STEAM_NOGRAPHICS_EXPORT void freeifaddrs(struct ifaddrs* ifaddrs)
{
  steam_nographics::synthetic_freeifaddrs(ifaddrs);
}

PUP_STEAM_NOGRAPHICS_EXPORT const unsigned char* glGetString(unsigned int name)
{
  return steam_nographics::synthetic_gl_get_string(name);
}

PUP_STEAM_NOGRAPHICS_EXPORT void vkGetPhysicalDeviceProperties(void* physical_device, void* properties)
{
  steam_nographics::synthetic_vk_get_physical_device_properties(physical_device, properties);
}

PUP_STEAM_NOGRAPHICS_EXPORT void vkGetPhysicalDeviceProperties2(void* physical_device, void* properties2)
{
  steam_nographics::synthetic_vk_get_physical_device_properties2(physical_device, properties2);
}

PUP_STEAM_NOGRAPHICS_EXPORT void vkGetPhysicalDeviceProperties2KHR(void* physical_device, void* properties2)
{
  steam_nographics::synthetic_vk_get_physical_device_properties2(physical_device, properties2);
}

PUP_STEAM_NOGRAPHICS_EXPORT const char* udev_device_get_sysattr_value(void* device, const char* attribute)
{
  return steam_nographics::synthetic_udev_sysattr_value(device, attribute);
}

PUP_STEAM_NOGRAPHICS_EXPORT const char* udev_device_get_property_value(void* device, const char* property)
{
  return steam_nographics::synthetic_udev_property_value(device, property);
}

PUP_STEAM_NOGRAPHICS_EXPORT const char* nm_device_wifi_get_hw_address(void* device)
{
  return steam_nographics::synthetic_nm_hw_address(device, "nm_device_wifi_get_hw_address");
}

PUP_STEAM_NOGRAPHICS_EXPORT const char* nm_device_wifi_get_permanent_hw_address(void* device)
{
  return steam_nographics::synthetic_nm_hw_address(device, "nm_device_wifi_get_permanent_hw_address");
}

PUP_STEAM_NOGRAPHICS_EXPORT const char* nm_device_ethernet_get_hw_address(void* device)
{
  return steam_nographics::synthetic_nm_hw_address(device, "nm_device_ethernet_get_hw_address");
}

PUP_STEAM_NOGRAPHICS_EXPORT const char* nm_device_get_vendor(void* device)
{
  return steam_nographics::synthetic_nm_device_string(device, "nm_device_get_vendor");
}

PUP_STEAM_NOGRAPHICS_EXPORT const char* nm_device_get_product(void* device)
{
  return steam_nographics::synthetic_nm_device_string(device, "nm_device_get_product");
}

PUP_STEAM_NOGRAPHICS_EXPORT const char* nm_device_get_udi(void* device)
{
  return steam_nographics::synthetic_nm_device_string(device, "nm_device_get_udi");
}

