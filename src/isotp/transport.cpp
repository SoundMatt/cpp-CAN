// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <can/isotp/transport.hpp>
#include <algorithm>
#include <chrono>
#include <system_error>
#include <thread>

namespace can::isotp {

// Frame type nibbles (upper nibble of first byte).
static constexpr uint8_t kTypeSF = 0x00;
static constexpr uint8_t kTypeFF = 0x10;
static constexpr uint8_t kTypeCF = 0x20;
static constexpr uint8_t kTypeFC = 0x30;

// Flow control status codes.
static constexpr uint8_t kFCContinueToSend = 0x00;
static constexpr uint8_t kFCWait           = 0x01;
static constexpr uint8_t kFCOverflow       = 0x02;

static constexpr std::size_t kMaxPayload = 4095;

static std::chrono::microseconds stmin_to_duration(uint8_t stmin) noexcept {
    if (stmin <= 0x7F) return std::chrono::milliseconds(stmin);
    if (stmin >= 0xF1 && stmin <= 0xF9)
        return std::chrono::microseconds((stmin - 0xF0) * 100);
    return std::chrono::microseconds(0);
}

// ── Constructor / factory ─────────────────────────────────────────────────────

Conn::Conn(std::shared_ptr<can::IBus> bus, Config cfg,
           std::shared_ptr<Chan<Frame>> rx_ch)
    : bus_(std::move(bus)), cfg_(cfg), rx_ch_(std::move(rx_ch)) {}

std::pair<std::unique_ptr<Conn>, std::error_code>
Conn::create(std::shared_ptr<can::IBus> bus, Config cfg) {
    auto [rx_ch, err] = bus->subscribe(
        {Filter{cfg.rx_id, 0x1FFFFFFFu}}, {});
    if (err) return {nullptr, err};
    auto conn = std::unique_ptr<Conn>(new Conn(std::move(bus), cfg, std::move(rx_ch)));
    std::pair<std::unique_ptr<Conn>, std::error_code> result;
    result.first = std::move(conn);
    return result;
}

// ── Send ──────────────────────────────────────────────────────────────────────

// fusa:req REQ-ISOTP-005 REQ-ISOTP-006 REQ-ISOTP-007 REQ-ISOTP-008
// fusa:req REQ-SEC-010
std::error_code Conn::send(const std::vector<uint8_t>& payload) {
    if (payload.empty())
        return std::make_error_code(std::errc::invalid_argument);
    if (payload.size() > kMaxPayload)
        return relay::ErrPayloadTooLarge();
    if (payload.size() <= 7)
        return send_single_frame(payload);
    return send_multi_frame(payload);
}

std::error_code Conn::send_single_frame(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> data(payload.size() + 1);
    data[0] = kTypeSF | static_cast<uint8_t>(payload.size());
    std::copy(payload.begin(), payload.end(), data.begin() + 1);
    return bus_->send(make_frame(std::move(data)));
}

std::error_code Conn::send_multi_frame(const std::vector<uint8_t>& payload) {
    // First Frame
    std::vector<uint8_t> ff(8);
    uint16_t len16 = static_cast<uint16_t>(payload.size());
    ff[0] = static_cast<uint8_t>(kTypeFF | ((len16 >> 8) & 0x0F));
    ff[1] = static_cast<uint8_t>(len16 & 0xFF);
    std::copy(payload.begin(), payload.begin() + 6, ff.begin() + 2);
    if (auto err = bus_->send(make_frame(ff)); err) return err;

    // Wait for FC
    auto [fc_data, fc_err] = wait_fc(cfg_.timeout);
    if (fc_err) return fc_err;
    if ((fc_data[0] & 0x0F) == kFCOverflow)
        return std::make_error_code(std::errc::no_buffer_space);

    // Send Consecutive Frames
    std::size_t offset = 6;
    uint8_t     sn = 1;
    int         block_count = 0;

    while (offset < payload.size()) {
        if ((fc_data[0] & 0x0F) == kFCWait) {
            auto [new_fc, new_err] = wait_fc(cfg_.timeout);
            if (new_err) return new_err;
            fc_data = new_fc;
            block_count = 0;
        }

        std::size_t chunk_len = std::min<std::size_t>(7, payload.size() - offset);
        std::vector<uint8_t> cf(chunk_len + 1);
        cf[0] = kTypeCF | (sn & 0x0F);
        std::copy(payload.begin() + offset, payload.begin() + offset + chunk_len, cf.begin() + 1);

        if (auto err = bus_->send(make_frame(cf)); err) return err;

        offset += chunk_len;
        ++sn;
        ++block_count;

        if (fc_data[1] > 0 && block_count >= fc_data[1]) {
            auto [new_fc, new_err] = wait_fc(cfg_.timeout);
            if (new_err) return new_err;
            fc_data = new_fc;
            block_count = 0;
        }

        if (fc_data[2] > 0)
            std::this_thread::sleep_for(stmin_to_duration(fc_data[2]));
    }
    return {};
}

// ── Recv ──────────────────────────────────────────────────────────────────────

// fusa:req REQ-ISOTP-009 REQ-ISOTP-010 REQ-ISOTP-011 REQ-ISOTP-012 REQ-ISOTP-013
// fusa:req REQ-SEC-011
std::pair<std::vector<uint8_t>, std::error_code>
Conn::recv(std::chrono::milliseconds timeout) {
    auto opt_frame = rx_ch_->recv();  // TODO: add timeout to Chan
    if (!opt_frame) return {{}, std::make_error_code(std::errc::connection_aborted)};

    const auto& first = *opt_frame;
    if (first.data.empty()) return {{}, std::make_error_code(std::errc::bad_message)};

    uint8_t frame_type = first.data[0] & 0xF0;

    if (frame_type == kTypeSF) {
        int length = first.data[0] & 0x0F;
        if (length == 0 || static_cast<std::size_t>(length) > first.data.size() - 1)
            return {{}, std::make_error_code(std::errc::bad_message)};
        return {std::vector<uint8_t>(first.data.begin() + 1, first.data.begin() + 1 + length), {}};
    }

    if (frame_type == kTypeFF) {
        if (first.data.size() < 2) return {{}, std::make_error_code(std::errc::bad_message)};
        int length = (static_cast<int>(first.data[0] & 0x0F) << 8) | first.data[1];
        std::vector<uint8_t> buf(first.data.begin() + 2, first.data.end());

        // Send FC
        std::vector<uint8_t> fc_data = {static_cast<uint8_t>(kTypeFC | kFCContinueToSend),
                                         cfg_.block_size, cfg_.st_min};
        if (auto err = bus_->send(make_frame(fc_data)); err) return {{}, err};

        uint8_t sn = 1;
        while (static_cast<int>(buf.size()) < length) {
            auto [cf, cf_err] = recv_cf(timeout);
            if (cf_err) return {{}, cf_err};
            if ((cf.data[0] & 0x0F) != (sn & 0x0F))
                return {{}, std::make_error_code(std::errc::bad_message)};
            int remaining = length - static_cast<int>(buf.size());
            int chunk_len = std::min<int>(static_cast<int>(cf.data.size()) - 1, remaining);
            buf.insert(buf.end(), cf.data.begin() + 1, cf.data.begin() + 1 + chunk_len);
            ++sn;
        }
        return {buf, {}};
    }

    return {{}, std::make_error_code(std::errc::bad_message)};
}

// ── Helpers ───────────────────────────────────────────────────────────────────

std::pair<std::vector<uint8_t>, std::error_code>
Conn::wait_fc(std::chrono::milliseconds /*timeout*/) {
    while (true) {
        auto opt = rx_ch_->recv();
        if (!opt) return {{}, std::make_error_code(std::errc::connection_aborted)};
        if (opt->data.size() >= 3 && (opt->data[0] & 0xF0) == kTypeFC)
            return {opt->data, {}};
    }
}

std::pair<Frame, std::error_code>
Conn::recv_cf(std::chrono::milliseconds /*timeout*/) {
    while (true) {
        auto opt = rx_ch_->recv();
        if (!opt) return {{}, std::make_error_code(std::errc::connection_aborted)};
        if (!opt->data.empty() && (opt->data[0] & 0xF0) == kTypeCF)
            return {*opt, {}};
    }
}

Frame Conn::make_frame(std::vector<uint8_t> data) const {
    return Frame{cfg_.tx_id, cfg_.ext_ids, false, false, false, std::move(data)};
}

} // namespace can::isotp
