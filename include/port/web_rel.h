#ifndef PORT_WEB_REL_H
#define PORT_WEB_REL_H

/* N87: the browser build links every REL statically (no dlopen); src/port/web/web_rel.c. */

#ifdef __cplusplus
extern "C" {
#endif

/* The REL named `name` ("m401Dll.so", as ovllist.c names them) for omDLLLink: the first link since it was last
 * unlinked puts its .data/.bss back to their boot values, as a fresh dlopen would. NULL if there is no such REL. */
void *web_rel_link(const char *name);
/* Runs the REL's ObjectSetup. */
void web_rel_object_setup(void *rel);
void web_rel_unlink(void *rel);
/* N99: what holds the code at `fn`: its REL (an opaque pointer, the same for all of a REL's functions), one other
 * pointer for code outside every REL (dol, libraries), NULL if `fn` is NULL or this can't be told (the RELs' marks
 * didn't hold in this link: see the boot log). As dladdr's dli_fbase on the desktop. */
const void *web_rel_function_owner(void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif
