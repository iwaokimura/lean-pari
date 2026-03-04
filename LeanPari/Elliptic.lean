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
