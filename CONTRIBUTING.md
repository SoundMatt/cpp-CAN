# Contributing to cpp-CAN

Thank you for your interest in contributing.

## Developer Certificate of Origin (DCO)

All contributions must be signed off under the
[Developer Certificate of Origin v1.1](https://developercertificate.org).

Add a `Signed-off-by` trailer to every commit:

```
git commit -s -m "feat: add awesome thing"
```

This produces:

```
feat: add awesome thing

Signed-off-by: Your Name <your@email.com>
```

If you forget to sign off, amend the commit:

```
git commit --amend -s
```

## Branch workflow

```
main
└── feat/<short-description>   ← all work happens here
    └── PR → main
```

Never commit directly to `main`.

## Building

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure -j1
```

Requires CMake ≥ 3.21, a C++17 compiler, and Ninja. Catch2 is fetched
automatically by FetchContent.

## Code style

- **C++17 only** — no C++20 features; target clang-14 / gcc-12 / MSVC 19.38
- `snake_case` for types, functions, and variables; `kConstantName` for
  `inline constexpr`; `MACRO_NAME` for macros
- No raw `new`/`delete` — use `std::make_unique` / `std::make_shared`
- No comments unless the WHY is non-obvious — code is self-documenting
- All public API must have a corresponding `fusa:req` annotation in the
  implementation and a `fusa:test` annotation in the test file

## Requirements traceability

Every source function implementing a requirement must be annotated:

```cpp
// fusa:req REQ-MODULE-NNN
```

Every test case covering a requirement must be annotated in the file header:

```cpp
// fusa:test REQ-MODULE-NNN
```

Add the requirement to `requirements/requirements.json` using the format:

```json
{
  "id": "REQ-MODULE-NNN",
  "title": "Short title",
  "text": "The system shall ...",
  "category": "functional",
  "criticality": "medium"
}
```

## Pull request checklist

- [ ] All tests pass: `ctest --test-dir build --output-on-failure -j1`
- [ ] No new warnings under `clang-14 -Wall -Wextra -Wpedantic -Werror`
- [ ] Requirements updated in `requirements/requirements.json`
- [ ] `fusa:req` annotations on all new implementation functions
- [ ] `fusa:test` annotations on all new test files
- [ ] DCO sign-off on every commit
- [ ] MPL-2.0 licence header on all new source files
- [ ] PR title follows Conventional Commits (`feat:`, `fix:`, `docs:`, etc.)
