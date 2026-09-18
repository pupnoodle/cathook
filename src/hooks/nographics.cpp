/*
 * nographics.cpp
 *
 *  Created on: Aug 1, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"

#if !ENABLE_TEXTMODE
static settings::Boolean null_graphics("hack.nullgraphics", "false");
#else
static settings::Boolean null_graphics("hack.nullgraphics", "true");
#endif
typedef ITexture *(*FindTexture_t)(void *, const char *, const char *, bool, int);
typedef IMaterial *(*FindMaterialEx_t)(void *, const char *, const char *, int, bool, const char *);
typedef IMaterial *(*FindMaterial_t)(void *, const char *, const char *, bool, const char *);
// 81
FindTexture_t FindTexture_Original;
FindMaterialEx_t FindMaterialEx_Original;
FindMaterial_t FindMaterial_Original;

ITexture *FindTexture_null_hook(void *this_, char const *pTextureName, const char *pTextureGroupName, bool complain, int nAdditionalCreationFlags)
{
    static ITexture *st = FindTexture_Original(this_, pTextureName, pTextureGroupName, complain, nAdditionalCreationFlags);
    return st;
}

// 123
IMaterial *FindMaterialEx_null_hook(void *this_, char const *pMaterialName, const char *pTextureGroupName, int nContext, bool complain, const char *pComplainPrefix)
{
    static IMaterial *st = FindMaterialEx_Original(this_, pMaterialName, pTextureGroupName, nContext, complain, pComplainPrefix);
    return st;
}

// 73
IMaterial *FindMaterial_null_hook(void *this_, char const *pMaterialName, const char *pTextureGroupName, bool complain, const char *pComplainPrefix)
{
    static IMaterial *st = FindMaterial_Original(this_, pMaterialName, pTextureGroupName, complain, pComplainPrefix);
    return st;
}

void ReloadTextures_null_hook(void *this_)
{
}
void ReloadMaterials_null_hook(void *this_, const char *pSubString)
{
}
void ReloadFilesInList_null_hook(void *this_, IFileList *pFilesToReload)
{
}
void NullHook()
{
    g_IMaterialSystem->SetInStubMode(true);
}
void RemoveNullHook()
{
    g_IMaterialSystem->SetInStubMode(false);
}
static CatCommand ApplyNullhook("debug_material_hook", "Debug", []() { NullHook(); });
static CatCommand RemoveNullhook("debug_material_hook_clear", "Debug", []() { RemoveNullHook(); });
static settings::Boolean debug_framerate("debug.framerate", "false");
static float framerate = 0.0f;
static Timer send_timer{};
static InitRoutine init_nographics(
    []()
    {
#if ENABLE_TEXTMODE
        NullHook();
#endif
        EC::Register(
            EC::Paint,
            []()
            {
                if (!*debug_framerate)
                    return;
                framerate = 0.9 * framerate + (1.0 - 0.9) * g_GlobalVars->absoluteframetime;
                if (send_timer.test_and_set(1000))
                    logging::Info("FPS: %f", 1.0f / framerate);
            },
            "material_cm");
    });
static bool blacklist_file(const char *&filename)
{
    const static char *blacklist[] = { ".ani", ".wav", ".mp3", ".vvd", ".vtx", ".vtf", ".vfe", ".cache" /*, ".pcf"*/ };
    if (!filename || !std::strncmp(filename, "materials/console/", 18))
        return false;

    std::size_t len = std::strlen(filename);
    if (len <= 3)
        return false;

    auto ext_p = strrchr(filename, '.');
    if (!ext_p)
        return false;

    if (!std::strcmp(ext_p, ".vmt"))
    {
        /* Not loading it causes extreme console spam */
        if (std::strstr(filename, "corner"))
            return false;
        /* minor console spam */
        if (std::strstr(filename, "hud") || std::strstr(filename, "vgui"))
            return false;

        return true;
    }
    if (std::strstr(filename, "sound.cache") || std::strstr(filename, "tf2_sound") || std::strstr(filename, "game_sounds"))
        return false;
    if (!std::strncmp(filename, "sound/player/footsteps", 22))
        return false;
    if (!std::strcmp(ext_p, ".mdl"))
    {
        return false;
    }
    if (!std::strncmp(filename, "/decal", 6))
        return true;

    for (int i = 0; i < sizeof(blacklist) / sizeof(blacklist[0]); ++i)
        if (!std::strcmp(ext_p, blacklist[i]))
            return true;

    return false;
}

static void *(*FSorig_Open)(void *, const char *, const char *, const char *);
static void *FSHook_Open(void *this_, const char *pFileName, const char *pOptions, const char *pathID)
{
    // fprintf(stderr, "Open: %s\n", pFileName);
    if (blacklist_file(pFileName))
        return nullptr;

    return FSorig_Open(this_, pFileName, pOptions, pathID);
}

