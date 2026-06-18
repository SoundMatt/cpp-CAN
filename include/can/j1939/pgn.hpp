// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// j1939/pgn.hpp — SAE J1939 protocol layer for heavy-duty vehicle networks.
// J1939 uses 29-bit extended CAN IDs and defines a PGN-based addressing scheme.
//
// J1939 29-bit ID layout:
//   Bits 28–26  Priority (P)     — 3 bits (0 highest, 7 lowest)
//   Bit  25     Reserved (R)     — always 0
//   Bit  24     Data Page (DP)   — 0 or 1
//   Bits 23–16  PDU Format (PF)  — 8 bits
//   Bits 15–8   PDU Specific (PS)— destination (PF<240) or group ext (PF≥240)
//   Bits  7–0   Source Address   — 8 bits
//
// fusa:req REQ-J1939-001 through REQ-J1939-006

#pragma once

#include <can/can.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace can::j1939 {

inline constexpr uint8_t kBroadcastAddr = 0xFF;
inline constexpr uint8_t kNullAddr      = 0xFE;

// fusa:req REQ-J1939-001
using Priority = uint8_t;  // 0 = highest, 7 = lowest

// fusa:req REQ-J1939-002
using PGN = uint32_t;  // 18-bit Parameter Group Number

inline bool pgn_is_peer_to_peer(PGN p) noexcept {
    return ((p >> 8) & 0xFF) < 240;
}

// Decoded J1939 message.
struct Frame {
    Priority             priority{};
    PGN                  pgn{};
    uint8_t              src{};
    uint8_t              dst{kBroadcastAddr};
    std::vector<uint8_t> data;
};

// Extracts J1939 fields from a 29-bit extended CAN ID.
// fusa:req REQ-J1939-001
// fusa:req REQ-J1939-002
// fusa:req REQ-J1939-003
struct DecodedID { Priority priority; PGN pgn; uint8_t src; };
DecodedID decode_id(uint32_t id) noexcept;

// Builds a 29-bit J1939 CAN extended ID.
// fusa:req REQ-J1939-004
uint32_t encode_id(Priority priority, PGN pgn, uint8_t src) noexcept;

// J1939 Bus — wraps IBus with PGN-aware send/receive.
// fusa:req REQ-J1939-005
// fusa:req REQ-J1939-006
class Bus {
public:
    // Creates a J1939 Bus with the given source address.
    static std::shared_ptr<Bus> create(std::shared_ptr<can::IBus> can_bus, uint8_t src_addr);

    // Sends a J1939 frame. Payloads > 8 bytes are sent via BAM (J1939 TP).
    // fusa:req REQ-J1939-005
    std::error_code send(const Frame& f);

    // Returns a channel delivering decoded J1939 frames that match the
    // given PGNs. With no PGNs, all J1939 frames are delivered.
    // fusa:req REQ-J1939-006
    std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
        subscribe(std::vector<PGN> pgns = {});

    // Transport protocol (J1939-21 BAM) for multi-packet messages.
    std::error_code send_tp(const Frame& f, std::chrono::milliseconds packet_delay
                            = std::chrono::milliseconds{50});

    std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
        subscribe_tp(std::vector<PGN> pgns = {});

    std::error_code close();

private:
    Bus(std::shared_ptr<can::IBus> can_bus, uint8_t src_addr)
        : can_(std::move(can_bus)), src_(src_addr) {}

    std::shared_ptr<can::IBus> can_;
    uint8_t                    src_;
};

} // namespace can::j1939
