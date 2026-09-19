#include "common.hpp"
#include "CServerTools.hpp"
#include "core/sharedobj.hpp"

IServerEntity *CServerTools::GetIServerEntity(IClientEntity *client)
{
    if (!client)
        return nullptr;

    static void *tools = nullptr;
    if (!tools)
    {
        std::string path;
        std::string name = "server.so";
        if (!sharedobj::LocateSharedObject(name, path))
            return nullptr;
        void *handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
        if (!handle)
            return nullptr;
        auto create = reinterpret_cast<fn_CreateInterface_t>(dlsym(handle, "CreateInterface"));
        if (!create)
            return nullptr;
        for (const char *ver : { "VSERVERTOOLS003", "VSERVERTOOLS002", "VSERVERTOOLS001" })
        {
            tools = create(ver, nullptr);
            if (tools)
                break;
        }
    }
    if (!tools)
        return nullptr;

    using Fn = IServerEntity *(*)(void *, IClientEntity *);
    return vfunc<Fn>(tools, vtables::server_tools::get_i_server_entity)(tools, client);
}

static CServerTools server_tools_wrapper;
static InitRoutine bind_server_tools([]() { g_IServerTools = &server_tools_wrapper; });
