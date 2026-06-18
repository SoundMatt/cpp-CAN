// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <can/j1939/pgn.hpp>
#include <thread>

namespace can::j1939 {

// ── ID decode / encode ────────────────────────────────────────────────────────

// fusa:req REQ-J1939-001 through REQ-J1939-003
DecodedID decode_id(uint32_t id) noexcept {
    Priority priority = static_cast<Priority>((id >> 26) & 0x07);
    uint8_t  src      = static_cast<uint8_t>(id & 0xFF);
    uint8_t  pf       = static_cast<uint8_t>((id >> 16) & 0xFF);
    uint8_t  ps       = static_cast<uint8_t>((id >> 8)  & 0xFF);
    uint8_t  dp       = static_cast<uint8_t>((id >> 24) & 0x01);

    PGN pgn;
    if (pf < 240) {
        pgn = static_cast<PGN>(static_cast<uint32_t>(dp) << 17 | static_cast<uint32_t>(pf) << 8);
    } else {
        pgn = static_cast<PGN>(static_cast<uint32_t>(dp) << 17 | static_cast<uint32_t>(pf) << 8 | ps);
    }
    return {priority, pgn, src};
}

// fusa:req REQ-J1939-004
uint32_t encode_id(Priority priority, PGN pgn, uint8_t src) noexcept {
    uint8_t  pf = static_cast<uint8_t>((pgn >> 8) & 0xFF);
    uint8_t  ps = static_cast<uint8_t>(pgn & 0xFF);
    uint8_t  dp = static_cast<uint8_t>((pgn >> 17) & 0x01);
    uint32_t id = 0;
    id |= static_cast<uint32_t>(priority & 0x07) << 26;
    id |= static_cast<uint32_t>(dp) << 24;
    id |= static_cast<uint32_t>(pf) << 16;
    if (pf >= 240) id |= static_cast<uint32_t>(ps) << 8;
    id |= src;
    return id;
}

// ── Bus factory ───────────────────────────────────────────────────────────────

std::shared_ptr<Bus> Bus::create(std::shared_ptr<can::IBus> can_bus, uint8_t src_addr) {
    return std::shared_ptr<Bus>(new Bus(std::move(can_bus), src_addr));
}

// ── Bus::send ─────────────────────────────────────────────────────────────────

// fusa:req REQ-J1939-005
std::error_code Bus::send(const Frame& f) {
    if (f.data.size() > 8)
        return send_tp(f, std::chrono::milliseconds{50});

    uint32_t id = encode_id(f.priority, f.pgn, src_);
    if (pgn_is_peer_to_peer(f.pgn)) id |= static_cast<uint32_t>(f.dst) << 8;
    return can_->send(can::Frame{id, true, false, false, false, f.data});
}

// ── Bus::subscribe ────────────────────────────────────────────────────────────

static bool matches_pgns(PGN pgn, const std::vector<PGN>& filter) {
    if (filter.empty()) return true;
    for (auto p : filter) if (p == pgn) return true;
    return false;
}

// fusa:req REQ-J1939-006
std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
Bus::subscribe(std::vector<PGN> pgns) {
    auto [raw, err] = can_->subscribe({}, {});
    if (err) return {nullptr, err};

    auto out = std::make_shared<Chan<Frame>>(64);

    std::thread([raw = std::move(raw), out, pgns = std::move(pgns)]() mutable {
        while (auto opt = raw->recv()) {
            const auto& cf = *opt;
            if (!cf.ext) continue;
            auto [priority, pgn, src] = decode_id(cf.id);
            if (!matches_pgns(pgn, pgns)) continue;

            uint8_t dst = kBroadcastAddr;
            if (pgn_is_peer_to_peer(pgn))
                dst = static_cast<uint8_t>((cf.id >> 8) & 0xFF);

            Frame jf{priority, pgn, src, dst, cf.data};
            out->try_send(std::move(jf));
        }
        out->close();
    }).detach();

    return {out, {}};
}

std::error_code Bus::close() {
    return can_->close();
}

// ── Transport Protocol (BAM) ──────────────────────────────────────────────────

static constexpr PGN  kPgnTPCM = 0xEC00;
static constexpr PGN  kPgnTPDT = 0xEB00;
static constexpr uint8_t kBAMControl = 0x20;
static constexpr int  kTPMaxDataBytes   = 1785;
static constexpr int  kTPBytesPerPacket = 7;

std::error_code Bus::send_tp(const Frame& f, std::chrono::milliseconds packet_delay) {
    int n = static_cast<int>(f.data.size());
    if (n < 9)  return std::make_error_code(std::errc::invalid_argument);
    if (n > kTPMaxDataBytes) return relay::ErrPayloadTooLarge();

    int num_packets = (n + kTPBytesPerPacket - 1) / kTPBytesPerPacket;

    // 1. Send TP.CM_BAM
    std::vector<uint8_t> bam = {
        kBAMControl,
        static_cast<uint8_t>(n),
        static_cast<uint8_t>(n >> 8),
        static_cast<uint8_t>(num_packets),
        0xFF,
        static_cast<uint8_t>(f.pgn),
        static_cast<uint8_t>(f.pgn >> 8),
        static_cast<uint8_t>(f.pgn >> 16),
    };
    uint32_t bam_id = encode_id(f.priority, kPgnTPCM, src_);
    if (auto err = can_->send({bam_id, true, false, false, false, bam}); err) return err;

    // 2. Send TP.DT packets
    uint32_t dt_base = encode_id(f.priority, kPgnTPDT, src_);
    for (int seq = 1; seq <= num_packets; ++seq) {
        if (seq > 1) std::this_thread::sleep_for(packet_delay);

        std::vector<uint8_t> pkt(8, 0xFF);
        pkt[0] = static_cast<uint8_t>(seq);
        int offset = (seq - 1) * kTPBytesPerPacket;
        for (int i = 1; i <= kTPBytesPerPacket; ++i) {
            int src = offset + i - 1;
            if (src < n) pkt[i] = f.data[src];
        }
        if (auto err = can_->send({dt_base, true, false, false, false, pkt}); err) return err;
    }
    return {};
}

std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
Bus::subscribe_tp(std::vector<PGN> pgns) {
    auto [raw, err] = can_->subscribe({}, {});
    if (err) return {nullptr, err};

    auto out = std::make_shared<Chan<Frame>>(64);

    std::thread([raw = std::move(raw), out, pgns = std::move(pgns)]() mutable {
        struct BamSession {
            int      total_size{};
            int      num_packets{};
            PGN      pgn{};
            Priority priority{};
            uint8_t  src{};
            std::vector<uint8_t> buf;
            int      received{};
        };
        std::unordered_map<uint8_t, BamSession> sessions;

        while (auto opt = raw->recv()) {
            const auto& cf = *opt;
            if (!cf.ext) continue;
            auto [priority, pgn, src] = decode_id(cf.id);

            if (pgn == kPgnTPCM) {
                if (cf.data.size() < 8 || cf.data[0] != kBAMControl) continue;
                int total = static_cast<int>(cf.data[1]) | static_cast<int>(cf.data[2]) << 8;
                int np    = cf.data[3];
                PGN target = static_cast<PGN>(
                    static_cast<uint32_t>(cf.data[5])       |
                    static_cast<uint32_t>(cf.data[6]) << 8  |
                    static_cast<uint32_t>(cf.data[7]) << 16);
                sessions[src] = {total, np, target, priority, src,
                                  std::vector<uint8_t>(total), 0};
            } else if (pgn == kPgnTPDT) {
                auto it = sessions.find(src);
                if (it == sessions.end() || cf.data.size() < 8) continue;
                auto& sess = it->second;
                int seq = cf.data[0];
                if (seq < 1 || seq > sess.num_packets) continue;
                int offset = (seq - 1) * kTPBytesPerPacket;
                for (int i = 1; i <= kTPBytesPerPacket; ++i) {
                    int dst = offset + i - 1;
                    if (dst < sess.total_size) sess.buf[dst] = cf.data[i];
                }
                ++sess.received;
                if (sess.received == sess.num_packets) {
                    if (matches_pgns(sess.pgn, pgns)) {
                        Frame jf{sess.priority, sess.pgn, sess.src, kBroadcastAddr, sess.buf};
                        out->try_send(std::move(jf));
                    }
                    sessions.erase(it);
                }
            }
        }
        out->close();
    }).detach();

    return {out, {}};
}

} // namespace can::j1939
