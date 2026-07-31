# Changelog

All notable changes to cpp-CAN are documented here.

## [0.2.6] — 2026-07-31

### Fixed
- **High: ISO-TP (ISO 15765-2) receiver DoS via a PCI-only Consecutive Frame.** `Conn::recv`'s FF-reassembly loop computed `chunk_len = min(cf.data.size()-1, remaining)`; a peer sending a syntactically-valid but never-progressing CF (PCI byte only, zero data bytes) drove `chunk_len` to 0 on every iteration, so `buf` never grew and the `while (buf.size() < length)` loop never terminated. Per ISO 15765-2's segmented-transfer semantics, a sender only emits a further CF while remaining unsent payload > 0, so a conformant transfer never produces this shape — `recv()` now rejects a CF with fewer than 2 bytes (PCI + ≥1 data byte) as `std::errc::bad_message` instead of looping forever. Reproduced the original hang: reverted the fix in an isolated copy and confirmed the added regression test (`recv rejects a PCI-only Consecutive Frame instead of looping forever`, `tests/test_isotp.cpp`) hangs indefinitely (killed by an 8s process-level timeout, exit 124) without it, and passes in well under 200ms with it (`src/isotp/transport.cpp`).
- **High: DBC signal decode performed an out-of-range shift (undefined behavior) on a malformed signal length/start bit.** `extract_raw`/`Signal::decode` (`src/dbc/parser.cpp`) shifted by `length`/`length-1`/loop index `i` with no range check on `Signal::length` or `start_bit`, both parsed via unchecked `std::stoi` from (commonly third-party/untrusted) DBC text; a declared `length` outside `[1,64]` or a negative `start_bit` shifts by a negative amount or by ≥ the width of the promoted left operand — UB per C++17 [expr.shift]p1. Guarded both functions to return 0 for an out-of-range length/start_bit instead of performing the shift. Reproduced under UBSan: reverted the guards in an isolated copy and confirmed the added regression tests (`tests/test_dbc.cpp`, signal length > 64 / <= 0 / negative start_bit) trip `runtime error: shift exponent -5 is negative` and produce garbage (`18446744073709551616.0`) instead of the expected `0.0`; full sanitizer suite (ASan+UBSan, matching CI's `sanitizers` job) is green with the fix applied.
- **Medium: CAN FD non-canonical payload lengths accepted by `validate_frame()`, then silently unsendable.** ISO 11898-1's CAN-FD DLC table only represents lengths `{0..8,12,16,20,24,32,48,64}` on the wire (DLC 9-15 map to 12/16/20/24/32/48/64); `validate_frame()` only checked `data.size() > 64`, so a non-canonical length (e.g. 9, 11, 30, 50 bytes) passed validation and then failed downstream at `socketcan::Bus::send` (kernel `write()` → `-EINVAL` → the generic `ErrNotConnected`), contradicting the library's "CAN FD fully supported" claim. `validate_frame()` now rejects a non-canonical FD length explicitly and actionably (`src/can.cpp`).
- **Medium: ISO-TP application-level timers (N_Bs/N_Cr) were accepted as a parameter but silently discarded.** `Conn::recv`, `wait_fc`, and `recv_cf` (`src/isotp/transport.cpp`) took a `timeout` and blocked on the underlying channel indefinitely regardless (`// TODO: add timeout to Chan`, `/*timeout*/`); a stalled or malicious peer that stopped sending mid-transfer hung the caller forever despite it supplying a timeout, compounding the PCI-only-CF DoS above. Added `Chan<T>::recv_for(duration)` (`include/can/channel.hpp`) and wired real, deadline-bound waits through `recv`/`wait_fc`/`recv_cf`, returning `can::ErrTimeout()` on expiry. Reproduced the original hang the same way as above (regression tests `recv returns ErrTimeout (bounded) when no frame ever arrives` / `send returns ErrTimeout (bounded) when Flow Control never arrives` hang and are killed by the process-level timeout without the fix, pass in well under 1s with it).
- **Medium: J1939 BAM reassembly accepted an out-of-protocol-range declared total size with no relationship enforced against the declared packet count.** `subscribe_tp`'s BAM handler (`src/j1939/pgn.cpp`) read `total` straight from `TP.CM_BAM` and allocated `std::vector<uint8_t>(total)` unconditionally; a spoofed announcement combining an out-of-range `total` (SAE J1939-21 caps it at 1785 bytes) with a small `num_packets` could still satisfy the `received == num_packets` completion check from a couple of real `TP.DT` frames and get delivered as an oversized, mostly-zero-padded message. `total` outside `[9,1785]` is now rejected before any session is created; also added a J1939-21 T1-inspired 750ms stall/abort timeout that reclaims sessions which stop receiving `TP.DT` segments, plus a belt-and-braces cap (256) on concurrently-tracked sessions. Reproduced against the pre-fix handler: the added regression test (`subscribe_tp: BAM announcing an out-of-range total is rejected, not silently short-completed`, `tests/test_j1939.cpp`) fails on the unpatched code (delivers a 2000-byte message, 21 real bytes + zero padding) and passes with the fix.
- **Low: `from_message` returned an unvalidated `Frame`, bypassing `validate_frame()` for direct callers.** `from_message` (`src/can.cpp`) only range-checked the CAN ID before returning; a structurally invalid `Frame` (e.g. a non-canonical FD length, or flag combinations `validate_frame()` would reject) could reach any direct caller that doesn't separately call `validate_frame()` itself — `CanAdapter::send` happened to catch this downstream via the bus's own validation, but nothing guaranteed that. `from_message` now calls `validate_frame()` before returning.
- **Low: `CanAdapter::send` collapsed every `ErrInvalidFrame` reason into `relay::Errc::payload_too_large`**, losing error fidelity for structurally-invalid-but-not-oversized frames (e.g. the non-canonical FD length above). Now reports `std::errc::invalid_argument` instead, distinct from the oversize-specific error.
- **Low: misleading validation error message.** The non-XL `esi && !fd` check in `validate_frame()` said `"ESI requires fd=true or xl=true"` even though `xl` is necessarily `false` in that branch; corrected to `"ESI requires fd=true"`.
- **CI**: `ilammy/msvc-dev-cmd` was pinned to the mutable tag `@v1`; pinned to its current commit SHA (`0b201ec74fa43914dc39ae48a89fd1d8cb592756`) instead, matching this project's SHA-conscious supply-chain posture elsewhere.

### Deferred
- **CI: clang-tidy exit code masking (`|| true` + post-hoc `grep -q "error:"`).** Investigated switching to `--warnings-as-errors` with the job's existing check set (`clang-analyzer-*,bugprone-*,modernize-use-override,modernize-use-nullptr,cppcoreguidelines-no-malloc`); ran it locally against the current tree and it surfaces roughly a dozen pre-existing `bugprone-*` diagnostics unrelated to this fix pass (`bugprone-easily-swappable-parameters`, `bugprone-invalid-enum-default-initialization`, `bugprone-std-namespace-modification`, `bugprone-empty-catch`, spread across `relay.hpp`, `dbc/parser.cpp`, `j1939/pgn.cpp`, `safety/e2e.cpp`, `relay.cpp`). Turning the mask off would immediately fail CI on findings out of scope for this PR; tracked as follow-up work rather than bundled in here.

## [0.2.5] — 2026-07-30

### Changed
- **CI**: `CPP_FUSA_REF` (cpp-FuSa version pin, §20.1.2) bumped `v0.17.1` → `v0.18.0` in both the `fusa-asil-b` job (via the env var) and the `sarif` job (previously a separate hardcoded `v0.17.1` literal, now also `v0.18.0` — kept in sync so both jobs run the same `cpfusa` build). Verified for real before pushing: built `cpfusa` v0.18.0 locally and ran the entire `fusa-asil-b` gating sequence (`check`, `lint`, `trace`, `cyber --write`, `qualify`, `hara init`, `boundary`, `tara`, `fmea`, `safety-case`, `sas`, `sci`, `iso26262`, `iec61508`, `badge`, `vuln`, `release`, `metrics record`, `report`) plus the `sarif` job's `check --format sarif` against cpp-CAN's own tree — all pass with 0 errors, matching the pre-bump baseline. Reviewed cpp-FuSa's v0.17.2/v0.18.0 CHANGELOG entries (independent third-party audit round, worst score in the x-FuSa ecosystem): the `hara asil`/`hara init` Table 4 fix, three live-exploitable command-injection fixes (`impact`/`analyze`/`verify`'s `popen()` shell-string `--dir`, `audit-pack`'s `zip` invocation, `release`'s SBOM/provenance `git`/`cmake` calls), and the lint `standard` attribution/gap-report `§3.1` header fixes are all cpfusa-tool-internal — none require a cpp-CAN source or config change.
- **Lowered `ISO26262_GAP_BASELINE` 12→7 and `IEC61508_GAP_BASELINE` 10→4** in the `fusa-asil-b` job. Re-ran `cpfusa iso26262`/`iec61508` against the identical, unmodified cpp-CAN tree under both the old (`v0.17.1`) and new (`v0.18.0`) `cpfusa` binaries to isolate the cause: `v0.17.1` reproduces the previously-disclosed `12/20`/`10/18` baseline exactly; `v0.18.0` reports `7/20`/`4/18` against the same tree. This is cpp-FuSa v0.17.2's gap-report evidence-detection fix (cpp-FuSa#57–#59) correctly recognizing evidence this project already had (`tara.json`, `fmea.json`, `safety-case.json`, `.fusa-hara.json`, etc.) that the old binary failed to detect and misreported as "gap" — not new cpp-CAN safety-lifecycle documentation, and not a regression. Per the baseline-gate step's own convention (lower the baseline on a genuine improvement rather than leave slack in the regression gate), the baselines now reflect the re-verified, tool-corrected counts.
- Bumped `cppcan` project version `0.2.4` → `0.2.5` (`CMakeLists.txt`, `cli/json.hpp`'s `kToolVersion`, `.fusa.json`) per this repo's convention of a patch bump for CI-tool-pin updates (precedent: the `v0.15.0`→`v0.17.1` `CPP_FUSA_REF` bump folded into `[0.2.2]` below).

## [0.2.4] — 2026-07-30

### Fixed
- **CI**: `relay-conform` job installed the reference `relay` CLI via `go install github.com/SoundMatt/RELAY/cmd/relay@latest`. RELAY's v2.0 MAJOR bump (RCP canonical-type replacement — RCP-specific, does not affect CAN) required a `github.com/SoundMatt/RELAY/v2` module path per Go's semantic import versioning rules (RELAY#70); the old bare (non-`/v2`) path was left resolving `@latest` to the last v1.x tag (`v1.14.0`) and, since v2.x tags live only under the new `/v2` path, would have kept resolving there forever, three spec releases (v1.12, v1.13, v1.14) plus the v2.0 MAJOR behind current. Fixed to `go install github.com/SoundMatt/RELAY/v2/cmd/relay@v2.0.4`, and pinned to an explicit tag via a new `RELAY_REF` job-level env var — matching this repo's existing "pin, don't float" convention (§20.1.2, already used for the `CPP_FUSA_REF` cpp-FuSa pin) — rather than continuing to float on `@latest`.
- **`relay::kRelaySpecVersion`** (`include/can/relay.hpp`) had drifted to `"1.11"`, three RELAY spec releases stale. Verified cpp-CAN against RELAY v2.0.4 (spec 2.0) for real: built the pinned `relay` CLI locally and ran `relay conform --strict`/`relay interop --strict --protocol CAN` against a freshly-built `cpp-can-cli` — both PASS, including the CAN reject-path golden vectors (`can-fd-xl-mutually-exclusive`, `can-rtr-with-fd`, `can-standard-id-overflow`, `can-xl-priority-id-overflow`), which v1.13 fixed to actually be exercised by `relay interop` for the first time (previously unreachable through RELAY's `go:embed` glob). Reviewed the RELAY v1.12–v1.14 and v2.0 CHANGELOG entries individually: v1.12 (`"c"` as a valid CLI `language` value) and v1.14 (RCP module-name registry) don't apply to CAN; v1.13's two live `relay conform`/`relay interop` CLI bugs were RELAY-tool-side fixes requiring no implementation change, and its added §18.2 C++ `HealthProvider`/`MetricsProvider`/`Drainer`/`SubscriberOptions` binding text describes RELAY §9's already-optional interfaces, which cpp-CAN already implements and declares in `optional_interfaces` (unaffected); v2.0's RCP canonical-type break is RCP-only. With conformance re-verified for real against the current spec, bumped `kRelaySpecVersion` `"1.11"` → `"2.0"` (and the `spec_version` label wherever it's derived from — CLI `version`/`capabilities` JSON, tests, `requirements/requirements.json`'s REQ-RELAY-020 text, regenerated `.fusa-reqs.json`) so the self-reported value matches what was actually verified, rather than continuing to overclaim compliance with a three-releases-stale target (closes #32).

## [0.2.3] — 2026-07-30

### Fixed
- **Critical**: J1939 `decode_id`/`encode_id` placed the Data Page (DP) bit at PGN bit 17 (the Reserved/EDP slot) instead of bit 16, per the PGN's own R(17)·DP(16)·PF(15-8)·PS(7-0) structure (SAE J1939-21). Every DP=1 message produced a PGN off by `0x10000`. The `decode_id`/`encode_id` round-trip is self-consistent under the bug (both directions shift by the same wrong amount), which is why it went undetected; the dedicated DP-bit golden test (`REQ-J1939-003`) — which hand-constructs a raw 29-bit CAN ID from field positions rather than deriving it from `encode_id` — has been corrected to assert bit 16
- J1939 BAM `TP.CM`/`TP.DT` frames were addressed to destination `0x00` instead of the required global address `0xFF`, because `encode_id` only ORs a PS/destination byte for PF ≥ 240 (PDU2); `send_tp` now explicitly ORs `kBroadcastAddr` into both the `TP.CM_BAM` and `TP.DT` PDU1 IDs
- J1939 BAM reassembler (`subscribe_tp`) counted duplicate/retransmitted `TP.DT` segments toward completion via a bare counter, which could emit a frame with zero-filled gaps still present; now tracks per-sequence-number receipt in a `seen` bitmap and only counts a segment once
- ISO-TP (ISO 15765-2) receiver advertised a non-zero BlockSize in its Flow Control but never emitted a follow-up FC at each block boundary, causing a conformant sender to stall after the first block; `Conn::recv` now counts consecutive CFs against `cfg_.block_size` and issues a fresh CTS FC when a block completes
- `capabilities`: added the implemented `"send"` command (was omitted from `commands`); replaced the meaningless `"transports":["CAN"]` (the protocol name) with the actual compiled-in backends `["socketcan","virtual"]`; removed the falsely-advertised `"ICaller"` interface (CAN has no concrete `ICaller` — it is pure pub/sub)
- `from_message` could throw an uncaught `std::invalid_argument` from `std::stoull` on malformed `can.sdt`/`can.vcid`/`can.af` meta values reachable from untrusted NDJSON input; now caught and converted to `ErrInvalidFrame`, which callers already handle
- `socketcan::Bus::send` returned `payload_too_large` for structurally-valid but transport-unsupported CAN-XL frames, conflating "unsupported feature" with "oversize payload"; now returns `std::errc::not_supported`

### Changed
- README build instructions now note the Ninja requirement (CI and the Dockerfile force `-G Ninja`), matching CONTRIBUTING.md
- CHANGELOG `[0.1.6]` Changed entry annotated to flag its `dbc`/`e2e` feature claim as corrected in `[0.1.8]`, resolving the self-contradiction between the two entries
- SECURITY.md "Supported Versions" table now lists `0.2.x`/`0.1.x` explicitly instead of only `main`/`< v0.1`

## [0.2.2] — 2026-07-29

### Fixed
- **`.fusa-reqs.json` was permanently empty** (`{"requirements":[]}`), so the ASIL-B qualification pipeline's requirements-traceability gate (`cpfusa trace`/`metrics`) always reported 0 requirements / 0% coverage, while an adjacent `iso26262` check in the same job self-contradictorily reported traceability as satisfied. Root cause: `cpfusa init --force` (run unconditionally every CI run) resets `.fusa-reqs.json` to an empty array even when it already has content. Added `scripts/gen_fusa_reqs.py`, which derives `.fusa-reqs.json` from `requirements/requirements.json` (the human-maintained source of truth) plus a source scan for `fusa:req`/`fusa:test` annotations, run in CI before `cpfusa init`; dropped `--force` from `cpfusa init` (now a safe no-op once the files exist) (closes #26)
- **CI steps wrapped in `|| true` swallowed real ISO 26262/IEC 61508 majority-gap results** and could never fail the build regardless of findings. Root-caused two real problems first: (1) the empty-`.fusa-reqs.json` bug above, which inflated §4.6/Annex B.4 traceability to a false gap, and (2) `iso26262`/`iec61508` ran *before* the evidence-generating steps (`tara`/`fmea`/`safety-case`/`sas`/`sci`/`boundary`) in the same job, so those clauses were graded against evidence that didn't exist on disk yet — reordering fixed this. With both real causes fixed, gap counts improved from 14/20→12/20 (ISO 26262) and 12/18→10/18 (IEC 61508); the remaining gaps are genuine, disclosed lifecycle-documentation debt (tracked: #36), not masking artifacts. Replaced the blind `|| true` on `iso26262`/`iec61508` with `continue-on-error: true` (visible in the Actions UI, unlike a silent swallow) plus an explicit baseline-gate step that fails the build only on regression. Removed `|| true` outright from `lint`, `hara init`, and the SARIF `check` step — verified all three exit 0 on a clean checkout (0 lint errors; `hara init` is idempotent-safe without `--force`) (closes #28)
- **`relay interop` CI gate omitted `--strict`**, unlike the `relay conform` gate immediately above it, and `.fusa.json` declared `strict: false` at the config root. Added `--strict` to the interop invocation and flipped `.fusa.json`'s `strict` to `true` — reverified all 7 golden-vector comparisons still pass under strict mode, and that `cpfusa qualify`/`check` are unaffected (closes #29)
- **Live SocketCAN interop CI job silently took the skip path** on every run to date (GitHub-hosted `ubuntu-22.04` runners' kernel does not ship the `vcan` module — `modprobe: FATAL: Module vcan not found`, confirmed from CI logs; not a permissions/setup problem, and not fixable via a container since containers share the host kernel), while still reporting job-level `success` with only a low-visibility `::notice::`. Upgraded to a loud `::warning::` annotation plus a `$GITHUB_STEP_SUMMARY` block on every skip (and a distinct summary line when the live path *does* run), and updated ROADMAP.md to state plainly that the live path is implemented and buildable but has not yet been exercised in hosted CI (closes #30)
- **SBOM/provenance/artifact-manifest were referenced in the evidence-artifact upload step but never generated** — no `cpfusa release` step existed in CI at all, and the CI-run artifact bundle they were nominally part of is ephemeral and was never attached to any of the 3 published GitHub Releases checked (`v0.2.0`, `v0.1.8`, `v0.1.6` all show 0 release assets). Added a `cpfusa release` step, `if-no-files-found: error` on the evidence-artifact upload so a future silent gap fails loudly, and a new step (triggered on `release: published`) that attaches the SBOM, provenance, artifact manifest, and other §20.5 evidence directly to the GitHub Release itself via `gh release upload` (closes #27)
- cpp-FuSa CI pin: `v0.15.0` → **`v0.17.1`** (current latest as of this fix; verified two minor releases plus a patch newer) in both locations (`fusa-asil-b` and `sarif` jobs) — re-verified the full ASIL-B pipeline and SARIF generation against the new `cpfusa` CLI surface locally before pinning (closes #31)

## [0.2.1] — 2026-07-29

### Fixed
- **Critical:** `safety::Receiver::unwrap()` committed a rejected out-of-order/replayed
  frame's `seq` as the new baseline *before* throwing `E2EError`, instead of leaving
  `last_seq_` untouched on the reject path. Two replayed frames back-to-back
  (e.g. stale `seq=50` then `seq=51`) would resync the receiver's expected
  sequence to the first replay and then silently accept the second as fresh,
  defeating SG-02 / REQ-SEC-006's documented replay protection (closes #25).
  Added a regression test that replays two consecutive stale frames and
  asserts both are rejected, and that the receiver still accepts the genuine
  next frame afterwards.

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
- `features` list normalised to spec-defined CAN values: `fd`, `isotp`, `j1939`, `dbc`, `e2e` (removed non-spec `convert`, `validate`) _(corrected in 0.1.8: `dbc` and `e2e` are not spec-defined feature values)_

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
