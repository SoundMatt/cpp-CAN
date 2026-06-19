# cpp-CAN — Hazard Analysis and Risk Assessment (HARA)

**Standard:** ISO 26262:2018 Part 3 §6 / IEC 61508-3:2010 §7.4  
**ASIL target:** ASIL-B (ISO 26262) / SIL 2 (IEC 61508)  
**Scope:** Software component only — system-level HARA, hardware fault model (FMEDA), and airworthiness are out of scope.

---

## 1. Hazard identification

| ID | Hazard | Trigger condition |
|----|--------|-------------------|
| H-01 | Silent frame corruption — wrong payload delivered to consumer | CRC/seq check not applied or bypassed |
| H-02 | Replay attack — stale frame injected, triggers unintended actuation | SeqCounter not checked |
| H-03 | Buffer overflow — heap exhaustion from unbounded channel growth | Back-pressure policy not applied |
| H-04 | Illegal CAN ID — frame with out-of-range ID forwarded to bus hardware | validate_frame() not called |
| H-05 | RTR+FD confusion — undefined CAN frame variant forwarded | RTR+FD combination not rejected |
| H-06 | Oversized ISO-TP payload — segmentation overflow | 4095-byte limit not enforced |
| H-07 | ISO-TP sequence gap — reassembled payload is silently truncated | CF sequence numbers not checked |
| H-08 | Use-after-free on closed bus — null dereference or data race | Bus closed state not checked on send/subscribe |

---

## 2. Risk assessment

| Hazard | Severity | Exposure | Controllability | ASIL |
|--------|----------|----------|-----------------|------|
| H-01 | S2 | E3 | C2 | ASIL-B |
| H-02 | S2 | E3 | C2 | ASIL-B |
| H-03 | S2 | E3 | C3 | ASIL-B |
| H-04 | S1 | E3 | C2 | ASIL-A |
| H-05 | S1 | E2 | C2 | QM |
| H-06 | S2 | E2 | C2 | ASIL-A |
| H-07 | S2 | E2 | C2 | ASIL-A |
| H-08 | S2 | E3 | C2 | ASIL-B |

---

## 3. Safety goals and requirements mapping

| Safety goal | Derived requirement | Implemented by |
|-------------|--------------------|----|
| SG-01: Corrupt frames shall not be silently delivered | REQ-SAFETY-005 through REQ-SAFETY-010 | `safety::Protector`, `safety::Receiver` |
| SG-02: Replayed frames shall be rejected | REQ-SAFETY-009, REQ-SEC-006 | `Receiver::unwrap()` sequence counter |
| SG-03: Channel capacity shall be bounded | REQ-SEC-008, REQ-VIRT-004 | `Chan<T>` capacity, back-pressure policies |
| SG-04: Illegal CAN IDs shall be rejected | REQ-CAN-009, REQ-CAN-010, REQ-SEC-001 | `validate_frame()` |
| SG-05: RTR+FD combination shall be rejected | REQ-CAN-013, REQ-SEC-003 | `validate_frame()` |
| SG-06: ISO-TP payload shall be bounded | REQ-ISOTP-008, REQ-SEC-010 | `Conn::send()` |
| SG-07: ISO-TP CF sequence shall be verified | REQ-ISOTP-012, REQ-SEC-011 | `Conn::recv()` |
| SG-08: Closed bus shall be detected | REQ-VIRT-005, REQ-VIRT-006, REQ-SEC-012 | `virt::Bus::send()`, `virt::Bus::subscribe()` |

---

## 4. Residual risks

- E2E header DataID/SourceID bytes in the received frame are **not** re-verified against configuration — the Receiver reconstructs the expected CRC from its own `cfg_.data_id`/`cfg_.source_id`. This is a known design characteristic: spoofed DataID/SourceID bytes in an otherwise valid frame will not be detected by CRC alone. Mitigation: configure each Receiver with a unique DataID+SourceID pair so cross-talk between channels is detectable via sequence counter discontinuity.

---

## 5. Assumptions of use (SEOOC)

1. The caller applies `validate_frame()` before calling `IBus::send()` on any hardware bus.
2. E2E `Protector`/`Receiver` pairs are configured with matching `data_id` and `source_id`; they are not shared across independent signal paths.
3. ISO-TP connections are used over a trusted CAN segment; network-level replay protection is provided by the system integrator.
4. Thread safety of `virt::Bus` is tested; for hardware bus implementations the integrator is responsible for access serialisation.
