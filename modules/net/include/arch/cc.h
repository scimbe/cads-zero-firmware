/*
 * CaDS Zero - lwIP compiler/platform port for the itsboard target.
 *
 * See lib/lwip/src/include/lwip/arch.h for what each override here replaces
 * and why it exists: everything not mentioned below (packed-struct macros,
 * stdint/stddef/ctype includes, LWIP_MEM_ALIGN...) already has a correct
 * GCC/ARM default in lwIP itself and needs nothing from this file.
 */

#ifndef CADS_LWIP_ARCH_CC_H
#define CADS_LWIP_ARCH_CC_H

#include "cads_hal.h"

#define BYTE_ORDER LITTLE_ENDIAN

/*
 * lwIP's own defaults for LWIP_PLATFORM_DIAG/_ASSERT pull in printf, fflush
 * and abort - the newlib formatted-I/O chain, which needs _sbrk to grow a
 * heap this firmware's linker script leaves undefined on purpose (no heap,
 * ever). modules/storage hit exactly this failure once already with
 * littlefs's own default assert (see modules/storage/CMakeLists.txt's
 * LFS_NO_ASSERT comment) - not repeating it here.
 *
 * DIAG is purely informational and only ever reached through LWIP_DEBUGF,
 * which compiles to nothing with LWIP_DEBUG off (lwipopts.h), so a no-op
 * costs nothing in this build. ASSERT stays a real fatal stop - routed
 * through the HAL panic path (which prints over the console UART and halts)
 * instead of through libc.
 */
#define LWIP_PLATFORM_DIAG(x) do { } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { cads_hal_panic(x); } while(0)

/*
 * NOT newlib's rand(): arm-none-eabi's newlib-nano rand() lazily malloc()s
 * a state table on its first call (confirmed by inspecting libc_a-rand.o's
 * undefined symbols - it pulls in malloc, and from there _sbrk, and this
 * firmware's linker script has no `end` symbol for _sbrk to find, by
 * design - no heap, ever). The exact same failure class as the littlefs
 * LFS_NO_ASSERT bug, just reached through rand() instead of assert().
 * cads_net_board.c defines and seeds a tiny xorshift32 instead.
 */
/* Hardware RNG per call, counted fallback otherwise - cads/net/rand.h. */
uint32_t cads_lwip_rand(void);
#define LWIP_RAND() ((u32_t)cads_lwip_rand())

#endif /* CADS_LWIP_ARCH_CC_H */