static bool (*FSorig_ReadFile)(void *, const char *, const char *, void *, int, int, void *);
static bool FSHook_ReadFile(void *this_, const char *pFileName, const char *pPath, void *buf, int nMaxBytes, int nStartingByte, void *pfnAlloc)
{
    // fprintf(stderr, "ReadFile: %s\n", pFileName);
    if (blacklist_file(pFileName))
        return false;

    return FSorig_ReadFile(this_, pFileName, pPath, buf, nMaxBytes, nStartingByte, pfnAlloc);
}

static void *(*FSorig_OpenEx)(void *, const char *, const char *, unsigned, const char *, char **);
static void *FSHook_OpenEx(void *this_, const char *pFileName, const char *pOptions, unsigned flags, const char *pathID, char **ppszResolvedFilename)
{
    // fprintf(stderr, "OpenEx: %s\n", pFileName);
    if (pFileName && blacklist_file(pFileName))
        return nullptr;

    return FSorig_OpenEx(this_, pFileName, pOptions, flags, pathID, ppszResolvedFilename);
}

static int (*FSorig_ReadFileEx)(void *, const char *, const char *, void **, bool, bool, int, int, void *);
static int FSHook_ReadFileEx(void *this_, const char *pFileName, const char *pPath, void **ppBuf, bool bNullTerminate, bool bOptimalAlloc, int nMaxBytes, int nStartingByte, void *pfnAlloc)
{
    // fprintf(stderr, "ReadFileEx: %s\n", pFileName);
    if (blacklist_file(pFileName))
        return 0;

    return FSorig_ReadFileEx(this_, pFileName, pPath, ppBuf, bNullTerminate, bOptimalAlloc, nMaxBytes, nStartingByte, pfnAlloc);
}

static void (*FSorig_AddFilesToFileCache)(void *, void *, const char **, int, const char *);
static void FSHook_AddFilesToFileCache(void *this_, void *cacheId, const char **ppFileNames, int nFileNames, const char *pPathID)
{
    fprintf(stderr, "AddFilesToFileCache: %d\n", nFileNames);
    for (int i = 0; i < nFileNames; ++i)
        fprintf(stderr, "%s\n", ppFileNames[i]);
}

static int (*FSorig_AsyncReadMultiple)(void *, const char **, int, void *);
static int FSHook_AsyncReadMultiple(void *this_, const char **pRequests, int nRequests, void *phControls)
{
    for (int i = 0; pRequests && i < nRequests; ++i)
    {
        // fprintf(stderr, "AsyncReadMultiple %d %s\n", nRequests, pRequests[i]);
        if (blacklist_file(pRequests[i]))
        {
            if (nRequests > 1)
                fprintf(stderr, "FIXME: blocked AsyncReadMultiple for %d requests due to some filename being blacklisted\n", nRequests);
            /* FSASYNC_ERR_FILEOPEN */
            return -1;
        }
    }
    return FSorig_AsyncReadMultiple(this_, pRequests, nRequests, phControls);
}

static const char *(*FSorig_FindNext)(void *, void *);
static const char *FSHook_FindNext(void *this_, void *handle)
{
    const char *p;
    do
        p = FSorig_FindNext(this_, handle);
    while (p && blacklist_file(p));

    return p;
}

static const char *(*FSorig_FindFirst)(void *, const char *, void **);
static const char *FSHook_FindFirst(void *this_, const char *pWildCard, void **pHandle)
{
    auto p = FSorig_FindFirst(this_, pWildCard, pHandle);
    while (p && blacklist_file(p))
        p = FSorig_FindNext(this_, *pHandle);

    return p;
}

static bool (*FSorig_Precache)(const char *, const char *);
static bool FSHook_Precache(const char *pFileName, const char *pPathID)
{
    return true;
}

static CatCommand debug_invalidate("invalidate_mdl_cache", "Invalidates MDL cache", []() { g_IBaseClient->InvalidateMdlCache(); });

static hooks::VMTHook fs_hook{}, fs_hook2{};
static bool hooked_fs = false;
static void ReduceRamUsage()
{
    if (!hooked_fs)
    {
        /* TO DO: Improves load speeds but doesn't reduce memory usage a lot
         * It seems engine still allocates significant parts without them
         * being really used
         * Plan B: null subsystems (Particle, Material, Model partially, Sound and etc.)
         */
        hooked_fs = true;
        fs_hook.Set(reinterpret_cast<void *>(g_IFileSystem));
        fs_hook.HookMethod(FSHook_FindFirst, 27, &FSorig_FindFirst);
        fs_hook.HookMethod(FSHook_FindNext, 28, &FSorig_FindNext);
        fs_hook.HookMethod(FSHook_AsyncReadMultiple, 37, &FSorig_AsyncReadMultiple);
        fs_hook.HookMethod(FSHook_OpenEx, 69, &FSorig_OpenEx);
        fs_hook.HookMethod(FSHook_ReadFileEx, 71, &FSorig_ReadFileEx);
        fs_hook.HookMethod(FSHook_AddFilesToFileCache, 103, &FSorig_AddFilesToFileCache);
        fs_hook.Apply();

        fs_hook2.Set(reinterpret_cast<void *>(g_IFileSystem), 4);
        fs_hook2.HookMethod(FSHook_Open, 2, &FSorig_Open);
        fs_hook2.HookMethod(FSHook_Precache, 9, &FSorig_Precache);
        fs_hook2.HookMethod(FSHook_ReadFile, 14, &FSorig_ReadFile);
        fs_hook2.Apply();
        /* Might give performance benefit, but mostly fixes annoying console
         * spam related to mdl not being able to play sequence that it
         * cannot play on error.mdl
         */
    }

    if (g_IBaseClient)
    {
        static BytePatch playSequence{ gSignatures.GetClientSignature, sigs::play_sequence, 0x00, { 0xC3 } };
        playSequence.Patch();

        static BytePatch particleCreate{ gSignatures.GetClientSignature, sigs::particle_property_create, 0x00, { 0x31, 0xC0, 0xC3 } };
        static BytePatch particlePrecache{ gSignatures.GetClientSignature, sigs::particle_system_precache, 0x00, { 0x31, 0xC0, 0xC3 } };
        static BytePatch particleCreating{ gSignatures.GetClientSignature, sigs::particle_effect_create_event, 0x00, { 0x31, 0xC0, 0xC3 } };
        particleCreate.Patch();
        particlePrecache.Patch();
        particleCreating.Patch();
    }
}

