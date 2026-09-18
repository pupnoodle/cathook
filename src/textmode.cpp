/*
 * textmode.cpp
 *
 *  Created on: Jul 28, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"

bool *allowSecureServers{ nullptr };

// valve pls no ban

void EXPOSED_Epic_VACBypass_1337_DoNotSteal_xXx_$1_xXx_MLG()
{
    logging::Info("VAC bypass is disabled");
}

CatCommand fixvac("fixvac", "Lemme in to secure servers", []() { EXPOSED_Epic_VACBypass_1337_DoNotSteal_xXx_$1_xXx_MLG(); });

static InitRoutine init_textmode([]() {
#if ENABLE_TEXTMODE_STDIN
    logging::Info("[TEXTMODE] Setting up input handling");
    int flags = fcntl(0, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(0, F_SETFL, flags);
    logging::Info("[TEXTMODE] stdin is now non-blocking");
#endif
#if ENABLE_VAC_BYPASS
    EXPOSED_Epic_VACBypass_1337_DoNotSteal_xXx_$1_xXx_MLG();
#endif
});

#if ENABLE_TEXTMODE_STDIN
void UpdateInput()
{
    char buffer[256];
    int bytes = read(0, buffer, 255);
    if (bytes > 0)
    {
        buffer[bytes] = '\0';
        g_IEngine->ExecuteClientCmd(buffer);
    }
}
#endif
