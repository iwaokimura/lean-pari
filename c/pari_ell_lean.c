/*
 * pari_ell_lean.c
 *
 * Lean 4 C FFI bindings for PARI elliptic-curve functions.
 * Separated from pari_lean.c per issue #1.
 *
 * Exposed symbols:
 *   lean_pari_ellinit  —  wraps ellinit()
 *   lean_pari_ellap    —  wraps ellap()
 */

#include "pari_lean_internal.h"

/* ================================================================
 * 1. ellinit のラッパー
 *    係数文字列 "[a1,a2,a3,a4,a6]" から楕円曲線 GEN を作る
 * ================================================================ */
LEAN_EXPORT lean_obj_res lean_pari_ellinit(
    b_lean_obj_arg coeffs_str,
    lean_obj_arg world)
{
    const char *cstr = lean_string_cstr(coeffs_str);
    GEN E = NULL;
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI ellinit error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        GEN coeffs = gp_read_str(cstr);
        E = ellinit(coeffs, NULL, DEFAULTPREC);
    } pari_ENDCATCH;
    return lean_io_result_mk_ok(gen_to_lean(E));
}

/* ================================================================
 * 2. ellap(E, p) — 楕円曲線の Frobenius トレース a_p を計算
 * ================================================================ */
LEAN_EXPORT lean_obj_res lean_pari_ellap(
    b_lean_obj_arg e_obj,
    b_lean_obj_arg p_obj,
    lean_obj_arg world)
{
    GEN E = lean_to_gen(e_obj);
    GEN p = lean_to_gen(p_obj);
    GEN ap = NULL;
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI ellap error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        ap = ellap(E, p);
    } pari_ENDCATCH;
    /* ap が small integer か bignum かで変換方法が異なる */
    if (signe(ap) == 0) {
        return lean_io_result_mk_ok(lean_int64_to_int(0));
    }
    long val = itos(ap);  /* GEN → long（範囲外は PARI がエラー） */
    return lean_io_result_mk_ok(lean_int64_to_int((int64_t)val));
}

/* ================================================================
 * 3. ellinit_gen — GEN（t_VEC）を直接受け取る ellinit ラッパー（issue #5）
 * ================================================================ */
LEAN_EXPORT lean_obj_res lean_pari_ellinit_gen(
    b_lean_obj_arg coeffs_obj,
    lean_obj_arg world)
{
    GEN coeffs = lean_to_gen(coeffs_obj);
    GEN E = NULL;
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI ellinit error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        E = ellinit(coeffs, NULL, DEFAULTPREC);
    } pari_ENDCATCH;
    return lean_io_result_mk_ok(gen_to_lean(E));
}
