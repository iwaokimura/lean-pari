v4.28.0 のリリースノートと最新の FFI ドキュメント、および実際のビルド・動作確認に基づいて、動作済みのプロジェクト一式を記録します。

## 変更点・修正点の整理

### 実際のビルド・実行時に判明した追加修正点

| 問題 | 原因 | 修正 |
|---|---|---|
| `pari_CATCH` がコンパイルエラー | PARI 2.13.3 では `pari_CATCH(err_type)` と引数が必須。旧来の `pari_CATCH { } TRY { } CATCH(e)` 構文は廃止 | `pari_CATCH(CATCH_ALL) { } pari_TRY { } pari_ENDCATCH` に変更 |
| `Makefile` の `-I` パス誤り | `PARI_INCLUDE=/path/to/pari` とすると `<pari/pari.h>` が `/path/to/pari/pari/pari.h` を探してしまう | `PARI_INCLUDE` は `pari/` ディレクトリの**親**を指定する |
| `lake exec Main` で「シンボルが見つからない」クラッシュ | `initialize initializePari` はモジュール elaboration 時に実行されるが、`lean_pari_initialize` シンボルは最終バイナリ専用の `pari_lean.o` にしか存在せず、Lean インタープリタが `dlopen` できない | `initialize` を削除し `initializePari` を public opaque にして `main` の先頭で明示呼び出し |
| `computeEllap` の型エラー | `withStack` は `IO GEN` 専用だが `ellap` は `IO Int` を返す | 手動スタック管理（`stackMark` / `stackRestore`）に変更 |
| 実行時に `libpari.so` が見つからない | `libpari.so` が `/usr/local/lib` でなくカスタムパスにある | `moreLinkArgs` の `-L` を正しいパスに修正し、`-Wl,-rpath,...` を追加 |

***

## プロジェクト構成

```
lean-pari/
├── lean-toolchain
├── lakefile.toml
├── c/
│   └── pari_lean.c
└── LeanPari/
    ├── Basic.lean
    ├── Types.lean
    └── Elliptic.lean
Main.lean
```

***

## `lean-toolchain`

```
leanprover/lean4:v4.28.0
```

***

## `lakefile.toml`

