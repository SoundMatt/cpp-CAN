# Changelog

All notable changes to cpp-CAN are documented here.

## [0.2.3] — 2026-07-29

### Fixed
- `relay::kRelaySpecVersion` was hardcoded to `"1.11"`, three minor releases
  behind RELAY's actual current, *reachable* spec version. RELAY has since
  tagged a breaking `v2.0.0` (RCP TC18 canonical types), but that release
  doesn't apply here — it's scoped entirely to §15.5's RCP canonical types,
  and cpp-CAN implements the CAN protocol only (no RCP module) — and isn't
  even reachable via `go install .../relay@latest` (the method CI and this
  repo's README use) as of this writing: RELAY's `go.mod` wasn't updated to
  the `/v2` module-path suffix Go's semantic-import-versioning rules require
  for a v2+ tag, so `go install`/`go get` both still silently resolve to
  `v1.14.0`; a binary built directly from the `v2.0.0` git tag (bypassing
  `go install`) still reports `SpecVersion = "1.14"` internally, confirming
  the tag predates that constant being bumped. Filed upstream:
  SoundMatt/RELAY#71. Bumped `kRelaySpecVersion` to `"1.14"` — the actual
  current, reachable, relevant spec version — and updated the matching
  literal in `requirements/requirements.json` (REQ-RELAY-020) and 3 test
  assertions. Reverified `relay conform --strict` and `relay interop --strict`
  PASS against both the `go install`-resolved tool (1.14.0) and a binary
  built from the `v2.0.0` tag (closes #32)

## [0.2.0] — 2026-07-27

### Added
- `can::socketcan::Bus` (`can/socketcan/bus.hpp`, `src/socketcan/bus.cpp`) — a real Linux SocketCAN `IBus` transport (hardware CAN or `vcan`), supporting classic CAN and CAN FD (with BRS/ESI). Mirrors go-CAN's `socketcan` package. Compiled into `cppcan_lib` automatically on Linux; no longer "future" per the README's prior placeholder. CAN XL is not yet supported by this transport (rejected by `send()`) — see ROADMAP.md
- `ROADMAP.md`'s "Interop testing" section, plus a new live SocketCAN interop test suite validating `can::socketcan::Bus` against genuine kernel CAN traffic (not just `can::virt::Bus`): a two-process self-interop harness (`interop/can_interop_peer.cpp`, `interop/test_two_process_interop.cpp` — two real OS processes on the same real `vcan0`, field-exact ID/DLC/data/FD/BRS verification) and a third-party-peer suite against `can-utils` (`interop/test_cangen_candump_interop.cpp` — `cangen`-injected frames decoded field-exact, and `can::socketcan::Bus`-sent frames captured byte-exact by `candump -L`). Opt-in via `-DCPPCAN_INTEROP_TESTS=ON` (Linux only), run by the new `can-interop` CI job (`.github/workflows/ci.yml`), which probes `vcan`/`can-utils` availability and skips cleanly (not a hard failure) if unavailable
- 8 new requirements, `REQ-SCAN-001` through `REQ-SCAN-008`, covering `can::socketcan::Bus` creation, FD support, send/validate, subscribe, close, and field-exact classic/FD decode

## [0.1.8] — 2026-07-27

### Fixed
- `relay::kSpecVersion` (now `relay::kRelaySpecVersion`) was hardcoded to `"0.2"` in both `relay.hpp` and `can.hpp`, disagreeing with the CLI's own reported `"1.10"` — collapsed to a single constant, `relay::kRelaySpecVersion = "1.11"` (RELAY spec §19.4), that `can::kSpecVersion` and the CLI's `version`/`capabilities` JSON now all derive from (closes #12, #14)
- `capabilities`: `features` incorrectly advertised `"dbc"` and `"e2e"`, which are not spec-defined CAN feature values per §12.2's table — now `["fd","isotp","j1939"]` only. **Correction to the [0.1.6] entry below:** that release's "normalised features to spec-defined CAN values (`fd`, `isotp`, `j1939`, `dbc`, `e2e`)" was inaccurate — `dbc`/`e2e` were never spec-defined values (closes #19)
- `version`/`status`: `--format` was accepted but ignored — always emitted JSON regardless of the flag's value, and never validated it. Both now honor `--format text|json`, render a distinct text summary for `text`, and exit `2` with `ErrUnsupportedFormat` on any other value, matching `convert`'s existing `--format` validation (closes #15)
- README's package table documented `can/relay_adapter.hpp`, a header that has never existed — `adapt()` has always lived in `can/can.hpp`; corrected the table and added a row for `can/relay.hpp` (closes #17)
- CMake `project()` version and the CLI's self-reported `version` field were stale at `0.1.6` although the previous release was tagged `v0.1.7`; bumped both to `0.1.8` and derived the CLI's three JSON builders from one `cli::kToolVersion` constant so they can't drift apart from each other again (closes #18)
- CI: the `cpfusa cyber` step was wrapped in `|| true`, so an ERROR-severity cybersecurity finding could never fail the build — RELAY spec §20.1.2 requires the cybersecurity analysis to gate on ERROR findings, same as `check`/`qualify` already do. Removed the escape hatch (verified clean against a fresh checkout: 0 errors, warnings only); `vuln` already gated correctly with no change needed (progress on #9)

### Added
- `relay::Context` (background/with_deadline/with_timeout/done/deadline) and `relay::Channel<T>` (aliasing `can::Chan<T>`, which already implements push/recv/try_recv/close/is_closed) per RELAY spec §18.2
- `relay::SubscriberOptions`, the plain-struct C++ binding shape used by `INode::subscribe()`, alongside (not replacing) the existing Go-mirrored `SubscriberConfig`/`SubscriberOption` functional-options helpers required by §14.1
- `Frame::to_message()` member function (delegates to the existing `can::to_message()` free function) per §18.2's member-function convention
- `send --format json`: reads `relay.Message` NDJSON on stdin and publishes each converted, validated frame until EOF (§11.2's streaming JSON sink) — the parse/convert/validate/publish pipeline (`cli::send_json_stream`) works against any `can::IBus`; the CLI currently publishes onto a fresh in-process virtual bus pending a real transport (closes #7)
- 27 new tests covering `Context`, `Channel<T>`, `SubscriberOptions`, `--format` validation, and the `send --format json` pipeline

### Changed
- **Breaking (C++ API):** `relay::INode`/`relay::ICaller` method signatures now match RELAY spec §18.2 exactly: `send(Context, const Message&)`, `subscribe(SubscriberOptions)`, `close() noexcept`, `call(Context, const Message&)` — previously `send(Message)`, `subscribe(vector<SubscriberOption>)`, `close()`, `call(Message, milliseconds)`. `can::adapt()`'s `CanAdapter` and its tests were updated accordingly (closes #16; `can::IBus` itself — the CAN-domain, `Filter`-based interface — is unaffected)
- Total: 126 requirements, 183 test cases

## [0.1.6] — 2026-06-19

### Fixed
- `version --format json`: added `protocol` and `protocol_int` fields (RELAY spec §12.1 v1.10); absence generated a WARN under `relay conform --strict` that escalates to FAIL in v1.10
- `capabilities`: added `protocol` and `protocol_int` fields (§12.2); normalised features to spec-defined CAN values (`fd`, `isotp`, `j1939`, `dbc`, `e2e`)
- `status`: added `protocol` field (§12.3)
- `convert`: `version` in relay.Message output was `{0,2,0}` — now correctly `{0,0,0}` (zero value per §4.1 golden vectors); would fail `relay interop` against the reference implementation
- `message_to_json`: `seq` now omitted when zero (matches Go's `omitempty` semantics per §4.1)
- `message_to_json`: `meta` now omitted when empty (matches Go's `omitempty` semantics per §4.1)
- `relay interop`: CAN XL frame (`can-xl-frame.json`) now passes — `Frame` extended with `esi`, `xl`, `sdt`, `vcid`, `af`, `sec` fields; `to_message` / `from_message` / `parse_frame_json` updated to handle them

### Added
- `relay interop --protocol CAN` CI gate (RELAY §20 Continuous Conformance)
- CAN XL fields on `Frame`: `esi` (Error State Indicator), `xl` (XL flag), `sdt` (SDU Type), `vcid` (Virtual CAN ID), `af` (Acceptance Field), `sec` (Simple Extended Content); `kCANXLMaxDataLen = 2048` constant (RELAY §15.1 / ISO 11898-1:2024)
- 13 new tests: XL frame validation, `to_message`/`from_message` round-trips, `parse_frame_json` XL fields, `message_to_json` conditional XL meta

### Changed
- `features` list normalised to spec-defined CAN values: `fd`, `isotp`, `j1939`, `dbc`, `e2e` (removed non-spec `convert`, `validate`)

- Total: 119 requirements, 163 test cases

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
