#include "features/automation/profile_stalker/profile_stalker.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <system_error>
#include <unordered_map>
#include <unistd.h>
#include <vector>
#include <arpa/inet.h>

#include "core/logger.hpp"
#include "core/print.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/steam_runtime.hpp"

namespace automation::profile_stalker
{

namespace
{

constexpr std::uint64_t steam_id64_base = 76561197960265728ull;
constexpr std::uint64_t tf2_app_id = 440;
constexpr float config_check_interval = 2.0f;
constexpr float a2s_timeout_seconds = 2.5f;
constexpr float snapshot_interval = 1.0f;
constexpr int max_tracked_accounts = 64;
constexpr int max_recv_per_tick = 8;
constexpr std::uint16_t unused_query_port = 0xFFFF;

constexpr std::array<char, 25> a2s_info_request{
  '\xff', '\xff', '\xff', '\xff', 'T',
  'S', 'o', 'u', 'r', 'c', 'e', ' ', 'E', 'n', 'g', 'i', 'n', 'e', ' ', 'Q', 'u', 'e', 'r', 'y', '\0'
};

struct stalker_account
{
  std::uint32_t account_id = 0;
  float next_request_time = 0.0f;
  presence_card card{};
};

struct stalker_file_cache
{
  std::filesystem::path loaded_path{};
  std::filesystem::file_time_type last_write_time{};
  std::vector<std::string> lines{};
  bool attempted = false;
};

struct public_server_info
{
  std::string hostname{};
  std::string map{};
  int players = 0;
  int max_players = 0;
  bool valve = false;
  bool valid = false;
  float fetched_at = 0.0f;
};

struct a2s_query
{
  std::uint32_t ip = 0;
  std::uint16_t port = 0;
  float sent_at = 0.0f;
  bool challenge = false;
};

struct stalker_state
{
  bool friends_failure_logged = false;
  bool socket_failure_logged = false;
  float next_file_check_time = 0.0f;
  float next_snapshot_time = 0.0f;
  int query_socket = -1;
  std::vector<stalker_account> accounts{};
  std::unordered_map<std::uint64_t, public_server_info> servers{};
  std::unordered_map<std::uint64_t, a2s_query> inflight{};
};

static_assert(sizeof(FriendGameInfo) == 24);

stalker_file_cache g_file_cache{};
stalker_state g_state{};

void close_query_socket();

struct query_socket_guard
{
  ~query_socket_guard()
  {
    close_query_socket();
  }
};

query_socket_guard g_query_socket_guard{};

std::uint64_t server_key(const std::uint32_t ip, const std::uint16_t port)
{
  return (static_cast<std::uint64_t>(ip) << 16u) | port;
}

void close_query_socket()
{
  if (g_state.query_socket >= 0)
  {
    ::close(g_state.query_socket);
    g_state.query_socket = -1;
  }
  g_state.inflight.clear();
}

int query_socket()
{
  if (g_state.query_socket >= 0)
  {
    return g_state.query_socket;
  }

  const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0)
  {
    if (!g_state.socket_failure_logged)
    {
      g_state.socket_failure_logged = true;
      print("[stalker] public server query socket unavailable\n");
    }
    return -1;
  }

  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
  {
    ::close(fd);
    return -1;
  }

