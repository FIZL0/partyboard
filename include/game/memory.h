#ifndef _GAME_MEMORY_H
#define _GAME_MEMORY_H

#include "dolphin/types.h"

// TODO rename to HU_MEMNUM_OVL
#define MEMORY_DEFAULT_NUM 0x10000000

typedef enum {
    HEAP_SYSTEM,
    HEAP_MUSIC,
    HEAP_DATA,
    HEAP_DVD,
    HEAP_MISC,
    HEAP_MAX
} HeapID;

void HuMemInitAll(void);
void *HuMemInit(void *ptr, size_t size);
void HuMemDCFlushAll();
void HuMemDCFlush(HeapID heap);
void *HuMemDirectMalloc(HeapID heap, size_t size);
void *HuMemDirectMallocNum(HeapID heap, size_t size, uintptr_t num);
void HuMemDirectFree(void *ptr);
void HuMemDirectFreeNum(HeapID heap, uintptr_t num);
size_t HuMemUsedMallocSizeGet(HeapID heap);
size_t HuMemUsedMallocBlockGet(HeapID heap);
size_t HuMemHeapSizeGet(HeapID heap);
void *HuMemHeapPtrGet(HeapID heap);

void *HuMemHeapInit(void *ptr, size_t size);
void *HuMemMemoryAlloc(void *heap_ptr, size_t size, uintptr_t retaddr);
void *HuMemMemoryAllocNum(void *heap_ptr, size_t size, uintptr_t num, uintptr_t retaddr);
void HuMemMemoryFree(void *ptr, uintptr_t retaddr);
void HuMemMemoryFreeNum(void *heap_ptr, uintptr_t num, uintptr_t retaddr);
size_t HuMemUsedMemorySizeGet(void *heap_ptr);
s32 HuMemUsedMemoryBlockGet(void *heap_ptr);
size_t HuMemMemorySizeGet(void *ptr);
size_t HuMemMemoryAllocSizeGet(size_t size);
void HuMemHeapDump(void *heap_ptr, s16 status);

#ifdef TARGET_PC
/* One block of a heap for HuMemHeapWalk: offset from the heap start, block size (header included), used or
 * free, its num, and its data (size - header bytes). */
typedef void (*HuMemWalkFunc)(void *user, uintptr_t offset, s32 size, BOOL used, uintptr_t num, void *data,
    size_t data_size);
/* Calls func for every block of the heap, in address order (netplay: heap census, scrubbing free blocks). */
void HuMemHeapWalk(void *heap_ptr, HuMemWalkFunc func, void *user);
#endif

#endif
