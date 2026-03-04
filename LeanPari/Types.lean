-- LeanPari/Types.lean
namespace Pari

/-- PARI の GEN 型を Lean の opaque 型としてラップ -/
private opaque GENPointed : NonemptyType.{0}
def GEN : Type := GENPointed.type
instance : Nonempty GEN := GENPointed.property

end Pari
