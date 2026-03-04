#include <lean/lean.h>
#include <pari/pari.h>
#include <string.h>

/* ================================================================
 * 1. External class の登録（GEN ラッパー用）
 * ================================================================ */

static lean_external_class *g_pari_gen_class = NULL;

// GEN は PARI の avma スタックが管理するので Lean 側では何もしない
static void pari_gen_finalize(void *ptr) { (void)ptr; }

// 子オブジェクトの巡回は不要（GEN はポインタ1つ）
static void pari_gen_foreach(void *ptr, b_lean_obj_arg f) { (void)ptr; (void)f; }

/* ================================================================
 * 2. モジュール初期化： initialize lean_pari_initialize
 * ================================================================ */
LEAN_EXPORT lean_obj_res lean_pari_initialize(lean_obj_arg world) {
    pari_init(8000000, 500000);  // stack: 8MB, maxprime: 500000
    g_pari_gen_class = lean_register_external_class(
        pari_gen_finalize,
        pari_gen_foreach
    );
    return lean_io_result_mk_ok(lean_box(0));
}

/* ================================================================
 * 3. GEN <-> lean_object* 変換ヘルパー（内部使用）
 * ================================================================ */
static lean_object *gen_to_lean(GEN x) {
    return lean_alloc_external(g_pari_gen_class, (void *)x);
}

static GEN lean_to_gen(lean_object *obj) {
    return (GEN)lean_get_external_data(obj);
}

/* ================================================================
 * 4. avma スタック管理
 * ================================================================ */
LEAN_EXPORT lean_obj_res lean_pari_stack_mark(lean_obj_arg world) {
    pari_sp mark = avma;
    return lean_io_result_mk_ok(lean_box_usize((size_t)mark));
}

LEAN_EXPORT lean_obj_res lean_pari_stack_restore(size_t mark, lean_obj_arg world) {
    avma = (pari_sp)mark;
    return lean_io_result_mk_ok(lean_box(0));
}

/* GEN を avma スタックから heap へコピーして永続化 */
LEAN_EXPORT lean_obj_res lean_pari_gcopy(b_lean_obj_arg gen_obj, lean_obj_arg world) {
    GEN x = lean_to_gen(gen_obj);
    GEN y = gcopy(x);
    return lean_io_result_mk_ok(gen_to_lean(y));
}

/* ================================================================
 * 5. 文字列パース（gp_read_str 経由）
 * ================================================================ */
LEAN_EXPORT lean_obj_res lean_pari_read_str(b_lean_obj_arg s, lean_obj_arg world) {
    const char *cstr = lean_string_cstr(s);
    GEN x = NULL;
    // PARI 2.13+ のエラー処理: pari_CATCH(err_type) { handler } pari_TRY { code } pari_ENDCATCH
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        x = gp_read_str(cstr);
    } pari_ENDCATCH;
    if (x == NULL) {
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("PARI: gp_read_str returned NULL")));
    }
    return lean_io_result_mk_ok(gen_to_lean(x));
}

/* ================================================================
 * 6. GEN を文字列に変換（デバッグ用）
 * ================================================================ */
LEAN_EXPORT lean_obj_res lean_pari_tostr(b_lean_obj_arg gen_obj, lean_obj_arg world) {
    GEN x = lean_to_gen(gen_obj);
    char *s = GENtostr(x);   // PARI が malloc した文字列
    lean_object *result = lean_mk_string(s);
    free(s);
    return lean_io_result_mk_ok(result);
}

/* ================================================================
 * 7. ellap(E, p) — 楕円曲線の Frobenius トレース
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
    // ap が small integer か bignum かで変換方法が異なる
    if (signe(ap) == 0) {
        return lean_io_result_mk_ok(lean_int64_to_int(0));
    }
    long val = itos(ap);  // GEN → long（範囲外は PARI がエラー）
    return lean_io_result_mk_ok(lean_int64_to_int((int64_t)val));
}

/* ================================================================
 * 8. ellinit のラッパー
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
