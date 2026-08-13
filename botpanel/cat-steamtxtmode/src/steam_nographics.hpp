#ifndef CAT_STEAM_NOGRAPHICS_HPP
#define CAT_STEAM_NOGRAPHICS_HPP

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <sys/types.h>

namespace steam_nographics
{

void initialize();
void on_library_loaded(const char* library_path, void* module_handle);

[[nodiscard]] bool is_enabled();
[[nodiscard]] bool should_hide_windows();
[[nodiscard]] bool should_skip_presentation();
[[nodiscard]] bool should_skip_draw_calls();
[[nodiscard]] int offscreen_coordinate();
[[nodiscard]] unsigned int offscreen_extent();
void limit_present_rate();

char** rewrite_webhelper_argv(const char* executable_path, char* const argv[]);

[[nodiscard]] bool should_synthesize_hardware();
int synthetic_open(const char* path, int flags, bool has_mode, unsigned int mode);
int synthetic_openat(int directory_fd, const char* path, int flags, bool has_mode, unsigned int mode);
FILE* synthetic_fopen(const char* path, const char* mode);
int synthetic_fclose(FILE* stream);
ssize_t synthetic_read(int file_descriptor, void* buffer, size_t count);
ssize_t synthetic_pread(int file_descriptor, void* buffer, size_t count, std::int64_t offset);
int synthetic_close(int file_descriptor);
std::int64_t synthetic_lseek(int file_descriptor, std::int64_t offset, int whence);
std::int64_t synthetic_lseek64(int file_descriptor, std::int64_t offset, int whence);
int synthetic_ioctl(int file_descriptor, unsigned long request, void* argument);
int synthetic_uname(void* name);
int synthetic_getifaddrs(void* ifaddrs);
void synthetic_freeifaddrs(void* ifaddrs);
const unsigned char* synthetic_gl_get_string(unsigned int name);
void synthetic_vk_get_physical_device_properties(void* physical_device, void* properties);
void synthetic_vk_get_physical_device_properties2(void* physical_device, void* properties2);
const char* synthetic_udev_sysattr_value(void* device, const char* attribute);
const char* synthetic_udev_property_value(void* device, const char* property);
const char* synthetic_nm_hw_address(void* device, const char* api_name);
const char* synthetic_nm_device_string(void* device, const char* api_name);
const void* synthetic_cpu_information_pointer();

}

#endif
