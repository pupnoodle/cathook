#include "steam_nographics.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <net/if_arp.h>
#include <linux/memfd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>

namespace steam_nographics
{

namespace
{

using open_function = int (*)(const char*, int, ...);
using openat_function = int (*)(int, const char*, int, ...);
using read_function = ssize_t (*)(int, void*, size_t);
using pread_function = ssize_t (*)(int, void*, size_t, off_t);
using close_function = int (*)(int);
using lseek_function = off_t (*)(int, off_t, int);
using ioctl_function = int (*)(int, unsigned long, ...);
using uname_function = int (*)(struct utsname*);
using getifaddrs_function = int (*)(struct ifaddrs**);
using freeifaddrs_function = void (*)(struct ifaddrs*);

struct tracked_file
{
  std::string contents;
  off_t offset = 0;
};

std::mutex tracked_files_mutex;
std::unordered_map<int, tracked_file> tracked_files;
thread_local bool inside_interposer = false;

template <typename function_type>
function_type next_symbol(const char* name)
{
  return reinterpret_cast<function_type>(dlsym(RTLD_NEXT, name));
}

bool env_flag(const char* name, bool fallback)
{
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0')
  {
    return fallback;
  }
  return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

std::string process_basename()
{
  char path[4096]{};
  const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (length <= 0)
  {
    return {};
  }
  path[length] = '\0';
  const char* slash = std::strrchr(path, '/');
  return slash == nullptr ? path : slash + 1;
}

bool target_process()
{
  const std::string name = process_basename();
  return name == "steam" || name == "steamwebhelper" || name == "gameoverlayui"
    || name == "tf_linux64" || name == "tf_linux" || name == "hl2_linux";
}

std::string seed()
{
  static const std::string random_session = []
  {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return std::string("session-") + std::to_string(static_cast<unsigned long long>(getpid()))
      + "-" + std::to_string(static_cast<unsigned long long>(now.tv_sec))
      + "-" + std::to_string(static_cast<unsigned long long>(now.tv_nsec));
  }();
  const char* explicit_seed = std::getenv("CAT_STEAM_TXTMODE_HARDWARE_SEED");
  if (explicit_seed != nullptr && explicit_seed[0] != '\0')
  {
    return explicit_seed;
  }
  if (env_flag("CAT_STEAM_TXTMODE_HARDWARE_RANDOMIZE", false))
  {
    return random_session;
  }
  const char* bot_id = std::getenv("CAT_BOT_ID");
  return bot_id != nullptr && bot_id[0] != '\0' ? bot_id : "default";
}

std::uint64_t hash64(const std::string& value, std::uint64_t salt)
{
  std::uint64_t hash = 1469598103934665603ULL ^ salt;
  for (unsigned char character : value)
  {
    hash ^= character;
    hash *= 1099511628211ULL;
    hash ^= hash >> 29;
  }
  return hash;
}

std::string hex64(std::uint64_t value)
{
  static constexpr char digits[] = "0123456789abcdef";
  std::string result(16, '0');
  for (int index = 15; index >= 0; --index)
  {
    result[static_cast<size_t>(index)] = digits[value & 0xfU];
    value >>= 4U;
  }
  return result;
}

std::string identity128(const std::string& purpose)
{
  const std::string value = seed() + ":" + purpose;
  return hex64(hash64(value, 0x4d414348494e4541ULL))
    + hex64(hash64(value, 0x4d414348494e4242ULL));
}

std::string synthetic_mac(const std::string& interface_name)
{
  std::uint64_t value = hash64(seed() + ":nic:" + interface_name, 0x4e49435f4944454eULL);
  static constexpr std::array<std::array<unsigned char, 3>, 16> nic_ouis{{
    {{ 0x02, 0x00, 0x00 }}, {{ 0x02, 0x11, 0x22 }}, {{ 0x02, 0x16, 0x3e }},
    {{ 0x02, 0x42, 0xac }}, {{ 0x02, 0x50, 0xf2 }}, {{ 0x02, 0x52, 0x54 }},
    {{ 0x02, 0x54, 0x91 }}, {{ 0x02, 0x5e, 0x7f }}, {{ 0x02, 0x60, 0x2f }},
    {{ 0x02, 0x70, 0xb3 }}, {{ 0x02, 0x80, 0xc2 }}, {{ 0x02, 0x90, 0x27 }},
    {{ 0x02, 0xa0, 0x4b }}, {{ 0x02, 0xb0, 0x5c }}, {{ 0x02, 0xc0, 0x6d }},
    {{ 0x02, 0xd0, 0x7e }},
  }};
  const auto& oui = nic_ouis[value % nic_ouis.size()];
  std::array<unsigned char, 6> mac{
    oui[0], oui[1], oui[2],
    static_cast<unsigned char>((value >> 16U) & 0xffU),
    static_cast<unsigned char>((value >> 8U) & 0xffU),
    static_cast<unsigned char>(value & 0xffU)
  };
  char result[18]{};
  std::snprintf(result, sizeof(result), "%02x:%02x:%02x:%02x:%02x:%02x",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return result;
}

struct hardware_profile
{
  const char* cpu_vendor;
  const char* cpu_brand;
  std::uint32_t cpu_family;
  std::uint32_t cpu_model;
  std::uint32_t cpu_stepping;
  std::uint32_t cpu_features[3];
  const char* cpu_flags;
  std::uint8_t logical_processors;
  std::uint8_t physical_processors;
  std::int64_t cpu_speed;
  bool rdtsc;
  bool cmov;
  bool fcmov;
  bool sse;
  bool sse2;
  bool dnow;
  bool mmx;
  bool hyper_threading;
  bool sse3;
  bool ssse3;
  bool sse4a;
  bool sse41;
  bool sse42;
  bool avx;
  const char* gpu_vendor;
  const char* gpu_renderer;
  const char* gpu_version;
  const char* gpu_driver;
  std::uint32_t gpu_vendor_id;
  std::uint32_t gpu_device_id;
  std::uint32_t gpu_driver_major;
  std::uint32_t gpu_driver_minor;
  std::uint32_t dx_level;
  const char* board_vendor;
  const char* board_name;
  const char* disk_vendor;
  const char* disk_model;
};

static constexpr hardware_profile hardware_profiles[] = {
  {
    "GenuineIntel", "Intel(R) Core(TM) i5-4570 CPU @ 3.20GHz", 6, 60, 3,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 4, 4, 3200000000LL,
    true, true, true, true, true, false, true, false, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 1060 6GB/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1c03, 535, 113,
    111, "ASUSTeK COMPUTER INC.", "H97-PRO", "Seagate", "ST1000DM010"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz", 6, 94, 3,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 8, 4, 4000000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "Intel", "Mesa Intel(R) HD Graphics 530 (SKL GT2)", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x1912, 24, 0,
    110, "Gigabyte Technology Co., Ltd.", "Z170X-Gaming 5", "Samsung", "SSD 850 EVO 500GB"
  },
  {
    "AuthenticAMD", "AMD Ryzen 5 3600 6-Core Processor", 23, 113, 0,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 3600000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "AMD", "AMD Radeon RX 580 Series (POLARIS10, DRM 3.54.0)", "4.6.0 AMD Radeon RX 580", "23.0.0", 0x1002, 0x67df, 23, 0,
    121, "MSI", "B450 TOMAHAWK MAX", "WDC", "WD Blue SN570 1TB"
  },
  {
    "AuthenticAMD", "AMD FX(TM)-8350 Eight-Core Processor", 21, 2, 0,
    { 0x178bfbffU, 0x2fd3fbffU, 0x00000000U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4a sse4_1 sse4_2", 8, 4, 4000000000LL,
    true, true, true, true, true, true, true, true, true, true, true, true, true, false,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 970/PCIe/SSE2", "4.6.0 NVIDIA 525.147.05", "525.147.05", 0x10de, 0x13c2, 525, 147,
    111, "ASRock", "970A-G/3.1", "TOSHIBA", "DT01ACA200"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i3-10100 CPU @ 3.60GHz", 6, 165, 3,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 8, 4, 3600000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "Intel", "Mesa Intel(R) UHD Graphics 630 (CML GT2)", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x9bc8, 24, 0,
    110, "ASUSTeK COMPUTER INC.", "PRIME B460-PLUS", "Crucial", "CT1000MX500SSD1"
  },
  {
    "AuthenticAMD", "AMD Ryzen 7 5800X 8-Core Processor", 25, 1, 0,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 16, 8, 3800000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "AMD", "AMD Radeon RX 6800 XT (SIENNA_CICHLID, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6800 XT", "23.0.0", 0x1002, 0x73bf, 23, 0,
    121, "ASUSTeK COMPUTER INC.", "ROG STRIX B550-F GAMING", "Kingston", "SKC3000S1024G"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i5-2500K CPU @ 3.30GHz", 6, 42, 7,
    { 0x178bfbffU, 0x1fd3fbffU, 0x00000000U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2", 4, 4, 3300000000LL,
    true, true, true, true, true, false, true, false, true, true, false, true, true, false,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 750 Ti/PCIe/SSE2", "4.6.0 NVIDIA 470.239.06", "470.239.06", 0x10de, 0x1380, 470, 239,
    110, "ASUSTeK COMPUTER INC.", "P8P67 DELUXE", "Western Digital", "WDC WD10EZEX-00WN4A0"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-2600K CPU @ 3.40GHz", 6, 42, 7,
    { 0x178bfbffU, 0x1fd3fbffU, 0x00000000U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2", 8, 4, 3400000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, false,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 680/PCIe/SSE2", "4.6.0 NVIDIA 470.239.06", "470.239.06", 0x10de, 0x1180, 470, 239,
    111, "Gigabyte Technology Co., Ltd.", "GA-Z68X-UD3H-B3", "Seagate", "ST2000DM001-1CH164"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i5-3570K CPU @ 3.40GHz", 6, 58, 9,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 4, 4, 3400000000LL,
    true, true, true, true, true, false, true, false, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 760/PCIe/SSE2", "4.6.0 NVIDIA 470.239.06", "470.239.06", 0x10de, 0x1187, 470, 239,
    111, "ASRock", "Z77 Extreme4", "Samsung", "SSD 840 PRO Series 256GB"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-3770 CPU @ 3.40GHz", 6, 58, 9,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 8, 4, 3400000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "Intel", "Mesa Intel(R) HD Graphics 4000 (IVB GT2)", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x0162, 24, 0,
    110, "Intel Corporation", "DQ77MK", "Intel", "SSDSC2CW240A3"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i5-4690K CPU @ 3.50GHz", 6, 60, 3,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 4, 4, 3500000000LL,
    true, true, true, true, true, false, true, false, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 970/PCIe/SSE2", "4.6.0 NVIDIA 525.147.05", "525.147.05", 0x10de, 0x13c2, 525, 147,
    111, "MSI", "Z97 GAMING 5 (MS-7917)", "SanDisk", "SDSSDA240G"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-4790K CPU @ 4.00GHz", 6, 60, 3,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 8, 4, 4000000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 980/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x13c0, 535, 113,
    111, "ASUSTeK COMPUTER INC.", "MAXIMUS VII HERO", "Crucial", "CT500MX500SSD1"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-5820K CPU @ 3.30GHz", 6, 63, 2,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 3300000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 1080/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1b80, 535, 113,
    111, "ASUSTeK COMPUTER INC.", "X99-A II", "Samsung", "SSD 850 EVO 1TB"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-7700K CPU @ 4.20GHz", 6, 158, 9,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 8, 4, 4200000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 1070/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1b81, 535, 113,
    111, "Gigabyte Technology Co., Ltd.", "Z270X-Gaming K5", "Intel", "SSDSC2KW512G8"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i5-8400 CPU @ 2.80GHz", 6, 158, 10,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 6, 6, 2800000000LL,
    true, true, true, true, true, false, true, false, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 1060 6GB/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1c03, 535, 113,
    111, "MSI", "Z370 GAMING PRO CARBON", "TOSHIBA", "DT01ACA200"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-8700K CPU @ 3.70GHz", 6, 158, 10,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 3700000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 2060/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1f08, 535, 113,
    111, "ASUSTeK COMPUTER INC.", "ROG STRIX Z370-F GAMING", "Western Digital", "WDS100T2B0A"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i5-9600K CPU @ 3.70GHz", 6, 158, 12,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 6, 6, 3700000000LL,
    true, true, true, true, true, false, true, false, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 1660 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2182, 535, 113,
    111, "Gigabyte Technology Co., Ltd.", "Z390 AORUS PRO WIFI", "Kingston", "SA2000M8500G"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz", 6, 158, 13,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 8, 8, 3600000000LL,
    true, true, true, true, true, false, true, false, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 2070/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1f02, 535, 113,
    111, "ASUSTeK COMPUTER INC.", "PRIME Z390-A", "Samsung", "SSD 970 EVO Plus 1TB"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i5-10400 CPU @ 2.90GHz", 6, 165, 3,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 2900000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "AMD", "AMD Radeon RX 5600 XT (NAVI10, DRM 3.54.0)", "4.6.0 AMD Radeon RX 5600 XT", "23.0.0", 0x1002, 0x731f, 23, 0,
    121, "MSI", "MAG B460 TOMAHAWK", "Crucial", "CT1000P1SSD8"
  },
  {
    "GenuineIntel", "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz", 6, 165, 5,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 16, 8, 3800000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2504, 535, 113,
    121, "ASUSTeK COMPUTER INC.", "ROG STRIX Z490-E GAMING", "Western Digital", "WD_BLACK SN750 1TB"
  },
  {
    "GenuineIntel", "11th Gen Intel(R) Core(TM) i5-11400 @ 2.60GHz", 6, 167, 1,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 2600000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "Intel", "Mesa Intel(R) UHD Graphics 730 (RKL GT1)", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x4c8a, 24, 0,
    110, "ASRock", "B560 Steel Legend", "Kingston", "KINGSTON SA2000M81000G"
  },
  {
    "GenuineIntel", "11th Gen Intel(R) Core(TM) i7-11700K @ 3.60GHz", 6, 167, 1,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 16, 8, 3600000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 3070/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2484, 535, 113,
    121, "Gigabyte Technology Co., Ltd.", "Z590 AORUS ELITE AX", "Samsung", "SSD 980 1TB"
  },
  {
    "GenuineIntel", "12th Gen Intel(R) Core(TM) i5-12400", 6, 151, 2,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 2500000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 3060 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2486, 535, 113,
    121, "MSI", "PRO B660M-A WIFI DDR4", "Solidigm", "P41 Plus 1TB"
  },
  {
    "GenuineIntel", "12th Gen Intel(R) Core(TM) i7-12700K", 6, 151, 2,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 20, 12, 3600000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "Intel", "Intel(R) Arc(TM) A770 Graphics", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x56a0, 24, 0,
    121, "ASUSTeK COMPUTER INC.", "ROG STRIX Z690-A GAMING WIFI D4", "Samsung", "SSD 980 PRO 1TB"
  },
  {
    "GenuineIntel", "13th Gen Intel(R) Core(TM) i5-13600K", 6, 183, 1,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 20, 14, 3500000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 4070/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2786, 535, 113,
    121, "MSI", "PRO Z690-A WIFI", "Solidigm", "P44 Pro 1TB"
  },
  {
    "GenuineIntel", "13th Gen Intel(R) Core(TM) i9-13900K", 6, 183, 1,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 32, 24, 3000000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "AMD", "AMD Radeon RX 7900 XTX (NAVI31, DRM 3.54.0)", "4.6.0 AMD Radeon RX 7900 XTX", "23.0.0", 0x1002, 0x744c, 23, 0,
    121, "Gigabyte Technology Co., Ltd.", "Z790 AORUS MASTER", "Western Digital", "WD_BLACK SN850X 2TB"
  },
  {
    "GenuineIntel", "14th Gen Intel(R) Core(TM) i7-14700K", 6, 183, 1,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 28, 20, 3400000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 4080/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2704, 550, 54,
    121, "ASUSTeK COMPUTER INC.", "ROG MAXIMUS Z790 HERO", "Kingston", "KC3000 2048GB"
  },
  {
    "GenuineIntel", "Intel(R) Pentium(R) CPU G4560 @ 3.50GHz", 6, 158, 9,
    { 0x178bfbffU, 0x7ffafbffU, 0x001c2fb9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2", 4, 2, 3500000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 1050 Ti/PCIe/SSE2", "4.6.0 NVIDIA 470.239.06", "470.239.06", 0x10de, 0x1c82, 470, 239,
    110, "MSI", "B250M PRO-VD", "ADATA", "SU800NS38 512GB"
  },
  {
    "AuthenticAMD", "AMD Ryzen 3 2200G with Radeon Vega Graphics", 23, 17, 0,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 4, 4, 3500000000LL,
    true, true, true, true, true, false, true, false, true, true, false, true, true, true,
    "AMD", "AMD Radeon Vega 8 Graphics (RAVEN, DRM 3.54.0)", "4.6.0 AMD Radeon Vega 8", "23.0.0", 0x1002, 0x15dd, 23, 0,
    121, "ASRock", "B450M-HDV R4.0", "Kingston", "SA400S37480G"
  },
  {
    "AuthenticAMD", "AMD Ryzen 5 1600 Six-Core Processor", 23, 1, 1,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 3200000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 1060 6GB/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1c03, 535, 113,
    111, "MSI", "B350 TOMAHAWK", "Samsung", "SSD 860 EVO 500GB"
  },
  {
    "AuthenticAMD", "AMD Ryzen 7 2700X Eight-Core Processor", 23, 8, 2,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 16, 8, 3700000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "AMD", "AMD Radeon RX Vega 64 (VEGA10, DRM 3.54.0)", "4.6.0 AMD Radeon RX Vega 64", "23.0.0", 0x1002, 0x687f, 23, 0,
    121, "ASUSTeK COMPUTER INC.", "ROG STRIX X470-F GAMING", "Intel", "SSDPEKKW512G8"
  },
  {
    "AuthenticAMD", "AMD Ryzen 7 3700X 8-Core Processor", 23, 113, 0,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 16, 8, 3600000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 2070 SUPER/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1e84, 535, 113,
    121, "MSI", "MPG X570 GAMING EDGE WIFI", "Sabrent", "Samsung SSD 970 EVO Plus 1TB"
  },
  {
    "AuthenticAMD", "AMD Ryzen 5 5600X 6-Core Processor", 25, 33, 0,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 3700000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2504, 535, 113,
    121, "ASUSTeK COMPUTER INC.", "TUF GAMING B550-PLUS", "Western Digital", "WD Blue SN570 1TB"
  },
  {
    "AuthenticAMD", "AMD Ryzen 9 5900X 12-Core Processor", 25, 33, 0,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 24, 12, 3700000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "AMD", "AMD Radeon RX 6800 (SIENNA_CICHLID, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6800", "23.0.0", 0x1002, 0x73bf, 23, 0,
    121, "MSI", "MAG X570 TOMAHAWK WIFI", "Seagate", "FireCuda 520 SSD 1TB"
  },
  {
    "AuthenticAMD", "AMD Ryzen 5 7600X 6-Core Processor", 25, 97, 2,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 12, 6, 4700000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce RTX 4070/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2786, 535, 113,
    121, "Gigabyte Technology Co., Ltd.", "B650 AORUS ELITE AX", "Crucial", "CT2000P5PSSD8"
  },
  {
    "AuthenticAMD", "AMD Ryzen 7 7800X3D 8-Core Processor", 25, 97, 2,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 16, 8, 4200000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "AMD", "AMD Radeon RX 7900 XT (NAVI31, DRM 3.54.0)", "4.6.0 AMD Radeon RX 7900 XT", "23.0.0", 0x1002, 0x744c, 23, 0,
    121, "ASRock", "B650E PG Riptide WiFi", "Solidigm", "P44 Pro 2TB"
  },
  {
    "AuthenticAMD", "AMD Ryzen Threadripper 1950X 16-Core Processor", 23, 1, 1,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 32, 16, 3400000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "NVIDIA Corporation", "NVIDIA GeForce GTX 1080 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1b06, 535, 113,
    121, "ASUSTeK COMPUTER INC.", "ROG ZENITH EXTREME", "Samsung", "SSD 960 EVO 1TB"
  },
  {
    "AuthenticAMD", "AMD Athlon 3000G with Radeon Vega Graphics", 23, 24, 1,
    { 0x178bfbffU, 0x7ed8320bU, 0x219c01a9U },
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2", 4, 2, 3500000000LL,
    true, true, true, true, true, false, true, true, true, true, false, true, true, true,
    "AMD", "AMD Radeon Vega 3 Graphics (PICASSO, DRM 3.54.0)", "4.6.0 AMD Radeon Vega 3", "23.0.0", 0x1002, 0x15d8, 23, 0,
    110, "Gigabyte Technology Co., Ltd.", "B450M DS3H", "Kingston", "SA400S37240G"
  },
};

static_assert(sizeof(hardware_profiles) / sizeof(hardware_profiles[0]) >= 38,
  "hardware profile catalog unexpectedly shrank");

struct catalog_cpu_variant
{
  const char* vendor;
  const char* brand;
  std::uint32_t family;
  std::uint32_t model;
  std::uint32_t stepping;
  std::uint8_t logical_processors;
  std::uint8_t physical_processors;
  std::int64_t speed;
  bool avx;
};

struct catalog_gpu_variant
{
  const char* vendor;
  const char* renderer;
  const char* version;
  const char* driver;
  std::uint32_t vendor_id;
  std::uint32_t device_id;
  std::uint32_t driver_major;
  std::uint32_t driver_minor;
  std::uint32_t dx_level;
};

static constexpr catalog_cpu_variant catalog_cpus[] = {
  { "GenuineIntel", "Intel(R) Core(TM)2 Duo CPU E8400 @ 3.00GHz", 6, 23, 10, 2, 2, 3000000000LL, false },
  { "GenuineIntel", "Intel(R) Core(TM)2 Quad CPU Q6600 @ 2.40GHz", 6, 15, 11, 4, 4, 2400000000LL, false },
  { "GenuineIntel", "Intel(R) Pentium(R) Dual CPU G3258 @ 3.20GHz", 6, 60, 3, 2, 2, 3200000000LL, true },
  { "GenuineIntel", "Intel(R) Pentium(R) CPU G4400 @ 3.30GHz", 6, 94, 3, 2, 2, 3300000000LL, true },
  { "GenuineIntel", "Intel(R) Pentium(R) CPU G4560 @ 3.50GHz", 6, 158, 9, 4, 2, 3500000000LL, true },
  { "GenuineIntel", "Intel(R) Pentium(R) Gold G5400 CPU @ 3.70GHz", 6, 158, 10, 4, 2, 3700000000LL, true },
  { "GenuineIntel", "Intel(R) Pentium(R) Gold G6400 CPU @ 4.00GHz", 6, 165, 3, 4, 2, 4000000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Pentium(R) Gold G7400", 6, 151, 5, 4, 2, 3700000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i3-2100 CPU @ 3.10GHz", 6, 42, 7, 4, 2, 3100000000LL, false },
  { "GenuineIntel", "Intel(R) Core(TM) i3-3220 CPU @ 3.30GHz", 6, 58, 9, 4, 2, 3300000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i3-4130 CPU @ 3.40GHz", 6, 60, 3, 4, 2, 3400000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i3-6100 CPU @ 3.70GHz", 6, 94, 3, 4, 2, 3700000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i3-8100 CPU @ 3.60GHz", 6, 158, 10, 4, 4, 3600000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i3-9100F CPU @ 3.60GHz", 6, 158, 13, 4, 4, 3600000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i3-10100 CPU @ 3.60GHz", 6, 165, 3, 8, 4, 3600000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Core(TM) i3-12100", 6, 151, 2, 8, 4, 3300000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i3-13100", 6, 183, 1, 8, 4, 3400000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-2400 CPU @ 3.10GHz", 6, 42, 7, 4, 4, 3100000000LL, false },
  { "GenuineIntel", "Intel(R) Core(TM) i5-2500K CPU @ 3.30GHz", 6, 42, 7, 4, 4, 3300000000LL, false },
  { "GenuineIntel", "Intel(R) Core(TM) i5-3470 CPU @ 3.20GHz", 6, 58, 9, 4, 4, 3200000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-3570K CPU @ 3.40GHz", 6, 58, 9, 4, 4, 3400000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-4460 CPU @ 3.20GHz", 6, 60, 3, 4, 4, 3200000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-4570 CPU @ 3.20GHz", 6, 60, 3, 4, 4, 3200000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-4690K CPU @ 3.50GHz", 6, 60, 3, 4, 4, 3500000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-6500 CPU @ 3.20GHz", 6, 94, 3, 4, 4, 3200000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-6600K CPU @ 3.50GHz", 6, 94, 3, 4, 4, 3500000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-7400 CPU @ 3.00GHz", 6, 158, 9, 4, 4, 3000000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-7600K CPU @ 3.80GHz", 6, 158, 9, 4, 4, 3800000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-8400 CPU @ 2.80GHz", 6, 158, 10, 6, 6, 2800000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-8600K CPU @ 3.60GHz", 6, 158, 10, 6, 6, 3600000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-9400F CPU @ 2.90GHz", 6, 158, 13, 6, 6, 2900000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-9600K CPU @ 3.70GHz", 6, 158, 12, 6, 6, 3700000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-10400F CPU @ 2.90GHz", 6, 165, 3, 12, 6, 2900000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-10600K CPU @ 4.10GHz", 6, 165, 5, 12, 6, 4100000000LL, true },
  { "GenuineIntel", "11th Gen Intel(R) Core(TM) i5-11400F @ 2.60GHz", 6, 167, 1, 12, 6, 2600000000LL, true },
  { "GenuineIntel", "11th Gen Intel(R) Core(TM) i5-11600K @ 3.90GHz", 6, 167, 1, 12, 6, 3900000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Core(TM) i5-12400F", 6, 151, 2, 12, 6, 2500000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Core(TM) i5-12600K", 6, 151, 2, 16, 10, 3700000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i5-13400F", 6, 183, 1, 16, 10, 2500000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i5-13600K", 6, 183, 1, 20, 14, 3500000000LL, true },
  { "GenuineIntel", "14th Gen Intel(R) Core(TM) i5-14400F", 6, 183, 1, 16, 10, 2500000000LL, true },
  { "GenuineIntel", "14th Gen Intel(R) Core(TM) i5-14600K", 6, 183, 1, 20, 14, 3500000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-2600K CPU @ 3.40GHz", 6, 42, 7, 8, 4, 3400000000LL, false },
  { "GenuineIntel", "Intel(R) Core(TM) i7-3770 CPU @ 3.40GHz", 6, 58, 9, 8, 4, 3400000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-4770K CPU @ 3.50GHz", 6, 60, 3, 8, 4, 3500000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-4790K CPU @ 4.00GHz", 6, 60, 3, 8, 4, 4000000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-5820K CPU @ 3.30GHz", 6, 63, 2, 12, 6, 3300000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz", 6, 94, 3, 8, 4, 4000000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-7700K CPU @ 4.20GHz", 6, 158, 9, 8, 4, 4200000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-8700K CPU @ 3.70GHz", 6, 158, 10, 12, 6, 3700000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz", 6, 158, 13, 8, 8, 3600000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz", 6, 165, 5, 16, 8, 3800000000LL, true },
  { "GenuineIntel", "11th Gen Intel(R) Core(TM) i7-11700K @ 3.60GHz", 6, 167, 1, 16, 8, 3600000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Core(TM) i7-12700K", 6, 151, 2, 20, 12, 3600000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i7-13700K", 6, 183, 1, 24, 16, 3400000000LL, true },
  { "GenuineIntel", "14th Gen Intel(R) Core(TM) i7-14700K", 6, 183, 1, 28, 20, 3400000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i9-9900K CPU @ 3.60GHz", 6, 158, 13, 16, 8, 3600000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i9-10900K CPU @ 3.70GHz", 6, 165, 5, 20, 10, 3700000000LL, true },
  { "GenuineIntel", "11th Gen Intel(R) Core(TM) i9-11900K @ 3.50GHz", 6, 167, 1, 16, 8, 3500000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Core(TM) i9-12900K", 6, 151, 2, 24, 16, 3200000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i9-13900K", 6, 183, 1, 32, 24, 3000000000LL, true },
  { "GenuineIntel", "14th Gen Intel(R) Core(TM) i9-14900K", 6, 183, 1, 32, 24, 3200000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) CPU E3-1230 v2 @ 3.30GHz", 6, 58, 9, 8, 4, 3300000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) CPU E3-1240 v3 @ 3.40GHz", 6, 60, 3, 8, 4, 3400000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) CPU E3-1270 v6 @ 3.80GHz", 6, 158, 9, 8, 4, 3800000000LL, true },
  { "AuthenticAMD", "AMD Athlon II X4 640 Processor", 16, 4, 3, 4, 4, 3000000000LL, false },
  { "AuthenticAMD", "AMD FX-6300 Six-Core Processor", 21, 2, 0, 6, 3, 3500000000LL, false },
  { "AuthenticAMD", "AMD FX-8350 Eight-Core Processor", 21, 2, 0, 8, 4, 4000000000LL, false },
  { "AuthenticAMD", "AMD A10-7850K Radeon R7, 12 Compute Cores 4C+8G", 21, 48, 1, 4, 4, 3700000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 1200 Quad-Core Processor", 23, 1, 1, 4, 4, 3100000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 2200G with Radeon Vega Graphics", 23, 17, 0, 4, 4, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 3100 4-Core Processor", 23, 113, 0, 8, 4, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 3300X 4-Core Processor", 23, 113, 0, 8, 4, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 4100 4-Core Processor", 25, 1, 0, 8, 4, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 4300G with Radeon Graphics", 25, 80, 0, 8, 4, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 5100 4-Core Processor", 25, 33, 0, 8, 4, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 5300G with Radeon Graphics", 25, 80, 0, 8, 4, 4000000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 7100 4-Core Processor", 25, 97, 2, 8, 4, 3900000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 8100 4-Core Processor", 25, 97, 2, 8, 4, 4000000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 1600 Six-Core Processor", 23, 1, 1, 12, 6, 3200000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 2600 Six-Core Processor", 23, 8, 2, 12, 6, 3400000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 3600 6-Core Processor", 23, 113, 0, 12, 6, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 5600 6-Core Processor", 25, 33, 0, 12, 6, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 5600X 6-Core Processor", 25, 33, 0, 12, 6, 3700000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 7600 6-Core Processor", 25, 97, 2, 12, 6, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 7600X 6-Core Processor", 25, 97, 2, 12, 6, 4700000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 8600G w/ Radeon 760M Graphics", 26, 20, 0, 12, 6, 4300000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 1700 Eight-Core Processor", 23, 1, 1, 16, 8, 3000000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 2700X Eight-Core Processor", 23, 8, 2, 16, 8, 3700000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 3700X 8-Core Processor", 23, 113, 0, 16, 8, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 3800X 8-Core Processor", 23, 113, 0, 16, 8, 3900000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 5700X 8-Core Processor", 25, 33, 0, 16, 8, 3400000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 5800X 8-Core Processor", 25, 33, 0, 16, 8, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 7700 8-Core Processor", 25, 97, 2, 16, 8, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 7700X 8-Core Processor", 25, 97, 2, 16, 8, 4500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 7800X3D 8-Core Processor", 25, 97, 2, 16, 8, 4200000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 3900X 12-Core Processor", 23, 113, 0, 24, 12, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 3950X 16-Core Processor", 23, 113, 0, 32, 16, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 5900X 12-Core Processor", 25, 33, 0, 24, 12, 3700000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 5950X 16-Core Processor", 25, 33, 0, 32, 16, 3400000000LL, true },
};

static constexpr catalog_cpu_variant catalog_extra_cpus[] = {
  { "GenuineIntel", "Intel(R) Celeron(R) CPU G3900 @ 2.80GHz", 6, 94, 3, 2, 2, 2800000000LL, true },
  { "GenuineIntel", "Intel(R) Celeron(R) CPU G3930 @ 2.90GHz", 6, 158, 9, 2, 2, 2900000000LL, true },
  { "GenuineIntel", "Intel(R) Celeron(R) Gold G5905 @ 3.50GHz", 6, 165, 3, 2, 2, 3500000000LL, true },
  { "GenuineIntel", "Intel(R) Celeron(R) G6900 @ 3.40GHz", 6, 151, 2, 2, 2, 3400000000LL, true },
  { "GenuineIntel", "11th Gen Intel(R) Pentium(R) Gold 7505 @ 2.00GHz", 6, 140, 1, 4, 2, 2000000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i3-6100U CPU @ 2.30GHz", 6, 78, 3, 4, 2, 2300000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i3-7100U CPU @ 2.40GHz", 6, 142, 9, 4, 2, 2400000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-6200U CPU @ 2.30GHz", 6, 78, 3, 4, 2, 2300000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-7200U CPU @ 2.50GHz", 6, 142, 9, 4, 2, 2500000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-8250U CPU @ 1.60GHz", 6, 142, 10, 8, 4, 1600000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-10210U CPU @ 1.60GHz", 6, 165, 3, 8, 4, 1600000000LL, true },
  { "GenuineIntel", "11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz", 6, 140, 1, 8, 4, 2400000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Core(TM) i5-1235U", 6, 154, 3, 12, 10, 1300000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i5-1335U", 6, 186, 2, 12, 10, 1300000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-6500U CPU @ 2.50GHz", 6, 78, 3, 4, 2, 2500000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-7500U CPU @ 2.70GHz", 6, 142, 9, 4, 2, 2700000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-8550U CPU @ 1.80GHz", 6, 142, 10, 8, 4, 1800000000LL, true },
  { "GenuineIntel", "10th Gen Intel(R) Core(TM) i7-1065G7 @ 1.30GHz", 6, 126, 5, 8, 4, 1300000000LL, true },
  { "GenuineIntel", "11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz", 6, 140, 1, 8, 4, 2800000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Core(TM) i7-1260P", 6, 154, 3, 16, 12, 2100000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i7-1360P", 6, 186, 2, 16, 12, 2200000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i7-13700H", 6, 183, 1, 20, 14, 2400000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Core(TM) i9-12900H", 6, 154, 3, 20, 14, 2500000000LL, true },
  { "GenuineIntel", "13th Gen Intel(R) Core(TM) i9-13900HX", 6, 183, 1, 32, 24, 2200000000LL, true },
  { "GenuineIntel", "12th Gen Intel(R) Xeon(R) E-2286M CPU @ 2.40GHz", 6, 165, 5, 16, 8, 2400000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) W-10855M CPU @ 2.80GHz", 6, 165, 5, 12, 6, 2800000000LL, true },
  { "GenuineIntel", "11th Gen Intel(R) Xeon(R) W-11955M @ 2.60GHz", 6, 167, 1, 16, 8, 2600000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) Gold 6130 CPU @ 2.10GHz", 6, 85, 4, 32, 16, 2100000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) Gold 6248 CPU @ 2.50GHz", 6, 85, 7, 40, 20, 2500000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) Platinum 8176 CPU @ 2.10GHz", 6, 85, 4, 112, 28, 2100000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) Platinum 8380 CPU @ 2.30GHz", 6, 106, 6, 80, 40, 2300000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) w5-2455X", 6, 143, 8, 24, 12, 3200000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) w7-2495X", 6, 143, 8, 36, 24, 2500000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-13500", 6, 183, 1, 20, 14, 2500000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-13700", 6, 183, 1, 24, 16, 2100000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i9-13900", 6, 183, 1, 32, 24, 2000000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) Ultra 5 125H", 6, 170, 4, 18, 14, 1200000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) Ultra 7 155H", 6, 170, 4, 22, 16, 1400000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) Ultra 9 185H", 6, 170, 4, 22, 16, 2300000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i5-14500", 6, 183, 1, 20, 14, 2600000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i7-14700", 6, 183, 1, 28, 20, 2100000000LL, true },
  { "GenuineIntel", "Intel(R) Core(TM) i9-14900", 6, 183, 1, 32, 24, 2000000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) E-2336 CPU @ 2.90GHz", 6, 167, 1, 12, 6, 2900000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) E-2486 CPU @ 2.50GHz", 6, 167, 1, 16, 8, 2500000000LL, true },
  { "GenuineIntel", "Intel(R) Atom(TM) CPU C3558 @ 2.20GHz", 6, 95, 1, 4, 4, 2200000000LL, true },
  { "GenuineIntel", "Intel(R) Atom(TM) CPU C3758 @ 2.20GHz", 6, 95, 1, 8, 8, 2200000000LL, true },
  { "GenuineIntel", "Intel(R) Atom(TM) x5-Z8350 CPU @ 1.44GHz", 6, 76, 4, 4, 4, 1440000000LL, true },
  { "GenuineIntel", "Intel(R) Celeron(R) CPU N5105 @ 2.00GHz", 6, 156, 0, 4, 4, 2000000000LL, true },
  { "GenuineIntel", "Intel(R) Pentium(R) Silver N6005 @ 2.00GHz", 6, 156, 0, 4, 4, 2000000000LL, true },
  { "GenuineIntel", "Intel(R) Xeon(R) D-2141I CPU @ 2.20GHz", 6, 85, 4, 16, 8, 2200000000LL, true },
  { "AuthenticAMD", "AMD Phenom(TM) II X4 965 Processor", 16, 4, 3, 4, 4, 3400000000LL, false },
  { "AuthenticAMD", "AMD Phenom(TM) II X6 1090T Processor", 16, 10, 0, 6, 6, 3200000000LL, false },
  { "AuthenticAMD", "AMD Athlon 200GE with Radeon Vega Graphics", 23, 17, 0, 4, 2, 3200000000LL, true },
  { "AuthenticAMD", "AMD Athlon 5150 APU with Radeon R3 Graphics", 22, 0, 1, 4, 4, 1600000000LL, false },
  { "AuthenticAMD", "AMD A6-7400K Radeon R5, 6 Compute Cores 2C+4G", 21, 48, 1, 2, 2, 3500000000LL, true },
  { "AuthenticAMD", "AMD A8-7600 Radeon R7, 10 Compute Cores 4C+6G", 21, 48, 1, 4, 4, 3100000000LL, true },
  { "AuthenticAMD", "AMD A12-9800 Radeon R7, 12 Compute Cores 4C+8G", 21, 101, 1, 4, 4, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 1300X Quad-Core Processor", 23, 1, 1, 4, 4, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 2300X Quad-Core Processor", 23, 8, 2, 4, 4, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 3200G with Radeon Vega Graphics", 23, 24, 1, 4, 4, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 4350G with Radeon Graphics", 25, 80, 0, 8, 4, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 3 8300G with Radeon 740M Graphics", 26, 20, 0, 8, 4, 3400000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 1400 Quad-Core Processor", 23, 1, 1, 8, 4, 3200000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 1500X Quad-Core Processor", 23, 1, 1, 8, 4, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 2400G with Radeon Vega Graphics", 23, 17, 0, 8, 4, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 2500X Quad-Core Processor", 23, 8, 2, 8, 4, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 3500 6-Core Processor", 23, 113, 0, 6, 6, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 3500X 6-Core Processor", 23, 113, 0, 6, 6, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 4500 6-Core Processor", 25, 1, 0, 12, 6, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 5500 6-Core Processor", 25, 33, 0, 12, 6, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 7500F 6-Core Processor", 25, 97, 2, 12, 6, 3700000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 8500G w/ Radeon 740M Graphics", 26, 20, 0, 12, 6, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 PRO 4650G with Radeon Graphics", 23, 113, 0, 12, 6, 3700000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 5 PRO 5650G with Radeon Graphics", 25, 33, 0, 12, 6, 3900000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 2700 Eight-Core Processor", 23, 8, 2, 16, 8, 3200000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 3700C 8-Core Processor", 23, 113, 0, 16, 8, 4000000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 4700G with Radeon Graphics", 25, 80, 0, 16, 8, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 5700G with Radeon Graphics", 25, 33, 0, 16, 8, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 5700X3D 8-Core Processor", 25, 33, 0, 16, 8, 3000000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 8700F 8-Core Processor", 26, 20, 0, 16, 8, 4100000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 PRO 4750G with Radeon Graphics", 25, 80, 0, 16, 8, 3600000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 7 PRO 5750G with Radeon Graphics", 25, 33, 0, 16, 8, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 3900 12-Core Processor", 23, 113, 0, 24, 12, 3100000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 4900HS with Radeon Graphics", 23, 96, 1, 16, 8, 3000000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 5900HS with Radeon Graphics", 25, 33, 0, 16, 8, 3000000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 6900HX with Radeon Graphics", 25, 68, 1, 16, 8, 3300000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 7940HS", 25, 97, 2, 16, 8, 4000000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 7945HX 16-Core Processor", 25, 97, 2, 32, 16, 2500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 9900X 12-Core Processor", 26, 20, 0, 24, 12, 4400000000LL, true },
  { "AuthenticAMD", "AMD Ryzen 9 9950X 16-Core Processor", 26, 20, 0, 32, 16, 4300000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper 1920X 12-Core Processor", 23, 1, 1, 24, 12, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper 2920X 12-Core Processor", 23, 8, 2, 24, 12, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper 2950X 16-Core Processor", 23, 8, 2, 32, 16, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper 3960X 24-Core Processor", 23, 113, 0, 48, 24, 3800000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper 3970X 32-Core Processor", 23, 113, 0, 64, 32, 3700000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper 3990X 64-Core Processor", 23, 113, 0, 128, 64, 2900000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper PRO 3955WX 16-Cores", 23, 113, 0, 32, 16, 3900000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper PRO 3975WX 32-Cores", 23, 113, 0, 64, 32, 3500000000LL, true },
  { "AuthenticAMD", "AMD Ryzen Threadripper PRO 5995WX 64-Cores", 25, 33, 0, 128, 64, 2500000000LL, true },
  { "AuthenticAMD", "AMD EPYC 7402P 24-Core Processor", 23, 49, 0, 48, 24, 2800000000LL, true },
  { "AuthenticAMD", "AMD EPYC 7763 64-Core Processor", 25, 1, 1, 128, 64, 2450000000LL, true },
};

static constexpr catalog_gpu_variant catalog_gpus[] = {
  { "NVIDIA Corporation", "NVIDIA GeForce GT 1030/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1d01, 535, 113, 110 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 750 Ti/PCIe/SSE2", "4.6.0 NVIDIA 470.239.06", "470.239.06", 0x10de, 0x1380, 470, 239, 110 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 760/PCIe/SSE2", "4.6.0 NVIDIA 470.239.06", "470.239.06", 0x10de, 0x1187, 470, 239, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 970/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x13c2, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 980/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x13c0, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1060 6GB/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1c03, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1070/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1b81, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1080/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1b80, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1080 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1b06, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1650/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1f82, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1660 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2182, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 2060/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1f08, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 2070 SUPER/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1e84, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 2080 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1e07, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2504, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3060 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2486, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3070/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2484, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3080/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2206, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3090/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2204, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4060/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2882, 550, 54, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4070/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2786, 550, 54, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4080/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2704, 550, 54, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4090/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2684, 550, 54, 121 },
  { "AMD", "AMD Radeon RX 550 Series (POLARIS12, DRM 3.54.0)", "4.6.0 AMD Radeon RX 550", "23.0.0", 0x1002, 0x699f, 23, 0, 110 },
  { "AMD", "AMD Radeon RX 560 Series (POLARIS11, DRM 3.54.0)", "4.6.0 AMD Radeon RX 560", "23.0.0", 0x1002, 0x67ff, 23, 0, 111 },
  { "AMD", "AMD Radeon RX 570 Series (POLARIS10, DRM 3.54.0)", "4.6.0 AMD Radeon RX 570", "23.0.0", 0x1002, 0x67df, 23, 0, 111 },
  { "AMD", "AMD Radeon RX 580 Series (POLARIS10, DRM 3.54.0)", "4.6.0 AMD Radeon RX 580", "23.0.0", 0x1002, 0x67df, 23, 0, 111 },
  { "AMD", "AMD Radeon RX Vega 56 (VEGA10, DRM 3.54.0)", "4.6.0 AMD Radeon RX Vega 56", "23.0.0", 0x1002, 0x69af, 23, 0, 121 },
  { "AMD", "AMD Radeon RX Vega 64 (VEGA10, DRM 3.54.0)", "4.6.0 AMD Radeon RX Vega 64", "23.0.0", 0x1002, 0x687f, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 5500 XT (NAVI14, DRM 3.54.0)", "4.6.0 AMD Radeon RX 5500 XT", "23.0.0", 0x1002, 0x7340, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 5600 XT (NAVI10, DRM 3.54.0)", "4.6.0 AMD Radeon RX 5600 XT", "23.0.0", 0x1002, 0x731f, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 5700 XT (NAVI10, DRM 3.54.0)", "4.6.0 AMD Radeon RX 5700 XT", "23.0.0", 0x1002, 0x731f, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 6600 (NAVI23, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6600", "23.0.0", 0x1002, 0x73ff, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 6700 XT (NAVI22, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6700 XT", "23.0.0", 0x1002, 0x73df, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 6800 XT (SIENNA_CICHLID, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6800 XT", "23.0.0", 0x1002, 0x73bf, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 6900 XT (SIENNA_CICHLID, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6900 XT", "23.0.0", 0x1002, 0x73bf, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 7600 (NAVI33, DRM 3.54.0)", "4.6.0 AMD Radeon RX 7600", "23.0.0", 0x1002, 0x7480, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 7700 XT (NAVI32, DRM 3.54.0)", "4.6.0 AMD Radeon RX 7700 XT", "23.0.0", 0x1002, 0x747e, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 7800 XT (NAVI32, DRM 3.54.0)", "4.6.0 AMD Radeon RX 7800 XT", "23.0.0", 0x1002, 0x747e, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 7900 XT (NAVI31, DRM 3.54.0)", "4.6.0 AMD Radeon RX 7900 XT", "23.0.0", 0x1002, 0x744c, 23, 0, 121 },
  { "Intel", "Mesa Intel(R) UHD Graphics 630 (CFL GT2)", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x3e92, 24, 0, 110 },
  { "Intel", "Mesa Intel(R) UHD Graphics 730 (RKL GT1)", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x4c8a, 24, 0, 110 },
  { "Intel", "Mesa Intel(R) UHD Graphics 770 (ADL GT1)", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x4680, 24, 0, 121 },
  { "Intel", "Intel(R) Arc(TM) A750 Graphics", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x56a1, 24, 0, 121 },
  { "Intel", "Intel(R) Arc(TM) A770 Graphics", "4.6 (Core Profile) Mesa 24.0.0", "intel 24.0.0", 0x8086, 0x56a0, 24, 0, 121 },
};

static constexpr catalog_gpu_variant catalog_extra_gpus[] = {
  { "NVIDIA Corporation", "NVIDIA GeForce GTX  Titan/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x100c, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 780/PCIe/SSE2", "4.6.0 NVIDIA 470.239.06", "470.239.06", 0x10de, 0x1004, 470, 239, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 780 Ti/PCIe/SSE2", "4.6.0 NVIDIA 470.239.06", "470.239.06", 0x10de, 0x100a, 470, 239, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 980 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x17c2, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1050/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1c81, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1060 3GB/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1c02, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1070 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1b82, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1660/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2184, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce GTX 1660 SUPER/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x21c4, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 2060 SUPER/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1f06, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 2070/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1e84, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 2080 SUPER/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x1e81, 535, 113, 111 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3050/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2582, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3070 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2482, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3080 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2208, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 3090 Ti/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2203, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4060 Ti/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2803, 550, 54, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4070 SUPER/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2783, 550, 54, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4070 Ti/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2782, 550, 54, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4070 Ti SUPER/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2705, 550, 54, 121 },
  { "NVIDIA Corporation", "NVIDIA GeForce RTX 4080 SUPER/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14", "550.54.14", 0x10de, 0x2705, 550, 54, 121 },
  { "NVIDIA Corporation", "NVIDIA RTX A2000/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x25b6, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA RTX A4000/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x24b0, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA RTX A5000/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2231, 535, 113, 121 },
  { "NVIDIA Corporation", "NVIDIA RTX A6000/PCIe/SSE2", "4.6.0 NVIDIA 535.113.01", "535.113.01", 0x10de, 0x2230, 535, 113, 121 },
  { "AMD", "AMD Radeon Pro WX 4100 (POLARIS11, DRM 3.54.0)", "4.6.0 AMD Radeon Pro WX 4100", "23.0.0", 0x1002, 0x67e3, 23, 0, 121 },
  { "AMD", "AMD Radeon Pro WX 5100 (POLARIS10, DRM 3.54.0)", "4.6.0 AMD Radeon Pro WX 5100", "23.0.0", 0x1002, 0x6980, 23, 0, 121 },
  { "AMD", "AMD Radeon Pro WX 7100 (POLARIS10, DRM 3.54.0)", "4.6.0 AMD Radeon Pro WX 7100", "23.0.0", 0x1002, 0x6983, 23, 0, 121 },
  { "AMD", "AMD Radeon Pro W5500 (NAVI14, DRM 3.54.0)", "4.6.0 AMD Radeon Pro W5500", "23.0.0", 0x1002, 0x7341, 23, 0, 121 },
  { "AMD", "AMD Radeon Pro W6800 (NAVI21, DRM 3.54.0)", "4.6.0 AMD Radeon Pro W6800", "23.0.0", 0x1002, 0x73bf, 23, 0, 121 },
  { "AMD", "AMD Radeon Pro W7900 (NAVI31, DRM 3.54.0)", "4.6.0 AMD Radeon Pro W7900", "23.0.0", 0x1002, 0x744c, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 6400 (NAVI24, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6400", "23.0.0", 0x1002, 0x743f, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 6500 XT (NAVI24, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6500 XT", "23.0.0", 0x1002, 0x743f, 23, 0, 121 },
  { "AMD", "AMD Radeon RX 6750 XT (NAVI22, DRM 3.54.0)", "4.6.0 AMD Radeon RX 6750 XT", "23.0.0", 0x1002, 0x73df, 23, 0, 121 },
};

struct catalog_board
{
  const char* vendor;
  const char* name;
};

struct catalog_disk
{
  const char* vendor;
  const char* model;
};

static constexpr catalog_board catalog_boards[] = {
  { "ASUSTeK COMPUTER INC.", "PRIME B650-PLUS" },
  { "Gigabyte Technology Co., Ltd.", "B650 AORUS ELITE AX" },
  { "MSI", "MAG B550 TOMAHAWK" },
  { "ASRock", "B650E PG Riptide WiFi" },
  { "Dell Inc.", "0Y2MRG" },
  { "LENOVO", "30E0" },
  { "Micro-Star International Co., Ltd.", "PRO Z690-A WIFI" },
  { "Supermicro", "X11SCA" },
  { "ASUSTeK COMPUTER INC.", "ROG STRIX Z790-E GAMING WIFI" },
  { "Gigabyte Technology Co., Ltd.", "X570 AORUS MASTER" },
  { "ASUSTeK COMPUTER INC.", "P8Z77-V PRO" },
  { "Gigabyte Technology Co., Ltd.", "GA-Z97X-UD5H" },
  { "MSI", "Z170A GAMING M5" },
  { "ASRock", "Z370 Taichi" },
  { "Dell Inc.", "0H0YJ" },
  { "Hewlett-Packard", "8801" },
  { "LENOVO", "3190" },
  { "ASUSTeK COMPUTER INC.", "ROG STRIX X570-E GAMING" },
  { "MSI", "MPG X570 GAMING PLUS" },
  { "Gigabyte Technology Co., Ltd.", "B550 AORUS PRO AC" },
  { "ASRock", "X470 Master SLI" },
  { "Supermicro", "H12SSL-i" },
  { "Tyan", "S8030" },
  { "Dell Inc.", "PowerEdge R740" },
  { "Hewlett-Packard", "ProLiant DL380 Gen10" },
  { "ASUSTeK COMPUTER INC.", "WS C621E SAGE" },
  { "Gigabyte Technology Co., Ltd.", "MW51-HP0" },
  { "Intel Corporation", "S2600WF" },
  { "MSI", "MEG Z690 ACE" },
  { "ASRock", "Z690 Steel Legend" },
  { "ASUSTeK COMPUTER INC.", "TUF GAMING Z790-PLUS WIFI" },
  { "Gigabyte Technology Co., Ltd.", "Z790 GAMING X AX" },
  { "MSI", "MAG B760M MORTAR WIFI" },
  { "ASRock", "B760 Pro RS" },
  { "Dell Inc.", "Precision 5820 Tower" },
  { "LENOVO", "ThinkStation P520" },
  { "Hewlett-Packard", "Z4 G4 Workstation" },
  { "Supermicro", "X12SPA-TF" },
  { "ASUSTeK COMPUTER INC.", "Pro WS WRX80E-SAGE SE WIFI" },
  { "Gigabyte Technology Co., Ltd.", "TRX40 AORUS MASTER" },
  { "MSI", "Creator TRX40" },
  { "ASRock", "TRX40 Creator" },
  { "Dell Inc.", "PowerEdge R7525" },
  { "Supermicro", "H12DSi" },
  { "Tyan", "Tomcat SX TS65-B8253" },
  { "ASUSTeK COMPUTER INC.", "ROG MAXIMUS XIII HERO" },
  { "Gigabyte Technology Co., Ltd.", "Z590 AORUS MASTER" },
  { "MSI", "MEG Z590 ACE" },
  { "ASRock", "Z590 Taichi" },
  { "Dell Inc.", "OptiPlex 7090" },
  { "LENOVO", "ThinkCentre M90q" },
  { "Hewlett-Packard", "EliteDesk 800 G6" },
  { "Intel Corporation", "NUC11TN" },
  { "ASUSTeK COMPUTER INC.", "ROG STRIX B660-A GAMING WIFI" },
  { "Gigabyte Technology Co., Ltd.", "B660 AORUS MASTER DDR4" },
  { "MSI", "MAG B660 TOMAHAWK WIFI" },
  { "ASRock", "B660 Steel Legend" },
  { "Dell Inc.", "XPS 8950" },
  { "LENOVO", "Legion T5 26IAB7" },
};

static constexpr catalog_disk catalog_disks[] = {
  { "Samsung", "SSD 970 EVO Plus 1TB" },
  { "Western Digital", "WD_BLACK SN850X 2TB" },
  { "Kingston", "SKC3000S1024G" },
  { "Crucial", "CT2000P5PSSD8" },
  { "Solidigm", "P44 Pro 2TB" },
  { "Seagate", "FireCuda 530 1TB" },
  { "Intel", "SSDPEKKW512G8" },
  { "Sabrent", "Rocket 4 Plus 2TB" },
  { "KIOXIA", "EXCERIA G2 SSD 1TB" },
  { "SK hynix", "Platinum P41 1TB" },
  { "Samsung", "SSD 850 EVO 500GB" },
  { "Samsung", "SSD 860 EVO 1TB" },
  { "Samsung", "SSD 980 PRO 2TB" },
  { "Western Digital", "WD Blue SN570 1TB" },
  { "Western Digital", "WD Blue SN580 1TB" },
  { "Western Digital", "WD_BLACK SN770 2TB" },
  { "Kingston", "KC3000 2048GB" },
  { "Kingston", "NV2 1TB" },
  { "Crucial", "CT1000P1SSD8" },
  { "Crucial", "CT1000MX500SSD1" },
  { "Seagate", "ST1000DM010" },
  { "Seagate", "ST2000DM008-2FR102" },
  { "Seagate", "BarraCuda Q1 960GB" },
  { "TOSHIBA", "DT01ACA200" },
  { "HGST", "HUS724020ALA640" },
  { "Intel", "SSD 660p Series 1TB" },
  { "Intel", "SSD D3-S4510 1.92TB" },
  { "Sabrent", "Rocket 4 Plus 1TB" },
  { "KIOXIA", "EXCERIA PLUS G3 2TB" },
  { "SK hynix", "Gold P31 1TB" },
  { "Micron", "2200S NVMe 1TB" },
  { "ADATA", "XPG SX8200 Pro 1TB" },
  { "Corsair", "Force MP600 1TB" },
  { "Patriot", "Viper VP4300 1TB" },
  { "Solidigm", "P41 Plus 1TB" },
};

hardware_profile make_catalog_profile(const catalog_cpu_variant& cpu,
  const catalog_gpu_variant& gpu, std::size_t index)
{
  const bool amd = std::strcmp(cpu.vendor, "AuthenticAMD") == 0;
  static constexpr char intel_flags[] =
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3 sse4_1 sse4_2 avx avx2";
  static constexpr char intel_legacy_flags[] =
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 ss ht sse3 ssse3";
  static constexpr char amd_flags[] =
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2";
  static constexpr char amd_legacy_flags[] =
    "fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge cmov pat pse36 clflush mmx fxsr sse sse2 sse3 ssse3 sse4a sse4_1 sse4_2";
  hardware_profile result = hardware_profiles[amd ? 2 : 0];
  result.cpu_vendor = cpu.vendor;
  result.cpu_brand = cpu.brand;
  result.cpu_family = cpu.family;
  result.cpu_model = cpu.model;
  result.cpu_stepping = cpu.stepping;
  result.logical_processors = cpu.logical_processors;
  result.physical_processors = cpu.physical_processors;
  result.cpu_speed = cpu.speed;
  result.cpu_flags = amd
    ? (cpu.avx ? amd_flags : amd_legacy_flags)
    : (cpu.avx ? intel_flags : intel_legacy_flags);
  result.cpu_features[0] = 0x178bfbffU;
  result.cpu_features[1] = amd
    ? (cpu.avx ? 0x7ed8320bU : 0x2fd3fbffU)
    : (cpu.avx ? 0x7ffafbffU : 0x1fd3fbffU);
  result.cpu_features[2] = cpu.avx ? 0x001c2fb9U : 0x00000000U;
  result.hyper_threading = cpu.logical_processors > cpu.physical_processors;
  result.dnow = amd && (cpu.family == 16 || cpu.family == 21);
  result.avx = cpu.avx;
  result.sse4a = amd && cpu.family >= 21;
  result.gpu_vendor = gpu.vendor;
  result.gpu_renderer = gpu.renderer;
  result.gpu_version = gpu.version;
  result.gpu_driver = gpu.driver;
  result.gpu_vendor_id = gpu.vendor_id;
  result.gpu_device_id = gpu.device_id;
  result.gpu_driver_major = gpu.driver_major;
  result.gpu_driver_minor = gpu.driver_minor;
  result.dx_level = gpu.dx_level;
  constexpr std::size_t board_count = sizeof(catalog_boards) / sizeof(catalog_boards[0]);
  constexpr std::size_t disk_count = sizeof(catalog_disks) / sizeof(catalog_disks[0]);
  result.board_vendor = catalog_boards[index % board_count].vendor;
  result.board_name = catalog_boards[index % board_count].name;
  result.disk_vendor = catalog_disks[(index / board_count) % disk_count].vendor;
  result.disk_model = catalog_disks[(index / board_count) % disk_count].model;
  return result;
}

constexpr std::size_t hardware_catalog_size = 38 + 4500 + 3434;

const std::array<hardware_profile, hardware_catalog_size>& hardware_catalog()
{
  static const auto catalog = []
  {
    std::array<hardware_profile, hardware_catalog_size> result{};
    constexpr std::size_t base_profile_count = sizeof(hardware_profiles) / sizeof(hardware_profiles[0]);
    constexpr std::size_t cpu_count = sizeof(catalog_cpus) / sizeof(catalog_cpus[0]);
    constexpr std::size_t gpu_source_count = sizeof(catalog_gpus) / sizeof(catalog_gpus[0]);
    constexpr std::size_t gpu_selection[] = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
      23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
      40, 41, 42, 43, 44
    };
    constexpr std::size_t gpu_count = sizeof(gpu_selection) / sizeof(gpu_selection[0]);
    constexpr std::size_t extra_cpu_count = sizeof(catalog_extra_cpus) / sizeof(catalog_extra_cpus[0]);
    constexpr std::size_t extra_gpu_count = sizeof(catalog_extra_gpus) / sizeof(catalog_extra_gpus[0]);
    constexpr std::size_t board_count = sizeof(catalog_boards) / sizeof(catalog_boards[0]);
    constexpr std::size_t disk_count = sizeof(catalog_disks) / sizeof(catalog_disks[0]);
    static_assert(base_profile_count == 38);
    static_assert(cpu_count == 100);
    static_assert(gpu_source_count == 45);
    static_assert(gpu_count == 30);
    static_assert(cpu_count * gpu_count == 3000);
    static_assert(extra_cpu_count == 101);
    static_assert(extra_gpu_count == 34);
    static_assert(board_count == 59);
    static_assert(disk_count == 35);
    static_assert(hardware_catalog_size == base_profile_count + cpu_count * gpu_source_count
      + extra_cpu_count * extra_gpu_count);

    for (std::size_t index = 0; index < base_profile_count; ++index)
    {
      result[index] = hardware_profiles[index];
    }
    std::size_t output_index = base_profile_count;
    for (const auto& cpu : catalog_cpus)
    {
      for (const auto& gpu : catalog_gpus)
      {
        result[output_index] = make_catalog_profile(cpu, gpu, output_index - base_profile_count);
        ++output_index;
      }
    }
    for (const auto& cpu : catalog_extra_cpus)
    {
      for (const auto& gpu : catalog_extra_gpus)
      {
        result[output_index] = make_catalog_profile(cpu, gpu, output_index - base_profile_count);
        ++output_index;
      }
    }
    return result;
  }();
  return catalog;
}

const hardware_profile& selected_profile()
{
  static const hardware_profile& profile = hardware_catalog()[
    hash64(seed() + ":hardware-profile", 0x50524f46494c4553ULL)
      % hardware_catalog_size];
  return profile;
}

bool has_prefix(const std::string& value, const char* prefix)
{
  const size_t length = std::strlen(prefix);
  return value.size() >= length && value.compare(0, length, prefix) == 0;
}

bool recognized_path(const std::string& path)
{
  return path == "/etc/machine-id" || path == "/var/lib/dbus/machine-id"
    || (path.size() >= 18 && path.compare(path.size() - 18, 18, "/.steam/machine-id") == 0)
    || path == "/proc/cpuinfo" || has_prefix(path, "/sys/class/net/")
    || has_prefix(path, "/sys/devices/virtual/net/")
    || has_prefix(path, "/sys/class/dmi/id/") || has_prefix(path, "/sys/devices/virtual/dmi/id/")
    || has_prefix(path, "/sys/block/") || has_prefix(path, "/sys/class/block/");
}

std::string synthetic_contents(const std::string& path)
{
  if (path == "/etc/machine-id" || path == "/var/lib/dbus/machine-id"
    || (path.size() >= 18 && path.compare(path.size() - 18, 18, "/.steam/machine-id") == 0))
  {
    return identity128("machine-id") + "\n";
  }

  if (path == "/proc/cpuinfo")
  {
    const auto& profile = selected_profile();
    std::string result;
    for (unsigned int processor = 0; processor < profile.logical_processors; ++processor)
    {
      result += "processor\t: " + std::to_string(processor) + "\nmodel name\t: ";
      result += profile.cpu_brand;
      result += "\ncpu family\t: " + std::to_string(profile.cpu_family);
      result += "\nmodel\t\t: " + std::to_string(profile.cpu_model);
      result += "\nstepping\t: " + std::to_string(profile.cpu_stepping);
      result += "\nvendor_id\t: ";
      result += profile.cpu_vendor;
      result += "\nflags\t\t: ";
      result += profile.cpu_flags;
      result += "\n\n";
    }
    return result;
  }

  if (has_prefix(path, "/sys/class/net/") || has_prefix(path, "/sys/devices/virtual/net/")
    || has_prefix(path, "/sys/class/dmi/id/")
    || has_prefix(path, "/sys/devices/virtual/dmi/id/"))
  {
    if (path.find("/address") != std::string::npos)
    {
      const size_t start = path.find("/sys/class/net/") == 0
        ? std::strlen("/sys/class/net/") : std::strlen("/sys/devices/virtual/net/");
      const size_t end = path.find('/', start);
      const std::string interface_name = path.substr(start, end - start);
      return synthetic_mac(interface_name) + "\n";
    }
    if (path.find("serial_number") != std::string::npos || path.find("serial") != std::string::npos)
    {
      return identity128("serial:" + path) + "\n";
    }
    if (path.find("uuid") != std::string::npos)
    {
      const std::string value = identity128("uuid");
      return value.substr(0, 8) + "-" + value.substr(8, 4) + "-" + value.substr(12, 4)
        + "-" + value.substr(16, 4) + "-" + value.substr(20, 12) + "\n";
    }
    if (path.find("manufacturer") != std::string::npos || path.find("vendor") != std::string::npos
      || path.find("sys_vendor") != std::string::npos || path.find("bios_vendor") != std::string::npos)
    {
      return std::string(selected_profile().board_vendor) + "\n";
    }
    if (path.find("product_name") != std::string::npos || path.find("board_name") != std::string::npos
      || path.find("model") != std::string::npos)
    {
      return std::string(selected_profile().board_name) + "\n";
    }
    if (path.find("bios_version") != std::string::npos)
    {
      return "1.0\n";
    }
    if (path.find("bios_date") != std::string::npos)
    {
      return "01/01/2020\n";
    }
  }

  if (has_prefix(path, "/sys/block/") || has_prefix(path, "/sys/class/block/"))
  {
    if (path.find("/serial") != std::string::npos)
    {
      return identity128("disk-serial:" + path) + "\n";
    }
    if (path.find("/manufacturer") != std::string::npos)
    {
      return std::string(selected_profile().disk_vendor) + "\n";
    }
    if (path.find("/model") != std::string::npos)
    {
      return std::string(selected_profile().disk_model) + "\n";
    }
  }

  return {};
}

int make_synthetic_fd(const std::string& contents)
{
  const int file_descriptor = static_cast<int>(syscall(SYS_memfd_create, "cat-steam-hardware", MFD_CLOEXEC));
  if (file_descriptor < 0)
  {
    return -1;
  }
  const char* data = contents.data();
  size_t remaining = contents.size();
  while (remaining != 0)
  {
    const ssize_t written = syscall(SYS_write, file_descriptor, data, remaining);
    if (written <= 0)
    {
      syscall(SYS_close, file_descriptor);
      return -1;
    }
    data += written;
    remaining -= static_cast<size_t>(written);
  }
  syscall(SYS_lseek, file_descriptor, 0, SEEK_SET);
  return file_descriptor;
}

void track_file(int file_descriptor, std::string contents)
{
  std::lock_guard lock(tracked_files_mutex);
  tracked_files[file_descriptor] = { std::move(contents), 0 };
}

bool tracked_contents(int file_descriptor, tracked_file& result)
{
  std::lock_guard lock(tracked_files_mutex);
  const auto iterator = tracked_files.find(file_descriptor);
  if (iterator == tracked_files.end())
  {
    return false;
  }
  result = iterator->second;
  return true;
}

bool synthetic_enabled()
{
  return is_enabled() && env_flag("CAT_STEAM_TXTMODE_SYNTHETIC_HARDWARE", false) && target_process();
}

template <typename function_type>
function_type original_or_null(const char* name)
{
  static_assert(sizeof(function_type) == sizeof(void*));
  return next_symbol<function_type>(name);
}

}

bool should_synthesize_hardware()
{
  return synthetic_enabled();
}

int synthetic_open(const char* path, int flags, bool has_mode, unsigned int mode)
{
  using namespace std;
  const auto original = original_or_null<open_function>("open");
  if (original == nullptr || inside_interposer || !synthetic_enabled() || path == nullptr)
  {
    if (original == nullptr) return -1;
    if (has_mode) return original(path, flags, mode);
    return original(path, flags);
  }

  const string requested(path);
  const string contents = recognized_path(requested) ? synthetic_contents(requested) : string{};
  const bool synthesize = !contents.empty();
  inside_interposer = true;
  int file_descriptor = has_mode ? original(path, flags, mode) : original(path, flags);
  inside_interposer = false;
  if (synthesize && file_descriptor >= 0)
  {
    track_file(file_descriptor, contents);
  }
  else if (synthesize && file_descriptor < 0 && (flags & O_DIRECTORY) == 0)
  {
    file_descriptor = make_synthetic_fd(contents);
    if (file_descriptor >= 0) track_file(file_descriptor, contents);
  }
  return file_descriptor;
}

int synthetic_openat(int directory_fd, const char* path, int flags, bool has_mode, unsigned int mode)
{
  const auto original = original_or_null<openat_function>("openat");
  if (original == nullptr || inside_interposer || !synthetic_enabled() || path == nullptr || path[0] != '/')
  {
    if (original == nullptr) return -1;
    if (has_mode) return original(directory_fd, path, flags, mode);
    return original(directory_fd, path, flags);
  }
  const std::string requested(path);
  const std::string contents = recognized_path(requested) ? synthetic_contents(requested) : std::string{};
  inside_interposer = true;
  int file_descriptor = has_mode ? original(directory_fd, path, flags, mode) : original(directory_fd, path, flags);
  inside_interposer = false;
  if (!contents.empty() && file_descriptor >= 0) track_file(file_descriptor, contents);
  else if (!contents.empty() && file_descriptor < 0 && (flags & O_DIRECTORY) == 0)
  {
    file_descriptor = make_synthetic_fd(contents);
    if (file_descriptor >= 0) track_file(file_descriptor, contents);
  }
  return file_descriptor;
}

FILE* synthetic_fopen(const char* path, const char* mode)
{
  using function_type = FILE* (*)(const char*, const char*);
  const auto original = original_or_null<function_type>("fopen");
  if (!synthetic_enabled() || path == nullptr || mode == nullptr
    || !recognized_path(path) || synthetic_contents(path).empty())
    return original == nullptr ? nullptr : original(path, mode);

  const int file_descriptor = make_synthetic_fd(synthetic_contents(path));
  if (file_descriptor < 0) return nullptr;
  using fdopen_function = FILE* (*)(int, const char*);
  const auto wrap_fdopen = original_or_null<fdopen_function>("fdopen");
  FILE* stream = wrap_fdopen == nullptr ? nullptr : wrap_fdopen(file_descriptor, mode);
  if (stream == nullptr)
  {
    syscall(SYS_close, file_descriptor);
    return nullptr;
  }
  track_file(file_descriptor, synthetic_contents(path));
  return stream;
}

int synthetic_fclose(FILE* stream)
{
  using function_type = int (*)(FILE*);
  const auto original = original_or_null<function_type>("fclose");
  if (original == nullptr) return EOF;
  using fileno_function = int (*)(FILE*);
  const auto get_fileno = original_or_null<fileno_function>("fileno");
  const int file_descriptor = get_fileno == nullptr ? -1 : get_fileno(stream);
  const int result = original(stream);
  if (file_descriptor >= 0)
  {
    std::lock_guard lock(tracked_files_mutex);
    tracked_files.erase(file_descriptor);
  }
  return result;
}

ssize_t synthetic_read(int file_descriptor, void* buffer, size_t count)
{
  const auto original = original_or_null<read_function>("read");
  tracked_file file;
  if (!synthetic_enabled() || !tracked_contents(file_descriptor, file))
  {
    return original == nullptr ? -1 : original(file_descriptor, buffer, count);
  }
  if (file.offset >= static_cast<off_t>(file.contents.size())) return 0;
  const size_t available = file.contents.size() - static_cast<size_t>(file.offset);
  const size_t amount = count < available ? count : available;
  std::memcpy(buffer, file.contents.data() + file.offset, amount);
  std::lock_guard lock(tracked_files_mutex);
  auto iterator = tracked_files.find(file_descriptor);
  if (iterator != tracked_files.end()) iterator->second.offset += static_cast<off_t>(amount);
  return static_cast<ssize_t>(amount);
}

ssize_t synthetic_pread(int file_descriptor, void* buffer, size_t count, std::int64_t offset)
{
  const auto original = original_or_null<pread_function>("pread64");
  tracked_file file;
  if (!synthetic_enabled() || !tracked_contents(file_descriptor, file))
  {
    return original == nullptr ? -1 : original(file_descriptor, buffer, count, static_cast<off_t>(offset));
  }
  if (offset < 0 || offset >= static_cast<std::int64_t>(file.contents.size())) return 0;
  const size_t available = file.contents.size() - static_cast<size_t>(offset);
  const size_t amount = count < available ? count : available;
  std::memcpy(buffer, file.contents.data() + offset, amount);
  return static_cast<ssize_t>(amount);
}

int synthetic_close(int file_descriptor)
{
  const auto original = original_or_null<close_function>("close");
  {
    std::lock_guard lock(tracked_files_mutex);
    tracked_files.erase(file_descriptor);
  }
  return original == nullptr ? -1 : original(file_descriptor);
}

std::int64_t synthetic_lseek(int file_descriptor, std::int64_t offset, int whence)
{
  const auto original = original_or_null<lseek_function>("lseek");
  tracked_file file;
  if (!synthetic_enabled() || !tracked_contents(file_descriptor, file))
  {
    return original == nullptr ? -1 : original(file_descriptor, static_cast<off_t>(offset), whence);
  }
  std::int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? file.offset
    : whence == SEEK_END ? static_cast<std::int64_t>(file.contents.size()) : -1;
  if (base < 0 || base + offset < 0) { errno = EINVAL; return -1; }
  std::lock_guard lock(tracked_files_mutex);
  auto iterator = tracked_files.find(file_descriptor);
  if (iterator != tracked_files.end()) iterator->second.offset = static_cast<off_t>(base + offset);
  return base + offset;
}

std::int64_t synthetic_lseek64(int file_descriptor, std::int64_t offset, int whence)
{
  return synthetic_lseek(file_descriptor, offset, whence);
}

int synthetic_ioctl(int file_descriptor, unsigned long request, void* argument)
{
  const auto original = original_or_null<ioctl_function>("ioctl");
  if (synthetic_enabled() && request == SIOCGIFHWADDR && argument != nullptr)
  {
    auto* request_data = static_cast<ifreq*>(argument);
    const std::string mac = synthetic_mac(request_data->ifr_name);
    unsigned int bytes[6]{};
    if (std::sscanf(mac.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
      &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6)
    {
      request_data->ifr_hwaddr.sa_family = ARPHRD_ETHER;
      for (size_t index = 0; index < 6; ++index)
        request_data->ifr_hwaddr.sa_data[index] = static_cast<char>(bytes[index]);
      return 0;
    }
  }
  if (original == nullptr) return -1;
  return original(file_descriptor, request, argument);
}

int synthetic_uname(void* name)
{
  const auto original = original_or_null<uname_function>("uname");
  if (original == nullptr || name == nullptr) return -1;
  const int result = original(static_cast<utsname*>(name));
  if (result == 0 && synthetic_enabled())
  {
    auto* value = static_cast<utsname*>(name);
    std::strncpy(value->nodename, "generic-host", sizeof(value->nodename) - 1);
    std::strncpy(value->machine, "x86_64", sizeof(value->machine) - 1);
  }
  return result;
}

int synthetic_getifaddrs(void* ifaddrs)
{
  const auto original = original_or_null<getifaddrs_function>("getifaddrs");
  if (original == nullptr) return -1;
  const int result = original(static_cast<struct ifaddrs**>(ifaddrs));
  if (result != 0 || !synthetic_enabled() || ifaddrs == nullptr) return result;
  for (struct ifaddrs* entry = *static_cast<struct ifaddrs**>(ifaddrs); entry != nullptr; entry = entry->ifa_next)
  {
    if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_PACKET) continue;
    auto* packet = reinterpret_cast<sockaddr_ll*>(entry->ifa_addr);
    const std::string mac = synthetic_mac(entry->ifa_name == nullptr ? "unknown" : entry->ifa_name);
    unsigned int bytes[6]{};
    if (std::sscanf(mac.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
      &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6)
    {
      packet->sll_halen = 6;
      for (size_t index = 0; index < 6; ++index) packet->sll_addr[index] = static_cast<unsigned char>(bytes[index]);
    }
  }
  return result;
}

void synthetic_freeifaddrs(void* ifaddrs)
{
  const auto original = original_or_null<freeifaddrs_function>("freeifaddrs");
  if (original != nullptr) original(static_cast<struct ifaddrs*>(ifaddrs));
}

const unsigned char* synthetic_gl_get_string(unsigned int name)
{
  using function_type = const unsigned char* (*)(unsigned int);
  const auto original = original_or_null<function_type>("glGetString");
  if (!synthetic_enabled())
    return original == nullptr ? nullptr : original(name);
  const auto& profile = selected_profile();
  switch (name)
  {
    case 0x1f00: return reinterpret_cast<const unsigned char*>(profile.gpu_vendor);
    case 0x1f01: return reinterpret_cast<const unsigned char*>(profile.gpu_renderer);
    case 0x1f02: return reinterpret_cast<const unsigned char*>(profile.gpu_version);
    default: return original == nullptr ? nullptr : original(name);
  }
}

void synthetic_vk_get_physical_device_properties(void* physical_device, void* properties)
{
  using function_type = void (*)(void*, void*);
  const auto original = original_or_null<function_type>("vkGetPhysicalDeviceProperties");
  if (original != nullptr) original(physical_device, properties);
  if (!synthetic_enabled() || properties == nullptr) return;
  struct properties_prefix
  {
    std::uint32_t api_version;
    std::uint32_t driver_version;
    std::uint32_t vendor_id;
    std::uint32_t device_id;
    std::uint32_t device_type;
    char device_name[256];
    unsigned char pipeline_cache_uuid[16];
  };
  auto* value = static_cast<properties_prefix*>(properties);
  const auto& profile = selected_profile();
  value->vendor_id = profile.gpu_vendor_id;
  value->device_id = profile.gpu_device_id;
  value->device_type = 2;
  std::strncpy(value->device_name, profile.gpu_renderer, sizeof(value->device_name) - 1);
  value->device_name[sizeof(value->device_name) - 1] = '\0';
}

void synthetic_vk_get_physical_device_properties2(void* physical_device, void* properties2)
{
  using function_type = void (*)(void*, void*);
  const auto original = original_or_null<function_type>("vkGetPhysicalDeviceProperties2");
  if (original != nullptr) original(physical_device, properties2);
  if (!synthetic_enabled() || properties2 == nullptr) return;
  auto* properties = static_cast<unsigned char*>(properties2) + 16;
  synthetic_vk_get_physical_device_properties(physical_device, properties);
}

const char* synthetic_udev_sysattr_value(void* device, const char* attribute)
{
  using function_type = const char* (*)(void*, const char*);
  const auto original = original_or_null<function_type>("udev_device_get_sysattr_value");
  if (!synthetic_enabled() || attribute == nullptr || original == nullptr) return original == nullptr ? nullptr : original(device, attribute);
  if (std::strcmp(attribute, "address") == 0)
  {
    using sysname_function = const char* (*)(void*);
    const auto get_sysname = original_or_null<sysname_function>("udev_device_get_sysname");
    const char* sysname = get_sysname == nullptr ? nullptr : get_sysname(device);
    static thread_local std::string value;
    value = synthetic_mac(sysname == nullptr ? "udev" : sysname);
    return value.c_str();
  }
  if (std::strcmp(attribute, "serial") == 0 || std::strcmp(attribute, "serial_number") == 0
    || std::strcmp(attribute, "uuid") == 0 || std::strcmp(attribute, "ID_SERIAL") == 0)
  {
    static thread_local std::string value;
    value = identity128(std::string("udev:") + attribute);
    return value.c_str();
  }
  if (std::strcmp(attribute, "manufacturer") == 0 || std::strcmp(attribute, "vendor") == 0)
    return selected_profile().gpu_vendor;
  if (std::strcmp(attribute, "model") == 0 || std::strcmp(attribute, "product") == 0)
    return selected_profile().gpu_renderer;
  return original(device, attribute);
}

const char* synthetic_udev_property_value(void* device, const char* property)
{
  using function_type = const char* (*)(void*, const char*);
  const auto original = original_or_null<function_type>("udev_device_get_property_value");
  if (!synthetic_enabled() || property == nullptr || original == nullptr) return original == nullptr ? nullptr : original(device, property);
  if (std::strcmp(property, "ID_SERIAL") == 0 || std::strcmp(property, "ID_SERIAL_SHORT") == 0
    || std::strcmp(property, "ID_WWN") == 0)
  {
    static thread_local std::string value;
    value = identity128(std::string("udev-property:") + property);
    return value.c_str();
  }
  if (std::strcmp(property, "ID_VENDOR") == 0 || std::strcmp(property, "ID_VENDOR_FROM_DATABASE") == 0)
    return selected_profile().gpu_vendor;
  if (std::strcmp(property, "ID_MODEL") == 0 || std::strcmp(property, "ID_MODEL_FROM_DATABASE") == 0)
    return selected_profile().gpu_renderer;
  return original(device, property);
}

const char* synthetic_nm_hw_address(void* device, const char* api_name)
{
  using function_type = const char* (*)(void*);
  const auto original = original_or_null<function_type>(api_name);
  const char* interface_name = nullptr;
  using iface_function = const char* (*)(void*);
  const auto get_iface = original_or_null<iface_function>("nm_device_get_iface");
  if (get_iface != nullptr) interface_name = get_iface(device);
  static thread_local std::string value;
  value = synthetic_mac(interface_name == nullptr ? api_name : interface_name);
  if (!synthetic_enabled()) return original == nullptr ? nullptr : original(device);
  return value.c_str();
}

const char* synthetic_nm_device_string(void* device, const char* api_name)
{
  using function_type = const char* (*)(void*);
  const auto original = original_or_null<function_type>(api_name);
  if (!synthetic_enabled()) return original == nullptr ? nullptr : original(device);
  if (std::strcmp(api_name, "nm_device_get_udi") == 0)
    return "/sys/devices/virtual/net/eth0";
  if (std::strcmp(api_name, "nm_device_get_vendor") == 0)
    return selected_profile().board_vendor;
  return selected_profile().board_name;
}

struct synthetic_cpu_information
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
  std::uint32_t l1_size;
  std::uint32_t l1_desc;
  std::uint32_t l2_size;
  std::uint32_t l2_desc;
  std::uint32_t l3_size;
  std::uint32_t l3_desc;
};

static_assert(sizeof(synthetic_cpu_information) == (sizeof(void*) == 8 ? 72 : 64));

const synthetic_cpu_information& synthetic_cpu()
{
  const auto& profile = selected_profile();
  static synthetic_cpu_information value{};
  static const hardware_profile* initialized_profile = nullptr;
  if (initialized_profile != &profile)
  {
    static char processor_id[32]{};
    static char processor_brand[128]{};
    std::strncpy(processor_id, profile.cpu_vendor, sizeof(processor_id) - 1);
    std::strncpy(processor_brand, profile.cpu_brand, sizeof(processor_brand) - 1);
    value = {};
    value.size = sizeof(value);
    value.rdtsc = profile.rdtsc;
    value.cmov = profile.cmov;
    value.fcmov = profile.fcmov;
    value.sse = profile.sse;
    value.sse2 = profile.sse2;
    value.dnow = profile.dnow;
    value.mmx = profile.mmx;
    value.hyper_threading = profile.hyper_threading;
    value.logical_processors = profile.logical_processors;
    value.physical_processors = profile.physical_processors;
    value.sse3 = profile.sse3;
    value.ssse3 = profile.ssse3;
    value.sse4a = profile.sse4a;
    value.sse41 = profile.sse41;
    value.sse42 = profile.sse42;
    value.avx = profile.avx;
    value.speed = profile.cpu_speed;
    value.processor_id = processor_id;
    value.model = profile.cpu_model;
    value.features[0] = profile.cpu_features[0];
    value.features[1] = profile.cpu_features[1];
    value.features[2] = profile.cpu_features[2];
    value.processor_brand = processor_brand;
    value.l1_size = 32;
    value.l2_size = profile.physical_processors > 4 ? 512 : 256;
    value.l3_size = profile.cpu_vendor[0] == 'A' ? 32768 : 8192;
    initialized_profile = &profile;
  }
  return value;
}

const void* synthetic_cpu_information_pointer()
{
  using function_type = const void* (*)();
  const auto original = original_or_null<function_type>("_Z17GetCPUInformationv");
  return synthetic_enabled() ? static_cast<const void*>(&synthetic_cpu())
    : (original == nullptr ? nullptr : original());
}

}
