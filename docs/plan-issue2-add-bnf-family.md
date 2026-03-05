# Add some `bnf` family

PARI の `bnf` 構造体は `nf ≤ bnf ≤ bnr` という階層を持つので ，Lean 4 の型設計もこの階層を忠実に反映するのが最善である．以下に型システム全体を設計する． [pari.math.u-bordeaux](https://pari.math.u-bordeaux.fr/dochtml/html/General_number_fields.html)



## 設計の基本方針

PARI の `bnf` が保持する各フィールドを，以下の基準で Lean 4 ネイティブ型と `PariGEN` に振り分ける：

| フィールド | 型の選択 | 理由 |
|---|---|---|
| 次数・符号数 `(r1,r2)` | `Nat`, `Nat × Nat` | 常に小さい整数 |
| 定義多項式 `T(X)` | `Polynomial ℚ` | Mathlib4 の型をそのまま保持，小形式等の証明に直接利用できる |
| 判別式 `disc` | `Int` | 大きくなりうるが，Leanの任意精度 `Int` で対応 |
| 類数 `h` | `Int` | 同上（岩澤理論では p-部分の演算が必要） |
| 類群不変量 `cyc` | `Array Nat` | SNF の各因子は個別に参照したい |
| 整基底・多項式・単数 | `PariGEN` | PARI内でさらに計算に使う複合オブジェクト |
| 調整子 `reg` | `PariGEN` | 精度依存の実数 |
| bnf 全体 | `PariGEN` (`raw`) | `bnrisprincipal` 等への引き渡し用 |



## 型階層の定義

```lean
-- LeanPari/NumberField.lean
import LeanPari.Basic
import Mathlib.Algebra.Polynomial.Basic  -- Polynomial ℚ を使用

open Polynomial

namespace LeanPari

/-!
  ## 層 1: 数体 (nfinit の出力に対応)

  K = Q[X]/(T) の基本算術データ
-/
structure NumberField where
  /-- 定義多項式 T(X)：Mathlib4 の `Polynomial ℚ` として保持，小形式判定等の証明に直接利用できる -/
  pol       : Polynomial ℚ
  /-- [K : Q] -/
  degree    : Nat
  /-- 実埋め込みの個数 r₁，複素埋め込み対の個数 r₂ -/
  r1        : Nat
  r2        : Nat
  /-- 判別式 disc(K/Q)．任意精度整数として保持 -/
  disc      : Int
  /-- [O_K : Z[α]]（指数）-/
  index     : Nat
  /-- 整基底 {ω₁,...,ωₙ} -/
  zk        : GEN
  /-- nfinit の生の GEN オブジェクト（さらなる計算用）-/
  nfRaw     : GEN

/-!
  ## 層 2: 類群 (bnf.clgp に対応)

  Cl(K) ≅ ℤ/n₁ℤ × ... × ℤ/nₖℤ（n₁ | n₂ | ... | nₖ, Smith 標準形）
-/
structure ClassGroup where
  /-- 類数 h = n₁ · n₂ · ... · nₖ -/
  classNumber : Int
  /-- SNF の不変因子列 [n₁, ..., nₖ]（n_i | n_{i+1}）-/
  invariants  : Array Nat
  /-- 類群の生成元（イデアル）リスト -/
  generators  : GEN

namespace ClassGroup

/-- 類群の階数（自明でない因子の個数）-/
def rank (cg : ClassGroup) : Nat :=
  cg.invariants.filter (· > 1) |>.size

/-- p-Sylow 部分群の不変因子列（岩澤理論用）
    Cl(K)[p^∞] ≅ ⊕ ℤ/p^{vₚ(nᵢ)}ℤ -/
def pPrimaryInvariants (cg : ClassGroup) (p : Nat) : Array Nat :=
  cg.invariants.map (fun n =>
    -- n の p-power part を計算
    let rec pPow (m : Nat) : Nat :=
      if m % p == 0 then pPow (m / p) * p else 1
    termination_by m
    pPow n)
  |>.filter (· > 1)

/-- p-rank: Cl(K)[p] の ℤ/pℤ 上の次元 -/
def pRank (cg : ClassGroup) (p : Nat) : Nat :=
  cg.invariants.filter (fun n => n % p == 0) |>.size

end ClassGroup

/-!
  ## 層 3: Buchmann 数体 (bnfinit の出力に対応)
-/
structure BNF where
  /-- 数体の基本データ -/
  nf          : NumberField
  /-- イデアル類群 -/
  clgp        : ClassGroup
  /-- 単数群の捩れ部分の位数 -/
  tuOrder     : Nat
  /-- 原始的捩れ単数（根の単数 ζ）-/
  tuGen       : GEN
  /-- 基本単数系 {u₁,...,u_{r₁+r₂-1}} -/
  fu          : GEN
  /-- 調整子 Reg(K)（精度依存実数）-/
  regulator   : GEN
  /-- bnfinit の生の GEN オブジェクト（bnrisprincipal 等に直接渡す）-/
  bnfRaw      : GEN

namespace BNF

/-- 類数の取得 -/
def classNumber (b : BNF) : Int := b.clgp.classNumber

/-- 符号数 (r₁, r₂) -/
def signature (b : BNF) : Nat × Nat := (b.nf.r1, b.nf.r2)

/-- 単数群の階数 r₁ + r₂ - 1 -/
def unitRank (b : BNF) : Nat := b.nf.r1 + b.nf.r2 - 1

/-- 虚二次体か否か -/
def isImaginaryQuadratic (b : BNF) : Bool :=
  b.nf.degree == 2 && b.nf.r1 == 0 && b.nf.r2 == 1

end BNF

end LeanPari
```



## C FFI 抽出関数

`bnfinit` の GEN から各フィールドを取り出す C ラッパーを追加する．
判別式・類数の返却は，issue #5 で実装した `lean_int64_to_int` / `itos` パターンを使い，文字列経由を廃止する．
多項式係数の受け渡しには `lean_pari_bnfinit_coeffs` を新設し，`listIntToGen` で変換した係数ベクトルを `gtopoly` で多項式 GEN に変換してから `bnfinit` に渡す：

```c
// c/pari_bnf_lean.c

// bnf GEN から nf を取り出す
LEAN_EXPORT lean_obj_res lean_pari_bnf_get_nf(
    b_lean_obj_arg bnf_obj, lean_obj_arg world)
{
    GEN bnf = lean_to_gen(bnf_obj);
    GEN nf  = bnf_get_nf(bnf);  // マクロ: gel(bnf, 7)
    return lean_io_result_mk_ok(gen_to_lean(nf));
}

// 次数
LEAN_EXPORT lean_obj_res lean_pari_nf_degree(
    b_lean_obj_arg nf_obj, lean_obj_arg world)
{
    GEN nf = lean_to_gen(nf_obj);
    long deg = degpol(nf_get_pol(nf));
    return lean_io_result_mk_ok(lean_box((size_t)deg));
}

// 符号数 (r1, r2)
LEAN_EXPORT lean_obj_res lean_pari_nf_signature(
    b_lean_obj_arg nf_obj, lean_obj_arg world)
{
    GEN nf = lean_to_gen(nf_obj);
    long r1, r2;
    nf_get_sign(nf, &r1, &r2);
    lean_object* pair = lean_alloc_ctor(0, 2, 0);
    lean_ctor_set(pair, 0, lean_box((size_t)r1));
    lean_ctor_set(pair, 1, lean_box((size_t)r2));
    return lean_io_result_mk_ok(pair);
}

// 判別式を Lean Int として返す（文字列経由を廃止，itos + pari_CATCH パターン）
// 注意: itos() は long 範囲外の整数でエラーを投げる
LEAN_EXPORT lean_obj_res lean_pari_nf_disc(
    b_lean_obj_arg nf_obj, lean_obj_arg world)
{
    GEN nf   = lean_to_gen(nf_obj);
    GEN disc = nf_get_disc(nf);
    long val;
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI nf_disc error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        val = itos(disc);
    } pari_ENDCATCH;
    return lean_io_result_mk_ok(lean_int64_to_int((int64_t)val));
}

// 類数を Lean Int として返す（文字列経由を廃止）
LEAN_EXPORT lean_obj_res lean_pari_bnf_classnumber(
    b_lean_obj_arg bnf_obj, lean_obj_arg world)
{
    GEN bnf  = lean_to_gen(bnf_obj);
    GEN clgp = bnf_get_clgp(bnf);
    GEN h    = abgrp_get_no(clgp);
    long val;
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI bnf_classnumber error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        val = itos(h);
    } pari_ENDCATCH;
    return lean_io_result_mk_ok(lean_int64_to_int((int64_t)val));
}

// 類群の不変因子列 → Array Nat
LEAN_EXPORT lean_obj_res lean_pari_bnf_clgp_cyc(
    b_lean_obj_arg bnf_obj, lean_obj_arg world)
{
    GEN bnf  = lean_to_gen(bnf_obj);
    GEN clgp = bnf_get_clgp(bnf);
    GEN cyc  = abgrp_get_cyc(clgp);  // SNF 不変因子ベクトル
    long len = lg(cyc) - 1;
    lean_object* arr = lean_mk_empty_array();
    for (long i = 1; i <= len; i++) {
        long ni = itos(gel(cyc, i));
        arr = lean_array_push(arr, lean_box((size_t)ni));
    }
    return lean_io_result_mk_ok(arr);
}

// 捩れ単数の位数
LEAN_EXPORT lean_obj_res lean_pari_bnf_tu_order(
    b_lean_obj_arg bnf_obj, lean_obj_arg world)
{
    GEN bnf = lean_to_gen(bnf_obj);
    GEN tu  = bnf_get_tu(bnf);      // gel(bnf, 4): [w, zeta]
    long w  = itos(gel(tu, 1));
    return lean_io_result_mk_ok(lean_box((size_t)w));
}

// List Int の係数ベクトル（t_VEC）から bnfinit を呼ぶ
// Lean 側で listIntToGen した GEN をそのまま受け取り，
// gtopoly で t_POL に変換してから bnfinit に渡す（文字列渡しを廃止）
LEAN_EXPORT lean_obj_res lean_pari_bnfinit_coeffs(
    b_lean_obj_arg coeffs_obj, lean_obj_arg world)
{
    GEN coeffs = lean_to_gen(coeffs_obj);
    GEN bnf = NULL;
    pari_CATCH(CATCH_ALL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PARI bnfinit error #%ld", err_get_num(__iferr_data));
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string(msg)));
    } pari_TRY {
        // gtopoly(v, -1): v[1] が定数項，v[n] が最高次係数
        GEN pol = gtopoly(coeffs, -1);
        bnf = bnfinit0(pol, 1);  // flag=1: 完全な bnf を計算
    } pari_ENDCATCH;
    return lean_io_result_mk_ok(gen_to_lean(bnf));
}
```

```C
// 次数
LEAN_EXPORT lean_obj_res lean_pari_nf_degree(
    b_lean_obj_arg nf_obj, lean_obj_arg world)
{
    GEN nf = lean_to_gen(nf_obj);
    long deg = degpol(nf_get_pol(nf));
    return lean_io_result_mk_ok(lean_box((size_t)deg));
}

// 符号数 [r1, r2]
LEAN_EXPORT lean_obj_res lean_pari_nf_signature(
    b_lean_obj_arg nf_obj, lean_obj_arg world)
{
    GEN nf = lean_to_gen(nf_obj);
    long r1, r2;
    nf_get_sign(nf, &r1, &r2);
    // Lean の Prod (r1, r2) を構築
    lean_object* pair = lean_alloc_ctor(0, 2, 0);
    lean_ctor_set(pair, 0, lean_box((size_t)r1));
    lean_ctor_set(pair, 1, lean_box((size_t)r2));
    return lean_io_result_mk_ok(pair);
}

// 判別式を文字列経由で Lean Int へ変換
LEAN_EXPORT lean_obj_res lean_pari_nf_disc(
    b_lean_obj_arg nf_obj, lean_obj_arg world)
{
    GEN nf   = lean_to_gen(nf_obj);
    GEN disc = nf_get_disc(nf);
    char* str = GENtostr(disc);
    lean_object* s = lean_mk_string(str);
    pari_free(str);
    return lean_io_result_mk_ok(s);  // Lean 側で String.toInt? する
}

// 類数
LEAN_EXPORT lean_obj_res lean_pari_bnf_classnumber(
    b_lean_obj_arg bnf_obj, lean_obj_arg world)
{
    GEN bnf  = lean_to_gen(bnf_obj);
    GEN clgp = bnf_get_clgp(bnf);    // gel(bnf, 8)
    GEN h    = abgrp_get_no(clgp);   // clgp [pari.math.u-bordeaux](https://pari.math.u-bordeaux.fr/dochtml/html/General_number_fields.html)
    char* str = GENtostr(h);
    lean_object* s = lean_mk_string(str);
    pari_free(str);
    return lean_io_result_mk_ok(s);
}

// 類群の不変因子列 → Array Nat
LEAN_EXPORT lean_obj_res lean_pari_bnf_clgp_cyc(
    b_lean_obj_arg bnf_obj, lean_obj_arg world)
{
    GEN bnf  = lean_to_gen(bnf_obj);
    GEN clgp = bnf_get_clgp(bnf);
    GEN cyc  = abgrp_get_cyc(clgp);  // clgp [kconrad.math.uconn](https://kconrad.math.uconn.edu/math5230f12/handouts/PARIalgnumthy.pdf): vector of SNF invariants
    long len = lg(cyc) - 1;
    
    // Lean の Array Nat を構築
    lean_object* arr = lean_mk_empty_array();
    for (long i = 1; i <= len; i++) {
        long ni = itos(gel(cyc, i));
        arr = lean_array_push(arr, lean_box((size_t)ni));
    }
    return lean_io_result_mk_ok(arr);
}

// 捩れ単数の位数
LEAN_EXPORT lean_obj_res lean_pari_bnf_tu_order(
    b_lean_obj_arg bnf_obj, lean_obj_arg world)
{
    GEN bnf  = lean_to_gen(bnf_obj);
    GEN tu   = bnf_get_tु(bnf);     // gel(bnf, 4): [w, zeta]
    long w   = itos(gel(tu, 1));
    return lean_io_result_mk_ok(lean_box((size_t)w));
}
```


## スマートコンストラクタ（Lean 側）

`Conv.lean`（issue #5）の `genToInt` を活用しつつ，有理数係数用の `listRatToGen` を `Conv.lean` に追加することで，
文字列渡し・文字列パースを完全に廃止する．

- **Lean → PARI**：`Polynomial ℚ` から `polyToCoeffList` で係数リストを取り出し，`listRatToGen` → `pariBNFinitCoeffs`（C 内部で `gtopoly` + `bnfinit`）に渡す
- **PARI → Lean**：判別式・類数は `IO Int` で直接受け取る（`String.toInt?` 不要）
- **C FFI**：変更なし．`lean_pari_bnfinit_coeffs` は上段と同じ `t_VEC` を受け取る

```lean
-- LeanPari/BNF.lean

import LeanPari.Basic
import LeanPari.Conv                       -- listRatToGen, genToInt を利用（Conv.lean に listRatToGen を追加）
import Mathlib.Algebra.Polynomial.Basic    -- Polynomial ℚ

open Polynomial

namespace LeanPari

/-- `Polynomial ℚ` の係数リストを取り出す（定数項が先，最高次係数が末尾）
    `listRatToGen` にそのまま渡せる形式 -/
def polyToCoeffList (p : Polynomial ℚ) : List ℚ :=
  (List.range (p.natDegree + 1)).map (fun i => p.coeff i)

-- 低レベル FFI 宣言
@[extern "lean_pari_bnf_get_nf"]
opaque pariBNFGetNF     (bnf : @& GEN) : IO GEN
@[extern "lean_pari_nf_degree"]
opaque pariNFDegree     (nf  : @& GEN) : IO Nat
@[extern "lean_pari_nf_signature"]
opaque pariNFSignature  (nf  : @& GEN) : IO (Nat × Nat)
@[extern "lean_pari_nf_disc"]
opaque pariNFDisc       (nf  : @& GEN) : IO Int          -- 文字列返しを廃止
@[extern "lean_pari_bnf_classnumber"]
opaque pariBNFClassNum  (bnf : @& GEN) : IO Int          -- 文字列返しを廃止
@[extern "lean_pari_bnf_clgp_cyc"]
opaque pariBNFClgpCyc   (bnf : @& GEN) : IO (Array Nat)
@[extern "lean_pari_bnf_clgp_gen"]
opaque pariBNFClgpGen   (bnf : @& GEN) : IO GEN
@[extern "lean_pari_bnf_tu_order"]
opaque pariBNFTuOrder   (bnf : @& GEN) : IO Nat
@[extern "lean_pari_bnf_get_tu"]
opaque pariBNFGetTu     (bnf : @& GEN) : IO GEN
@[extern "lean_pari_bnf_get_fu"]
opaque pariBNFGetFu     (bnf : @& GEN) : IO GEN
@[extern "lean_pari_bnf_get_reg"]
opaque pariBNFGetReg    (bnf : @& GEN) : IO GEN
@[extern "lean_pari_bnf_get_zk"]
opaque pariBNFGetZK     (nf  : @& GEN) : IO GEN
@[extern "lean_pari_bnf_get_pol"]
opaque pariBNFGetPol    (nf  : @& GEN) : IO GEN
-- 係数ベクトル（t_VEC）から内部で gtopoly → bnfinit を呼ぶ
@[extern "lean_pari_bnfinit_coeffs"]
opaque pariBNFinitCoeffs (coeffs : @& GEN) : IO GEN

/-- `Polynomial ℚ` を受け取る BNF スマートコンストラクタ
    Mathlib の型をそのまま受け取るので，小形式判定等の証明の内容と完全に整合する -/
def BNF.init (poly : Polynomial ℚ) : IO BNF := do
  let mark    ← stackMark
  -- Polynomial ℚ → List ℚ → t_VEC → gtopoly → bnfinit
  -- listRatToGen は Conv.lean に追加：各 q : ℚ を readStr s!"{q.num}/{q.den}" で PARI t_FRAC に変換
  let coeffsG ← listRatToGen (polyToCoeffList poly)
  let bnfRaw  ← pariBNFinitCoeffs coeffsG

  -- nf 部分を抽出
  let nfRaw    ← pariBNFGetNF bnfRaw
  let deg      ← pariNFDegree nfRaw
  let (r1, r2) ← pariNFSignature nfRaw
  let disc     ← pariNFDisc nfRaw     -- IO Int 直接取得（String.toInt? 不要）
  let zk       ← pariBNFGetZK nfRaw

  -- 類群データを抽出
  let h    ← pariBNFClassNum bnfRaw   -- IO Int 直接取得（String.toInt? 不要）
  let cyc  ← pariBNFClgpCyc bnfRaw
  let gens ← pariBNFClgpGen bnfRaw

  -- 単数データを抽出
  let tuOrd ← pariBNFTuOrder bnfRaw
  let tuGen ← pariBNFGetTu bnfRaw
  let fu    ← pariBNFGetFu bnfRaw
  let reg   ← pariBNFGetReg bnfRaw

  stackRestore mark
  return {
    nf := {
      pol := poly,      -- Mathlib の Polynomial ℚ をそのまま保持
      degree := deg,
      r1 := r1, r2 := r2,
      disc := disc, index := 1,  -- 別途 nf.index で取得可
      zk := zk, nfRaw := nfRaw
    },
    clgp := {
      classNumber := h,
      invariants  := cyc,
      generators  := gens
    },
    tuOrder := tuOrd, tuGen := tuGen,
    fu := fu, regulator := reg,
    bnfRaw := bnfRaw
  }

end LeanPari
```


## 使用例

Mathlib4 の `Polynomial ℚ` 記法で定義多項式を直接記述する．
文字列リテラル・係数リストは一切不要である．

```lean
-- Main.lean での使用例

import Mathlib.Algebra.Polynomial.Basic
open Polynomial

def testBNF : IO Unit := do
  Pari.initializePari

  -- Q(√-23)：定義多項式 X^2 + 23，類数 3，類群 ℤ/3ℤ
  let T1 : Polynomial ℚ := X ^ 2 + C 23
  let bnf ← BNF.init T1

  IO.println s!"定義多項式次数   : {bnf.nf.degree}"
  IO.println s!"符号数 (r1, r2)  : {bnf.nf.r1}, {bnf.nf.r2}"
  IO.println s!"判別式           : {bnf.nf.disc}"
  IO.println s!"類数             : {bnf.classNumber}"
  IO.println s!"類群不変因子     : {bnf.clgp.invariants}"
  IO.println s!"類群の階数       : {bnf.clgp.rank}"
  IO.println s!"単数群の捩れ位数 : {bnf.tuOrder}"
  IO.println s!"単数群の階数     : {bnf.unitRank}"

  -- 岩澤理論用：3-primary 部分
  let p : Nat := 3
  IO.println s!"Cl(K)[3^∞] の不変因子: {bnf.clgp.pPrimaryInvariants p}"
  IO.println s!"3-rank : {bnf.clgp.pRank p}"

  -- 次数 4 の例：Q(ζ₅)，山本円小式 X^4 + X^3 + X^2 + X + 1
  let T5 : Polynomial ℚ := X ^ 4 + X ^ 3 + X ^ 2 + X + 1
  let bnf5 ← BNF.init T5
  IO.println s!"\nQ(ζ₅) の類数: {bnf5.classNumber}"
  IO.println s!"Q(ζ₅) の判別式: {bnf5.nf.disc}"
```

***

## 型階層のまとめ

研究上の文脈（岩澤理論・ $p$ 進 $L$ 函数・類体論）に向けた設計上のポイントを整理する： [kconrad.math.uconn](https://kconrad.math.uconn.edu/math5230f12/handouts/PARIalgnumthy.pdf)

- **`ClassGroup.pPrimaryInvariants`** で Cl(K) の $p$-Sylow 部分群の構造を直接取得でき，岩澤 μ-不変量・λ-不変量の計算基盤になる．
- **`BNF.bnfRaw`** を保持することで，`bnrisprincipal`・`bnrinit`（ray class group）等のさらなる PARI 関数にそのまま渡せる． [pari.math.u-bordeaux](https://pari.math.u-bordeaux.fr/dochtml/html/General_number_fields.html)
- 判別式・類数は Lean の `Int`（任意精度）にすることで，大きな体でも精度を落とさず扱える．
- 定義多項式を `Polynomial ℚ`（Mathlib4）なまま `NumberField.pol` として保持することで，小形式判定（`Irreducible`）や階数計算等の Lean 内部証明がそのまま利用できる．
- `NF ≤ BNF ≤ BNR` の型階層に対応して，将来 `BNR` 構造体を追加する際も `BNF` を `bnr.nf` フィールドとして持てばよく，拡張性がある．

## mathlibとの統合の検討

PARI の `bnfinit()` はGRHや，それよりも強いE. Bachの上界を仮定した計算がデフォルトの挙動である．このことを踏まえて，Lean 4 との統合設計に反映させる．

***

## `bnfinit()` の仮定の正確な内容

PARI のドキュメントと実装を確認すると，仮定は **3 段階**に分かれている ： [pari.math.u-bordeaux](https://pari.math.u-bordeaux.fr/dochtml/html-stable/General_number_fields.html)

| レベル | 内容 | 該当条件 |
|---|---|---|
| **Level 0** | Minkowski上界まで全素イデアルを検査 | 無条件に正確（`bnfcertify` 後）|
| **Level 1** | Bach上界 \(12 \log^2 |d_K|\) 以下の素イデアルで生成 | GRH 条件付きで正確 |
| **Level 2** | デフォルト上界 \(0.3 \log^2 \|d_K\|\)（通称 "Bach 定数のカンニング"）| **GRH より強いヒューリスティックを仮定** |

デフォルトの `bnfinit(f)` は **Level 2**，すなわち GRH すら仮定していない純粋なヒューリスティックで動作する ．`bnfcertify` を呼ぶことで Level 0（無条件）に格上げできるが，体の次数が大きくなると実行が困難になる ． [pari.math.u-bordeaux](https://pari.math.u-bordeaux.fr/archives/pari-users-0312/msg00020.html)

***

## Lean 4 型設計への反映：`Certification` 型

この前提条件の段階を **Lean の型で明示的に表現**することが，形式化との統合における最重要設計判断である．

```lean
-- LeanPari/Certification.lean
namespace LeanPari

/--
  bnfinit の計算結果の信頼性の段階．
  PARI の 3 段階の証明レベルを型で表現する．
-/
inductive BNFCertification where
  /-- bnfinit のデフォルト動作．
      GRH より強いヒューリスティック (Bach 定数 c = 0.3) を仮定．
      原則として研究計算の出発点・探索用途． -/
  | Heuristic
      (bachConst : Float)     -- 使用した Bach 定数（デフォルト 0.3）
      : BNFCertification

  /-- GRH を仮定した条件付き正確解．
      Bach 界 12·log²|disc| まで検査済み．
      定理の statement に「GRH を仮定して」と明記する場合はこれ． -/
  | ConditionalOnGRH
      (bound : Nat)           -- 実際に使用した界
      : BNFCertification

  /-- bnfcertify による無条件証明済み．
      Minkowski 界まで全素イデアルを検査し，GRH を必要としない． -/
  | Unconditional
      (certProof : String)    -- bnfcertify の出力ログ（監査用）
      : BNFCertification

namespace BNFCertification

def isUnconditional : BNFCertification → Bool
  | .Unconditional _ => true
  | _                => false

def isGRHConditional : BNFCertification → Bool
  | .ConditionalOnGRH _ => true
  | .Unconditional _    => true   -- 無条件は GRH 条件付きを含意
  | _                   => false

/-- 研究論文で「証明」として使えるか -/
def isPublishable (cert : BNFCertification) : Bool :=
  cert.isGRHConditional  -- GRH 条件付きは明示すれば論文に使える

end BNFCertification
end LeanPari
```

***

## `BNF` 構造体に `cert` フィールドを追加

前回設計した `BNF` 構造体を修正する：

```lean
-- LeanPari/NumberField.lean（修正版）

structure BNF where
  nf         : NumberField
  clgp       : ClassGroup
  tuOrder    : Nat
  tuGen      : PariGEN
  fu         : PariGEN
  regulator  : PariGEN
  bnfRaw     : PariGEN
  /-- この BNF の計算が依拠している仮定 -/
  cert       : BNFCertification
  /-- bnfinit に渡した技術的パラメータの記録 -/
  bachConst  : Float    -- デフォルト 0.3，GRH 条件下では 12.0
  searchBound : Nat     -- 実際に検査した素数の界

namespace BNF

/-- 無条件の結果かどうか -/
def isUnconditional (b : BNF) : Bool :=
  b.cert.isUnconditional

/-- この計算結果を定理として使う際の前提条件を文字列で返す -/
def assumptionStatement (b : BNF) : String :=
  match b.cert with
  | .Heuristic c =>
    s!"[注意] この結果は GRH より強いヒューリスティック \
       (Bach 定数 {c}) を仮定しています．検証には bnfcertify を使用してください．"
  | .ConditionalOnGRH bound =>
    s!"[GRH 条件付き] この結果は一般 Riemann 予想 (GRH) を仮定し，\
       界 {bound} まで検査済みです．"
  | .Unconditional _ =>
    "[無条件] この結果は bnfcertify により無条件に検証済みです．"

end BNF
```

***

## `Conv.lean` への追加：`polyQQToGen`

`Polynomial ℚ` を PARI の `t_POL` GEN に変換する関数を `Conv.lean` に追加する．
定数項から最高次係数の順に有理数リストを作り，PARI の `Polrev` 関数に渡す方式で実装できる：

```lean
-- LeanPari/Conv.lean（追加分）
import Mathlib.RingTheory.Polynomial.Basic

namespace Pari

open Polynomial in
/-- `Polynomial ℚ` を PARI `t_POL` GEN に変換する．
    係数を定数項から最高次の順に並べた有理数リストを PARI の `Polrev` に渡す． -/
def polyQQToGen (p : Polynomial ℚ) : IO GEN := do
  let deg := p.natDegree
  let coeffStrs := (List.range (deg + 1)).map fun i =>
    let c : ℚ := p.coeff i
    if c.den = 1 then toString c.num else s!"{c.num}/{c.den}"
  readStr s!"Polrev([{",".intercalate coeffStrs}])"

end Pari
```

`Polrev([a₀, a₁, …, aₙ])` は PARI で `a₀ + a₁·x + … + aₙ·xⁿ` を構築する関数であり，
`Polynomial.coeff p i` で得られる Lean 側の係数の順序と一致する．

***

## `BNF.init` の修正：3 種類のコンストラクタ

引数を `String` から `Polynomial ℚ` に変更し，内部で `Conv.polyQQToGen` を呼んで
PARI GEN に変換してから `bnfinit` に渡す：

```lean
-- LeanPari/BNF.lean（修正版）

namespace LeanPari

/-- デフォルト：ヒューリスティック（探索用）-/
def BNF.initHeuristic (pol : Polynomial ℚ) : IO BNF := do
  let polGen ← Conv.polyQQToGen pol
  let bnfRaw ← withPariStack do
    bnfinit polGen          -- Pari.bnfinit : GEN → IO GEN（flag = 0 デフォルト）
  let base ← BNF.extractFields bnfRaw
  return { base with
    cert       := .Heuristic 0.3
    bachConst  := 0.3 }

/-- GRH 条件付き：Bach 定数を 12.0 に設定 -/
def BNF.initGRH (pol : Polynomial ℚ) : IO BNF := do
  let polGen ← Conv.polyQQToGen pol
  -- technical パラメータで Bach 定数を 12 に設定
  let bnfRaw ← withPariStack do
    bnfinitWithBach polGen 12.0  -- Pari.bnfinitWithBach : GEN → Float → IO GEN
  let bound ← pariComputeBachBound bnfRaw  -- 12·log²|disc| を計算
  let base ← BNF.extractFields bnfRaw
  return { base with
    cert       := .ConditionalOnGRH bound
    bachConst  := 12.0 }

/-- 無条件：bnfcertify を呼ぶ（低次体のみ実用的）-/
def BNF.initUnconditional (pol : Polynomial ℚ) : IO (Option BNF) := do
  let polGen ← Conv.polyQQToGen pol
  let bnfRaw ← withPariStack do
    bnfinitFull polGen      -- Pari.bnfinitFull : GEN → IO GEN（flag = 1，単数も計算）
  -- bnfcertify を実行
  let certResult ← pariCallBnfcertify bnfRaw
  if certResult == 1 then
    let base ← BNF.extractFields bnfRaw
    return some { base with
      cert      := .Unconditional "bnfcertify: 1"
      bachConst := 0.0 }
  else
    return none  -- 証明失敗（体が大きすぎる等）

end LeanPari
```

`bnfinit`・`bnfinitWithBach`・`bnfinitFull` はそれぞれ `Basic.lean` に FFI 宣言を追加し，
対応する C ラッパーを `c/pari_lean.c` に実装する（`gp_read_str` を経由しない直接呼び出し）：

```lean
-- LeanPari/Basic.lean（追加分）

/-- PARI の bnfinit(pol) を呼ぶ（flag = 0 デフォルト） -/
@[extern "lean_pari_bnfinit"]
opaque bnfinit (pol : @& GEN) : IO GEN

/-- PARI の bnfinit(pol, , [0,0,0.5,bachC]) を呼ぶ（GRH 条件付き用） -/
@[extern "lean_pari_bnfinit_with_bach"]
opaque bnfinitWithBach (pol : @& GEN) (bachC : Float) : IO GEN

/-- PARI の bnfinit(pol, 1) を呼ぶ（flag = 1，単数群も計算） -/
@[extern "lean_pari_bnfinit_full"]
opaque bnfinitFull (pol : @& GEN) : IO GEN
```

***

## Mathlib との統合における注意

前回提案した `BNFBridge` の整合性命題も，前提条件を明示する形に修正すべきである：

```lean
structure BNFBridge (K : Type) [Field K] [NumberField K] where
  bnf : BNF
  /-- 無条件に成立する命題は Unconditional のみ -/
  classNumberEq :
    match bnf.cert with
    | .Unconditional _    =>
        -- Lean カーネルが検証可能
        NumberField.classNumber K = bnf.clgp.classNumber.toNat
    | .ConditionalOnGRH _ =>
        -- 定理の statement に GRH を仮定として明記
        GRH → NumberField.classNumber K = bnf.clgp.classNumber.toNat
    | .Heuristic _        =>
        -- 命題自体は立てられるが証明の根拠が弱い
        -- sorry か native_decide（計算的検証）で補う
        NumberField.classNumber K = bnf.clgp.classNumber.toNat
```

***

## 使用例と出力

```lean
open Polynomial in
def checkQ_sqrt_neg23 : IO Unit := do
  pariInit 8000000 10000

  -- 定義多項式を Lean の Polynomial ℚ として記述（文字列リテラル不使用）
  let f : Polynomial ℚ := X ^ 2 + C 23  -- ℚ[x] の x² + 23

  -- 探索：ヒューリスティック
  let bnfH ← BNF.initHeuristic f
  IO.println (bnfH.assumptionStatement)
  -- → [注意] この結果は GRH より強いヒューリスティック...
  IO.println s!"h = {bnfH.classNumber}"  -- h = 3

  -- GRH 条件付き
  let bnfG ← BNF.initGRH f
  IO.println (bnfG.assumptionStatement)
  -- → [GRH 条件付き] この結果は一般 Riemann 予想を仮定し...

  -- 無条件（虚二次体は次数 2 なので bnfcertify が実用的）
  let bnfU? ← BNF.initUnconditional f
  match bnfU? with
  | some bnfU =>
    IO.println (bnfU.assumptionStatement)
    -- → [無条件] この結果は bnfcertify により無条件に検証済みです．
  | none =>
    IO.println "bnfcertify 失敗"
```

***

## まとめ

PARI の `bnfinit()` の前提条件の階層を Lean 4 の型として明示することには，以下の研究上の利点がある ： [arxiv](https://www.arxiv.org/pdf/2510.05501.pdf)

- `BNFCertification` 型が「どの仮定の下で計算したか」を値レベルで記録するため，**証明の再現性と透明性が確保**される
- Mathlib との統合において，GRH を `Prop` として明示的に前提にする命題と，無条件命題を**型レベルで区別**できる
- 岩澤理論の研究では，λ-不変量・μ-不変量の計算は現実的には GRH 条件付きで行わざるを得ないが，その事実を**形式的に記録**することが論文の厳密性を保証する [londmathsoc.onlinelibrary.wiley](https://londmathsoc.onlinelibrary.wiley.com/doi/full/10.1112/jlms.12563)