#include "common.hpp"
#include "hack.hpp"
#include "HookedMethods.hpp"

namespace hooked_methods
{
DEFINE_HOOKED_METHOD(FrameStageNotify, void, void *this_, ClientFrameStage_t stage)
{
    if (!isHackActive())
        return original::FrameStageNotify(this_, stage);

    if (stage == FRAME_START)
        hack::PumpEngine();

    return original::FrameStageNotify(this_, stage);
}
} // namespace hooked_methods