  g_state.query_socket = fd;
  return fd;
}

std::string trim_line(std::string line)
{
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t'))
  {
    line.pop_back();
  }

  std::size_t start = 0;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
  {
    ++start;
  }

  return line.substr(start);
}

std::filesystem::path resolve_stalk_path(const std::string& filename)
{
  if (filename.empty())
  {
    return {};
  }

  std::error_code error{};
  const std::filesystem::path requested_path{ filename };
  if (requested_path.is_absolute() && std::filesystem::exists(requested_path, error))
  {
    return requested_path;
  }

  const std::array<std::filesystem::path, 4> candidates{
    puphook::core::root_directory() / requested_path,
    puphook::core::config_directory() / requested_path,
    std::filesystem::current_path(error) / requested_path,
    std::filesystem::current_path(error) / "config_data" / requested_path
  };

  for (const auto& candidate : candidates)
  {
    error.clear();
    if (!std::filesystem::exists(candidate, error))
    {
      continue;
    }

    return candidate;
  }

  return {};
}

const std::vector<std::string>& load_stalk_lines()
{
  const auto path = resolve_stalk_path("stalk.txt");
  std::error_code error{};
  const auto write_time = !path.empty() && std::filesystem::exists(path, error)
    ? std::filesystem::last_write_time(path, error)
    : std::filesystem::file_time_type{};

  if (g_file_cache.attempted && g_file_cache.loaded_path == path && g_file_cache.last_write_time == write_time)
  {
    return g_file_cache.lines;
  }

  g_file_cache.loaded_path = path;
  g_file_cache.last_write_time = write_time;
  g_file_cache.lines.clear();
  g_file_cache.attempted = true;

  if (path.empty())
  {
    return g_file_cache.lines;
  }

  std::ifstream input{ path };
  if (!input.is_open())
  {
    return g_file_cache.lines;
  }

  std::string raw_line{};
  while (std::getline(input, raw_line))
  {
    const auto line = trim_line(std::move(raw_line));
    if (line.empty() || line.front() == '#' || line.compare(0, 2, "//") == 0)
    {
      continue;
    }

    g_file_cache.lines.emplace_back(line);
  }

  return g_file_cache.lines;
}

std::uint32_t account_id_from_token(const std::string& token)
{
  if (token.empty())
  {
    return 0;
  }

  char* end_pointer = nullptr;
  const unsigned long long value = std::strtoull(token.c_str(), &end_pointer, 10);
  if (end_pointer == token.c_str())
  {
    return 0;
  }

  if (value >= steam_id64_base)
  {
    return static_cast<std::uint32_t>(value - steam_id64_base);
  }

  if (value > 0 && value <= 0xFFFFFFFFull)
  {
    return static_cast<std::uint32_t>(value);
  }

  return 0;
}

void refresh_accounts()
{
  const auto& lines = load_stalk_lines();

  std::vector<stalker_account> refreshed{};
  refreshed.reserve(std::min(lines.size(), static_cast<std::size_t>(max_tracked_accounts)));
  for (const auto& line : lines)
  {
    if (static_cast<int>(refreshed.size()) >= max_tracked_accounts)
    {
      break;
    }

    const auto account_id = account_id_from_token(line);
    if (account_id == 0)
    {
      continue;
    }

    if (std::any_of(refreshed.begin(), refreshed.end(),
          [account_id](const stalker_account& entry) { return entry.account_id == account_id; }))
    {
      continue;
    }

    bool merged = false;
    for (auto& existing : g_state.accounts)
    {
      if (existing.account_id != account_id)
      {
        continue;
      }

      refreshed.emplace_back(std::move(existing));
      merged = true;
      break;
    }

    if (!merged)
    {
      stalker_account fresh{};
      fresh.account_id = account_id;
      fresh.next_request_time = global_vars != nullptr ? global_vars->realtime : 0.0f;
      fresh.card.name = std::to_string(account_id);
      fresh.card.heading = "Unknown";
      fresh.card.card = fresh.card.name + "\n\nUnknown";
      refreshed.emplace_back(std::move(fresh));
      print("[stalker] watching %u\n", account_id);
    }
  }

  for (const auto& existing : g_state.accounts)
  {
    const bool still_present = std::any_of(refreshed.begin(), refreshed.end(),
      [existing](const stalker_account& entry) { return entry.account_id == existing.account_id; });
    if (!still_present)
    {
      print("[stalker] stopped watching %u\n", existing.account_id);
    }
  }

  g_state.accounts = std::move(refreshed);
}

SteamID make_steam_id(const std::uint32_t account_id)
{
  return SteamID(static_cast<int>(account_id), 1, k_EUniversePublic, k_EAccountTypeIndividual);
}

std::string view_or_empty(const char* value)
{
  return value != nullptr ? std::string{ value } : std::string{};
}

std::string rich_presence_value(SteamFriends* friends, const SteamID steam_id, const char* key)
{
  return view_or_empty(friends->get_friend_rich_presence(steam_id, key));
}

bool parse_ipv4(const std::string_view text, std::uint32_t& ip)
{
  unsigned a = 0;
  unsigned b = 0;
  unsigned c = 0;
  unsigned d = 0;
  char extra = 0;
  if (std::sscanf(std::string{ text }.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4
    || a > 255u || b > 255u || c > 255u || d > 255u)
  {
    return false;
  }

  ip = (a << 24u) | (b << 16u) | (c << 8u) | d;
  return ip != 0;
}

bool parse_connect_endpoint(std::string_view value, std::uint32_t& ip, std::uint16_t& port)
{
  if (value.find("tf_party") != std::string_view::npos)
  {
    return false;
  }

  if (!value.empty() && value.front() == '+')
  {
    value.remove_prefix(1);
  }

  const auto connect_at = value.find("connect");
  if (connect_at == std::string_view::npos)
  {
    return false;
  }

  value.remove_prefix(connect_at + 7);
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
  {
    value.remove_prefix(1);
  }

  const auto colon = value.find(':');
  if (colon == std::string_view::npos)
  {
    return false;
  }

  unsigned parsed_port = 0;
  if (!parse_ipv4(value.substr(0, colon), ip))
  {
    return false;
  }

  const auto port_text = std::string{ value.substr(colon + 1) };
  if (std::sscanf(port_text.c_str(), "%u", &parsed_port) != 1 || parsed_port == 0 || parsed_port > 65535u)
  {
    return false;
  }

  port = static_cast<std::uint16_t>(parsed_port);
  return true;
}

std::string format_match_group(std::string value)
{
  for (char& character : value)
  {
    if (character == '_')
    {
      character = ' ';
    }
  }
  return value;
}

const char* mode_from_map(const std::string_view map)
{
  if (map.starts_with("mvm_"))
  {
    return "Mann vs Machine";
  }
  if (map.starts_with("plr_"))
  {
    return "Payload Race";
  }
  if (map.starts_with("pl_"))
  {
    return "Payload";
  }
  if (map.starts_with("koth_"))
  {
    return "King of the Hill";
  }
  if (map.starts_with("ctf_"))
  {
    return "Capture the Flag";
  }
  if (map.starts_with("cp_"))
  {
    return "Control Point";
  }
  if (map.starts_with("sd_"))
  {
    return "Special Delivery";
  }
  if (map.starts_with("tc_"))
  {
    return "Territorial Control";
  }
  if (map.starts_with("pd_"))
  {
    return "Player Destruction";
  }
  if (map.starts_with("rd_"))
  {
    return "Robot Destruction";
  }
  if (map.starts_with("pass_"))
  {
    return "PASS Time";
  }
  if (map.starts_with("arena_"))
  {
    return "Arena";
  }
  if (map.starts_with("tr_"))
  {
    return "Training";
  }
  return "";
}

bool hostname_looks_valve(const std::string_view hostname)
{
  return hostname.starts_with("Valve ") || hostname.find("Valve Matchmaking") != std::string_view::npos;
}

bool read_cstring(const std::uint8_t* data, const std::size_t size, std::size_t& position, std::string& output)
{
  if (position >= size)
  {
    return false;
  }

  const auto* begin = reinterpret_cast<const char*>(data + position);
  const auto* end = static_cast<const char*>(std::memchr(begin, 0, size - position));
  if (end == nullptr)
  {
    return false;
  }

  output.assign(begin, end);
  position += static_cast<std::size_t>(end - begin) + 1u;
  return true;
}

bool parse_a2s_info(const std::uint8_t* data, const std::size_t size, public_server_info& info)
{
  if (size < 6 || data[0] != 0xff || data[1] != 0xff || data[2] != 0xff || data[3] != 0xff || data[4] != 'I')
  {
    return false;
  }

  std::size_t position = 6;
  std::string hostname{};
  std::string map{};
  std::string folder{};
  std::string game{};
  if (!read_cstring(data, size, position, hostname)
    || !read_cstring(data, size, position, map)
    || !read_cstring(data, size, position, folder)
    || !read_cstring(data, size, position, game)
    || position + 5 > size)
  {
    return false;
  }

  position += 2;
  info.hostname = std::move(hostname);
  info.map = std::move(map);
  info.players = data[position];
  info.max_players = data[position + 1];
  info.valve = hostname_looks_valve(info.hostname);
  info.valid = true;
  return true;
}

bool send_a2s_query(const std::uint32_t ip, const std::uint16_t port, const float now, const std::int32_t* challenge)
{
  const int fd = query_socket();
  if (fd < 0 || ip == 0 || port == 0)
  {
    return false;
  }

  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(port);
  destination.sin_addr.s_addr = htonl(ip);

  std::array<char, 29> payload{};
  std::memcpy(payload.data(), a2s_info_request.data(), a2s_info_request.size());
  std::size_t payload_size = a2s_info_request.size();
  if (challenge != nullptr)
  {
    std::memcpy(payload.data() + payload_size, challenge, sizeof(*challenge));
    payload_size += sizeof(*challenge);
  }

  const auto sent = ::sendto(
    fd,
    payload.data(),
    payload_size,
    0,
    reinterpret_cast<sockaddr*>(&destination),
    sizeof(destination));
  if (sent < 0)
  {
    return false;
  }

  a2s_query query{};
  query.ip = ip;
  query.port = port;
  query.sent_at = now;
  query.challenge = challenge != nullptr;
  g_state.inflight[server_key(ip, port)] = query;
  return true;
}

void request_server_info(const std::uint32_t ip, const std::uint16_t port, const float now, const float refresh_after)
{
  if (ip == 0 || port == 0)
  {
    return;
  }

  const auto key = server_key(ip, port);
  const auto cached = g_state.servers.find(key);
  if (cached != g_state.servers.end() && cached->second.valid && now - cached->second.fetched_at < refresh_after)
  {
    return;
  }

  if (g_state.inflight.contains(key))
  {
    return;
  }

  send_a2s_query(ip, port, now, nullptr);
}

void drain_server_queries(const float now)
{
  const int fd = g_state.query_socket;
  if (fd < 0)
  {
    return;
  }

  for (int received = 0; received < max_recv_per_tick; ++received)
  {
    std::uint8_t buffer[1400]{};
    sockaddr_in source{};
    socklen_t source_size = sizeof(source);
    const auto bytes = ::recvfrom(
      fd,
      buffer,
      sizeof(buffer),
      0,
      reinterpret_cast<sockaddr*>(&source),
      &source_size);
    if (bytes <= 0)
    {
      break;
    }

    const auto ip = ntohl(source.sin_addr.s_addr);
    const auto port = ntohs(source.sin_port);
    const auto key = server_key(ip, port);
    auto inflight = g_state.inflight.find(key);
    if (inflight == g_state.inflight.end())
    {
      continue;
    }

    if (bytes >= 9 && buffer[4] == 'A')
    {
      std::int32_t challenge = 0;
      std::memcpy(&challenge, buffer + 5, sizeof(challenge));
      g_state.inflight.erase(inflight);
      send_a2s_query(ip, port, now, &challenge);
      continue;
    }

    public_server_info info{};
    if (parse_a2s_info(buffer, static_cast<std::size_t>(bytes), info))
    {
      info.fetched_at = now;
      g_state.servers[key] = std::move(info);
    }
    g_state.inflight.erase(inflight);
  }

  for (auto it = g_state.inflight.begin(); it != g_state.inflight.end();)
  {
    if (now - it->second.sent_at > a2s_timeout_seconds)
    {
      it = g_state.inflight.erase(it);
      continue;
    }
    ++it;
  }
}

const public_server_info* cached_server(const std::uint32_t ip, const std::uint16_t port, const float now, const float stale_after)
{
  if (ip == 0 || port == 0)
  {
    return nullptr;
  }

  const auto found = g_state.servers.find(server_key(ip, port));
  if (found == g_state.servers.end() || !found->second.valid)
  {
    return nullptr;
  }

  if (now - found->second.fetched_at > stale_after)
  {
    return nullptr;
  }

  return &found->second;
}

bool known_presence_key(const std::string_view key)
{
  return key == "state" || key == "currentmap" || key == "status" || key == "steam_display"
    || key == "steam_player_group" || key == "steam_player_group_size" || key == "matchgrouploc"
    || key == "matchgrouploc_token" || key == "connect";
}

presence_card build_card(
  const stalker_account& account,
  SteamFriends* friends,
  const float now,
  const float refresh_after,
  const float stale_after)
{
  presence_card card{};
  const SteamID steam_id = make_steam_id(account.account_id);
  const std::string persona = view_or_empty(friends->get_friend_persona_name(steam_id));
  card.name = !persona.empty() && persona != "[unknown]" ? persona : std::to_string(account.account_id);

  const int persona_state = friends->get_friend_persona_state(steam_id);
  FriendGameInfo game{};
  const bool playing = friends->get_friend_game_played(steam_id, &game);
  const std::uint32_t app_id = static_cast<std::uint32_t>(game.game_id & 0xffffffull);
  const bool in_tf2 = playing && app_id == tf2_app_id;

  const std::string state = rich_presence_value(friends, steam_id, "state");
  const std::string currentmap = rich_presence_value(friends, steam_id, "currentmap");
  const std::string status_text = rich_presence_value(friends, steam_id, "status");
  const std::string match_group = rich_presence_value(friends, steam_id, "matchgrouploc");
  const std::string connect = rich_presence_value(friends, steam_id, "connect");
  const std::string party_size_text = rich_presence_value(friends, steam_id, "steam_player_group_size");

  std::uint32_t server_ip = in_tf2 ? game.game_ip : 0;
  std::uint16_t game_port = in_tf2 ? game.game_port : 0;
  std::uint16_t query_port = in_tf2 ? game.query_port : 0;
  if (server_ip == 0 || game_port == 0)
  {
    std::uint32_t connect_ip = 0;
    std::uint16_t connect_port = 0;
    if (parse_connect_endpoint(connect, connect_ip, connect_port))
    {
      server_ip = connect_ip;
      if (game_port == 0)
      {
        game_port = connect_port;
      }
    }
  }

  if (query_port == 0 || query_port == unused_query_port)
  {
    query_port = game_port;
  }

  const bool match_group_state = state.find("MatchGroup") != std::string::npos;
  const bool community_state = state.find("Community") != std::string::npos;
  const bool searching = state.starts_with("Searching");
  const bool loading = state.starts_with("Loading");
  const bool playing_state = state.starts_with("Playing");
  const bool has_endpoint = server_ip != 0 && query_port != 0;
  if (has_endpoint)
  {
    request_server_info(server_ip, query_port, now, refresh_after);
  }

  const auto* server = has_endpoint ? cached_server(server_ip, query_port, now, stale_after) : nullptr;
  const bool identified_server = server != nullptr;
  const bool in_game_presence = (playing_state || community_state || has_endpoint || !currentmap.empty())
    && state != "MainMenu" && !searching;

  std::vector<std::string> details{};
  if (persona_state == k_EPersonaStateOffline || persona_state == k_EPersonaStateInvisible)
  {
    card.heading = "Offline";
  }
  else if (!in_tf2)
  {
    card.heading = "Online";
    details.emplace_back("Main Menu");
    if (playing && app_id != 0)
    {
      details.emplace_back("In another game");
    }
  }
  else if (identified_server)
  {
    card.heading = "In Server";
    card.in_server = true;
    details.emplace_back("Hostname: " + server->hostname);
    if (!server->map.empty())
    {
      details.emplace_back("Map: " + server->map);
    }
    details.emplace_back("Players: " + std::to_string(server->players) + " / " + std::to_string(server->max_players));
    const char* map_mode = mode_from_map(server->map);
    if (!match_group.empty())
    {
      details.emplace_back("Mode: " + format_match_group(match_group));
    }
    else if (map_mode[0] != '\0')
    {
      details.emplace_back(std::string{ "Mode: " } + map_mode);
    }
    details.emplace_back(std::string{ "Type: " } + (server->valve || match_group_state ? "Valve" : "Community"));
  }
  else if (in_game_presence)
  {
    card.heading = "In TF2";
    details.emplace_back("Server details unavailable");
    if (loading)
    {
      details.emplace_back(community_state ? "Loading community server" : "Loading");
    }
    if (!currentmap.empty())
    {
      details.emplace_back("Map: " + currentmap);
    }
    if (!match_group.empty())
    {
      details.emplace_back("Mode: " + format_match_group(match_group));
    }
    if (match_group_state)
    {
      details.emplace_back("Type: Valve");
    }
    else if (community_state)
    {
      details.emplace_back("Type: Community");
    }
  }
  else
  {
    card.heading = "In TF2";
    if (searching)
    {
      details.emplace_back(match_group.empty() ? "Searching" : "Searching " + format_match_group(match_group));
    }
    else if (loading)
    {
      details.emplace_back("Loading");
    }
    else
    {
      details.emplace_back("Main Menu");
    }
    if (!match_group.empty() && !searching)
    {
      details.emplace_back("Mode: " + format_match_group(match_group));
    }
  }

  if (!party_size_text.empty() && party_size_text != "0")
  {
    details.emplace_back("Party: " + party_size_text);
  }

  if (!status_text.empty()
    && status_text != card.heading
    && std::find(details.begin(), details.end(), status_text) == details.end())
  {
    details.emplace_back(status_text);
  }

  const int extra_keys = std::clamp(friends->get_friend_rich_presence_key_count(steam_id), 0, 32);
  for (int index = 0; index < extra_keys; ++index)
  {
    const char* key = friends->get_friend_rich_presence_key_by_index(steam_id, index);
    if (key == nullptr || known_presence_key(key))
    {
      continue;
    }

    const std::string extra = rich_presence_value(friends, steam_id, key);
    if (!extra.empty())
    {
      details.emplace_back(std::string{ key } + ": " + extra);
    }
  }

  if (card.heading.empty())
  {
    card.heading = "Unknown";
  }

  if (!details.empty())
  {
    card.summary = details.front();
  }

  card.card = card.name;
  card.card += "\n\n";
  card.card += card.heading;
  for (const auto& line : details)
  {
    card.card += '\n';
    card.card += line;
  }

  return card;
}

void request_presence(stalker_account& account, SteamFriends* friends, const float now)
{
  if (now < account.next_request_time)
  {
    return;
  }

  const SteamID steam_id = make_steam_id(account.account_id);
  friends->request_user_information(steam_id, false);
  friends->request_friend_rich_presence(steam_id);
  account.next_request_time = now + static_cast<float>(std::max(5, config.misc.automation.stalker_interval));
}

void refresh_snapshots(SteamFriends* friends, const float now)
{
  const float refresh_after = static_cast<float>(std::max(5, config.misc.automation.stalker_interval));
  const float stale_after = refresh_after * 3.0f;

  for (auto& account : g_state.accounts)
  {
    request_presence(account, friends, now);
    auto card = build_card(account, friends, now, refresh_after, stale_after);
    if (card.card != account.card.card)
    {
      print("[stalker] %s\n", card.card.c_str());
    }
    account.card = std::move(card);
  }
}

}

void tick()
{
  if (!config.misc.automation.stalker_enabled)
  {
    close_query_socket();
    return;
  }

  if (global_vars == nullptr)
  {
    return;
  }

  const float now = global_vars->realtime;
  if (now >= g_state.next_file_check_time)
  {
    g_state.next_file_check_time = now + config_check_interval;
    refresh_accounts();
  }

  auto* friends = steam_runtime::resolve_steam_friends();
  if (friends == nullptr)
  {
    if (!g_state.friends_failure_logged)
    {
      g_state.friends_failure_logged = true;
      print("[stalker] SteamFriends017 unavailable\n");
    }
    return;
  }

  g_state.friends_failure_logged = false;
  drain_server_queries(now);

  if (now >= g_state.next_snapshot_time)
  {
    g_state.next_snapshot_time = now + snapshot_interval;
    refresh_snapshots(friends, now);
  }
}

int status_count()
{
  return static_cast<int>(g_state.accounts.size());
}

presence_card status(const int index)
{
  if (index < 0 || index >= static_cast<int>(g_state.accounts.size()))
  {
    return {};
  }

  return g_state.accounts[static_cast<std::size_t>(index)].card;
}

std::string status_line(const int index)
{
  return status(index).card;
}

}
