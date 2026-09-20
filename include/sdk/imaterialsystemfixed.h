#pragma once

#include "core/vfunc.hpp"
#include "core/vtables.hpp"
#include "materialsystem/imaterialsystem.h"
#include "sdk/material.hpp"
#include "sdk/texture.hpp"
#include "sdk/mat_render_context.hpp"

class KeyValues;

class IMaterialSystemFixed
{
public:
    void SetInStubMode(bool enabled)
    {
        vfunc<void (*)(IMaterialSystemFixed *, bool)>(this, vtables::material_system::set_in_stub_mode)(this, enabled);
    }
    CMaterial *CreateMaterial(const char *name, KeyValues *keyValues)
    {
        return vfunc<CMaterial *(*)(IMaterialSystemFixed *, const char *, KeyValues *)>(this, vtables::material_system::create_material)(this, name, keyValues);
    }
    CMaterial *FindMaterial(const char *name, const char *textureGroupName, bool complain = true, const char *complainPrefix = nullptr)
    {
        return vfunc<CMaterial *(*)(IMaterialSystemFixed *, const char *, const char *, bool, const char *)>(this, vtables::material_system::find_material)(this, name, textureGroupName, complain, complainPrefix);
    }
    MaterialHandle_t FirstMaterial()
    {
        return vfunc<MaterialHandle_t (*)(IMaterialSystemFixed *)>(this, vtables::material_system::first_material)(this);
    }
    MaterialHandle_t NextMaterial(MaterialHandle_t handle)
    {
        return vfunc<MaterialHandle_t (*)(IMaterialSystemFixed *, MaterialHandle_t)>(this, vtables::material_system::next_material)(this, handle);
    }
    MaterialHandle_t InvalidMaterial()
    {
        return vfunc<MaterialHandle_t (*)(IMaterialSystemFixed *)>(this, vtables::material_system::invalid_material)(this);
    }
    CMaterial *GetMaterial(MaterialHandle_t handle)
    {
        return vfunc<CMaterial *(*)(IMaterialSystemFixed *, MaterialHandle_t)>(this, vtables::material_system::get_material)(this, handle);
    }
    CTexture *FindTexture(const char *name, const char *textureGroupName, bool complain = true, int additionalCreationFlags = 0)
    {
        return vfunc<CTexture *(*)(IMaterialSystemFixed *, const char *, const char *, bool, int)>(this, vtables::material_system::find_texture)(this, name, textureGroupName, complain, additionalCreationFlags);
    }
    CTexture *CreateProceduralTexture(const char *name, const char *textureGroupName, int w, int h, ImageFormat fmt, int nFlags)
    {
        using fn_t = CTexture *(*)(IMaterialSystemFixed *, const char *, const char *, int, int, ImageFormat, int);
        return vfunc<fn_t>(this, vtables::material_system::create_procedural_texture)(this, name, textureGroupName, w, h, fmt, nFlags);
    }
    void BeginRenderTargetAllocation()
    {
        vfunc<void (*)(IMaterialSystemFixed *)>(this, vtables::material_system::begin_render_target_allocation)(this);
    }
    void EndRenderTargetAllocation()
    {
        vfunc<void (*)(IMaterialSystemFixed *)>(this, vtables::material_system::end_render_target_allocation)(this);
    }
    CTexture *CreateNamedRenderTargetTextureEx(const char *name, int w, int h, RenderTargetSizeMode_t sizeMode, ImageFormat format, MaterialRenderTargetDepth_t depth, unsigned int textureFlags, unsigned int renderTargetFlags)
    {
        using fn_t = CTexture *(*)(IMaterialSystemFixed *, const char *, int, int, RenderTargetSizeMode_t, ImageFormat, MaterialRenderTargetDepth_t, unsigned int, unsigned int);
        return vfunc<fn_t>(this, vtables::material_system::create_named_render_target_texture_ex)(this, name, w, h, sizeMode, format, depth, textureFlags, renderTargetFlags);
    }
    CMatRenderContext *GetRenderContext()
    {
        return vfunc<CMatRenderContext *(*)(IMaterialSystemFixed *)>(this, vtables::material_system::get_render_context)(this);
    }
    CMaterial *FindProceduralMaterial(const char *name, const char *textureGroupName, KeyValues *keyValues)
    {
        return vfunc<CMaterial *(*)(IMaterialSystemFixed *, const char *, const char *, KeyValues *)>(this, vtables::material_system::find_procedural_material)(this, name, textureGroupName, keyValues);
    }
    void OverrideRenderTargetAllocation(bool rtAlloc)
    {
        vfunc<void (*)(IMaterialSystemFixed *, bool)>(this, vtables::material_system::override_render_target_allocation)(this, rtAlloc);
    }
};
