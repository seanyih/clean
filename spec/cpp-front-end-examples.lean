-- cpp-front-end-examples.lean — the Lean that spec/cpp-front-end-design.md promises.
-- Not generated: written by hand as the target output of the proposed C++ front end,
-- and checked with `lean spec/cpp-front-end-examples.lean` (Lean 4.35.0).
set_option autoImplicit false

namespace Part1

-- E1: Pos with constructor, method, operators, to_string
structure Pos where
  pred : Nat
  deriving Repr, BEq, Inhabited

def Pos.new (n : Nat) : Pos := { pred := n - 1 }
def Pos.toNat (self : Pos) : Nat := self.pred + 1
instance {n : Nat} : OfNat Pos n := ⟨Pos.new n⟩

def Pos.add (a b : Pos) : Pos := Pos.new (a.toNat + b.toNat)
instance : Add Pos := ⟨Pos.add⟩
def Pos.mul (a b : Pos) : Pos := Pos.new (a.toNat * b.toNat)
instance : Mul Pos := ⟨Pos.mul⟩
instance : ToString Pos := ⟨fun p => toString p.toNat⟩

def seven : Pos := 7
def fourteen : Pos := seven + seven
#eval s!"There are {fourteen}"
#eval [seven + seven, seven * seven]

-- E1b ordering via <=>
def Pos.compare (a b : Pos) : Ordering := Id.run do
  if a.pred < b.pred then
    return .lt
  if a.pred > b.pred then
    return .gt
  return .eq
instance : Ord Pos := ⟨Pos.compare⟩
instance : LT Pos := ltOfOrd
instance : LE Pos := leOfOrd
#eval (seven < fourteen : Bool)
instance : Max Pos := maxOfLe
#eval max seven fourteen

-- hash
def Pos.hash (p : Pos) : UInt64 := UInt64.ofNat p.pred
instance : Hashable Pos := ⟨Pos.hash⟩

-- conversion operator
instance : Coe Pos Nat := ⟨Pos.toNat⟩
#eval [1, 2, 3, 4].drop (2 : Pos)

-- E2 trait class Plus
class Plus (T : Type) where
  plus : T → T → T
instance : Plus Nat where
  plus a b := a + b
instance : Plus Pos where
  plus a b := a + b
def twice {T : Type} [Plus T] (x : T) : T := Plus.plus x x
#eval twice (3 : Nat)
#eval twice seven

-- E3 generic struct + constrained instance
structure PPoint (T : Type) where
  x : T
  y : T
  deriving Repr, BEq, Inhabited
def replaceX {T : Type} (point : PPoint T) (newX : T) : PPoint T :=
  { point with x := newX }
#eval replaceX (T := Nat) ⟨1, 2⟩ 5
instance {T : Type} [Add T] : Add (PPoint T) where
  add p q := { x := p.x + q.x, y := p.y + q.y }
def sumOfContents {T : Type} [Add T] [OfNat T 0] [Inhabited T] (xs : Array T) (n : Nat) : T := Id.run do
  let mut s : T := 0
  for i in [0:n] do
    s := s + xs[i]!
  return s
#eval sumOfContents #[1, 2, 3, 4] 4
#eval (({ x := 1.5, y := 32 } : PPoint Float) + { x := -8, y := 0.2 })

-- associated type → outParam
class HPlus (A : Type) (B : Type) (Out : outParam Type) where
  hPlus : A → B → Out
instance : HPlus Nat Pos Pos where
  hPlus n p := Pos.new (n + p.toNat)
#eval toString (HPlus.hPlus (3 : Nat) seven)

end Part1

namespace Part2

-- E4 tagged union → inductive, optional → Option, let-else
inductive Arith where
  | plus | minus | times | divide
  deriving Repr, DecidableEq, Inhabited
open Arith

inductive Expr where
  | CONST (value : Int)
  | PRIM (op : Arith) (lhs : Expr) (rhs : Expr)
  deriving Repr, BEq, Inhabited
open Expr

def applyPrim (op : Arith) (x y : Int) : Option Int := Id.run do
  match op with
  | plus => return some (x + y)
  | minus => return some (x - y)
  | times => return some (x * y)
  | divide =>
    if y = 0 then
      return none
    return some (x / y)

def evaluate (e : Expr) : Option Int := Id.run do
  match e with
  | CONST value =>
    return some value
  | PRIM op lhs rhs =>
    let some v1 := evaluate lhs | return none
    let some v2 := evaluate rhs | return none
    return applyPrim op v1 v2

def fourteenDivided : Expr := PRIM divide (CONST 14) (PRIM minus (CONST 45) (PRIM times (CONST 5) (CONST 9)))
#eval evaluate fourteenDivided
#eval evaluate (PRIM plus (CONST 2) (CONST 3))

