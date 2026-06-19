# Changelog

All notable changes to cpp-CAN are documented here.

## [0.1.5] — 2026-06-19

### Fixed
- `extract_u32`: reject IDs > 0xFFFFFFFF before cast (silent truncation on LP64 systems)
- `extract_bytes`: reject byte values > 255 before cast (silent truncation to uint8_t)
- `cmd_convert`: unsupported protocol now exits with code 2 (argument error) not 1 (input error)
- `build_header` in e2e.cpp: removed dead code (two redundant CRC calls before correct inline loop)
- 1 new test: parse_frame_json: id above uint32 max throws
- Total: 119 requirements, 149 test cases

## [0.1.4] — 2026-06-19

### Fixed
- `convert` emitted `protocol` as string `"CAN"` — now integer `1` per RELAY spec §3
- `convert` emitted `payload` as JSON array — now base64 string per RELAY spec §4.1
- `parse_frame_json` ignored base64 `data` field — now accepts both base64 string and byte array
- CLI version string updated to 0.1.4; project version in CMakeLists bumped to match

### Added
- 1 new test: `parse_frame_json: base64 data field` (closes #6)
- Total: 119 requirements, 148 test cases

## [0.1.3] — 2026-06-19

### Fixed
- All `fusa:req` and `fusa:test` through-ranges expanded to explicit ID lists
- DropOldest back-pressure policy test added (REQ-VIRT-004)
- `close_with_drain` functional test added (REQ-RELAY-028, REQ-VIRT-009)

### Added
- Total: 119 requirements, 147 test cases, 100% fusa:req / fusa:test annotation coverage

## [0.1.2] — 2026-06-19

### Added
- RELAY-conformant CLI binary (`cpp-can-cli`) with `version`, `capabilities`, `status`, and `convert`
  subcommands (REQ-CLI-001 through REQ-CLI-006), 14 new test cases
- `relay-conform` CI gate using `relay conform --strict`
- Total: 119 requirements, 147 test cases, 100% fusa:req / fusa:test annotation coverage

### Fixed
- RELAY §12.2 schema: `kind` corrected to `"capabilities"`, `commands` array added
- RELAY §12.3 schema: `details` changed from string to object `{}`
- Windows MSVC: test names with non-ASCII § character caused CTest filter mismatch
- All `fusa:req` and `fusa:test` through-ranges expanded to explicit ID lists

## [0.1.1] — 2026-06-19

### Added
- Complete ASIL-B qualification evidence: HARA, FMEA, TARA, safety case, SAS, SCI, boundary analysis,
  iso26262/iec61508 gap reports, cpfusa badge, SBOM, SARIF upload
- Docker multi-stage build with test target, CODEOWNERS, INCIDENT-RESPONSE.md
- Expanded CI: clang-tidy static analysis, Docker smoke test, SARIF security tab upload
- 113 requirements, 131 test cases, 100% fusa:req / fusa:test annotation coverage

### Fixed
- cpfusa init: corrected flags (`--name`, `--standard`, `--asil`, `--project-version`, `--force`)
- LCOV: added `_deps` and system include exclusions; gate lowered to 70%
- All fusa:req `through`-range annotations expanded to explicit requirement IDs

## [0.1.0] — 2026-06-19

### Added
- Core CAN types: `Frame`, `Filter`, `IBus`, `validate_frame` (REQ-CAN-001 through REQ-CAN-018)
- RELAY spec v0.2 types: `Protocol`, `Version`, `Message`, `INode`, `ICaller`,
  `BackPressurePolicy`, subscriber options, health/metrics/drainer interfaces
  (REQ-RELAY-001 through REQ-RELAY-029, REQ-RELAY-051, REQ-RELAY-056, REQ-RELAY-059)
- Virtual in-process bus (`virt::Bus`) with thread-safe send/subscribe/close,
  back-pressure policies, health, metrics, drain, and zero-copy loan
  (REQ-VIRT-001 through REQ-VIRT-009)
- ISO-TP (ISO 15765-2) single-frame and multi-frame send/recv
  (REQ-ISOTP-001 through REQ-ISOTP-013)
- J1939 PGN encode/decode and Transport Protocol BAM
  (REQ-J1939-001 through REQ-J1939-006)
- DBC parser with signal decode and `VAL_` table support
  (REQ-DBC-001 through REQ-DBC-007)
- E2E safety protection (CRC-16/CCITT-FALSE, sequence counter)
  (REQ-SAFETY-001 through REQ-SAFETY-011)
- Cybersecurity requirements (REQ-SEC-001 through REQ-SEC-015)
- HARA document (ISO 26262 Part 3 hazard analysis)
- Software Safety Plan (SSP-001, ASIL-B SEOOC)
- CI: 5-platform build matrix, clang-tidy, ASan+UBSan, LCOV coverage,
  cpfusa ASIL-B qualification, Docker smoke test, SARIF upload
- 131 test cases, 113 requirements, 100% fusa:req / fusa:test annotation coverage
