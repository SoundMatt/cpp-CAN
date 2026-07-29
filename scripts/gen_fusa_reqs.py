#!/usr/bin/env python3
# Copyright (c) 2026 Matt Jones. All rights reserved.
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Regenerate .fusa-reqs.json from requirements/requirements.json plus a
source scan for `// fusa:req REQ-XXX` / `// fusa:test REQ-XXX` annotations.

requirements/requirements.json is the single human-maintained source of
truth for requirement text (see CONTRIBUTING.md). This script derives
.fusa-reqs.json — the machine-readable file cpfusa's `trace`/`metrics`/
`iso26262`/etc. subcommands read — from it, so the two files can never
drift apart. Run before any cpfusa command that consumes .fusa-reqs.json
(CI does this automatically; see .github/workflows/ci.yml).

Field mapping to cpfusa's canonical Requirement schema
(id/title/description/standard_ref/severity/asil/parent_id):
  - description  <- requirements.json's "text"
  - severity     <- "cybersecurity" if category == "security", else "safety"
  - standard_ref <- .fusa.json's "standard"
  - asil         <- .fusa.json's "asil"
  - implementations/tests <- file:line locations of matching
    `// fusa:req <ID>` / `// fusa:test <ID>` annotations, found by scanning
    the whole repo (src/, include/, cli/, tests/, interop/) with the same
    regex cpfusa's own trace scanner uses. cpfusa's `trace` subcommand only
    scans .fusa.json's sourceDirs (src, include) for MISRA/lint-scan
    purposes; this script scans more broadly purely to populate evidence
    arrays that cpfusa's `metrics` subcommand reads directly from
    .fusa-reqs.json (it does not itself re-scan source).
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REQS_SRC = ROOT / "requirements" / "requirements.json"
FUSA_CONFIG = ROOT / ".fusa.json"
FUSA_REQS = ROOT / ".fusa-reqs.json"

SCAN_DIRS = ["src", "include", "cli", "tests", "interop"]
SCAN_EXTS = {".cpp", ".hpp", ".h", ".hxx", ".cxx", ".cc"}

REQ_TAG_RE = re.compile(r"//\s*fusa:req\s+")
TEST_TAG_RE = re.compile(r"//\s*fusa:test\s+")
ID_RE = re.compile(r"REQ-\S+")


def scan_annotations():
    """Returns {req_id: {"impl": [...], "test": [...]}} of "file:line" strings."""
    hits = {}
    for scan_dir in SCAN_DIRS:
        base = ROOT / scan_dir
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in SCAN_EXTS:
                continue
            rel = path.relative_to(ROOT).as_posix()
            for lineno, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
            ):
                is_req = REQ_TAG_RE.search(line)
                is_test = TEST_TAG_RE.search(line)
                if not is_req and not is_test:
                    continue
                kind = "test" if is_test else "impl"
                for req_id in ID_RE.findall(line):
                    hits.setdefault(req_id, {"impl": [], "test": []})
                    hits[req_id][kind].append(f"{rel}:{lineno}")
    return hits


def main():
    reqs = json.loads(REQS_SRC.read_text(encoding="utf-8"))
    cfg = json.loads(FUSA_CONFIG.read_text(encoding="utf-8"))
    standard = cfg.get("standard", "iso26262")
    asil = cfg.get("asil", "")

    annotations = scan_annotations()

    out = []
    for r in reqs:
        severity = "cybersecurity" if r.get("category") == "security" else "safety"
        entry = {
            "id": r["id"],
            "title": r["title"],
            "description": r["text"],
            "standard_ref": standard,
            "severity": severity,
        }
        if asil:
            entry["asil"] = asil
        hit = annotations.get(r["id"])
        if hit and hit["impl"]:
            entry["implementations"] = hit["impl"]
        if hit and hit["test"]:
            entry["tests"] = hit["test"]
        out.append(entry)

    doc = {"requirements": out}
    FUSA_REQS.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")

    total = len(out)
    traced = sum(1 for e in out if "implementations" in e or "tests" in e)
    print(f"wrote {FUSA_REQS.relative_to(ROOT)}: {total} requirements, "
          f"{traced} traced ({100.0 * traced / total:.1f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
