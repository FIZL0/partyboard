/* N137: the Linux release (tools/linux-release.sh) is built against glibc 2.31 (Steam Runtime sniper) so it starts
 * on older distros, but aurora's prebuilt Dawn (libwebgpu_dawn.a) was compiled on a newer one and references a few
 * symbols that glibc 2.31 and sniper's static libstdc++ don't have. These stand in for them inside libdol.so only
 * (hidden: nothing else in the process binds to them). Built when PARTY_BOARD_GLIBC_COMPAT is on (the linux-release
 * preset). */
#include <stdlib.h>

#define PB_HIDDEN __attribute__((visibility("hidden")))

/* glibc 2.38's C23 strto* (they also take a 0b prefix with base 0 or 2; Dawn parses decimal and hex) */
PB_HIDDEN long __isoc23_strtol(const char *s, char **end, int base) { return strtol(s, end, base); }
PB_HIDDEN long long __isoc23_strtoll(const char *s, char **end, int base) { return strtoll(s, end, base); }
PB_HIDDEN unsigned long __isoc23_strtoul(const char *s, char **end, int base) { return strtoul(s, end, base); }
PB_HIDDEN unsigned long long __isoc23_strtoull(const char *s, char **end, int base) { return strtoull(s, end, base); }

/* glibc 2.32: libstdc++'s headers skip atomics while it is non-zero. 0 = always take the atomic path, which is safe. */
PB_HIDDEN char __libc_single_threaded = 0;

/* GCC 13's <iostream> references std::ios_base_library_init() only to pull libstdc++'s stream init into the link;
 * the older static libstdc++ sets the streams up through ios_base::Init objects instead. */
PB_HIDDEN void pb_ios_base_library_init(void) __asm__("_ZSt21ios_base_library_initv");
void pb_ios_base_library_init(void) {}
