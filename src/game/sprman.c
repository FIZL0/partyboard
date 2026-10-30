#include "game/sprite.h"
#include "game/disp.h"
#include "game/memory.h"
#include "game/init.h"

#include "dolphin/mtx.h"

#ifndef __MWERKS__
#include "game/hu3d.h"
#endif

#ifdef TARGET_PC
#include "port/byteswap.h"
#include "port/button_art.h"
#include <string.h>
#endif

#define SPRITE_DIRTY_ATTR 0x1
#define SPRITE_DIRTY_XFORM 0x2
#define SPRITE_DIRTY_COLOR 0x4

typedef struct sprite_order {
    u16 group;
    u16 sprite;
    u16 prio;
    u16 next;
} SpriteOrder;

SHARED_SYM HUSPRITE HuSprData[HUSPR_MAX];
SHARED_SYM HUSPRGRP HuSprGrpData[HUSPR_GRP_MAX];
static SpriteOrder HuSprOrder[HUSPR_MAX*2];

static s16 HuSprOrderNum;
static s16 HuSprOrderNo;
static BOOL HuSprPauseF;

static void HuSprOrderEntry(s16 group, s16 sprite);


void HuSprInit(void)
{
    s16 i;
    HUSPRITE *sprite;
    HUSPRGRP *group;
    for(sprite = &HuSprData[1], i=1; i<HUSPR_MAX; i++, sprite++) {
        sprite->data = NULL;
    }
    for(group = HuSprGrpData, i=0; i<HUSPR_GRP_MAX; i++, group++) {
        group->capacity = 0;
    }
    sprite = &HuSprData[0];
    sprite->prio = 0;
    sprite->data = (void *)1;
    HuSprPauseF = FALSE;
}

void HuSprClose(void)
{
    s16 i;
    HUSPRGRP *group;
    HUSPRITE *sprite;
    
    for(group = HuSprGrpData, i=0; i<HUSPR_GRP_MAX; i++, group++) {
        if(group->capacity != 0) {
            HuSprGrpKill(i);
        }
    }
    for(sprite = &HuSprData[1], i=1; i<HUSPR_MAX; i++, sprite++) {
        if(sprite->data) {
            HuSprKill(i);
        }
    }
    HuSprPauseF = FALSE;
}

void HuSprExec(s16 draw_no)
{
    HUSPRITE *sprite;
    while(sprite = HuSprCall()) {
        if(!(sprite->attr & HUSPR_ATTR_DISPOFF) && sprite->drawNo == draw_no) {
            HuSprDisp(sprite);
        }
    }
}

void HuSprBegin(void)
{
    Mtx temp, rot;
    s16 i, j;
    Vec axis = {0, 0, 1};
    HUSPRGRP *group;
    group = HuSprGrpData;
    HuSprOrderNum = 1;
    HuSprOrder[0].next = 0;
    HuSprOrder[0].prio = -1;
    for(i=0; i<HUSPR_GRP_MAX; i++, group++) {
        if(group->capacity != 0) {
            MTXTrans(temp, group->center.x*group->scale.x, group->center.y*group->scale.y, 0.0f);
            MTXRotAxisDeg(rot, &axis, group->zRot);
            MTXConcat(rot, temp, group->mtx);
            MTXScale(temp, group->scale.x, group->scale.y, 1.0f);
            MTXConcat(group->mtx, temp, group->mtx);
            mtxTransCat(group->mtx, group->pos.x, group->pos.y, 0);
            for(j=0; j<group->capacity; j++) {
                if(group->members[j] != -1) {
                    HuSprOrderEntry(i, group->members[j]);
                }
            }
        }
    }
    HuSprOrderNo = 0;
}

static void HuSprOrderEntry(s16 group, s16 sprite)
{
    SpriteOrder *order = &HuSprOrder[HuSprOrderNum];
    s16 prio = HuSprData[sprite].prio;
    s16 prev, next;
    if(HuSprOrderNum >= HUSPR_MAX*2) {
        OSReport("Order Max Over!\n");
        return;
    }
    next = HuSprOrder[0].next;
    for(prev = 0; next != 0; prev = next, next = HuSprOrder[next].next) {
        if(HuSprOrder[next].prio < prio) {
            break;
        }
    }
    order->next = HuSprOrder[prev].next;
    HuSprOrder[prev].next = HuSprOrderNum;
    order->prio = prio;
    order->group = group;
    order->sprite = sprite;
    HuSprOrderNum++;
}

