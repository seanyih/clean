#!/usr/bin/env python3
"""check.py — prove that every Clean (C) block in this manual really translates.

    python3 manual/fp-in-lean/check.py [file.md ...]     (needs `make` first)

Rules for the Markdown files:
  * a ```c block is one translation unit and is fed to release/clean;
  * `<!-- clean: continue -->` on the line before a block means "append this
    block to the previous one(s)" (so a chapter can build on its own earlier code);
  * `<!-- clean: skip -->` excludes the next ```c block (an illustration only);
  * ```lean blocks are never checked: they are the book's original Lean, kept
    for the parts Clean cannot express yet (look for the ❌ marks).
Exit status is non-zero if any block is rejected.
"""
import glob, os, re, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
clean = os.path.join(here, "..", "..", "release", "clean")
files = sys.argv[1:] or sorted(glob.glob(os.path.join(here, "[0-9][0-9]-*.md")))
bad = total = 0
for f in files:
    text = open(f, encoding="utf-8").read()
    acc, mode = "", "new"
    for m in re.finditer(r"(<!-- clean: (continue|skip) -->\s*\n)?```c\n(.*?)```", text, re.S):
        mode = m.group(2) or "new"
        line = text.count("\n", 0, m.start(3)) + 1
        if mode == "skip":
            continue
        acc = (acc + "\n" + m.group(3)) if mode == "continue" else m.group(3)
        total += 1
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "b.c")
            open(src, "w").write('#include "lean.h"\n' + acc)
            r = subprocess.run([clean, src, "-o", os.path.join(d, "b.lean")],
                               capture_output=True, text=True)
        if r.returncode != 0:
            bad += 1
            print(f"FAIL {os.path.basename(f)}:{line}\n{r.stderr.strip()}\n")
print(f"{total - bad}/{total} Clean blocks translate")
sys.exit(1 if bad else 0)
