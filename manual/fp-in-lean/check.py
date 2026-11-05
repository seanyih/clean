#!/usr/bin/env python3
"""check.py — prove that every Clean (C) block in this manual really translates.

    python3 manual/fp-in-lean/check.py [file.md ...]     (needs `make` first)

Rules for the Markdown files:
  * a ```c block is one translation unit and is fed to release/clean;
  * `<!-- clean: continue -->` on the line before a block means "append this
    block to the previous one(s)" (so a chapter can build on its own earlier code);
  * `<!-- clean: skip -->` excludes the next ```c block (an illustration only);
  * `<!-- clean: lean-rejects -->` marks a block that must translate but that
    Lean is expected to reject (e.g. a deliberately false claim);
  * ```lean blocks are never checked: they are the book's original Lean, kept
    for the parts Clean cannot express yet (look for the ❌ marks).
When `lean` is on PATH, every translation is also type-checked by Lean
(the same check test/run_e2e.sh does); without it that step is skipped.
Exit status is non-zero if any block is rejected.
"""
import glob, os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
clean = os.path.join(here, "..", "..", "release", "clean")
lean = shutil.which("lean")
files = sys.argv[1:] or sorted(glob.glob(os.path.join(here, "[0-9][0-9]-*.md")))
bad = total = 0
for f in files:
    text = open(f, encoding="utf-8").read()
    acc = ""
    pat = r"(<!-- clean: (continue|skip|lean-rejects) -->\s*\n)?```c\n(.*?)```"
    for m in re.finditer(pat, text, re.S):
        mode = m.group(2) or "new"
        line = text.count("\n", 0, m.start(3)) + 1
        if mode == "skip":
            continue
        acc = (acc + "\n" + m.group(3)) if mode == "continue" else m.group(3)
        total += 1
        where = f"{os.path.basename(f)}:{line}"
        with tempfile.TemporaryDirectory() as d:
            src, out = os.path.join(d, "b.c"), os.path.join(d, "b.lean")
            open(src, "w").write('#include "lean.h"\n' + acc)
            r = subprocess.run([clean, src, "-o", out], capture_output=True, text=True)
            if r.returncode != 0:
                bad += 1
                print(f"FAIL (clean) {where}\n{r.stderr.strip()}\n")
                continue
            if lean:
                l = subprocess.run([lean, out], capture_output=True, text=True, cwd=d)
                ok = l.returncode == 0
                if ok == (mode == "lean-rejects"):
                    bad += 1
                    msg = (l.stdout + l.stderr).strip()[:1500]
                    print(f"FAIL (lean) {where}\n{msg or 'Lean accepted a block marked lean-rejects'}\n")
print(f"{total - bad}/{total} Clean blocks translate"
      + (" and type-check in Lean" if lean else " (Lean not found: type-check skipped)"))
sys.exit(1 if bad else 0)
