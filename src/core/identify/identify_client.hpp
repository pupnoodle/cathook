#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cathook::core::identify
{

class identify_client
{
public:
  using peers_callback_t = std::function<void(const std::vector<std::string>&)>;
  using chat_callback_t = std::function<void(const std::string&, const std::string&)>;

  identify_client() = default;
  ~identify_client();

  identify_client(const identify_client&) = delete;
  auto operator=(const identify_client&) -> identify_client& = delete;

  void set_peers_handler(peers_callback_t callback);
  void set_chat_handler(chat_callback_t callback);

  void connect(std::string host, int port);
  void stop();

  void update_identity(std::string_view server_id, std::string_view player_hash, std::string_view signature, int head_scale);
  void send_chat(std::string_view message);

private:
  void close_socket();
  [[nodiscard]] bool dial();
  void worker_loop();
  void send_line(std::string line);

  [[nodiscard]] static std::string escape_string(std::string_view value);
  [[nodiscard]] static std::string find_json_value(std::string_view json, std::string_view key);
  [[nodiscard]] static std::vector<std::string> parse_string_array(std::string_view array_str);
  void handle_line(std::string_view line);

  std::string m_host{};
  int m_port = 0;
  std::atomic<int> m_socket{-1};
  std::atomic<bool> m_running{false};

  std::thread m_worker{};
  std::mutex m_socket_mutex{};
  std::mutex m_send_mutex{};
  std::mutex m_identity_mutex{};
  std::condition_variable m_cv{};

  std::string m_current_server_id{};
  std::string m_current_player_hash{};
  std::string m_current_signature{};
  int m_current_head_scale = 0;

  peers_callback_t m_peers_callback{};
  chat_callback_t m_chat_callback{};
};

}
