// prelude.cpp — implementation of prelude.h: the helper table.
//
// One row per helper: its name, the helpers it calls, its Lean text. The rows
// are in definition order, so printing the needed rows in table order is
// always valid Lean. Every text here was type-checked on Lean 4.35.0 by the
// end-to-end suite (a case that uses it, test/cases/*.checks.lean).

#include "prelude.h"

namespace {

struct Helper {
    const char* name;
    std::vector<const char*> needs;
    const char* text;
};

const std::vector<Helper>& table() {
    static const std::vector<Helper> rows = {
        {"hexOf", {},
         "-- helper for printf `%x` / `%X` (main-spec §8.1)\n"
         "def hexOf (n : Nat) : String := String.ofList (Nat.toDigits 16 n)\n"},
        {"fgetc", {},
         R"LEAN(-- helper for fgetc/getc: one byte as an int, -1 at the end of the input (main-spec §8.1)
def fgetc (f : IO.FS.Stream) : IO Int := do
  let b ← f.read 1
  return if b.isEmpty then -1 else Int.ofNat b[0]!.toNat
)LEAN"},
        {"getchar", {"fgetc"},
         R"LEAN(-- helper for getchar
def getchar : IO Int := do
  fgetc (← IO.getStdin)
)LEAN"},
        {"fopen", {},
         R"LEAN(-- helper for fopen: "r" reads, "w" writes (truncating), "a" appends; a failure is an IO error
def fopen (path mode : String) : IO IO.FS.Stream := do
  let m := if mode.startsWith "w" then IO.FS.Mode.write
    else if mode.startsWith "a" then IO.FS.Mode.append else IO.FS.Mode.read
  return IO.FS.Stream.ofHandle (← IO.FS.Handle.mk path m)
)LEAN"},
        {"fgets", {},
         R"LEAN(-- helper for fgets: a line of at most n-1 bytes, newline kept; `old` stays when nothing was read
def fgets (f : IO.FS.Stream) (n : Nat) (old : String) : IO String := do
  let mut bytes := ByteArray.empty
  let mut room := n - 1
  while room > 0 do
    let b ← f.read 1
    if b.isEmpty then break
    bytes := bytes.push b[0]!
    room := room - 1
    if b[0]! == 10 then break
  if bytes.isEmpty then return old
  return (String.fromUTF8? bytes).getD ""
)LEAN"},
        {"scanWord", {},
         R"LEAN(-- helper for scanf: the next whitespace-separated word, none at the end of the input
def scanWord (f : IO.FS.Stream) : IO (Option String) := do
  let mut bytes := ByteArray.empty
  repeat
    let b ← f.read 1
    if b.isEmpty then break
    let c := b[0]!
    if c == 32 || (c >= 9 && c <= 13) then
      if bytes.isEmpty then continue
      break
    bytes := bytes.push c
  if bytes.isEmpty then return none
  return some ((String.fromUTF8? bytes).getD "")
)LEAN"},
        {"scanInt", {"scanWord"},
         R"LEAN(-- helpers for scanf: read a word into a variable; the variable keeps its value when there is nothing to read
def scanInt (f : IO.FS.Stream) (old : Int) : IO Int := do
  return ((← scanWord f).bind String.toInt?).getD old
)LEAN"},
        {"scanNat", {"scanWord"},
         R"LEAN(def scanNat (f : IO.FS.Stream) (old : Nat) : IO Nat := do
  return ((← scanWord f).bind String.toNat?).getD old
)LEAN"},
        {"scanStr", {"scanWord"},
         R"LEAN(def scanStr (f : IO.FS.Stream) (old : String) : IO String := do
  return (← scanWord f).getD old
)LEAN"},
        {"scanChar", {},
         R"LEAN(def scanChar (f : IO.FS.Stream) (old : Char) : IO Char := do
  let b ← f.read 1
  return if b.isEmpty then old else Char.ofNat b[0]!.toNat
)LEAN"},
        {"scanCharWs", {},
         R"LEAN(-- helper for scanf ` %c`: skip whitespace, then read one character
def scanCharWs (f : IO.FS.Stream) (old : Char) : IO Char := do
  repeat
    let b ← f.read 1
    if b.isEmpty then return old
    let c := b[0]!
    if !(c == 32 || (c >= 9 && c <= 13)) then return Char.ofNat c.toNat
  return old
)LEAN"},
        {"parseFloat", {},
         R"LEAN(-- helper for scanf `%f`: a decimal floating-point text as a Float
def parseFloat (w : String) : Option Float :=
  let (neg, cs) := match w.toList with
    | '-' :: r => (true, r)
    | '+' :: r => (false, r)
    | r => (false, r)
  let ds := cs.takeWhile Char.isDigit
  let r1 := cs.dropWhile Char.isDigit
  let (fs, r2) := match r1 with
    | '.' :: r => (r.takeWhile Char.isDigit, r.dropWhile Char.isDigit)
    | r => ([], r)
  let (eneg, es, r3) := match r2 with
    | 'e' :: '-' :: r => (true, r.takeWhile Char.isDigit, r.dropWhile Char.isDigit)
    | 'e' :: '+' :: r => (false, r.takeWhile Char.isDigit, r.dropWhile Char.isDigit)
    | 'e' :: r => (false, r.takeWhile Char.isDigit, r.dropWhile Char.isDigit)
    | r => (false, [], r)
  if (ds.isEmpty && fs.isEmpty) || !r3.isEmpty then none
  else
    let m : Nat := (ds ++ fs).foldl (fun acc c => acc * 10 + (c.toNat - 48)) 0
    let e : Nat := es.foldl (fun acc c => acc * 10 + (c.toNat - 48)) 0
    let ex : Int := (if eneg then -(e : Int) else (e : Int)) - (fs.length : Int)
    let v := if ex >= 0 then Float.ofScientific m false ex.toNat else Float.ofScientific m true (-ex).toNat
    some (if neg then -v else v)
)LEAN"},
        {"scanFloat", {"scanWord", "parseFloat"},
         R"LEAN(def scanFloat (f : IO.FS.Stream) (old : Float) : IO Float := do
  return ((← scanWord f).bind parseFloat).getD old
)LEAN"},
    };
    return rows;
}

}  // namespace

std::vector<std::string> helperClosure(const std::set<std::string>& used) {
    std::set<std::string> want = used;
    bool grew = true;
    while (grew) {  // add what the wanted helpers call, until nothing new appears
        grew = false;
        for (const Helper& h : table())
            if (want.count(h.name))
                for (const char* dep : h.needs)
                    grew = want.insert(dep).second || grew;
    }
    std::vector<std::string> out;
    for (const Helper& h : table())
        if (want.count(h.name)) out.push_back(h.name);
    return out;
}

std::string helperDefinitions(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& n : names)
        for (const Helper& h : table())
            if (n == h.name) out += std::string("\n") + h.text;
    return out;
}
