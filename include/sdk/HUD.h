/*
 * HUD.h
 *
 *  Created on: Jun 4, 2017
 *      Author: nullifiedcat
 */

#pragma once

#include <core/logging.hpp>
#include <utlvector.h>
#include "core/vfunc.hpp"
#include "core/vtables.hpp"

class CHudBaseChat
{
public:
    void *vtable;
    inline void Printf(const char *string)
    {
        typedef void (*original_t)(CHudBaseChat *, int, const char *, ...);
        original_t function = vfunc<original_t>(this, vtables::hud_chat::chat_printf);
        function(this, 0, "%s", string);
    }
};

class CHudElement
{
public:
    void *vtable;
};

class CHud
{
public:
    CHudElement *FindElement(const char *name);
    float &GetSensitivityFactor();
};
