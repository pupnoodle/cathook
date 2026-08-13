#include <fcntl.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

namespace
{

bool read_file(const char* path, std::string& result)
{
  const int file_descriptor = open(path, O_RDONLY);
  if (file_descriptor < 0) return false;
  char buffer[4096]{};
  const ssize_t length = read(file_descriptor, buffer, sizeof(buffer) - 1);
  close(file_descriptor);
  if (length < 0) return false;
  result.assign(buffer, static_cast<size_t>(length));
  return true;
}

bool read_file_stdio(const char* path, std::string& result)
{
  FILE* stream = std::fopen(path, "rb");
  if (stream == nullptr) return false;
  char buffer[4096]{};
  const size_t length = std::fread(buffer, 1, sizeof(buffer) - 1, stream);
  const int close_result = std::fclose(stream);
  if (close_result != 0) return false;
  result.assign(buffer, length);
  return true;
}

struct cpu_information_prefix
{
  int size;
  bool rdtsc : 1;
  bool cmov : 1;
  bool fcmov : 1;
  bool sse : 1;
  bool sse2 : 1;
  bool dnow : 1;
  bool mmx : 1;
  bool hyper_threading : 1;
  std::uint8_t logical_processors;
  std::uint8_t physical_processors;
  bool sse3 : 1;
  bool ssse3 : 1;
  bool sse4a : 1;
  bool sse41 : 1;
  bool sse42 : 1;
  bool avx : 1;
  std::int64_t speed;
  char* processor_id;
  std::uint32_t model;
  std::uint32_t features[3];
  char* processor_brand;
};

}

int main()
{
  const bool supplied_seed = std::getenv("CAT_STEAM_TXTMODE_HARDWARE_SEED") != nullptr;
  if (!supplied_seed)
  {
    setenv("CAT_STEAM_TXTMODE_HARDWARE_SEED", "seed-a", 1);
    std::string seed_a;
    std::string seed_b;
    if (!read_file("/etc/machine-id", seed_a))
    {
      std::fprintf(stderr, "seed-a machine-id read failed\n");
      return 1;
    }
    setenv("CAT_STEAM_TXTMODE_HARDWARE_SEED", "seed-b", 1);
    if (!read_file("/etc/machine-id", seed_b) || seed_a == seed_b)
    {
      std::fprintf(stderr, "seed isolation failed\n");
      return 1;
    }
    unsetenv("CAT_STEAM_TXTMODE_HARDWARE_SEED");
  }

  std::string first_machine_id;
  std::string second_machine_id;
  if (!read_file("/etc/machine-id", first_machine_id) || !read_file("/etc/machine-id", second_machine_id)
    || first_machine_id != second_machine_id || first_machine_id.size() != 33 || first_machine_id.back() != '\n')
  {
    std::fprintf(stderr, "machine-id substitution failed\n");
    return 2;
  }
  std::string stdio_machine_id;
  if (!read_file_stdio("/etc/machine-id", stdio_machine_id) || stdio_machine_id != first_machine_id)
  {
    std::fprintf(stderr, "stdio machine-id substitution failed\n");
    return 10;
  }

  struct utsname system_name{};
  if (uname(&system_name) != 0 || std::strcmp(system_name.nodename, "generic-host") != 0)
  {
    std::fprintf(stderr, "uname substitution failed\n");
    return 3;
  }

  struct ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0)
  {
    std::fprintf(stderr, "getifaddrs failed\n");
    return 4;
  }
  bool saw_packet = false;
  std::string first_mac;
  for (struct ifaddrs* entry = interfaces; entry != nullptr; entry = entry->ifa_next)
  {
    if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_PACKET) continue;
    const auto* packet = reinterpret_cast<const sockaddr_ll*>(entry->ifa_addr);
    if (packet->sll_halen == 6 && (packet->sll_addr[0] & 0x02) != 0)
    {
      saw_packet = true;
      if (first_mac.empty())
      {
        char formatted[18]{};
        std::snprintf(formatted, sizeof(formatted), "%02x:%02x:%02x:%02x:%02x:%02x",
          packet->sll_addr[0], packet->sll_addr[1], packet->sll_addr[2],
          packet->sll_addr[3], packet->sll_addr[4], packet->sll_addr[5]);
        first_mac = formatted;
      }
    }
  }
  freeifaddrs(interfaces);
  if (!saw_packet)
  {
    std::fprintf(stderr, "MAC substitution failed\n");
    return 5;
  }

  std::string cpu_info;
  if (!read_file("/proc/cpuinfo", cpu_info)
    || cpu_info.find("model name") == std::string::npos
    || (cpu_info.find("vendor_id\t: GenuineIntel") == std::string::npos
      && cpu_info.find("vendor_id\t: AuthenticAMD") == std::string::npos))
  {
    std::fprintf(stderr, "cpuinfo substitution failed\n");
    return 6;
  }

  using get_cpu_information_function = const void* (*)();
  const auto get_cpu_information = reinterpret_cast<get_cpu_information_function>(
    dlsym(RTLD_DEFAULT, "_Z17GetCPUInformationv"));
  const auto* cpu_information = get_cpu_information == nullptr
    ? nullptr : static_cast<const cpu_information_prefix*>(get_cpu_information());
  if (cpu_information == nullptr || cpu_information->size < static_cast<int>(sizeof(void*) == 8 ? 72 : 64)
    || cpu_information->processor_id == nullptr || cpu_information->processor_brand == nullptr
    || (std::strcmp(cpu_information->processor_id, "GenuineIntel") != 0
      && std::strcmp(cpu_information->processor_id, "AuthenticAMD") != 0)
    || cpu_information->processor_brand[0] == '\0')
  {
    std::fprintf(stderr, "GetCPUInformation substitution failed\n");
    return 9;
  }

  using gl_get_string_function = const unsigned char* (*)(unsigned int);
  const auto gl_get_string = reinterpret_cast<gl_get_string_function>(dlsym(RTLD_DEFAULT, "glGetString"));
  const unsigned char* renderer = gl_get_string == nullptr ? nullptr : gl_get_string(0x1f01);
  if (renderer == nullptr
    || (std::strstr(reinterpret_cast<const char*>(renderer), "NVIDIA") == nullptr
      && std::strstr(reinterpret_cast<const char*>(renderer), "Intel") == nullptr
      && std::strstr(reinterpret_cast<const char*>(renderer), "AMD") == nullptr))
  {
    std::fprintf(stderr, "OpenGL substitution failed\n");
    return 7;
  }

  std::string ordinary_file;
  if (!read_file("/etc/hostname", ordinary_file) || ordinary_file.empty())
  {
    std::fprintf(stderr, "ordinary file passthrough failed\n");
    return 8;
  }
  if (std::getenv("CAT_STEAM_TXTMODE_TEST_PRINT") != nullptr)
  {
    std::printf("cpu=%s\ngpu=%s\nmachine_id=%snic=%s\n", cpu_information->processor_brand,
      renderer == nullptr ? "" : reinterpret_cast<const char*>(renderer),
      first_machine_id.c_str(), first_mac.c_str());
  }
  return 0;
}
