// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// can.hpp — Core CAN bus types and IBus interface.
// C++ translation of github.com/SoundMatt/go-CAN.

#pragma once

#include "relay.hpp"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// fusa:req REQ-CAN-001
// fusa:req REQ-CAN-002
// fusa:req REQ-CAN-003
// fusa:req REQ-CAN-004
// fusa:req REQ-CAN-005

namespace can {

// ── Spec version ─────────────────────────────────────────────────────────────

inline constexpr const char* kSpecVersion = "0.2";

// ── Constants ─────────────────────────────────────────────────────────────────

inline constexpr uint32_t kCANMaxDataLen   = 8;
inline constexpr uint32_t kCANFDMaxDataLen = 64;
inline constexpr uint32_t kCANXLMaxDataLen = 2048;
inline constexpr uint32_t kCANMaxStdID     = 0x7FF;
inline constexpr uint32_t kCANMaxExtID     = 0x1FFFFFFF;

// ── Error sentinels (aliases for relay errors) ────────────────────────────────

// fusa:req REQ-CAN-008
inline std::error_code ErrClosed()          noexcept { return relay::ErrClosed(); }
inline std::error_code ErrNotConnected()    noexcept { return relay::ErrNotConnected(); }
inline std::error_code ErrTimeout()         noexcept { return relay::ErrTimeout(); }
inline std::error_code ErrPayloadTooLarge() noexcept { return relay::ErrPayloadTooLarge(); }

// ── Frame ─────────────────────────────────────────────────────────────────────

// fusa:req REQ-CAN-001 REQ-CAN-001B REQ-CAN-001C
struct Frame {
    uint32_t             id{};    // arbitration / priority ID (11 or 29 bits)
    bool                 ext{};   // true → 29-bit extended ID
    bool                 rtr{};   // Remote Transmission Request
    bool                 fd{};    // CAN FD frame (payload up to 64 bytes)
    bool                 brs{};   // Bit-Rate Switch (FD only)
    std::vector<uint8_t> data;    // payload
    bool                 esi{};   // Error State Indicator (FD/XL)
    bool                 xl{};    // CAN XL frame (payload up to 2048 bytes)
    uint8_t              sdt{};   // SDU Type (XL only)
    uint8_t              vcid{};  // Virtual CAN network ID (XL only)
    uint32_t             af{};    // Acceptance Field (XL only)
    bool                 sec{};   // Simple Extended Content (XL only)
};

// ── Filter ────────────────────────────────────────────────────────────────────

// fusa:req REQ-CAN-003
struct Filter {
    uint32_t id{};
    uint32_t mask{};  // zero mask passes all frames

    // Returns true when (frame.id & mask) == (id & mask).
    // fusa:req REQ-CAN-004
    bool matches(const Frame& f) const noexcept {
        return (f.id & mask) == (id & mask);
    }
};

// ── Invalid frame error ───────────────────────────────────────────────────────

class ErrInvalidFrame : public std::runtime_error {
public:
    explicit ErrInvalidFrame(std::string reason)
        : std::runtime_error("can: invalid frame: " + reason)
        , reason_(std::move(reason))
    {}
    const std::string& reason() const noexcept { return reason_; }
private:
    std::string reason_;
};

// ── Free functions ────────────────────────────────────────────────────────────

// Returns max payload length for the given frame type.
// fusa:req REQ-CAN-002
inline uint32_t max_data_len(bool fd) noexcept {
    return fd ? kCANFDMaxDataLen : kCANMaxDataLen;
}

// Validates f against CAN protocol constraints.
// Throws ErrInvalidFrame if any constraint is violated.
// fusa:req REQ-CAN-009 REQ-CAN-010 REQ-CAN-011 REQ-CAN-012 REQ-CAN-013 REQ-CAN-014
void validate_frame(const Frame& f);

// ── Bus interface ─────────────────────────────────────────────────────────────

// fusa:req REQ-CAN-006
// fusa:req REQ-CAN-007
// fusa:req REQ-CAN-008
class IBus {
public:
    virtual ~IBus() = default;

    // Transmits f. Blocks until accepted or fails.
    // Returns ErrClosed if the bus is closed.
    virtual std::error_code send(Frame frame) = 0;

    // Returns a channel delivering frames that match any of the supplied
    // filters. With no filters (empty vector) all frames are delivered.
    // The channel is closed when the bus is closed.
    virtual std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
        subscribe(std::vector<Filter> filters = {},
                  std::vector<relay::SubscriberOption> opts = {}) = 0;

    // Idempotent close. Subsequent Send/Subscribe return ErrClosed.
    virtual std::error_code close() = 0;
};

// ── RELAY bridge ─────────────────────────────────────────────────────────────

// fusa:req REQ-CAN-007 REQ-CAN-015
relay::Message to_message(const Frame& f);
Frame          from_message(const relay::Message& m);  // throws ErrInvalidFrame on bad ID

// Wraps an IBus as a relay::INode for cross-protocol routing.
// fusa:req REQ-CAN-016
std::unique_ptr<relay::INode> adapt(std::shared_ptr<IBus> bus);

} // namespace can
