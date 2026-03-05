import LeanPari.Elliptic

def main : IO Unit := do
  -- PARI ライブラリを初期化（initialize を使わないので main の先頭で必ず呼ぶ）
  Pari.initializePari
  -- 例1: y^2 = x^3 - x  (a4 = -1, a6 = 0)
  --   LMFDB: 32.a2 など
  let coeffs1 : List Int := [0, 0, 0, -1, 0]
  let primes1 : List Int := [2, 3, 5, 7, 11, 13, 17, 19, 23]
  for p in primes1 do
    match (← (Pari.computeEllapNative coeffs1 p).toBaseIO) with
    | .ok ap   => IO.println s!"ap({p}) = {ap}"
    | .error e => IO.println s!"Error at p={p}: {e}"

  IO.println "---"

  -- 例2: secp256k1 曲線  y^2 = x^3 + 7
  let coeffs2 : List Int := [0, 0, 0, 0, 7]
  let testPrimes : List Int := [2, 3, 5, 7, 11, 101, 1009]
  for p in testPrimes do
    match (← (Pari.computeEllapNative coeffs2 p).toBaseIO) with
    | .ok ap   => IO.println s!"secp256k1: ap({p}) = {ap}"
    | .error e => IO.println s!"Error at p={p}: {e}"