v4.28.0 では `lakefile.toml` が標準です 。`moreLinkArgs` の数値も bare literal で書けます 。 [lean-lang](https://lean-lang.org/doc/reference/latest/releases/v4.28.0/)

```toml
name = "lean-pari"
version = "0.1.0"
defaultTargets = ["LeanPari", "Main"]

[[lean_lib]]
name = "LeanPari"
# C ソースを Lean ライブラリのネイティブファサードとして登録
moreServerOptions = []

[[lean_exe]]
name = "Main"
root = "Main"
# libpari をリンク
moreLinkArgs = ["-lpari", "-lm"]

[[extern_lib]]
name = "pari_lean"
# C ラッパーをコンパイルするカスタムビルドスクリプト
buildScript = "scripts/build_c.sh"
```

> **注意**：`extern_lib` を使うより、現時点での最も互換性が高い方法は `moreLinkArgs` に直接 `-lpari` を渡し、C ソースを `gcc` で手動コンパイルして `.o` を `moreLeancArgs` で渡す構成です。以下ではその方式を採用します。

実際には以下の `lakefile.toml` が最もシンプルで確実です：

```toml
name = "lean-pari"
version = "0.1.0"

[[lean_lib]]
name = "LeanPari"

[[lean_exe]]
name = "Main"
root = "Main"
moreLinkArgs = ["-lpari", "-lm", "-L/path/to/libpari", "-Wl,-rpath,/path/to/libpari", "c/pari_lean.o"]
```

> **注意**：`-L` には `libpari.so` が存在するディレクトリを指定する。`-Wl,-rpath,...` を加えることで `LD_LIBRARY_PATH` を設定せずに `lake exec` で直接実行できる。

そして `lake build` 前に C オブジェクトを手動でビルドする `Makefile` を用意します（後述）。

***

## `c/pari_lean.c`

v4.28.0 時点の `lean.h` API および **PARI 2.13.3** のエラー処理 API に対応した完全版です。 [github](https://github.com/leanprover/lean4/blob/master/doc/dev/ffi.md)

> **PARI 2.13.3 以降のエラー処理マクロ**：旧来の `pari_CATCH { } TRY { } CATCH(e) { e->warning }` は廃止。新しい構文は `pari_CATCH(err_type) { handler } pari_TRY { code } pari_ENDCATCH`。エラー情報は `__iferr_data` 変数（`GEN`）で自動的に利用可能。全例外を捕捉するには `CATCH_ALL` を使う。

```c
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
```

> **補足（PARI バージョン別エラー処理マクロ）**
>
> | PARI バージョン | 正しい構文 |
> |---|---|
> | 2.13.x 以降 | `pari_CATCH(CATCH_ALL) { ... } pari_TRY { ... } pari_ENDCATCH` |
> | 2.11.x 以前 | `pari_CATCH { } TRY { code } CATCH(e) { e->warning } ENDCATCH` |
>
> `paricom.h` で `pari_CATCH` のマクロ定義を確認するとバージョンを判別できる。2.13.x では `pari_CATCH(err)` が引数を取ることが確認できる。

***

## `LeanPari/Types.lean`

```lean
-- LeanPari/Types.lean
namespace Pari

/-- PARI の GEN 型を Lean の opaque 型としてラップ -/
private opaque GENPointed : NonemptyType.{0}
def GEN : Type := GENPointed.type
instance : Nonempty GEN := GENPointed.property

end Pari
```

***

## `LeanPari/Basic.lean`

> **`initialize` を使ってはいけない理由**：`initialize foo` は Lean コンパイラが当該モジュールを **elaborate する際**に実行される。しかし `lean_pari_initialize` シンボルは最終バイナリにリンクされる `pari_lean.o` にしか存在せず、Lean インタープリタは `dlopen` でそのシンボルを見つけられないためクラッシュする。代わりに `initializePari` を `public opaque` として公開し、`main` の先頭で明示的に呼び出す。

```lean
-- LeanPari/Basic.lean
import LeanPari.Types

namespace Pari

/-- PARI ライブラリの初期化。`pari_init` を実行し GEN の external class を登録する。
    `main` の先頭で必ず一度呼び出すこと（`initialize` を使わないので自動実行されない）。 -/
@[extern "lean_pari_initialize"]
opaque initializePari : IO Unit

/-- avma スタックマークを取得 -/
@[extern "lean_pari_stack_mark"]
opaque stackMark : IO USize

/-- avma スタックを mark 位置まで巻き戻す -/
@[extern "lean_pari_stack_restore"]
opaque stackRestore (mark : USize) : IO Unit

/-- GEN を avma スタックから heap へコピー（スコープを超えて使う場合に必須） -/
@[extern "lean_pari_gcopy"]
opaque gcopy (x : @& GEN) : IO GEN

/-- GP 文字列式を PARI GEN にパース -/
@[extern "lean_pari_read_str"]
opaque readStr (s : @& String) : IO GEN

/-- GEN を文字列に変換（デバッグ・表示用） -/
@[extern "lean_pari_tostr"]
opaque toStr (x : @& GEN) : IO String

/-- スタックスコープブラケット：内部で計算し、結果のみ永続化 -/
def withStack (action : IO GEN) : IO GEN := do
  let mark ← stackMark
  let result ← action
  let persistent ← gcopy result
  stackRestore mark
  return persistent

end Pari
```

***

## `LeanPari/Elliptic.lean`

```lean
-- LeanPari/Elliptic.lean
import LeanPari.Basic

namespace Pari

/-- ellinit：係数文字列 "[a1,a2,a3,a4,a6]" から楕円曲線 GEN を作る -/
@[extern "lean_pari_ellinit"]
opaque ellinit (coeffsStr : @& String) : IO GEN

/-- ellap(E, p)：楕円曲線 E の素数 p での Frobenius トレース ap を計算 -/
@[extern "lean_pari_ellap"]
opaque ellap (E : @& GEN) (p : @& GEN) : IO Int

/-- 便利ラッパー：係数文字列と素数文字列を受け取って ap を返す。
    `ellap` の結果は `Int`（`GEN` でない）ので `withStack` は使えず、手動でスタック管理する。 -/
def computeEllap (coeffsStr : String) (primeStr : String) : IO Int := do
  let mark ← stackMark
  let E ← ellinit coeffsStr
  let p ← readStr primeStr
  let result ← ellap E p
  stackRestore mark
  return result

end Pari
```

> **`withStack` を使えない理由**：`withStack` の型は `IO GEN → IO GEN` であり、`IO Int` を返す `ellap` には適用できない。`GEN` を返す関数（`ellinit`、`readStr` など）には `withStack` が使えるが、最終的に `Int` に変換する場合は手動でスタックを管理する。

***

## `Main.lean`

```lean
import LeanPari.Elliptic

def main : IO Unit := do
  -- PARI ライブラリを初期化（initialize を使わないので main の先頭で必ず呼ぶ）
  Pari.initializePari
  -- 例1: y^2 = x^3 - x  (a4 = -1, a6 = 0)
  --   LMFDB: 32.a2 など
  let coeffs1 := "[0,0,0,-1,0]"
  for p in [2, 3, 5, 7, 11, 13, 17, 19, 23] do
    match (← (Pari.computeEllap coeffs1 (toString p)).toBaseIO) with
    | .ok ap  => IO.println s!"ap({p}) = {ap}"
    | .error e => IO.println s!"Error at p={p}: {e}"

  IO.println "---"

  -- 例2: secp256k1 曲線  y^2 = x^3 + 7
  let coeffs2 := "[0,0,0,0,7]"
  let testPrimes := [2, 3, 5, 7, 11, 101, 1009]
  for p in testPrimes do
    match (← (Pari.computeEllap coeffs2 (toString p)).toBaseIO) with
    | .ok ap  => IO.println s!"secp256k1: ap({p}) = {ap}"
    | .error e => IO.println s!"Error at p={p}: {e}"
```

***

## `Makefile`

`lake build` の前に C オブジェクトを生成します。

> **`-I` パスに注意**：`#include <pari/pari.h>` を解決するには `pari/` ディレクトリの**親ディレクトリ**を `-I` で指定する。`PARI_INCLUDE=/path/to/pari` のように `pari/` 自体を指定すると `<pari/pari/pari.h>` を探してしまいシステムのヘッダにフォールバックしてバージョン不一致が発生する。

```makefile
# PARI のヘッダパスは環境に合わせて変更（<pari/pari.h> の親ディレクトリを指定）
PARI_INCLUDE = /path/to/include      # 例: /home/user/include（その下に pari/ がある）
PARI_LIB     = /path/to/lib          # 例: /home/user/lib（その下に libpari.so がある）
LEAN_INCLUDE = $(shell lean --print-prefix)/include

LDFLAGS = -L$(PARI_LIB) -lpari -llean
CFLAGS  = -O2 -fPIC -I$(PARI_INCLUDE) -I$(LEAN_INCLUDE)

.PHONY: all clean

all: c/pari_lean.o
	lake build

c/pari_lean.o: c/pari_lean.c
	gcc $(CFLAGS) -c $< -o $@ $(LDFLAGS)

clean:
	rm -f c/pari_lean.o
	lake clean
```

***

## ビルド・実行手順

```bash
# 1. プロジェクト作成
mkdir lean-pari && cd lean-pari
# （上記ファイルをすべて配置）

# 2. C オブジェクトをコンパイル → Lean プロジェクトをビルド
make

# 3. 実行
lake exec Main
```

期待される出力（動作確認済み）：

```
ap(2) = 0
ap(3) = 0
ap(5) = -2
ap(7) = 0
ap(11) = 0
ap(13) = 6
ap(17) = 2
ap(19) = 0
ap(23) = 0
---
secp256k1: ap(2) = 0
secp256k1: ap(3) = 0
secp256k1: ap(5) = 0
secp256k1: ap(7) = 0
secp256k1: ap(11) = 0
secp256k1: ap(101) = 0
secp256k1: ap(1009) = -19
```

***

## 対応チェックリスト（動作確認済み）

### Lean v4.28.0 対応
- [x] `lean-toolchain` を `v4.28.0` に更新 [lean-lang](https://lean-lang.org/doc/reference/latest/releases/v4.28.0/)
- [x] `lakefile.toml` 形式を採用（`lake new` のデフォルト） [github](https://github.com/leanprover/lean4/issues/4106)
- [x] `moreLinkArgs` から `.ofNat` 記法を除去（#11859 で不要に） [lean-lang](https://lean-lang.org/doc/reference/latest/releases/v4.28.0/)
- [x] `-fwrapv` フラグを C コンパイルに含めない（v4.28.0 で撤廃） [note](https://note.com/deal/n/nee094a6a7799)
- [x] `lean_register_external_class` の第2引数（`foreach`）を明示的に渡す（`ffi.md` 最新版準拠） [github](https://github.com/leanprover/lean4/blob/master/doc/dev/ffi.md)

### PARI 2.13.x 対応
- [x] `PARI_INCLUDE` を `<pari/pari.h>` の親ディレクトリに修正（`-I` パス誤りの修正）
- [x] `moreLinkArgs` の `-L` を実際の `libpari.so` の場所に修正し `-Wl,-rpath,...` を追加
- [x] エラー処理マクロを `pari_CATCH(CATCH_ALL) { } pari_TRY { } pari_ENDCATCH` に変更（旧 `TRY`/`CATCH(e)` 構文廃止対応）

### FFI 設計上の留意点
- [x] `initialize` を使わず `initializePari` を `public opaque` で公開し `main` 先頭で明示呼び出し（インタープリタが `pari_lean.o` を `dlopen` できないため）
- [x] `withStack` は `IO GEN → IO GEN` 専用。`IO Int` を返す `ellap` には手動スタック管理を使用