HUSPRITE *HuSprCall(void)
{
    HuSprOrderNo = HuSprOrder[HuSprOrderNo].next;
    if(HuSprOrderNo != 0) {
        SpriteOrder *order = &HuSprOrder[HuSprOrderNo];
        HUSPRITE *sprite = &HuSprData[order->sprite];
        sprite->groupMtx = &HuSprGrpData[order->group].mtx;
        if(sprite->attr & HUSPR_ATTR_FUNC) {
            return sprite;
        }
        sprite->frameP = &sprite->data->bank[sprite->bank].frame[sprite->animNo];
        sprite->patP = &sprite->data->pat[sprite->frameP->pat];
        return sprite;
    } else {
        return NULL;
    }
}

static inline void SpriteCalcFrame(HUSPRITE *sprite, ANIMBANK *bank, ANIMFRAME **frame, s16 loop)
{
    if(sprite->time >= (*frame)->time) {
        sprite->animNo++;
        sprite->time -= (*frame)->time;
        if(sprite->animNo >= bank->timeNum || (*frame)[1].time == -1) {
            if(loop) {
                sprite->animNo = 0;
            } else {
                sprite->animNo = bank->timeNum-1;
            }
        }
        *frame = &bank->frame[sprite->animNo];
    } else if(sprite->time < 0) {
        sprite->animNo--;
        if(sprite->animNo < 0) {
            if(loop) {
                sprite->animNo = bank->timeNum-1;
            } else {
                sprite->animNo = 0;
            }
        }
        *frame = &bank->frame[sprite->animNo];
        sprite->time += (*frame)->time;
    }
}

void HuSprFinish(void)
{
    ANIMDATA *anim;
    ANIMBANK *bank;
    ANIMFRAME *frame;
    HUSPRITE *sprite;
    s16 i;
    s16 j;
    s16 loop;
    s16 dir;
    
    for(sprite = &HuSprData[1], i=1; i<HUSPR_MAX; i++, sprite++) {
        if(sprite->data && !(sprite->attr & HUSPR_ATTR_FUNC)) {
            if(!HuSprPauseF || (sprite->attr & HUSPR_ATTR_NOPAUSE)) {
                anim = sprite->data;
                bank = &anim->bank[sprite->bank];
                frame = &bank->frame[sprite->animNo];
                loop = (sprite->attr & HUSPR_ATTR_LOOP) ? 0 : 1;
                if(!(sprite->attr & HUSPR_ATTR_NOANIM)) {
                    dir = (sprite->attr & HUSPR_ATTR_REVERSE) ? -1 : 1;
                    for(j=0; j<(s32)sprite->speed*minimumVcount; j++) {
                        sprite->time += dir;
                        SpriteCalcFrame(sprite, bank, &frame, loop);
                    }
                    sprite->time += (sprite->speed*(float)minimumVcount)-j;
                    SpriteCalcFrame(sprite, bank, &frame, loop);
                }
                sprite->dirty = 0;
            }
        }
    }
}

void HuSprPauseSet(BOOL value)
{
    HuSprPauseF = value;
}

