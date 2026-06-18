// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// isotp/transport.hpp — ISO 15765-2 (ISO-TP) transport protocol over CAN.
// Enables multi-frame message transfer, supporting payloads up to 4095 bytes.
// Used by UDS (ISO 14229) and OBD-II (ISO 15031).
//
// Frame types:
//   SF  Single Frame        — payload fits in one CAN frame (≤7 bytes)
//   FF  First Frame         — first segment of a multi-frame message
//   CF  Consecutive Frame   — subsequent segments
//   FC  Flow Control        — receiver pacing signal
//
// fusa:req REQ-ISOTP-001 through REQ-ISOTP-013

#pragma once

#include <can/can.hpp>
#include <chrono>
#include <memory>
#include <system_error>
#include <vector>

namespace can::isotp {

// Configuration for an ISO-TP connection.
// fusa:req REQ-ISOTP-001
// fusa:req REQ-ISOTP-002
// fusa:req REQ-ISOTP-003
struct Config {
    uint32_t          tx_id{};              // outgoing CAN arbitration ID
    uint32_t          rx_id{};              // incoming CAN arbitration ID
    bool              ext_ids{};            // use 29-bit extended IDs
    uint8_t           block_size{};         // max CFs per block (0 = unlimited)
    uint8_t           st_min{};             // min separation time
    std::chrono::milliseconds timeout{250}; // flow-control / CF timeout
};

// Conn is a point-to-point ISO-TP session over a CAN bus.
// fusa:req REQ-ISOTP-004
class Conn {
public:
    // Creates a new Conn, subscribing for rx_id on bus.
    // fusa:req REQ-ISOTP-001 through REQ-ISOTP-004
    static std::pair<std::unique_ptr<Conn>, std::error_code>
        create(std::shared_ptr<IBus> bus, Config cfg);

    // Transmits payload using ISO-TP segmentation (up to 4095 bytes).
    // fusa:req REQ-ISOTP-005 through REQ-ISOTP-008
    std::error_code send(const std::vector<uint8_t>& payload);

    // Reassembles and returns the next ISO-TP message.
    // fusa:req REQ-ISOTP-009 through REQ-ISOTP-013
    std::pair<std::vector<uint8_t>, std::error_code>
        recv(std::chrono::milliseconds timeout = std::chrono::milliseconds{250});

private:
    Conn(std::shared_ptr<IBus> bus, Config cfg,
         std::shared_ptr<Chan<Frame>> rx_ch);

    std::error_code send_single_frame(const std::vector<uint8_t>& payload);
    std::error_code send_multi_frame(const std::vector<uint8_t>& payload);
    std::pair<std::vector<uint8_t>, std::error_code>
        wait_fc(std::chrono::milliseconds timeout);
    std::pair<Frame, std::error_code>
        recv_cf(std::chrono::milliseconds timeout);
    Frame make_frame(std::vector<uint8_t> data) const;

    std::shared_ptr<IBus>        bus_;
    Config                        cfg_;
    std::shared_ptr<Chan<Frame>>  rx_ch_;
};

} // namespace can::isotp
