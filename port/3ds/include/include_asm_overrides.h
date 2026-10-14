/* port/3ds/include/include_asm_overrides.h
 *
 * Hand-maintained file. Grows wave-by-wave in M4 phase 2 as we
 * encounter INCLUDE_ASM call sites that need the panic-stub
 * treatment (no NON_MATCHING C path exists and no shim provides
 * the symbol).
 *
 * Format: a per-symbol #define that expands to the stub body.
 *
 * Example (commented out — actual entries land per-wave):
 *
 *   #define INCLUDE_ASM_some_func(TYPE, FOLDER, NAME, ARGS...) \
 *       TYPE NAME(ARGS) { pm3ds_panic("INCLUDE_ASM unresolved: " #NAME); }
 *
 * Then the matching call in src/foo.c, e.g.,
 *   INCLUDE_ASM(s32, foo_dir, some_func, s32 x);
 * needs an explicit
 *   #define INCLUDE_ASM INCLUDE_ASM_some_func
 * right before it — OR a more general dispatch keyed on NAME.
 *
 * The wave-N commit listing INCLUDE_ASM stubs identifies which
 * symbol names landed here in that wave.
 */

#ifndef PORT_3DS_INCLUDE_ASM_OVERRIDES_H
#define PORT_3DS_INCLUDE_ASM_OVERRIDES_H

/* (empty in phase 1 — grows in phase 2 waves) */

#endif
