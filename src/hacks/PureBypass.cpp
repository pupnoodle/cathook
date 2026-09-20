#include <settings/Bool.hpp>
#include <cstdio>
#include "common.hpp"
#include "DetourHook.hpp"

namespace hacks::shared::purebypass
{
static settings::Boolean pure_bypass{ "misc.pure-bypass", "true" };
static settings::Boolean cheats_bypass{ "misc.cheats-bypass", "false" };
#if ENABLE_TEXTMODE || ENABLE_VAC_BYPASS
static settings::Boolean insecure_bypass{ "misc.insecure-bypass", "true" };
#else
static settings::Boolean insecure_bypass{ "misc.insecure-bypass", "false" };
#endif

static DetourHook load_white_list_detour{};
static DetourHook host_secure_detour{};

static bool *allow_secure_servers = nullptr;
static bool cheats_bypass_applied = false;
static int original_sv_cheats     = 0;
static bool insecure_bypass_applied      = false;
static bool original_allow_secure_servers = false;

static constexpr std::size_t convar_parent_offset        = 0x38;
static constexpr std::size_t convar_string_value_offset  = 0x48;
static constexpr std::size_t convar_string_length_offset = 0x50;
static constexpr std::size_t convar_float_value_offset   = 0x54;
static constexpr std::size_t convar_int_value_offset     = 0x58;

template <typename T> static T *convar_field(void *object, std::size_t offset)
{
    return reinterpret_cast<T *>(reinterpret_cast<std::uint8_t *>(object) + offset);
}

static void *convar_value_target(ConVar *var)
{
    auto *parent = *convar_field<ConVar *>(var, convar_parent_offset);
    return parent ? parent : var;
}

static int silent_convar_get_int(ConVar *var)
{
    return *convar_field<int>(convar_value_target(var), convar_int_value_offset);
}

static void silent_convar_set_int(ConVar *var, int value)
{
    void *target                                      = convar_value_target(var);
    *convar_field<int>(target, convar_int_value_offset)     = value;
    *convar_field<float>(target, convar_float_value_offset) = static_cast<float>(value);
    auto *string_value                                = *convar_field<char *>(target, convar_string_value_offset);
    const int string_length                           = *convar_field<int>(target, convar_string_length_offset);
    if (!string_value || string_length <= 0)
        return;
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%d", value);
    if (written < 0 || written + 1 > string_length)
        return;
    std::memcpy(string_value, buffer, static_cast<std::size_t>(written + 1));
}

static bool *get_allow_secure_servers_flag()
{
    if (allow_secure_servers)
        return allow_secure_servers;

    auto *match = reinterpret_cast<void *>(gSignatures.GetEngineSignature(sigs::allow_secure_servers_flag_ref));
    if (!match)
        return nullptr;

    allow_secure_servers = static_cast<bool *>(cathook::core::memory::resolve_lea_rip(match));
    if (allow_secure_servers)
        BytePatch::mprotectAddr(uintptr_t(allow_secure_servers), 1, PROT_READ | PROT_WRITE | PROT_EXEC);
    return allow_secure_servers;
}

using LoadWhiteList_t               = void *(*)(void *);
using Host_IsSecureServerAllowed_t = bool (*)();

static void *LoadWhiteList_hook(void *self)
{
    if (pure_bypass)
        return nullptr;
    auto original = (LoadWhiteList_t) load_white_list_detour.GetOriginalFunc();
    return original ? original(self) : nullptr;
}

static bool Host_IsSecureServerAllowed_hook()
{
    if (insecure_bypass)
    {
        if (auto *flag = get_allow_secure_servers_flag())
            *flag = true;
        return true;
    }
    auto original = (Host_IsSecureServerAllowed_t) host_secure_detour.GetOriginalFunc();
    return original ? original() : true;
}

static void apply_insecure_flag()
{
    auto *flag = get_allow_secure_servers_flag();
    if (!flag)
        return;

    if (insecure_bypass)
    {
        if (!insecure_bypass_applied)
        {
            original_allow_secure_servers = *flag;
            insecure_bypass_applied       = true;
        }
        *flag = true;
    }
    else if (insecure_bypass_applied)
    {
        *flag                     = original_allow_secure_servers;
        insecure_bypass_applied   = false;
    }
}

static void apply_cheats_bypass()
{
    if (!g_ICvar)
        return;

    ConVar *sv_cheats = g_ICvar->FindVar("sv_cheats");
    if (!sv_cheats)
        return;

    if (cheats_bypass)
    {
        if (!cheats_bypass_applied)
        {
            original_sv_cheats     = silent_convar_get_int(sv_cheats);
            cheats_bypass_applied  = true;
        }
        if (silent_convar_get_int(sv_cheats) != 1)
            silent_convar_set_int(sv_cheats, 1);
    }
    else if (cheats_bypass_applied)
    {
        silent_convar_set_int(sv_cheats, original_sv_cheats);
        cheats_bypass_applied = false;
    }
}

static void CreateMove()
{
    apply_insecure_flag();
    apply_cheats_bypass();
}

static void EnableInsecureBypass()
{
    insecure_bypass = true;
    apply_insecure_flag();
}

CatCommand fixvac("fixvac", "Lemme in to secure servers", []() { EnableInsecureBypass(); });

static InitRoutine init(
    []()
    {
        auto load_whitelist = gSignatures.GetEngineSignature(sigs::load_white_list);
        if (load_whitelist)
        {
            load_white_list_detour.Init(load_whitelist, (void *) LoadWhiteList_hook);
            logging::Info("Hooked CL_LoadWhitelist at %p", (void *) load_whitelist);
        }
        else
            logging::Info("CL_LoadWhitelist signature not found");

        auto host_secure = gSignatures.GetEngineSignature(sigs::host_is_secure_server_allowed);
        if (host_secure)
        {
            host_secure_detour.Init(host_secure, (void *) Host_IsSecureServerAllowed_hook);
            logging::Info("Hooked Host_IsSecureServerAllowed at %p", (void *) host_secure);
        }
        else
            logging::Info("Host_IsSecureServerAllowed signature not found");

        apply_insecure_flag();
#if ENABLE_VAC_BYPASS
        EnableInsecureBypass();
#endif
        EC::Register(EC::CreateMove, CreateMove, "cm_purebypass", EC::average);
        EC::Register(EC::Paint, CreateMove, "paint_purebypass", EC::average);
        EC::Register(
            EC::Shutdown,
            []()
            {
                if (cheats_bypass_applied && g_ICvar)
                {
                    if (ConVar *sv_cheats = g_ICvar->FindVar("sv_cheats"))
                        silent_convar_set_int(sv_cheats, original_sv_cheats);
                    cheats_bypass_applied = false;
                }
                if (insecure_bypass_applied)
                {
                    if (auto *flag = get_allow_secure_servers_flag())
                        *flag = original_allow_secure_servers;
                    insecure_bypass_applied = false;
                }
                load_white_list_detour.Shutdown();
                host_secure_detour.Shutdown();
            },
            "shutdown_purebypass");
    });
} // namespace hacks::shared::purebypass
