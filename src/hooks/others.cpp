/*
 * others.cpp
 *
 *  Created on: Jan 8, 2017
 *      Author: nullifiedcat
 *
 */

#include "common.hpp"
#include "hack.hpp"
#include "hitrate.hpp"
#include "chatlog.hpp"
#include "sdk/netmessage.hpp"
#include <boost/algorithm/string.hpp>
#include <MiscTemporary.hpp>
#include "HookedMethods.hpp"
#if ENABLE_VISUALS

float last_say = 0.0f;

CatCommand spectate("spectate", "Spectate", [](const CCommand &args) {
    if (args.ArgC() < 2)
    {
        spectator_target = 0;
        return;
    }
    int id;
    try
    {
        id = std::stoi(args.Arg(1));
    }
    catch (const std::exception &e)
    {
        logging::Info("Error while setting spectate target. Error: %s", e.what());
        id = 0;
    }
    if (!id)
        spectator_target = 0;
    else
    {
        spectator_target = GetPlayerForUserID(id);
    }
});

#endif
