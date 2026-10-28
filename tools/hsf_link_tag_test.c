/* N81: a link's HSFDATA must not carry a stale tag: with one, another live link's kill (HuMemDirectFreeNum of
 * its own address) frees ours too. Real Hu3DModelLink / Hu3DModelKill / heap from libdol.so; tools/hsf_link_tag_test.sh. */
#include <stdio.h>
#include <string.h>
#include "game/hu3d.h"
#include "game/memory.h"
#include <stdlib.h>

/* HuMemInitAll's OS heap, without booting aurora's OS: these override libdol's port/OS.c */
void *OSAllocFromHeap(int heap, u32 size)
{
    return aligned_alloc(64, (size + 63) & ~63u);
}

s32 OSCheckHeap(int heap)
{
    return 0x100000;
}

static void slot_init(s16 i)
{
    HU3DMODEL *m = &Hu3DData[i];
    s16 j;
    memset(m, 0, sizeof(*m));
    m->motId = m->motIdSrc = m->motIdShift = m->motIdOvl = m->motIdShape = -1;
    m->linkMdlId = -1;
    for (j = 0; j < 4; j++) {
        m->motIdCluster[j] = -1;
    }
}

int main(void)
{
    HSFDATA *src;
    s16 i, a, b, c;
    uintptr_t addrA;

    HuMemInitAll();
    for (i = 0; i < HU3D_MODEL_MAX; i++) {
        slot_init(i);
    }
    /* slot 0: the source model (no objects, no motion) */
    src = HuMemDirectMallocNum(HEAP_DATA, sizeof(HSFDATA), 1);
    memset(src, 0, sizeof(*src));
    Hu3DData[0].hsf = src;

    a = Hu3DModelLink(0); /* slot 1 */
    addrA = (uintptr_t)Hu3DData[a].hsf;
    Hu3DModelKill(a); /* slot 1 free, its mallocNoLink stays addrA */
    Hu3DData[1].hsf = src; /* a stand-in keeps slot 1 busy */
    b = Hu3DModelLink(0); /* slot 2, first fit puts its HSFDATA at addrA */
    Hu3DData[1].hsf = NULL;
    c = Hu3DModelLink(0); /* slot 1 again */
    printf("A slot %d hsf %#lx; B slot %d hsf %#lx (reused A: %s); C slot %d hsf %#lx tag %#lx\n", a,
        (unsigned long)addrA, b, (unsigned long)Hu3DData[b].hsf, (uintptr_t)Hu3DData[b].hsf == addrA ? "yes" : "no",
        c, (unsigned long)Hu3DData[c].hsf, (unsigned long)HuMemDirectNumGet(Hu3DData[c].hsf));
    if ((uintptr_t)Hu3DData[b].hsf != addrA || c != a) {
        printf("setup failed: B's HSFDATA is not at A's address, or C is not in A's slot\n");
        return 2;
    }
    Hu3DModelKill(b); /* HuMemDirectFreeNum(addrA) */
    printf("after B's kill, C's HSFDATA used: %s\n", HuMemDirectUsedCheck(Hu3DData[c].hsf) ? "yes" : "NO (use-after-free)");
    return HuMemDirectUsedCheck(Hu3DData[c].hsf) ? 0 : 1;
}
