//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $Workfile:     $
// $NoKeywords: $
//===========================================================================//

#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
#include "tier1/KeyValues.h"

#include "common.hpp"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Little utility class to deal with material references
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
CMaterialReference::CMaterialReference(char const *pMaterialName, const char *pTextureGroupName, bool bComplain) : m_pMaterial(0)
{
    if (pMaterialName)
    {
        Assert(pTextureGroupName);
        Init(pMaterialName, pTextureGroupName, bComplain);
    }
}

CMaterialReference::~CMaterialReference()
{
    Shutdown();
}

//-----------------------------------------------------------------------------
// Attach to a material
//-----------------------------------------------------------------------------
void CMaterialReference::Init(char const *pMaterialName, const char *pTextureGroupName, bool bComplain)
{
    CMaterial *found = g_IMaterialSystem->FindMaterial(pMaterialName, pTextureGroupName, bComplain);
    IMaterial *pMaterial = found ? found->AsIMaterial() : nullptr;
    if (!found || found->IsErrorMaterial())
    {
        if (IsOSX())
        {
            printf("\n ##### CMaterialReference::Init got error material for "
                   "%s in tex group %s",
                   pMaterialName, pTextureGroupName);
        }
    }

    Assert(pMaterial);
    Init(pMaterial);
}

void CMaterialReference::Init(const char *pMaterialName, KeyValues *pVMTKeyValues)
{
    // CreateMaterial has a refcount of 1
    Shutdown();
    CMaterial *created = g_IMaterialSystem->CreateMaterial(pMaterialName, pVMTKeyValues);
    m_pMaterial        = created ? created->AsIMaterial() : nullptr;
}

void CMaterialReference::Init(const char *pMaterialName, const char *pTextureGroupName, KeyValues *pVMTKeyValues)
{
    CMaterial *pMaterial = g_IMaterialSystem->FindProceduralMaterial(pMaterialName, pTextureGroupName, pVMTKeyValues);
    Assert(pMaterial);
    Init(pMaterial ? pMaterial->AsIMaterial() : nullptr);
}

void CMaterialReference::Init(IMaterial *pMaterial)
{
    if (m_pMaterial != pMaterial)
    {
        Shutdown();
        m_pMaterial = pMaterial;
        if (m_pMaterial)
        {
            LiveMaterial(m_pMaterial)->IncrementReferenceCount();
        }
    }
}

void CMaterialReference::Init(CMaterialReference &ref)
{
    if (m_pMaterial != ref.m_pMaterial)
    {
        Shutdown();
        m_pMaterial = ref.m_pMaterial;
        if (m_pMaterial)
        {
            LiveMaterial(m_pMaterial)->IncrementReferenceCount();
        }
    }
}

//-----------------------------------------------------------------------------
// Detach from a material
//-----------------------------------------------------------------------------
void CMaterialReference::Shutdown()
{
    if (m_pMaterial && g_IMaterialSystem)
    {
        LiveMaterial(m_pMaterial)->DecrementReferenceCount();
        m_pMaterial = NULL;
    }
}

//-----------------------------------------------------------------------------
// Little utility class to deal with texture references
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
CTextureReference::CTextureReference() : m_pTexture(NULL)
{
}

CTextureReference::CTextureReference(const CTextureReference &ref)
{
    m_pTexture = ref.m_pTexture;
    if (m_pTexture)
    {
        LiveTexture(m_pTexture)->IncrementReferenceCount();
    }
}

void CTextureReference::operator=(CTextureReference &ref)
{
    m_pTexture = ref.m_pTexture;
    if (m_pTexture)
    {
        LiveTexture(m_pTexture)->IncrementReferenceCount();
    }
}

CTextureReference::~CTextureReference()
{
    Shutdown();
}

//-----------------------------------------------------------------------------
// Attach to a texture
//-----------------------------------------------------------------------------
void CTextureReference::Init(char const *pTextureName, const char *pTextureGroupName, bool bComplain)
{
    Shutdown();
    CTexture *found = g_IMaterialSystem->FindTexture(pTextureName, pTextureGroupName, bComplain);
    m_pTexture      = found ? found->AsITexture() : nullptr;
    if (m_pTexture)
    {
        LiveTexture(m_pTexture)->IncrementReferenceCount();
    }
}