static void UnHookFs()
{
    fs_hook.Release();
    fs_hook2.Release();
    hooked_fs = false;
    if (g_IBaseClient)
        g_IBaseClient->InvalidateMdlCache();
}

#if ENABLE_TEXTMODE
static InitRoutineEarly nullify_textmode(
    []()
    {
        // SDL_CreateWindow has a "flag" parameter. We simply give it HIDDEN as a flag
        // 0x8 = SDL_HIDDEN
        static BytePatch patch1(gSignatures.GetLauncherSignature, "C7 43 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24", 0xb, { 0x8 });

        // all are the same size so use same patch for all
        std::vector<unsigned char> patch_arr = { 0x90, 0x90, 0x90, 0x90, 0x90 };

        // Hide the SDL window
        static BytePatch patch2(gSignatures.GetLauncherSignature, "E8 ? ? ? ? C6 43 25 01 83 C4 5C", 0x0, patch_arr);
        static BytePatch patch3(gSignatures.GetLauncherSignature, "E8 ? ? ? ? 8B 43 14 89 04 24 E8 ? ? ? ? C6 43 25 01 83 C4 1C", 0x0, patch_arr);
        static BytePatch patch4(gSignatures.GetLauncherSignature, "89 14 24 E8 ? ? ? ? 8B 45 B4", 0x3, patch_arr);

        ReduceRamUsage();
        static BytePatch patch5(gSignatures.GetEngineSignature(sigs::video_mode_setup_startup_graphic), { 0xC3 });
        static BytePatch patch6(gSignatures.GetEngineSignature, sigs::material_system_swap_buffers, 0x0, { 0x31, 0xC0, 0x40, 0xC3 });
        static BytePatch patch7(gSignatures.GetEngineSignature, sigs::v_render_view, 0x0, { 0xC3 });

        patch1.Patch();
        patch2.Patch();
        patch3.Patch();
        patch4.Patch();
        patch5.Patch();
        patch6.Patch();
        patch7.Patch();
    });
#endif

static Timer signon_timer;
static InitRoutine nullifiy_textmode2(
    []()
    {
#if ENABLE_TEXTMODE
        ReduceRamUsage();
#endif
        null_graphics.installChangeCallback(
            [](settings::VariableBase<bool> &, bool after)
            {
                if (after)
                    ReduceRamUsage();
                else
                    UnHookFs();
            });
#if ENABLE_TEXTMODE
        // Catbots still hit properly, this just makes it easier to Stub stuff not needed in textmode
        uintptr_t g_bTextMode_ptrptr = gSignatures.GetEngineSignature("A2 ? ? ? ? 8B 43 04") + 0x1;

        BytePatch::mprotectAddr(g_bTextMode_ptrptr, 4, PROT_READ | PROT_WRITE | PROT_EXEC);
        BytePatch::mprotectAddr(*(uintptr_t *) g_bTextMode_ptrptr, 4, PROT_READ | PROT_WRITE | PROT_EXEC);
        BytePatch::mprotectAddr(**(uintptr_t **) g_bTextMode_ptrptr, 4, PROT_READ | PROT_WRITE | PROT_EXEC);

        bool *g_bTextMode_ptr = *((bool **) g_bTextMode_ptrptr);
        *g_bTextMode_ptr      = true;
        // Skip downloading ressources
        static BytePatch patch1(gSignatures.GetEngineSignature, "0F 85 ? ? ? ? A1 ? ? ? ? 8D 8B ? ? ? ?", 0x1, { 0x81 });
        patch1.Patch();
        // CViewRender::Render
        static BytePatch patch2(gSignatures.GetClientSignature, sigs::view_render_render, 0x0, { 0x31, 0xC0, 0x40, 0xC3 });
        patch2.Patch();
#endif
    });
