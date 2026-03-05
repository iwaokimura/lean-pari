# Plan: Lean基本型 ↔ PARI基本型 相互変換レイヤーの追加

closes #5

## Context

現状、LeanからPARIへのデータ渡しは全て文字列経由（`gp_read_str`）で行っている。
例：`computeEllap "[0,0,0,0,7]" "101"` — `List Int`や`Int`を文字列にしてから渡す。

目標：Leanネイティブ型とPARI型を直接相互変換できる変換レイヤーを追加し、
`computeEllap [0,0,0,0,7] 101` のように書けるようにする。

対象型：`Int ↔ t_INT`、`Nat ↔ t_INT`、`List Int ↔ t_VEC`、`Float ↔ t_REAL`

---

## 変換方針

### Lean → PARI（→ 方向）

**文字列経由（新規Cコード不要）**
既存の `readStr`（= `gp_read_str`）を活用。Leanの `toString` は任意精度整数も正しく変換できるため安全。

- `Int → GEN` : `readStr (toString n)`
- `Nat → GEN` : `readStr (toString n)`
- `List Int → GEN` : `readStr ("[" ++ ",".intercalate (l.map toString) ++ "]")`
- `Float → GEN` : 専用C関数（`dbltor(f)` 使用）— `toString`はFloat精度が不十分なため

### PARI → Lean（← 方向）

**専用C関数が必要**（GENの型タグを検査し値を抽出する必要がある）

- `GEN → Int` : C関数で `signe` / `itos` を使用
- `GEN → List Int` : C関数でPARI vectorを走査
- `GEN → Float` : C関数で `gtodouble` を使用

---

## 実装内容

### 1. 新規作成: `LeanPari/Conv.lean`

`Basic.lean` の既存 `readStr` / `toStr` を活用した変換関数をまとめる新ファイル。

```lean
import LeanPari.Basic
namespace Pari

-- Lean → PARI（文字列経由、既存 readStr を再利用）
def intToGen  (n : Int)      : IO GEN := readStr (toString n)
def natToGen  (n : Nat)      : IO GEN := readStr (toString n)
def listIntToGen (l : List Int) : IO GEN :=
  readStr ("[" ++ ",".intercalate (l.map toString) ++ "]")

-- Float → PARI（C関数 dbltor 使用）
@[extern "lean_pari_float_to_gen"]
opaque floatToGen (f : Float) : IO GEN

-- PARI → Lean（C関数で値を抽出）
@[extern "lean_pari_gen_to_int"]
opaque genToInt (g : @& GEN) : IO Int

@[extern "lean_pari_gen_to_list_int"]
opaque genToListInt (g : @& GEN) : IO (List Int)

@[extern "lean_pari_gen_to_float"]
opaque genToFloat (g : @& GEN) : IO Float

end Pari
```

### 2. 変更: `c/pari_lean.c` — C変換関数を追加

以下の4関数を追記：

```c
// Float → GEN（dbltor: double → PARI t_REAL）
LEAN_EXPORT lean_obj_res lean_pari_float_to_gen(double f, lean_obj_arg world) {
    GEN x = dbltor(f);
    return lean_io_result_mk_ok(gen_to_lean(x));
}

// GEN → Int（itos: PARI t_INT → long → Lean Int）
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

// GEN → List Int（PARI t_VEC を走査してLean Listを構築）
LEAN_EXPORT lean_obj_res lean_pari_gen_to_list_int(b_lean_obj_arg gen_obj, lean_obj_arg world) {
    GEN v = lean_to_gen(gen_obj);
    if (typ(v) != t_VEC && typ(v) != t_COL)
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("PARI: not a vector")));
    long n = lg(v) - 1;              // PARI vector: 1-indexed, length = lg(v)-1
    lean_object *list = lean_box(0); // List.nil
    for (long i = n; i >= 1; i--) { // 末尾から逆順にconsしてリストを構築
        long val = itos(gel(v, i));  // gel(v,i): i番目要素（1-indexed）
        lean_object *cons = lean_alloc_ctor(1, 2, 0); // List.cons
        lean_ctor_set(cons, 0, lean_int64_to_int((int64_t)val));
        lean_ctor_set(cons, 1, list);
        list = cons;
    }
    return lean_io_result_mk_ok(list);
}

// GEN → Float（gtodouble: PARI 数値型 → double）
LEAN_EXPORT lean_obj_res lean_pari_gen_to_float(b_lean_obj_arg gen_obj, lean_obj_arg world) {
    GEN x = lean_to_gen(gen_obj);
    double result = gtodouble(x);
    return lean_io_result_mk_ok(lean_box_float(result));
}
```

> **注意：** `lean_box_float` の正確なシグネチャは実装時に `lean.h` で確認すること。

### 3. 変更: `c/pari_ell_lean.c` — `ellinit` のGEN直接受け取り版を追加