void CTextureReference::Init(ITexture *pTexture)
{
    Shutdown();

    m_pTexture = pTexture;
    if (m_pTexture)
    {
        LiveTexture(m_pTexture)->IncrementReferenceCount();
    }
}

void CTextureReference::InitProceduralTexture(const char *pTextureName, const char *pTextureGroupName, int w, int h, ImageFormat fmt, int nFlags)
{
    Shutdown();

    CTexture *created = g_IMaterialSystem->CreateProceduralTexture(pTextureName, pTextureGroupName, w, h, fmt, nFlags);
    m_pTexture        = created ? created->AsITexture() : nullptr;
    // NOTE: The texture reference is already incremented internally above!
    /*
    if ( m_pTexture )
    {
        m_pTexture->IncrementReferenceCount();
    }
    */
}

void CTextureReference::InitRenderTarget(int w, int h, RenderTargetSizeMode_t sizeMode, ImageFormat fmt, MaterialRenderTargetDepth_t depth, bool bHDR, char *pStrOptionalName /* = NULL */)
{
    Shutdown();

    int textureFlags = TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT;
    if (depth == MATERIAL_RT_DEPTH_ONLY)
        textureFlags |= TEXTUREFLAGS_POINTSAMPLE;

    int renderTargetFlags = bHDR ? CREATERENDERTARGETFLAGS_HDR : 0;

    // NOTE: Refcount returned by CreateRenderTargetTexture is 1
    // m_pTexture = vfunc<ITexture*(*)(IMaterialSystem*, char
    // const*,int,int,RenderTargetSizeMode_t,ImageFormat,MaterialRenderTargetDepth_t,uint,uint)>(materials,
    // 87, 0)(materials, pStrOptionalName, w, h, sizeMode, fmt, 		depth,
    // textureFlags, renderTargetFlags);
    CTexture *rt = g_IMaterialSystem->CreateNamedRenderTargetTextureEx(pStrOptionalName, w, h, sizeMode, fmt, depth, textureFlags, renderTargetFlags);
    m_pTexture   = rt ? rt->AsITexture() : nullptr;

    Assert(m_pTexture);
}

//-----------------------------------------------------------------------------
// Detach from a texture
//-----------------------------------------------------------------------------
void CTextureReference::Shutdown(bool bDeleteIfUnReferenced)
{
    if (m_pTexture && g_IMaterialSystem)
    {
        LiveTexture(m_pTexture)->DecrementReferenceCount();
        if (bDeleteIfUnReferenced)
        {
            m_pTexture->DeleteIfUnreferenced();
        }
        m_pTexture = NULL;
    }
}

//-----------------------------------------------------------------------------
// Builds ONLY the system ram render target. Used when caller is explicitly
// managing. The paired EDRAM surface can be built in an alternate format.
//-----------------------------------------------------------------------------
#if defined(_X360)
void CTextureReference::InitRenderTargetTexture(int w, int h, RenderTargetSizeMode_t sizeMode, ImageFormat fmt, MaterialRenderTargetDepth_t depth, bool bHDR, char *pStrOptionalName)
{
    // other variants not implemented yet
    Assert(depth == MATERIAL_RT_DEPTH_NONE || depth == MATERIAL_RT_DEPTH_SHARED);
    Assert(!bHDR);

    int renderTargetFlags = CREATERENDERTARGETFLAGS_NOEDRAM;

    m_pTexture = materials->CreateNamedRenderTargetTextureEx(pStrOptionalName, w, h, sizeMode, fmt, depth, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, renderTargetFlags);
    Assert(m_pTexture);
}
#endif

//-----------------------------------------------------------------------------
// Builds ONLY the EDRAM render target surface. Used when caller is explicitly
// managing. The paired system memory texture can be built in an alternate
// format.
//-----------------------------------------------------------------------------
#if defined(_X360)
void CTextureReference::InitRenderTargetSurface(int width, int height, ImageFormat fmt, bool bSameAsTexture)
{
    // texture has to be created first
    Assert(m_pTexture && m_pTexture->IsRenderTarget());

    m_pTexture->CreateRenderTargetSurface(width, height, fmt, bSameAsTexture);
}
#endif
