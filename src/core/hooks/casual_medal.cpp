namespace casual_medal
{

using random_int_fn = int (*)(int, int);
using rank_record_fn = std::int64_t (*)(std::uintptr_t, bool);

inline random_int_fn random_int_original = nullptr;
inline rank_record_fn rank_record_original = nullptr;

namespace
{

constexpr std::size_t rank_record_size = 40;
constexpr std::uintptr_t panel_table_offset = 632;
constexpr std::uintptr_t table_records_offset = 32;
constexpr std::uintptr_t table_count_offset = 48;
constexpr std::int32_t casual_match_group_first = 5;
constexpr std::int32_t casual_match_group_last = 7;

thread_local std::array<std::byte, rank_record_size> overridden_record{};

void* resolve_random_int_symbol()
{
  if (void* symbol = dlsym(RTLD_DEFAULT, "RandomInt"))
  {
    return symbol;
  }

  if (void* handle = open_loaded_library("libvstdlib.so"))
  {
    if (void* symbol = dlsym(handle, "RandomInt"))
    {
      return symbol;
    }
  }

  return nullptr;
}

bool is_casual_panel(const std::uintptr_t panel)
{
  if (panel == 0)
  {
    return false;
  }

  const auto match_group = *reinterpret_cast<const std::int32_t*>(panel + 584);
  return match_group >= casual_match_group_first && match_group <= casual_match_group_last;
}

std::int64_t get_overridden_record(const std::uintptr_t panel)
{
  const auto table = *reinterpret_cast<const std::uintptr_t*>(panel + panel_table_offset);
  if (table == 0)
  {
    return 0;
  }

  const auto count = *reinterpret_cast<const std::int32_t*>(table + table_count_offset);
  const auto records = *reinterpret_cast<const std::uintptr_t*>(table + table_records_offset);
  if (records == 0 || count <= 0)
  {
    return 0;
  }

  const auto level = std::clamp(config.visuals.casual_medal.rank, 1, count);
  const auto selected_record = records + rank_record_size * static_cast<std::size_t>(level - 1);
  std::memcpy(overridden_record.data(), reinterpret_cast<const void*>(selected_record), rank_record_size);
  return reinterpret_cast<std::int64_t>(overridden_record.data());
}

}

int random_int_hook(const int min, const int max)
{
  PUPHOOK_HOOK_GUARD();
  if (config.visuals.casual_medal.guaranteed_flip && min == 0 && max == 9)
  {
    return 0;
  }

  if (random_int_original == nullptr)
  {
    return min;
  }

  return random_int_original(min, max);
}

std::int64_t rank_record_hook(const std::uintptr_t panel, const bool target)
{
  PUPHOOK_HOOK_GUARD();
  if (rank_record_original == nullptr)
  {
    return 0;
  }

  const auto original_record = rank_record_original(panel, target);

  if (!config.visuals.casual_medal.changer || !is_casual_panel(panel))
  {
    return original_record;
  }

  const auto overridden = get_overridden_record(panel);
  return overridden != 0 ? overridden : original_record;
}

void resolve_random_int()
{
  random_int_original = reinterpret_cast<random_int_fn>(resolve_random_int_symbol());
}

}
