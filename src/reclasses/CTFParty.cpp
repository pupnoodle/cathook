/*
 * CTFParty.cpp
 *
 *  Created on: Dec 7, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"

re::CTFParty *re::CTFParty::GetParty()
{
    auto *gc = re::CTFGCClientSystem::GTFGCClientSystem();
    return gc ? gc->GetParty() : nullptr;
}
