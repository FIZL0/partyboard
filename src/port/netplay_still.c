/*
 * Still frame for lockstep WAITs (N19). While netplay_tick_begin waits for a peer, main.c runs no game tick,
 * but RmlUi (toasts, the "Waiting for <name>" overlay) must still be drawn, and that needs an aurora frame.
 * aurora clears the EFB at the start of every frame and has no call to present the last one again, so at
 * the end of every lockstep tick the EFB is copied to a texture (GXCopyTex, as WipeCrossFade does), and a
 * WAIT frame draws that copy over the whole EFB (as WipeFrameStill does) before RmlUi goes on top.
 *
 * Only GX state and a host-heap buffer are touched: no game heap, nothing netplay_hash() looks at.
 */

#include "dolphin.h"
#include "game/init.h"
#include "port/netplay.h"

#include <stdlib.h>

static void *still_data;
static u16 still_w;
static u16 still_h;
static BOOL still_kept;

void netplay_still_keep(void)
{
    u16 w = RenderMode->fbWidth;
    u16 h = RenderMode->efbHeight;
    u32 size = GXGetTexBufferSize(w, h, GX_TF_RGB565, GX_FALSE, 0);

    if (still_data == NULL || w != still_w || h != still_h) {
        /* aurora keys the copy by this pointer; the zeroed bytes are only read (as black) if its copy
         * cache was dropped, so they must cover the whole texture. */
        free(still_data);
        still_data = calloc(1, size);
        still_w = w;
        still_h = h;
        still_kept = FALSE;
        if (still_data == NULL) {
            return;
        }
    }
    GXSetTexCopySrc(0, 0, w, h);
    GXSetTexCopyDst(w, h, GX_TF_RGB565, GX_FALSE);
    GXCopyTex(still_data, GX_FALSE);
    still_kept = TRUE;
}

void netplay_still_draw(void)
{
    GXTexObj tex;
    static GXColor white = { 0xFF, 0xFF, 0xFF, 0xFF };
    Mtx44 proj;
    Mtx modelview;
    f32 old_proj[7];
    f32 old_vp[6];
    u32 old_sc[4];

    if (!still_kept) {
        return;
    }
    /* The game sets what it draws with, but give back the camera state a later tick may not set again. */
    GXGetProjectionv(old_proj);
    GXGetViewportv(old_vp);
    GXGetScissor(&old_sc[0], &old_sc[1], &old_sc[2], &old_sc[3]);

    MTXOrtho(proj, 0, still_h, 0, still_w, 0, 10);
    GXSetProjection(proj, GX_ORTHOGRAPHIC);
    MTXIdentity(modelview);
    GXLoadPosMtxImm(modelview, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetViewport(0, 0, still_w, still_h, 0, 1);
    GXSetScissor(0, 0, still_w, still_h);
    GXClearVtxDesc();
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 1, GX_DF_CLAMP, GX_AF_SPOT);
    GXSetChanAmbColor(GX_COLOR0A0, white);
    GXSetChanMatColor(GX_COLOR0A0, white);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumIndStages(0);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXInitTexObj(&tex, still_data, still_w, still_h, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&tex, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&tex, GX_TEXMAP0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetFog(GX_FOG_NONE, 0, 0, 0, 0, white);
    GXSetCullMode(GX_CULL_NONE);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(0, 0);
    GXTexCoord2f32(0, 0);
    GXPosition2u16(still_w, 0);
    GXTexCoord2f32(1, 0);
    GXPosition2u16(still_w, still_h);
    GXTexCoord2f32(1, 1);
    GXPosition2u16(0, still_h);
    GXTexCoord2f32(0, 1);
    GXEnd();

    GXSetProjectionv(old_proj);
    GXSetViewport(old_vp[0], old_vp[1], old_vp[2], old_vp[3], old_vp[4], old_vp[5]);
    GXSetScissor(old_sc[0], old_sc[1], old_sc[2], old_sc[3]);
}

void netplay_still_drop(void)
{
    free(still_data);
    still_data = NULL;
    still_kept = FALSE;
}
