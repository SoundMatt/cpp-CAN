// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// safety/e2e.hpp — End-to-end data protection for CAN payloads.
//
// Wire format (little-endian, 10-byte header + original payload):
//   Bytes  0–1   DataID (uint16)
//   Bytes  2–3   SourceID (uint16)
//   Bytes  4–7   SequenceCounter (uint32, monotonically increasing)
//   Bytes  8–9   CRC-16/CCITT-FALSE over bytes 0–7 and original payload
//   Bytes 10+    Original payload
//
// The 10-byte header does not fit in a standard CAN frame (8-byte limit).
// Use with ISO-TP, CAN FD, or J1939 TP.
//
// fusa:req REQ-SAFETY-001 through REQ-SAFETY-011

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace can::safety {

inline constexpr std::size_t kHeaderSize = 10;

// fusa:req REQ-SAFETY-001
// fusa:req REQ-SAFETY-002
struct Config {
    uint16_t data_id{};
    uint16_t source_id{};
};

// fusa:req REQ-SAFETY-003 — E2E error kinds
enum class E2EErrorKind : int {
    CRCMismatch    = 0,
    SequenceGap    = 1,
    HeaderTooShort = 2,
};

class E2EError : public std::runtime_error {
public:
    E2EError(E2EErrorKind kind, uint32_t counter, std::string msg)
        : std::runtime_error("can/safety: E2E error: " + msg)
        , kind_(kind), counter_(counter) {}
    E2EErrorKind kind()    const noexcept { return kind_; }
    uint32_t     counter() const noexcept { return counter_; }
private:
    E2EErrorKind kind_;
    uint32_t     counter_;
};

// Protector adds E2E headers before transmission.
// fusa:req REQ-SAFETY-003
// fusa:req REQ-SAFETY-004
// fusa:req REQ-SAFETY-005
// fusa:req REQ-SAFETY-006
class Protector {
public:
    explicit Protector(Config cfg) : cfg_(cfg), seq_(0) {}

    // Prepends the 10-byte E2E header and returns the protected payload.
    std::vector<uint8_t> protect(const std::vector<uint8_t>& payload);

private:
    Config   cfg_;
    uint32_t seq_;
};

// Receiver validates E2E headers on received payloads.
// fusa:req REQ-SAFETY-007 REQ-SAFETY-008 REQ-SAFETY-009 REQ-SAFETY-010 REQ-SAFETY-011
class Receiver {
public:
    explicit Receiver(Config cfg) : cfg_(cfg), last_seq_(0), first_(true) {}

    // Validates the E2E header in data and returns the original payload.
    // Throws E2EError if validation fails.
    std::vector<uint8_t> unwrap(const std::vector<uint8_t>& data);

private:
    Config              cfg_;
    std::mutex          mu_;
    uint32_t            last_seq_;
    bool                first_;
};

// Internal CRC helpers (exposed for testing).
uint16_t crc16(const uint8_t* data, std::size_t len) noexcept;

} // namespace can::safety
