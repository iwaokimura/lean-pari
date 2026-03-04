#pragma once
/*
 * pari_lean_internal.h
 *
 * Shared helpers used by pari_lean.c and pari_ell_lean.c.
 * Not part of the public API — do not include from Lean-generated C files.
 */

#include <lean/lean.h>
#include <pari/pari.h>

/* External class for GEN — defined in pari_lean.c */
extern lean_external_class *g_pari_gen_class;

/* GEN <-> lean_object* conversion helpers */
static inline lean_object *gen_to_lean(GEN x) {
    return lean_alloc_external(g_pari_gen_class, (void *)x);
}

static inline GEN lean_to_gen(lean_object *obj) {
    return (GEN)lean_get_external_data(obj);
}
