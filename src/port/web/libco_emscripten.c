/* N87: libco for the browser build, on Emscripten's fibers (Asyncify: a switch unwinds the wasm call stack into
 * the fiber's Asyncify buffer and rewinds the other one). Linked instead of extern/libco/libco.c, which has no
 * wasm backend (wasm can't switch stacks by itself). */
#include <libco/libco.h>

#include <emscripten/fiber.h>
#include <stdlib.h>

/* Asyncify saves a fiber's whole unwound call stack (wasm locals included), not only its C stack. */
#define CO_ASYNCIFY_STACK_SIZE (256 * 1024)

typedef struct WebCothread {
    emscripten_fiber_t fiber;
    void (*entry)(void);
    void *c_stack;
    void *asyncify_stack;
} WebCothread;

static WebCothread co_main;
static WebCothread *co_current;

static void co_entry(void *arg)
{
    ((WebCothread *)arg)->entry();
    /* A libco entry must never return. */
    abort();
}

cothread_t co_active(void)
{
    if (co_current == NULL) {
        co_main.asyncify_stack = malloc(CO_ASYNCIFY_STACK_SIZE);
        emscripten_fiber_init_from_current_context(&co_main.fiber, co_main.asyncify_stack, CO_ASYNCIFY_STACK_SIZE);
        co_current = &co_main;
    }
    return co_current;
}

cothread_t co_derive(void *memory, unsigned int size, void (*entry)(void))
{
    (void)memory;
    (void)size;
    (void)entry;
    return NULL;
}

cothread_t co_create(unsigned int size, void (*entry)(void))
{
    WebCothread *co;
    co_active();
    co = calloc(1, sizeof(*co));
    if (co == NULL) {
        return NULL;
    }
    co->entry = entry;
    /* 16-byte aligned, as clang assumes the stack pointer is (it computes &a[1] of a stack array as `sp | 8`):
     * malloc's 8 bytes left printf's __stdio_write writing its first iovec for ever. */
    size &= ~15u;
    co->c_stack = aligned_alloc(16, size);
    co->asyncify_stack = malloc(CO_ASYNCIFY_STACK_SIZE);
    if (co->c_stack == NULL || co->asyncify_stack == NULL) {
        free(co->c_stack);
        free(co->asyncify_stack);
        free(co);
        return NULL;
    }
    emscripten_fiber_init(&co->fiber, co_entry, co, co->c_stack, size, co->asyncify_stack, CO_ASYNCIFY_STACK_SIZE);
    return co;
}

void co_delete(cothread_t handle)
{
    WebCothread *co = handle;
    if (co == NULL || co == &co_main) {
        return;
    }
    free(co->c_stack);
    free(co->asyncify_stack);
    free(co);
}

void co_switch(cothread_t handle)
{
    WebCothread *from = co_active();
    WebCothread *to = handle;
    if (to == from) {
        return;
    }
    co_current = to;
    emscripten_fiber_swap(&from->fiber, &to->fiber);
}

int co_serializable(void)
{
    return 0;
}
