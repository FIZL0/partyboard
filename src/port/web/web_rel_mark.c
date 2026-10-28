/* N99: compiled twice per REL by cmake/web.cmake, as PB_REL_MARK=pbrel_first_<rel> and pbrel_last_<rel>, and
 * prelinked before and after the REL's own objects: wasm-ld keeps an object's functions together and in input order,
 * so the REL's code lies between its two marks in the module's function index space (src/port/web/web_rel.c,
 * web_rel_function_owner). PB_REL_MARK_ID differs for every mark, so wasm-opt's duplicate function elimination
 * keeps them apart. N102: called once at boot, it leaves where its code is in the module (web_rel_mark_pc): the REL's
 * code also lies between its marks' code offsets, which is what a block's caller (HU_MEM_CALLER) is on the web. */
#include <stdint.h>

uintptr_t web_rel_caller_pc(int level);
extern uintptr_t web_rel_mark_pc;

int PB_REL_MARK(void)
{
    web_rel_mark_pc = web_rel_caller_pc(0);
    return PB_REL_MARK_ID;
}
