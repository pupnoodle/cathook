#include "common.hpp"
#include "MiscTemporary.hpp"
#include "bone_setup.h"
#include "animationlayer.h"
#include "sdk/client_entity.hpp"

static_assert(sizeof(C_AnimationLayer) == 0x2C, "TF2 linux64 C_AnimationLayer is 0x2C");

namespace setupbones_reconst
{
#define MAX_OVERLAYS 15

static settings::Boolean remove_taunts("remove.taunts", "false");

// This is to fix hitboxes while taunting. In most cases. Sometimes these sequences suddenly
// Have normal hitboxes.

// clang-format off
static std::vector<int> ignore_sequences{
    125, 147, 127, 150, 118, 165, 131, 154, 124, 173, 251, 279, 249,
    277, 252, 280, 256, 282, 257, 285, 273, 289, 299, 301, 247, 275,
    303, 347, 308, 354, 298, 334, 313, 359, 311, 357, 338, 383, 343,
    388, 347, 392, 353, 424, 355, 426, 303, 304, 349, 342, 381, 394,
    396, 404, 408, 416, 295, 225, 193, 205, 139, 185, 129, 152, 127,
    309, 223, 171, 198, 209, 138, 183, 184, 137, 207, 196, 307, 150,
    118, 165, 286, 210, 151, 176, 194, 124, 187, 154, 288, 212, 153,
    178, 126, 189, 173, 220, 167, 192, 202, 136, 180, 251, 279, 305,
    243, 204, 182, 222, 249, 277, 221, 168, 241, 203, 181, 257, 285,
    330, 324, 236, 188, 245, 273, 289, 269, 346, 267, 271, 253, 259,
    265, 250, 206, 275, 291, 334, 263, 244, 200, 348, 297, 299, 356,
    256, 344, 208, 301, 358, 258, 247, 239, 228, 314, 322, 237, 389,
    377, 238, 287, 308, 354, 292, 382, 306, 327, 384, 294, 282, 372,
    296, 233, 317, 386, 284, 374, 235, 313, 399, 401, 248, 359, 397,
    385, 246, 332, 428, 412, 360, 343, 388, 435, 337, 329, 417, 274,
    365, 345, 347, 392, 441, 341, 333, 421, 349, 278, 373, 353, 424,
    450, 348, 438, 355, 286, 387, 426, 350, 452, 440, 288, 357, 389,
    281, 334, 336, 351, 430, 439, 448, 342, 344, 130, 212, 280, 282,
    322, 335, 340, 269, 356, 369, 375, 377, 383, 228, 331, 347, 351,
    299, 408, 423, 430, 434
};
//clang-format on

static QAngle YawOnly(QAngle angles)
{
    angles.x = 0;
    angles.z = 0;
    return angles;
}

static Vector PlayerOrigin(IClientEntity *ent)
{
    const Vector abs = re::C_BaseEntity::GetAbsOrigin(ent);
    Vector net{};
    if (netvar.m_vecOrigin)
        net = NET_VECTOR(ent, netvar.m_vecOrigin);
    if (!nolerp && !abs.IsZero())
        return abs;
    if (!net.IsZero())
        return net;
    return abs;
}

static QAngle PlayerAngles(IClientEntity *ent)
{
    if (!nolerp)
        return YawOnly(re::C_BaseEntity::GetAbsAngles(ent));
    if (netvar.m_angRotation)
        return YawOnly(VectorToQAngle(NET_VECTOR(ent, netvar.m_angRotation)));
    return YawOnly(re::C_BaseEntity::GetAbsAngles(ent));
}

static const float *PoseParameters(IClientEntity *ent)
{
    static float dummy[MAXSTUDIOPOSEPARAM];
    if (netvar.m_flPoseParameter)
        return &NET_FLOAT(ent, netvar.m_flPoseParameter);
    return dummy;
}

void GetSkeleton(IClientEntity *ent, CStudioHdr *pStudioHdr, Vector pos[], Quaternion q[], int boneMask)
{
    if (!pStudioHdr)
        return;

    IBoneSetup boneSetup(pStudioHdr, boneMask, PoseParameters(ent));
    boneSetup.InitPose(pos, q);

    if (!pStudioHdr->SequencesAvailable())
        return;

    const int sequence = NET_INT(ent, netvar.m_nSequence);
    if (sequence >= 0 && sequence < pStudioHdr->GetNumSeq())
        boneSetup.AccumulatePose(pos, q, sequence, NET_FLOAT(ent, netvar.m_flCycle), 1.0, g_GlobalVars->curtime, nullptr);

    int overlay_count = 0;
    C_AnimationLayer *layers = nullptr;
    if (netvar.m_AnimOverlay)
    {
        layers        = *reinterpret_cast<C_AnimationLayer **>(uintptr_t(ent) + netvar.m_AnimOverlay);
        overlay_count = *reinterpret_cast<int *>(uintptr_t(ent) + netvar.m_AnimOverlay + 16);
        if (!layers || overlay_count < 0 || overlay_count > MAX_OVERLAYS)
        {
            layers        = nullptr;
            overlay_count = 0;
        }
    }

    int layer[MAX_OVERLAYS];
    int i;
    for (i = 0; i < MAX_OVERLAYS; i++)
        layer[i] = MAX_OVERLAYS;
    for (i = 0; i < overlay_count; i++)
    {
        C_AnimationLayer &pLayer = layers[i];
        const int order          = pLayer.m_nOrder;
        if (order >= 0 && order < MAX_OVERLAYS && layer[order] == MAX_OVERLAYS)
            layer[order] = i;
    }
    for (i = 0; i < MAX_OVERLAYS; i++)
    {
        if (layer[i] < 0 || layer[i] >= overlay_count)
            continue;
        C_AnimationLayer pLayer = layers[layer[i]];
        if (pLayer.m_flWeight <= 0)
            continue;
        const int layer_seq = pLayer.m_nSequence;
        if (layer_seq < 0 || layer_seq >= pStudioHdr->GetNumSeq())
            continue;
        if (!remove_taunts || std::find(ignore_sequences.begin(), ignore_sequences.end(), layer_seq) == ignore_sequences.end())
            boneSetup.AccumulatePose(pos, q, layer_seq, pLayer.m_flCycle, pLayer.m_flWeight, g_GlobalVars->curtime, nullptr);
    }

    CIKContext auto_ik;
    auto_ik.Init(pStudioHdr, PlayerAngles(ent), PlayerOrigin(ent), g_GlobalVars->curtime, 0, boneMask);
    boneSetup.CalcAutoplaySequences(pos, q, g_GlobalVars->curtime, &auto_ik);

    if (netvar.m_flEncodedController)
        boneSetup.CalcBoneAdj(pos, q, &NET_FLOAT(ent, netvar.m_flEncodedController));
}

bool SetupBones(IClientEntity *ent, matrix3x4_t *pBoneToWorld, int boneMask)
{
    if (!ent || !pBoneToWorld || !g_IModelInfo)
        return false;

    const model_t *model = EntGetModel(ent);
    if (!model)
        return false;
    studiohdr_t *raw = g_IModelInfo->GetStudiomodel(model);
    if (!raw)
        return false;

    CStudioHdr studioHdr(raw, g_IMDLCache);
    if (!studioHdr.IsValid())
        return false;

    Vector pos[MAXSTUDIOBONES];
    Quaternion q[MAXSTUDIOBONES];

    const Vector adjOrigin = PlayerOrigin(ent);
    const QAngle angles2   = PlayerAngles(ent);

    GetSkeleton(ent, &studioHdr, pos, q, boneMask);

    float scale = 1.0f;
    if (netvar.m_flModelScale)
        scale = NET_FLOAT(ent, netvar.m_flModelScale);
    if (scale <= 0.0f)
        scale = 1.0f;

    Studio_BuildMatrices(&studioHdr, angles2, adjOrigin, pos, q, -1, scale, pBoneToWorld, boneMask);
    return true;
}
} // namespace setupbones_reconst