現状の `lean_pari_ellinit` は 文字列 → `gp_read_str` → `ellinit` の経路。
`Conv.lean` の `listIntToGen` で変換したGENをそのまま渡せる新関数を追加：

```c
// GEN（t_VEC）を直接受け取る ellinit ラッパー
LEAN_EXPORT lean_obj_res lean_pari_ellinit_gen(
    b_lean_obj_arg coeffs_obj, lean_obj_arg world)
{
    GEN coeffs = lean_to_gen(coeffs_obj);
    GEN E = NULL;
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI ellinit error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        E = ellinit(coeffs, NULL, DEFAULTPREC);
    } pari_ENDCATCH;
    return lean_io_result_mk_ok(gen_to_lean(E));
}
```

### 4. 変更: `LeanPari/Elliptic.lean` — ネイティブ型APIを追加

既存の文字列ベースAPIを残しつつ（後方互換性）、新APIを追加：

```lean
import LeanPari.Conv  -- 追加

-- GEN直接受け取り版（Conv.lean の listIntToGen と組み合わせて使う）
@[extern "lean_pari_ellinit_gen"]
opaque ellinitGen (coeffs : @& GEN) : IO GEN

-- 新API: List Int と Int をネイティブで受け取る
def computeEllapNative (coeffs : List Int) (p : Int) : IO Int := do
  let mark    ← stackMark
  let coeffsG ← listIntToGen coeffs   -- List Int → t_VEC
  let E       ← ellinitGen coeffsG    -- t_VEC → 楕円曲線 GEN
  let pG      ← intToGen p            -- Int → t_INT
  let result  ← ellap E pG
  stackRestore mark
  return result
```

スタック管理の流れ（既存 `computeEllap` と同じパターン）：
- `stackMark` で avma を保存
- `coeffsG`、`E`、`pG` は全て avma スタック上に確保
- `ellap` は `Int`（Leanネイティブ値）を返す → avmaに依存しない
- `stackRestore` でスタックを巻き戻し（E、pGのGENポインタは無効化されるが、以降は使わない）

### 5. 変更: `Main.lean` — 文字列をネイティブ型に置き換え

```lean
import LeanPari.Elliptic

def main : IO Unit := do
  Pari.initializePari
  let coeffs1 : List Int := [0, 0, 0, -1, 0]   -- y^2 = x^3 - x
  for p : Int in [2, 3, 5, 7, 11, 13, 17, 19, 23] do
    match (← (Pari.computeEllapNative coeffs1 p).toBaseIO) with
    | .ok ap   => IO.println s!"ap({p}) = {ap}"
    | .error e => IO.println s!"Error at p={p}: {e}"
  IO.println "---"
  let coeffs2 : List Int := [0, 0, 0, 0, 7]    -- secp256k1: y^2 = x^3 + 7
  let testPrimes : List Int := [2, 3, 5, 7, 11, 101, 1009]
  for p in testPrimes do
    match (← (Pari.computeEllapNative coeffs2 p).toBaseIO) with
    | .ok ap   => IO.println s!"secp256k1: ap({p}) = {ap}"
    | .error e => IO.println s!"Error at p={p}: {e}"
```

### 6. 変更: `LeanPari.lean` — Conv を再エクスポート

```lean
import LeanPari.Basic
import LeanPari.Conv   -- 追加
```

---

## 変更ファイル一覧

| ファイル | 変更種別 | 内容 |
|---|---|---|
| `LeanPari/Conv.lean` | **新規作成** | Lean ↔ PARI 型変換レイヤー |
| `c/pari_lean.c` | 変更（追記） | `gen_to_int`、`gen_to_list_int`、`float_to_gen`、`gen_to_float` |
| `c/pari_ell_lean.c` | 変更（追記） | `ellinit_gen` |
| `LeanPari/Elliptic.lean` | 変更（追記） | `ellinitGen`、`computeEllapNative` |
| `Main.lean` | 変更（置き換え） | 文字列 → ネイティブ型 |
| `LeanPari.lean` | 変更（1行追加） | `import LeanPari.Conv` |

**変更なし：** `Makefile`、`lakefile.toml`、`LeanPari/Types.lean`、`LeanPari/Basic.lean`

---

## 注意事項・制約

- `lean_pari_gen_to_int` / `gen_to_list_int`：PARIの `itos()` は `long` 範囲外でエラーを投げる。`pari_CATCH` でラップ済み。任意精度整数が必要な場合は将来的に `GENtostr` + 文字列パースへの拡張を検討。
- `lean_box_float`：実装時に `lean.h` で正確なシグネチャを確認すること。
- 既存の文字列ベース `computeEllap` と `ellinit` は削除せず残す（後方互換性）。

---

## 動作確認方法

```bash
make          # C .o を再コンパイル（pari_lean.c, pari_ell_lean.c が変更されているため必須）
lake exec Main
```

期待出力（現在と同一）：

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
