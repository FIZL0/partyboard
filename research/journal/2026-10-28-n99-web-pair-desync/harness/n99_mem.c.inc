
/* N99 TEMP: every heap alloc/free while a match runs, with the caller chain (wasm: the JS stack's function names) */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
void n99_mem(int kind, void *data, s32 size, uintptr_t num)
{
    static FILE *f;
    static int busy;
    int heap;
    uintptr_t off = (uintptr_t)-1;
    if (busy || !netplay_match_running()) return;
    busy = 1;
    if (f == NULL) f = fopen("/tmp/alloc.log", "w");
    if (f == NULL) { busy = 0; return; }
    for (heap = 0; heap < HEAP_MAX; heap++) {
        uintptr_t base = (uintptr_t)HuMemHeapPtrGet((HeapID)heap);
        if (base != 0 && (uintptr_t)data >= base && (uintptr_t)data - base < HuMemHeapSizeGet((HeapID)heap)) {
            off = (uintptr_t)data - base;
            break;
        }
    }
    fprintf(f, "f%u ovl%d %c h%d off %lx size %x num %llx:", netplay_frame(), (int)omcurovl, "?AFE"[kind], heap,
        (unsigned long)off, (unsigned)size, (unsigned long long)heap_num(num));
#ifdef __EMSCRIPTEN__
    {
        static char st[8192];
        char *p, *q;
        int k = 0;
        emscripten_get_callstack(EM_LOG_C_STACK | EM_LOG_NO_PATHS, st, sizeof(st));
        /* lines "    at NAME (...)": skip this function and the HuMem ones */
        for (p = st; (p = strstr(p, " at ")) != NULL && k < 10; p = q) {
            p += 4;
            q = strchr(p, ' ');
            if (q == NULL) break;
            if (strncmp(p, "n99_mem", 7) == 0 || strncmp(p, "HuMemMemory", 11) == 0) continue;
            fprintf(f, " %.*s", (int)(q - p), p);
            k++;
        }
    }
#endif
    fputc('\n', f);
    fflush(f);
    busy = 0;
}
