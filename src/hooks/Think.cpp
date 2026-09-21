#include "common.hpp"
#include "hack.hpp"

extern settings::Boolean engine_pred;
namespace hooked_methods
{
void UpdatePred()
{
    if (isHackActive() && g_IEngine->IsInGame() && CE_GOOD(LOCAL_E) && engine_pred && g_IBaseClientState)
    {
        auto cs = g_IBaseClientState;
        if (cs->m_nSignonState() == 6 && cs->m_nDeltaTick() > 0)
            g_IPrediction->Update(cs->m_nDeltaTick() ? cs->m_nDeltaTick() + 1 : 0, cs->m_nDeltaTick() > 0, cs->last_command_ack(), cs->lastoutgoingcommand() + cs->chokedcommands());
    }
}
DEFINE_HOOKED_METHOD(Think, void, IToolFrameworkInternal *_this, bool finaltick)
{
    UpdatePred();
#if ENABLE_TEXTMODE
    hack::PumpEngine();
#endif
    return original::Think(_this, finaltick);
}

} // namespace hooked_methods
