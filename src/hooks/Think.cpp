#include "HookedMethods.hpp"

extern settings::Boolean engine_pred;
namespace hooked_methods
{
void UpdatePred()
{
    if (isHackActive() && g_IEngine->IsInGame() && CE_GOOD(LOCAL_E) && engine_pred && g_IBaseClientState)
    {
        auto cs                    = uintptr_t(g_IBaseClientState);
        int signon_state           = *reinterpret_cast<int *>(cs + offsets::m_nSignonState());
        int m_nDeltaTick           = *reinterpret_cast<int *>(cs + offsets::m_nDeltaTick());
        int lastoutgoingcommand    = *reinterpret_cast<int *>(cs + offsets::lastoutgoingcommand());
        int chokedcommands         = *reinterpret_cast<int *>(cs + offsets::lastoutgoingcommand() + 4);
        int last_command_ack       = *reinterpret_cast<int *>(cs + offsets::lastoutgoingcommand() + 8);

        if (signon_state == 6 && m_nDeltaTick > 0)
            g_IPrediction->Update(m_nDeltaTick ? m_nDeltaTick + 1 : 0, m_nDeltaTick > 0, last_command_ack, lastoutgoingcommand + chokedcommands);
    }
}
DEFINE_HOOKED_METHOD(Think, void, IToolFrameworkInternal *_this, bool finaltick)
{
    UpdatePred();
    return original::Think(_this, finaltick);
}

} // namespace hooked_methods
