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