-- expected → Except
def applyPrimE (op : Arith) (x y : Int) : Except String Int := Id.run do
  match op with
  | divide =>
    if y = 0 then
      return .error s!"Tried to divide {x} by zero"
    return .ok (x / y)
  | plus => return .ok (x + y)
  | minus => return .ok (x - y)
  | times => return .ok (x * y)

def evaluateE (e : Expr) : Except String Int := Id.run do
  match e with
  | CONST value => return .ok value
  | PRIM op lhs rhs =>
    let .ok v1 := evaluateE lhs | return evaluateE lhs
    let .ok v2 := evaluateE rhs | return evaluateE rhs
    return applyPrimE op v1 v2
#eval evaluateE fourteenDivided

-- E5 inheritance
structure MythicalCreature where
  large : Bool
  deriving Repr, BEq, Inhabited
def MythicalCreature.small (self : MythicalCreature) : Bool := !self.large
structure Monster extends MythicalCreature where
  vulnerability : String
  deriving Repr, BEq, Inhabited
def troll : Monster := { large := true, vulnerability := "sunlight" }
#eval troll.small
#eval troll.toMythicalCreature
structure Helper extends MythicalCreature where
  assistance : String
  payment : String
  deriving Repr
structure MonstrousAssistant extends Monster, Helper where
  deriving Repr
def domesticatedTroll : MonstrousAssistant :=
  { large := true, assistance := "heavy labor", payment := "toy goats", vulnerability := "sunlight" }
#eval domesticatedTroll.toHelper
-- default member initialisers
structure Config where
  useASCII : Bool := false
  currentPrefix : String := ""
  deriving Repr
def c1 : Config := {}
def c2 : Config := { useASCII := true }
#eval c2

-- E6 methods: const method and mutating method, lambdas
structure Point where
  x : Float
  y : Float
  deriving Repr, BEq, Inhabited
def Point.modifyBoth (self : Point) (f : Float → Float) : Point := { x := f self.x, y := f self.y }
def Point.scale (self : Point) (k : Float) : Point := Id.run do
  let mut self := self
  self := { self with x := self.x * k }
  self := { self with y := self.y * k }
  return self
def demo : Point := Id.run do
  let mut p : Point := ⟨4.3, 3.4⟩
  p := p.scale 2.0
  p := p.modifyBoth Float.floor
  p := p.modifyBoth (fun v => v + 1.0)
  return p
#eval demo

-- E7 operator[] → GetElem with trivial bound
structure NonEmptyList (T : Type) where
  head : T
  tail : Array T
  deriving Repr
def NonEmptyList.get {T : Type} [Inhabited T] (self : NonEmptyList T) (i : Nat) : T :=
  if i = 0 then self.head else self.tail[i - 1]!
instance {T : Type} [Inhabited T] : GetElem (NonEmptyList T) Nat T (fun _ _ => True) where
  getElem xs i _ := xs.get i
def spiders : NonEmptyList String := ⟨"Banded Garden Spider", #["Wolf Spider", "Hobo Spider"]⟩
#eval spiders[2]

-- operator() → CoeFun
structure Adder where
  howMuch : Nat
def Adder.call (self : Adder) (n : Nat) : Nat := n + self.howMuch
instance : CoeFun Adder (fun _ => Nat → Nat) := ⟨Adder.call⟩
def add5 : Adder := ⟨5⟩
#eval add5 3

-- pair / structured binding
def divmod (a b : Nat) : Nat × Nat := (a / b, a % b)
def useIt : Nat := Id.run do
  let (q, r) := divmod 17 5
  return q * 10 + r
#eval useIt

end Part2

namespace Part3
structure PPoint (T : Type) where
  x : T
  y : T
  deriving Repr
instance : Functor PPoint where
  map f p := { x := f p.x, y := f p.y }
#eval (· * 2) <$> ({ x := 1, y := 2 } : PPoint Nat)

def firstThird (xs : Array Int) : Option (Int × Int) := do
  let first ← xs[0]?
  let third ← xs[2]?
  return (first, third)
#eval firstThird #[1, 2, 3]
#eval firstThird #[1]

def mapM' {M : Type → Type} [Monad M] {A B : Type} (f : A → M B) (xs : List A) : M (List B) := do
  let mut out : List B := []
  for x in xs do
    out := out ++ [← f x]
  return out
#eval mapM' (fun (x : Nat) => if x > 0 then some x else none) [1, 2, 3]

-- generic function with explicit constraint and template-like call
def max2 {T : Type} [LT T] [DecidableRel (α := T) (· < ·)] (a b : T) : T := if a < b then b else a
#eval max2 3 7

end Part3
