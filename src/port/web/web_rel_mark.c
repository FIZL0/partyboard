/* N99: compiled twice per REL by cmake/web.cmake, as PB_REL_MARK=pbrel_first_<rel> and pbrel_last_<rel>, and
 * prelinked before and after the REL's own objects: wasm-ld keeps an object's functions together and in input order,
 * so the REL's code lies between its two marks in the module's function index space (src/port/web/web_rel.c,
 * web_rel_function_owner). PB_REL_MARK_ID differs for every mark, so wasm-opt's duplicate function elimination
 * keeps them apart. */
int PB_REL_MARK(void)
{
    return PB_REL_MARK_ID;
}
