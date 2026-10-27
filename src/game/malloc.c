#include "game/memory.h"
#include "game/init.h"
#include "dolphin/os.h"

static u32 HeapSizeTbl[HEAP_MAX] = { 0x240000, 0x140000, 0xA80000, 0x580000, 0 };
static void *HeapTbl[HEAP_MAX];

void HuMemInitAll(void)
{
    s32 i;
    void *ptr;
    u32 free_size;
    for(i=0; i<4; i++) {
#ifdef TARGET_PC
        HeapSizeTbl[i] *= 4;
#endif
        ptr = OSAlloc(HeapSizeTbl[i]);
        if(ptr == NULL) {
            OSReport("HuMem> Failed OSAlloc Size:%d\n", HeapSizeTbl[i]);
            return;
        }
        HeapTbl[i] = HuMemInit(ptr, HeapSizeTbl[i]);
    }
    free_size = OSCheckHeap(currentHeapHandle);
    OSReport("HuMem> left memory space %dKB(%d)\n", free_size/1024, free_size);
    ptr = OSAlloc(free_size);
    if(ptr == NULL) {
        OSReport("HuMem> Failed OSAlloc left space\n");
        return;
    }
    HeapTbl[4] = HuMemInit(ptr, free_size);
    HeapSizeTbl[4] = free_size;
}

void *HuMemInit(void *ptr, size_t size)
{
    return HuMemHeapInit(ptr, size);
}

void HuMemDCFlushAll()
{
    HuMemDCFlush(2);
    HuMemDCFlush(0);
}

void HuMemDCFlush(HeapID heap)
{
    DCFlushRangeNoSync(HeapTbl[heap], HeapSizeTbl[heap]);
}

void *HuMemDirectMalloc(HeapID heap, size_t size)
{
#ifdef TARGET_PC
    uintptr_t retaddr = HU_MEM_CALLER();
#else
    register u32 retaddr;
    asm {
        mflr retaddr
    }
#endif
    size = (size + 31) & ~0x1F;
    return HuMemMemoryAlloc(HeapTbl[heap], size, retaddr);
}

void *HuMemDirectMallocNum(HeapID heap, size_t size, uintptr_t num)
{
#ifdef TARGET_PC
    uintptr_t retaddr = HU_MEM_CALLER();
#else
    register u32 retaddr;
    asm {
        mflr retaddr
    }
#endif
    size = (size + 31) & ~0x1F;
    return HuMemMemoryAllocNum(HeapTbl[heap], size, num, retaddr);
}

void HuMemDirectFree(void *ptr)
{
#ifdef TARGET_PC
    uintptr_t retaddr = HU_MEM_CALLER();
#else
    register u32 retaddr;
    asm {
        mflr retaddr
    }
#endif
    HuMemMemoryFree(ptr, retaddr);
}

void HuMemDirectFreeNum(HeapID heap, uintptr_t num)
{
#ifdef TARGET_PC
    uintptr_t retaddr = HU_MEM_CALLER();
#else
    register u32 retaddr;
    asm {
        mflr retaddr
    }
#endif
    HuMemMemoryFreeNum(HeapTbl[heap], num, retaddr);
}

size_t HuMemUsedMallocSizeGet(HeapID heap)
{
    return HuMemUsedMemorySizeGet(HeapTbl[heap]);
}

size_t HuMemUsedMallocBlockGet(HeapID heap)
{
    return HuMemUsedMemoryBlockGet(HeapTbl[heap]);
}

size_t HuMemHeapSizeGet(HeapID heap)
{
    return HeapSizeTbl[heap];
}

void *HuMemHeapPtrGet(HeapID heap)
{
    return HeapTbl[heap];
}

#ifdef TARGET_PC
typedef struct {
    void *data;
    BOOL used;
    uintptr_t num;
} NumFind;

static void NumFindBlock(void *user, uintptr_t offset, s32 size, BOOL used, uintptr_t num, void *data, size_t data_size)
{
    NumFind *find = user;
    if (used && data == find->data) {
        find->used = TRUE;
        find->num = num;
    }
}

static NumFind NumFindGet(void *ptr)
{
    NumFind find = { ptr, FALSE, (uintptr_t)-256 };
    s32 i;
    for (i = 0; i < HEAP_MAX; i++) {
        if (HeapTbl[i] != NULL && (uintptr_t)ptr >= (uintptr_t)HeapTbl[i]
            && (uintptr_t)ptr - (uintptr_t)HeapTbl[i] < HeapSizeTbl[i]) {
            HuMemHeapWalk(HeapTbl[i], NumFindBlock, &find);
            break;
        }
    }
    return find;
}

uintptr_t HuMemDirectNumGet(void *ptr)
{
    return NumFindGet(ptr).num;
}

BOOL HuMemDirectUsedCheck(void *ptr)
{
    return NumFindGet(ptr).used;
}
#endif