ANIMDATA *HuSprAnimRead(void *data)
{
    s16 i;
    ANIMBMP *bmp;
    ANIMBANK *bank;
    ANIMPAT *pat;

    ANIMDATA *anim = data;
#ifdef BYTESWAPPING
    s16 j;
    uintptr_t num;
    if (anim->valid == ANIM_DATA_ALLOCATION_VALID) {
        anim->useNum++;
        return anim;
    }
    if (((u32 *)data)[2] == ANIM_DATA_ALLOCATION_VALID) {
        /* this file block was read before: same as GC's second read of it, the same anim once more */
        memcpy(&anim, (u8 *)data + 12, sizeof(anim));
        anim->useNum++;
        return anim;
    }
    /* On GC the anim is the file block and goes when its num is freed (omOvlKill: an overlay's anims a particle or
     * the overlay itself never killed). The converted one goes with it (N57: a minigame left mid-play kept them). */
    num = HuMemDirectNumGet(data);
    anim = HuMemDirectMallocNum(HEAP_DATA, sizeof(ANIMDATA), num);
    byteswap_animdata(data, anim);
    anim->valid = ANIM_DATA_ALLOCATION_VALID;
    anim->raw = data;
#else
    if((uintptr_t)anim->bank & ~0xFFFF) {
        anim->useNum++;
        return anim;
    }
#endif
    bank = (void *)((uintptr_t)anim->bank+(uintptr_t)data);
#ifdef BYTESWAPPING
    bank = HuMemDirectMallocNum(HEAP_DATA, anim->bankNum * sizeof(ANIMBANK), num);
    for(i=0; i<anim->bankNum; i++) {
        byteswap_animbankdata(&((AnimBankData32b*)((uintptr_t)anim->bank+(uintptr_t)data))[i], &bank[i]);
    }
#endif
    anim->bank = bank;
    pat = (void *)((uintptr_t)anim->pat+(uintptr_t)data);
#ifdef BYTESWAPPING
    pat = HuMemDirectMallocNum(HEAP_DATA, anim->patNum * sizeof(ANIMPAT), num);
    for(i=0; i<anim->patNum; i++) {
        byteswap_animpatdata(&((AnimPatData32b*)((uintptr_t)anim->pat+(uintptr_t)data))[i], &pat[i]);
    }
#endif
    anim->pat = pat;
    bmp = (void *)((uintptr_t)anim->bmp+(uintptr_t)data);
#ifdef BYTESWAPPING
    bmp = HuMemDirectMallocNum(HEAP_DATA, anim->bmpNum * sizeof(ANIMBMP), num);
    for(i=0; i<anim->bmpNum; i++) {
        byteswap_animbmpdata(&((AnimBmpData32b*)((uintptr_t)anim->bmp+(uintptr_t)data))[i], &bmp[i]);
#ifdef OPTIMIZED_TEXTURE_LOADING
        for (j = 0; j < sizeof(anim->bmp[0].texData) / sizeof(anim->bmp[0].texData[0]); j++) {
            AnimTexData *tex = &bmp[i].texData[j];
            tex->tex_initialized = FALSE;
            tex->tlut_initialized = FALSE;
            memset(&tex->tex_obj, 0, sizeof(tex->tex_obj));
            memset(&tex->tlut_obj, 0, sizeof(tex->tlut_obj));
        }
#endif
    }
#endif
    anim->bmp = bmp;
    for(i=0; i<anim->bankNum; i++, bank++) {
        bank->frame = (ANIMFRAME *)((uintptr_t)bank->frame+(uintptr_t)data);
#ifdef BYTESWAPPING
        for (j = 0; j < bank->timeNum; j++) {
            byteswap_animframedata(&bank->frame[j]);
        }
#endif
    }
    for(i=0; i<anim->patNum; i++, pat++) {
        pat->layer = (ANIMLAYER *)((uintptr_t)pat->layer+(uintptr_t)data);
#ifdef BYTESWAPPING
        for (j = 0; j < pat->layerNum; j++) {
            byteswap_animlayerdata(&pat->layer[j]);
        }
#endif
    }
    for(i=0; i<anim->bmpNum; i++, bmp++) {
        bmp->palData = (void *)((uintptr_t)bmp->palData+(uintptr_t)data);
        bmp->data = (void *)((uintptr_t)bmp->data+(uintptr_t)data);
    }
    anim->useNum = 0;
#ifdef BYTESWAPPING
    /* mark the file block as read (its 20-byte header is no longer needed): bank offset, then the anim */
    ((u32 *)data)[2] = ANIM_DATA_ALLOCATION_VALID;
    memcpy((u8 *)data + 12, &anim, sizeof(anim));
#endif
#ifdef TARGET_PC
    /* N150: note GameCube button pictures (drawn for the player's controller in HuSprDisp) */
    pb_button_art_anim_read(anim);
#endif
    return anim;
}

void HuSprAnimLock(ANIMDATA *anim)
{
    anim->useNum++;
}

s16 HuSprCreate(ANIMDATA *anim, s16 prio, s16 bank)
{
    HUSPRITE *sprite;
    s16 i;
    for(sprite = &HuSprData[1], i=1; i<HUSPR_MAX; i++, sprite++) {
        if(!sprite->data) {
            break;
        }
    }
    if(i == HUSPR_MAX) {
        return HUSPR_NONE;
    }
    sprite->data = anim;
    sprite->speed = 1.0f;
    sprite->animNo = 0;
    sprite->bank = bank;
    sprite->time = 0.0f;
    sprite->attr = 0;
    sprite->drawNo = 0;
    sprite->r = sprite->g = sprite->b = sprite->a = 255;
    sprite->pos.x = sprite->pos.y = sprite->zRot = 0.0f;
    sprite->prio = prio;
    sprite->scale.x = sprite->scale.y = 1.0f;
    sprite->wrapS = sprite->wrapT = GX_CLAMP;
    sprite->uvScaleX = sprite->uvScaleY = 1;
    sprite->bg = NULL;
    sprite->scissorX = sprite->scissorY = 0;
    sprite->scissorW = HU_FB_WIDTH;
    sprite->scissorH = HU_FB_HEIGHT;
    if(anim) {
        HuSprAnimLock(anim);
    }
    return i;
}

