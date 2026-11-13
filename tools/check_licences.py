#!/usr/bin/env python3
"""Fail if anything copyleft could end up in the build (the game is MIT).

- Every directory under thirdparty/ has a licence file.
- No vendored file mentions the GPL or LGPL, apart from the one known, guarded
  include in DaisySP (daisysp.h only includes LGPL code if USE_DAISYSP_LGPL is defined).
- USE_DAISYSP_LGPL is never defined anywhere in our sources or build.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
COPYLEFT = re.compile(r"GNU (Lesser )?General Public|\bL?GPL\b", re.I)
ALLOWED = {("thirdparty/daisysp/Source/daisysp.h", "LGPL")}  # guarded by USE_DAISYSP_LGPL
problems = []

for lib in sorted((ROOT / "thirdparty").iterdir()):
    if lib.is_dir() and not any(lib.glob("LICENSE*")):
        problems.append(f"{lib.relative_to(ROOT)}: no LICENSE file")

for path in (ROOT / "thirdparty").rglob("*"):
    if not path.is_file() or path.suffix not in {".h", ".hpp", ".c", ".cc", ".cpp"}:
        continue
    rel = str(path.relative_to(ROOT))
    for n, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
        if COPYLEFT.search(line) and not any(rel == f and tok.lower() in line.lower() for f, tok in ALLOWED):
            problems.append(f"{rel}:{n}: mentions a copyleft licence: {line.strip()[:80]}")

for path in [ROOT / "SConstruct", *(ROOT / "src").rglob("*")]:
    if path.is_file() and re.search(r"USE_DAISYSP_LGPL", path.read_text(errors="ignore")):
        problems.append(f"{path.relative_to(ROOT)}: defines or mentions USE_DAISYSP_LGPL")

if problems:
    print("Licence check FAILED:\n  " + "\n  ".join(problems))
    sys.exit(1)
print("Licence check passed: thirdparty/ is MIT/ISC/public-domain only.")
