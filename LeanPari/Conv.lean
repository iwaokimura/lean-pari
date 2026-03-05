-- LeanPari/Conv.lean
-- Lean ↔ PARI 基本型相互変換レイヤー（issue #5）
import LeanPari.Basic

namespace Pari

-- ---------------------------------------------------------------
-- Lean → PARI（文字列経由：既存 readStr を再利用）
-- ---------------------------------------------------------------

/-- `Int` を PARI `t_INT` GEN に変換する（文字列経由） -/
def intToGen (n : Int) : IO GEN := readStr (toString n)

/-- `Nat` を PARI `t_INT` GEN に変換する（文字列経由） -/
def natToGen (n : Nat) : IO GEN := readStr (toString n)

/-- `List Int` を PARI `t_VEC` GEN に変換する（文字列経由） -/
def listIntToGen (l : List Int) : IO GEN :=
  readStr ("[" ++ ",".intercalate (l.map toString) ++ "]")

-- ---------------------------------------------------------------
-- Float → PARI（C関数 dbltor 使用：toString では精度不足）
-- ---------------------------------------------------------------

/-- `Float` を PARI `t_REAL` GEN に変換する（`dbltor` 使用） -/
@[extern "lean_pari_float_to_gen"]
opaque floatToGen (f : Float) : IO GEN

-- ---------------------------------------------------------------
-- PARI → Lean（C関数で GEN の型タグを検査し値を抽出）
-- ---------------------------------------------------------------

/-- PARI `t_INT` GEN を `Int` に変換する（`itos` 使用、long 範囲内のみ）  -/
@[extern "lean_pari_gen_to_int"]
opaque genToInt (g : @& GEN) : IO Int

/-- PARI `t_VEC` GEN を `List Int` に変換する（各要素に `itos` 使用） -/
@[extern "lean_pari_gen_to_list_int"]
opaque genToListInt (g : @& GEN) : IO (List Int)

/-- PARI 数値型 GEN を `Float` に変換する（`gtodouble` 使用） -/
@[extern "lean_pari_gen_to_float"]
opaque genToFloat (g : @& GEN) : IO Float

end Pari