s16 HuSprFuncCreate(HUSPRFUNC func, s16 prio)
{
    HUSPRITE *sprite;
    s16 index = HuSprCreate(NULL, prio, 0);
    if(index == HUSPR_NONE) {
        return HUSPR_NONE;
    }
    sprite = &HuSprData[index];
    sprite->func = func;
    sprite->attr |= HUSPR_ATTR_FUNC;
    return index;
}

s16 HuSprGrpCreate(s16 capacity)
{
    HUSPRGRP *group;
    s16 i, j;
    for(group = HuSprGrpData, i=0; i<HUSPR_GRP_MAX; i++, group++) {
        if(group->capacity == 0) {
            break;
        }
    }
    if(i == HUSPR_GRP_MAX) {
        return HUSPR_GRP_NONE;
    }
    group->members = HuMemDirectMalloc(HEAP_SYSTEM, sizeof(s16)*capacity);
    for(j=0; j<capacity; j++) {
        group->members[j] = HUSPR_NONE;
    }
    group->capacity = capacity;
    group->pos.x = group->pos.y = group->zRot = group->center.x = group->center.y = 0.0f;
    group->scale.x = group->scale.y = 1.0f;
    return i;
}

s16 HuSprGrpCopy(s16 group)
{
    HUSPRGRP *new_group_ptr;
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 new_group = HuSprGrpCreate(group_ptr->capacity);
    s16 i;
    if(new_group == HUSPR_GRP_NONE) {
        return HUSPR_GRP_NONE;
    }
    new_group_ptr = &HuSprGrpData[new_group];
    new_group_ptr->pos.x = group_ptr->pos.x;
    new_group_ptr->pos.y = group_ptr->pos.y;
    new_group_ptr->zRot = group_ptr->zRot;
    new_group_ptr->scale.x = group_ptr->scale.x;
    new_group_ptr->scale.y = group_ptr->scale.y;
    new_group_ptr->center.x = group_ptr->center.x;
    new_group_ptr->center.y = group_ptr->center.y;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != HUSPR_NONE) {
            HUSPRITE *old_sprite = &HuSprData[group_ptr->members[i]];
            s16 new_sprite = HuSprCreate(old_sprite->data, old_sprite->prio, old_sprite->bank);
            HuSprData[new_sprite] = *old_sprite;
            HuSprGrpMemberSet(new_group, i, new_sprite);
        }
    }
    return new_group;
}

void HuSprGrpMemberSet(s16 group, s16 member, s16 sprite)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    HUSPRITE *sprite_ptr = &HuSprData[sprite];
    if(group_ptr->capacity == 0 || group_ptr->capacity <= member || group_ptr->members[member] != HUSPR_NONE) {
        return;
    }
    group_ptr->members[member] = sprite;
}

void HuSprGrpMemberKill(s16 group, s16 member)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    if(group_ptr->capacity == 0 || group_ptr->capacity <= member || group_ptr->members[member] == HUSPR_NONE) {
        return;
    }
    HuSprKill(group_ptr->members[member]);
    group_ptr->members[member] = HUSPR_NONE;
}

void HuSprGrpKill(s16 group)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 i;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != HUSPR_NONE) {
            HuSprKill(group_ptr->members[i]);
        }
    }
    group_ptr->capacity = 0;
    HuMemDirectFree(group_ptr->members);
}

void HuSprKill(s16 sprite)
{
    HUSPRITE *sprite_ptr = &HuSprData[sprite];
    if(!sprite_ptr->data) {
        return;
    }
    if(!(sprite_ptr->attr & HUSPR_ATTR_FUNC)) {
        HuSprAnimKill(sprite_ptr->data);
        if(sprite_ptr->bg) {
            HuSprAnimKill(sprite_ptr->bg);
            sprite_ptr->bg = NULL;
        }
    }
    sprite_ptr->data = NULL;
}

