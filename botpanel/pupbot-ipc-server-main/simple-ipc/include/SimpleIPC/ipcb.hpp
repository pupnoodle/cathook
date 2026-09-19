#pragma once

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <thread>
#include <condition_variable>
#include <mutex>

#include "util.hpp"
#include "cmp.hpp"

namespace pup_ipc
{
constexpr unsigned char max_peers     = UCHAR_MAX;
constexpr unsigned int command_buffer = max_peers * 2;
constexpr unsigned int pool_size      = command_buffer * 4096;
constexpr unsigned int command_data   = 64;

struct PeerData
{
    bool free;
    time_t heartbeat;
    pid_t pid;
    unsigned long starttime;
};

struct Command
{
    unsigned int command_number;
    int target_peer;
    int sender;
    unsigned long payload_offset;
    unsigned int payload_size;
    unsigned int cmd_type;
    unsigned char cmd_data[command_data];
};

template <typename S, typename U> struct IPCMemory
{
    static_assert(std::is_standard_layout<S>::value && std::is_trivial<S>::value, "Global data struct must be POD");
    static_assert(std::is_standard_layout<U>::value && std::is_trivial<U>::value, "Peer data struct must be POD");
    boost::interprocess::interprocess_mutex mutex;
    unsigned int peer_count{};
    unsigned long command_count{};
    PeerData peer_data[max_peers]{};
    Command commands[command_buffer]{};
    std::array<unsigned char, pool_size> pool{};
    S global_data;
    std::array<U, max_peers> peer_user_data;
};

template <typename S, typename U> class Peer
{
public:
    using memory_t            = IPCMemory<S, U>;
    using CommandCallbackFn_t = std::function<void(const Command &, void *)>;

    explicit Peer(std::string name, bool process_old_commands = true, bool manager = false, bool ghost = false) : name(std::move(name)), process_old_commands(process_old_commands), is_manager(manager), is_ghost(ghost)
    {
    }

    ~Peer()
    {
        shutting_down = true;
        if (heartbeat_thread.joinable())
            heartbeat_thread.join();

        if (is_manager)
        {

            if (mem_map != nullptr)
            {
                delete mem_map;
                memory = nullptr;
            }

            if (shm_unlink(name.c_str()) == -1)
                throw std::runtime_error("Failed to unlink shared memory: " + std::string(strerror(errno)));
        }

        if (is_ghost)
            return;

        if (memory)
        {
            std::unique_lock<boost::interprocess::interprocess_mutex> lock(memory->mutex);
            memory->peer_data[client_id].free = true;
        }
    }

    bool HasCommands() const
    {
        return last_command != memory->command_count;
    }

    static void Heartbeat(bool *shutting_down, PeerData *data)
    {
        while (!*shutting_down)
        {
            data->heartbeat = std::time(nullptr);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void Connect()
    {
        connected = true;

        boost::interprocess::shared_memory_object shm_obj;

        if (is_manager)
        {

            boost::interprocess::shared_memory_object::remove(name.c_str());
            shm_obj = boost::interprocess::shared_memory_object(boost::interprocess::create_only, name.c_str(), boost::interprocess::read_write);

            shm_obj.truncate(sizeof(memory_t));
        }
        else
        {

            shm_obj = boost::interprocess::shared_memory_object(boost::interprocess::open_only, name.c_str(), boost::interprocess::read_write);
        }

        mem_map = new boost::interprocess::mapped_region(shm_obj, boost::interprocess::read_write);
        memory = static_cast<memory_t *>(mem_map->get_address());

        pool = std::make_unique<simple_ipc::CatMemoryPool>(&memory->pool, pool_size);

        if (is_manager)
            InitManager();

        if (!is_ghost)
        {
            memory->mutex.lock();
            client_id = FirstAvailableSlot();
            StorePeerData();
            memory->mutex.unlock();
        }
        else
            client_id = -1;

        if (!process_old_commands)
            last_command = memory->command_count;

        if (!is_ghost)
        {
            heartbeat_thread = std::jthread(Heartbeat, &this->shutting_down, &memory->peer_data[client_id]);
            if (!heartbeat_thread.joinable())
                throw std::runtime_error("Failed to crate heartbeat thread: " + std::string(strerror(errno)));
        }
    }

    int FirstAvailableSlot()
    {
        for (unsigned int i = 0; i < max_peers; ++i)
            if (memory->peer_data[i].free)
                return static_cast<int>(i);

        throw std::runtime_error("No available slots");
    }

    bool IsPeerDead(int id) const
    {
        return std::time(nullptr) - memory->peer_data[id].heartbeat >= 10;
    }

    void InitManager()
    {
        std::memset(memory, 0, sizeof(memory_t));
        for (int i = 0; i < max_peers; ++i)
            memory->peer_data[i].free = true;
        pool->init();
    }

    void SweepDead()
    {
        this->memory->mutex.lock();
        memory->peer_count = 0;
        for (int i = 0; i < max_peers; ++i)
        {
            if (IsPeerDead(i))
                memory->peer_data[i].free = true;

            if (!memory->peer_data[i].free)
                memory->peer_count++;
        }
        this->memory->mutex.unlock();
    }

    void StorePeerData()
    {
        if (is_ghost)
            return;

        ProcStat stat{};
        ReadStat(getpid(), &stat);

        auto &current_peer_data = memory->peer_data[client_id];

        current_peer_data.free      = false;
        current_peer_data.pid       = getpid();
        current_peer_data.starttime = stat.starttime;
    }

    void SetGeneralHandler(CommandCallbackFn_t new_callback)
    {
        callback = std::move(new_callback);
    }

    void SetCommandHandler(unsigned int command_type, const CommandCallbackFn_t &handler)
    {
        auto [iterator, inserted] = callback_map.insert({ command_type, handler });

        if (!inserted)
            throw std::logic_error("Single command type can't have multiple callbacks (" + std::to_string(command_type) + ")");
    }

    void ProcessCommands()
    {
        for (unsigned int i = 0; i < command_buffer; ++i)
        {
            Command &cmd = memory->commands[i];

            if (cmd.command_number <= last_command)
                continue;

            last_command = cmd.command_number;
            if (cmd.sender == client_id || is_ghost || (cmd.target_peer >= 0 && cmd.target_peer != client_id))
                continue;

            void *payload = cmd.payload_size ? pool->real_pointer<void>(reinterpret_cast<void *>(cmd.payload_offset)) : nullptr;

            if (callback)
                callback(cmd, payload);

            auto callback_it = callback_map.find(cmd.cmd_type);
            if (callback_it != callback_map.end())
                callback_it->second(cmd, payload);
        }
    }

    void SendMessage(const char *data_small, int peer_id, unsigned int command_type, const void *payload, size_t payload_size)
    {
        std::scoped_lock lock(this->memory->mutex);
        Command &cmd = memory->commands[++memory->command_count % command_buffer];
        if (cmd.payload_size)
        {
            pool->free(pool->real_pointer<void>(reinterpret_cast<void *>(cmd.payload_offset)));
            cmd.payload_offset = 0;
            cmd.payload_size   = 0;
        }
        if (data_small)
            std::memcpy(cmd.cmd_data, data_small, sizeof(cmd.cmd_data));
        if (payload_size)
        {
            void *block = pool->alloc(payload_size);
            std::memcpy(block, payload, payload_size);
            cmd.payload_offset = reinterpret_cast<unsigned long>(pool->pool_pointer<void>(block));
            cmd.payload_size   = payload_size;
        }
        cmd.cmd_type       = command_type;
        cmd.sender         = client_id;
        cmd.target_peer    = peer_id;
        cmd.command_number = memory->command_count;
    }

    boost::interprocess::mapped_region *mem_map{ nullptr };
    memory_t *memory{ nullptr };
    bool connected{ false };
    int client_id{ 0 };
    const bool is_ghost{ false };
    std::shared_ptr<simple_ipc::CatMemoryPool> pool{ nullptr };

private:
    std::unordered_map<unsigned int, CommandCallbackFn_t> callback_map{};
    unsigned long last_command{ 0 };
    CommandCallbackFn_t callback{ nullptr };
    const std::string name;
    bool process_old_commands{ true };
    const bool is_manager{ false };
    std::jthread heartbeat_thread;
    bool shutting_down{false};
};
}
