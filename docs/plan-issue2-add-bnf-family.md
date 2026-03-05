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