#ifdef OPTIMIZED_TEXTURE_LOADING
static void HuSprAnimTexDestroy(ANIMDATA *anim)
{
    int i;
    int j;
    /* N69: without the mask a HuSprAnimMake anim (bmpNum 1|ANIM_BMP_ALLOC) walked 0x8001 bitmaps past its block
     * and destroyed the texture objects of whatever lay there (and zeroed their ids) */
    for(i=0; i<(anim->bmpNum & ANIM_BMP_NUM_MASK); i++) {
        for (j = 0; j < sizeof(anim->bmp[0].texData) / sizeof(anim->bmp[0].texData[0]); j++) {
            AnimTexData *tex = &anim->bmp[i].texData[j];
            if (tex->tex_initialized) {
                GXDestroyTexObj(&tex->tex_obj);
            }
            if (tex->tlut_initialized) {
                GXDestroyTlutObj(&tex->tlut_obj);
            }
        }
    }
}
#endif

void HuSprAnimKill(ANIMDATA *anim)
{
    if(--anim->useNum <= 0) {
#ifdef TARGET_PC
        /* N69: a minigame's own end (m456 fn_1_8C68) kills anims its particles still draw with. The particle's kill
         * (Hu3DModelKill at the overlay's end) then killed them again: five 'memory free error's per anim, and a
         * free of whatever reused a block in between. Keep the anim for the particle; its kill frees it. */
        if(Hu3DParticleAnimUseCheck(anim)) {
            return;
        }
#endif
        if(anim->bmpNum & ANIM_BMP_ALLOC) {
            if(anim->bmp->data) {
                HuMemDirectFree(anim->bmp->data);
            }
            if(anim->bmp->palData) {
                HuMemDirectFree(anim->bmp->palData);
            }
        }
#ifdef TARGET_PC
        /* N75: m411's end kills its made anims while their texture anims (Hu3DAnimCreate) still draw with them, and
         * omOvlKill's Hu3DAllKill -> Hu3DAnimKill then freed each one again (8 'memory free error's per play, GC
         * too). The owner's bitmap data went above, as on GC; the anim itself goes with its last texture anim. */
        if(Hu3DTexAnimUseCheck(anim)) {
            return;
        }
#endif
#ifdef OPTIMIZED_TEXTURE_LOADING
        HuSprAnimTexDestroy(anim);
#endif
#ifdef BYTESWAPPING
        if (!anim->raw) {
            /* HuSprAnimMake's: its frame and layer are blocks of their own (N57: every window leaked both) */
            HuMemDirectFree(anim->bank->frame);
            HuMemDirectFree(anim->pat->layer);
        }
        HuMemDirectFree(anim->bank);
        HuMemDirectFree(anim->pat);
        HuMemDirectFree(anim->bmp);
        if (anim->raw) {
            HuMemDirectFree(anim->raw);
        }
#endif
        HuMemDirectFree(anim);
    }
}

#ifdef BYTESWAPPING
/* Hu3DAnimKill's free of an anim. On GC that is one block (the file block, or HuSprAnimMake's) and its bitmap data
 * stays; here the converted anim's own blocks go with it (N57: m411 left mid-play kept five blocks per anim). An anim
 * that is not a used block any more is only reported, as before. */
void HuSprAnimFree(ANIMDATA *anim)
{
    if (!HuMemDirectUsedCheck(anim)) {
        HuMemDirectFree(anim);
        return;
    }
#ifdef OPTIMIZED_TEXTURE_LOADING
    /* N75: its texture anim drew it, so it has texture objects (HuSprAnimKill's deferred kill ends here) */
    HuSprAnimTexDestroy(anim);
#endif
    if (anim->raw) {
        HuMemDirectFree(anim->raw);
    } else {
        HuMemDirectFree(anim->bank->frame);
        HuMemDirectFree(anim->pat->layer);
    }
    HuMemDirectFree(anim->bank);
    HuMemDirectFree(anim->pat);
    HuMemDirectFree(anim->bmp);
    HuMemDirectFree(anim);
}
#endif

void HuSprAttrSet(s16 group, s16 member, s32 attr)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    HUSPRITE *sprite_ptr;
    if(group_ptr->capacity == 0 || group_ptr->capacity <= member || group_ptr->members[member] == HUSPR_NONE) {
        return;
    }
    sprite_ptr = &HuSprData[group_ptr->members[member]];
    sprite_ptr->attr |= attr;
    sprite_ptr->dirty |= SPRITE_DIRTY_ATTR;
}

