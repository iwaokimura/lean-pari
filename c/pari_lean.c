#include "pari_lean_internal.h"
#include <string.h>

/* ================================================================
 * 1. External class の登録（GEN ラッパー用）
 * ================================================================ */

/* g_pari_gen_class は pari_ell_lean.c からも参照されるため非 static で定義 */
lean_external_class *g_pari_gen_class = NULL;

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
 * 3. avma スタック管理
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
 * 4. 文字列パース（gp_read_str 経由）
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
 * 5. GEN を文字列に変換（デバッグ用）
 * ================================================================ */
LEAN_EXPORT lean_obj_res lean_pari_tostr(b_lean_obj_arg gen_obj, lean_obj_arg world) {
    GEN x = lean_to_gen(gen_obj);
    char *s = GENtostr(x);   // PARI が malloc した文字列
    lean_object *result = lean_mk_string(s);
    free(s);
    return lean_io_result_mk_ok(result);
}

/* ================================================================
 * 6. Lean ↔ PARI 基本型相互変換（issue #5）
 * ================================================================ */

/* Float → GEN（dbltor: double → PARI t_REAL） */
LEAN_EXPORT lean_obj_res lean_pari_float_to_gen(double f, lean_obj_arg world) {
    GEN x = dbltor(f);
    return lean_io_result_mk_ok(gen_to_lean(x));
}

/* GEN → Int（itos: PARI t_INT → long → Lean Int） */
LEAN_EXPORT lean_obj_res lean_pari_gen_to_int(b_lean_obj_arg gen_obj, lean_obj_arg world) {
    GEN x = lean_to_gen(gen_obj);
    long val;
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI gen_to_int error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        val = itos(x);
    } pari_ENDCATCH;
    return lean_io_result_mk_ok(lean_int64_to_int((int64_t)val));
}

/* GEN → List Int（PARI t_VEC を走査して Lean List を構築） */
LEAN_EXPORT lean_obj_res lean_pari_gen_to_list_int(b_lean_obj_arg gen_obj, lean_obj_arg world) {
    GEN v = lean_to_gen(gen_obj);
    if (typ(v) != t_VEC && typ(v) != t_COL)
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("PARI: not a vector")));
    long n = lg(v) - 1;               /* PARI vector: 1-indexed, length = lg(v)-1 */
    lean_object *list = lean_box(0);  /* List.nil */
    for (long i = n; i >= 1; i--) {  /* 末尾から逆順にcons してリストを構築 */
        long val = itos(gel(v, i));   /* gel(v,i): i番目要素（1-indexed） */
        lean_object *cons = lean_alloc_ctor(1, 2, 0); /* List.cons */
        lean_ctor_set(cons, 0, lean_int64_to_int((int64_t)val));
        lean_ctor_set(cons, 1, list);
        list = cons;
    }
    return lean_io_result_mk_ok(list);
}

/* GEN → Float（gtodouble: PARI 数値型 → double） */
LEAN_EXPORT lean_obj_res lean_pari_gen_to_float(b_lean_obj_arg gen_obj, lean_obj_arg world) {
    GEN x = lean_to_gen(gen_obj);
    double result = gtodouble(x);
    return lean_io_result_mk_ok(lean_box_float(result));
}
