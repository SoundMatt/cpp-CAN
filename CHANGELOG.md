# Changelog

All notable changes to cpp-CAN are documented here.

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