void HuSprAttrReset(s16 group, s16 member, s32 attr)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    HUSPRITE *sprite_ptr;
    if(group_ptr->capacity == 0 || group_ptr->capacity <= member || group_ptr->members[member] == HUSPR_NONE) {
        return;
    }
    sprite_ptr = &HuSprData[group_ptr->members[member]];
    sprite_ptr->attr &= ~attr;
    sprite_ptr->dirty |= SPRITE_DIRTY_ATTR;
}

void HuSprPosSet(s16 group, s16 member, float x, float y)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    sprite_ptr->pos.x = x;
    sprite_ptr->pos.y = y;
    sprite_ptr->dirty |= SPRITE_DIRTY_XFORM;
}

void HuSprZRotSet(s16 group, s16 member, float z_rot)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    sprite_ptr->zRot = z_rot;
    sprite_ptr->dirty |= SPRITE_DIRTY_XFORM;
}

void HuSprScaleSet(s16 group, s16 member, float x, float y)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    sprite_ptr->scale.x = x;
    sprite_ptr->scale.y = y;
    sprite_ptr->dirty |= SPRITE_DIRTY_XFORM;
}

void HuSprTPLvlSet(s16 group, s16 member, float tp_lvl)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    sprite_ptr->a = tp_lvl*255;
    sprite_ptr->dirty |= SPRITE_DIRTY_COLOR;
}

void HuSprColorSet(s16 group, s16 member, u8 r, u8 g, u8 b)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    sprite_ptr->r = r;
    sprite_ptr->g = g;
    sprite_ptr->b = b;
    sprite_ptr->dirty |= SPRITE_DIRTY_COLOR;
}

void HuSprSpeedSet(s16 group, s16 member, float speed)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    HuSprData[group_ptr->members[member]].speed = speed;
}

void HuSprBankSet(s16 group, s16 member, s16 bank)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    ANIMDATA *anim = sprite_ptr->data;
    ANIMBANK *bank_ptr = &anim->bank[sprite_ptr->bank];
    ANIMFRAME *frame_ptr = &bank_ptr->frame[sprite_ptr->animNo];
    sprite_ptr->bank = bank;
    if(sprite_ptr->attr & HUSPR_ATTR_REVERSE) {
        sprite_ptr->animNo = bank_ptr->timeNum-1;
        frame_ptr = &bank_ptr->frame[sprite_ptr->animNo];
        sprite_ptr->time = frame_ptr->time;
    } else {
        sprite_ptr->time = 0;
        sprite_ptr->animNo = 0;
    }
}

void HuSprGrpPosSet(s16 group, float x, float y)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 i;
    group_ptr->pos.x = x;
    group_ptr->pos.y = y;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != -1) {
            HuSprData[group_ptr->members[i]].dirty |= SPRITE_DIRTY_XFORM;
        }
    }
}

void HuSprGrpCenterSet(s16 group, float x, float y)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 i;
    group_ptr->center.x = x;
    group_ptr->center.y = y;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != HUSPR_NONE) {
            HuSprData[group_ptr->members[i]].dirty |= SPRITE_DIRTY_XFORM;
        }
    }
}

void HuSprGrpZRotSet(s16 group, float z_rot)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 i;
    group_ptr->zRot = z_rot;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != HUSPR_NONE) {
            HuSprData[group_ptr->members[i]].dirty |= SPRITE_DIRTY_XFORM;
        }
    }
}

void HuSprGrpScaleSet(s16 group, float x, float y)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 i;
    group_ptr->scale.x = x;
    group_ptr->scale.y = y;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != HUSPR_NONE) {
            HuSprData[group_ptr->members[i]].dirty |= SPRITE_DIRTY_XFORM;
        }
    }
}

void HuSprGrpTPLvlSet(s16 group, float tp_lvl)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 i;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != HUSPR_NONE) {
            HuSprData[group_ptr->members[i]].a = tp_lvl*255;
            HuSprData[group_ptr->members[i]].dirty |= SPRITE_DIRTY_COLOR;
        }
    }
}

void HuSprGrpDrawNoSet(s16 group, s32 draw_no)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 i;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != HUSPR_NONE) {
            HuSprData[group_ptr->members[i]].drawNo = draw_no;
        }
    }
}

void HuSprDrawNoSet(s16 group, s16 member, s32 draw_no)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    sprite_ptr->drawNo = draw_no;
}

void HuSprPriSet(s16 group, s16 member, s16 prio)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    sprite_ptr->prio = prio;
}

void HuSprGrpScissorSet(s16 group, s16 x, s16 y, s16 w, s16 h)
{
    HUSPRGRP *group_ptr = &HuSprGrpData[group];
    s16 i;
    for(i=0; i<group_ptr->capacity; i++) {
        if(group_ptr->members[i] != HUSPR_NONE) {
            HuSprScissorSet(group, i, x, y, w, h);
        }
    }
}

void HuSprScissorSet(s16 group, s16 member, s16 x, s16 y, s16 w, s16 h)
{
    HUSPRITE *sprite_ptr = &HuSprData[HuSprGrpData[group].members[member]];
    sprite_ptr->scissorX = x;
    sprite_ptr->scissorY = y;
    sprite_ptr->scissorW = w;
    sprite_ptr->scissorH = h;
}

static s16 bitSizeTbl[11] = { 32, 24, 16, 8, 4, 16, 8, 8, 4, 8, 4 };

ANIMDATA *HuSprAnimMake(s16 sizeX, s16 sizeY, s16 dataFmt)
{
    ANIMLAYER *layer;
    ANIMBMP *bmp;
    ANIMDATA *anim;
    ANIMPAT *pat;
    ANIMFRAME *frame;
    void *temp;
    ANIMBANK *bank;
    ANIMDATA *new_anim;

#ifdef BYTESWAPPING
    // as these are allocated in HuSprAnimRead, we need to do so here too so we don't get issues when freeing
    anim = HuMemDirectMalloc(HEAP_DATA, sizeof(ANIMDATA));
    anim->bank = bank = HuMemDirectMalloc(HEAP_DATA, sizeof(ANIMBANK));
    bank->frame = frame = HuMemDirectMalloc(HEAP_DATA, sizeof(ANIMFRAME));
    anim->pat = pat = HuMemDirectMalloc(HEAP_DATA, sizeof(ANIMPAT));
    pat->layer = layer = HuMemDirectMalloc(HEAP_DATA, sizeof(ANIMLAYER));
    anim->bmp = bmp = HuMemDirectMalloc(HEAP_DATA, sizeof(ANIMBMP));
    anim->valid = ANIM_DATA_ALLOCATION_VALID;
    anim->raw = NULL;
    {
        /* the anim belongs to whoever made it (N57: a minigame's, left mid-play, is freed with its code) */
        uintptr_t caller = HU_MEM_CALLER();
        HuMemMemoryCallerSet(anim, caller);
        HuMemMemoryCallerSet(bank, caller);
        HuMemMemoryCallerSet(frame, caller);
        HuMemMemoryCallerSet(pat, caller);
        HuMemMemoryCallerSet(layer, caller);
        HuMemMemoryCallerSet(bmp, caller);
    }
#else
    anim = new_anim = HuMemDirectMalloc(HEAP_DATA, sizeof(ANIMDATA)+sizeof(ANIMBANK)+sizeof(ANIMFRAME)
                                            +sizeof(ANIMPAT)+sizeof(ANIMLAYER)+sizeof(ANIMBMP));

    bank = temp = &new_anim[1];
    anim->bank = bank;
    frame = temp = ((char *)temp+sizeof(ANIMBANK));
    bank->frame = frame;
    pat = temp = ((char *)temp+sizeof(ANIMFRAME));
    anim->pat = pat;
    layer = temp = ((char *)temp+sizeof(ANIMPAT));
    pat->layer = layer;
    bmp = temp = ((char *)temp+sizeof(ANIMLAYER));
    anim->bmp = bmp;
#endif
    anim->useNum = 0;
    anim->bankNum = 1;
    anim->patNum = 1;
    anim->bmpNum = (1|ANIM_BMP_ALLOC);
    bank->timeNum = 1;
    bank->unk = 10;
    frame->pat = 0;
    frame->time = 10;
    frame->shiftX = frame->shiftY = frame->flip = 0;
    pat->layerNum = 1;
    pat->centerX = sizeX/2;
    pat->centerY = sizeY/2;
    pat->sizeX = sizeX;
    pat->sizeY = sizeY;
    layer->alpha = 255;
    layer->flip = 0;
    layer->bmpNo = 0;
    layer->startX = layer->startY = 0;
    layer->sizeX = sizeX;
    layer->sizeY = sizeY;
    layer->shiftX = layer->shiftY = 0;
    layer->vtx[0] = layer->vtx[1] = 0;
    layer->vtx[2] = sizeX;
    layer->vtx[3] = 0;
    layer->vtx[4] = sizeX;
    layer->vtx[5] = sizeY;
    layer->vtx[6] = 0;
    layer->vtx[7] = sizeY;
    bmp->pixSize = bitSizeTbl[dataFmt];
    bmp->dataFmt = dataFmt;
    bmp->palNum = 0;
    bmp->sizeX = sizeX;
    bmp->sizeY = sizeY;
    bmp->dataSize = sizeX*sizeY*bitSizeTbl[dataFmt]/8;
    bmp->palData = NULL;
    bmp->data = NULL;
#ifdef OPTIMIZED_TEXTURE_LOADING
    int i;
    for (i = 0; i < sizeof(bmp->texData) / sizeof(bmp->texData[0]); i++) {
        AnimTexData *tex = &bmp->texData[i];
        tex->tex_initialized = FALSE;
        tex->tlut_initialized = FALSE;
        memset(&tex->tex_obj, 0, sizeof(tex->tex_obj));
        memset(&tex->tlut_obj, 0, sizeof(tex->tlut_obj));
    }
#endif
    return anim;
}

void HuSprBGSet(s16 group, s16 member,  ANIMDATA *bg, s16 bg_bank)
{
    s16 sprite = HuSprGrpData[group].members[member];
    HuSprSprBGSet(sprite, bg, bg_bank);
}

void HuSprSprBGSet(s16 sprite, ANIMDATA *bg, s16 bg_bank)
{
    HUSPRITE *sprite_ptr = &HuSprData[sprite];
    sprite_ptr->bg = bg;
    sprite_ptr->bgBank = bg_bank;
    sprite_ptr->wrapT = sprite_ptr->wrapS = GX_REPEAT;
    sprite_ptr->attr &= ~HUSPR_ATTR_LINEAR;
}

void AnimDebug(ANIMDATA *anim)
{
    ANIMPAT *pat;
    ANIMLAYER *layer;
    s16 i;
    s16 j;
    ANIMFRAME *frame;
    ANIMBANK *bank;
    ANIMBMP *bmp;
    
    OSReport("patNum %d,bankNum %d,bmpNum %d\n", anim->patNum, anim->bankNum, anim->bmpNum & ANIM_BMP_NUM_MASK);
    pat = anim->pat;
    for(i=0; i<anim->patNum; i++) {
        OSReport("PATTERN%d:\n", i);
        OSReport("\tlayerNum %d,center (%d,%d),size (%d,%d)\n", pat->layerNum, pat->centerX, pat->centerX, pat->sizeX, pat->sizeY);
        layer = pat->layer;
        for(j=0; j<pat->layerNum; j++) {
            OSReport("\t\tfileNo %d,flip %x\n", layer->bmpNo, layer->flip);
            OSReport("\t\tstart (%d,%d),size (%d,%d),shift (%d,%d)\n", layer->startX, layer->startY, layer->sizeX, layer->sizeY, layer->shiftX, layer->shiftY);
            if(j != pat->layerNum-1) {
                OSReport("\n");
            }
            layer++;
        }
        pat++;
    }
    bank = anim->bank;
    for(i=0; i<anim->bankNum; i++) {
        OSReport("BANK%d:\n", i);
        OSReport("\ttimeNum %d\n", bank->timeNum);
        frame = bank->frame;
        for(j=0; j<bank->timeNum; j++) {
            OSReport("\t\tpat %d,time %d,shift(%d,%d),flip %x\n", frame->pat, frame->time, frame->shiftX, frame->shiftY, frame->flip);
            frame++;
        }
        bank++;
    }
    bmp = anim->bmp;
#ifdef NON_MATCHING
    for(i = 0; i < (anim->bmpNum & ANIM_BMP_NUM_MASK); i++) {
#else
    for(i = 0; (i < anim->bmpNum) & ANIM_BMP_NUM_MASK; i++) {
#endif
        OSReport("BMP%d:\n", i);
        OSReport("\tpixSize %d,palNum %d,size (%d,%d)\n", bmp->pixSize, bmp->palNum, bmp->sizeX, bmp->sizeY);
        bmp++;
    }
}
