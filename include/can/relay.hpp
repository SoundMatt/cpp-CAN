// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// relay.hpp — RELAY spec types shared across all protocol implementations.
// Local bundled copy of the RELAY core types (§13.7.3) until relay.hpp (§18.2)
// is published as a standalone binding.

#pragma once

#include "channel.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace relay {

// ── Spec version ─────────────────────────────────────────────────────────────

// The single source of truth for the targeted RELAY spec version (§19.4).
//
// Not "2.0": RELAY v2.0.0 (spec/version.json "2.0") is a MAJOR breaking
// release that replaces §15.5's RCP canonical types with the real OPEN
// Alliance TC18 protocol — it does not touch the CAN canonical types cpp-CAN
// implements, and is not reachable via `go install .../relay@latest` as of
// this writing: RELAY's own go.mod was not updated to the `/v2` module-path
// suffix Go's module-versioning rules require for a v2+ tag, so `go install`
// (as CI's relay-conform job uses) and `go get` both still resolve to
// v1.14.0 regardless (confirmed: building relay directly from the v2.0.0 git
// tag still reports `relay.SpecVersion = "1.14"` internally — the tag itself
// predates that constant being bumped). Filed upstream:
// https://github.com/SoundMatt/RELAY/issues/71 (go.mod /v2 path + SpecVersion
// const both need fixing before a v2.0.0-targeting pin is meaningful here).
// "1.14" is the actual current, reachable, and relevant spec version:
// verified via `relay conform --strict` and `relay interop --strict` against
// both `go install .../relay@latest` (resolves to 1.14.0) and a binary built
// directly from the v2.0.0 tag — both PASS.
//
// fusa:req REQ-RELAY-020
inline constexpr std::string_view kRelaySpecVersion = "1.14";

// ── Protocol ─────────────────────────────────────────────────────────────────

// fusa:req REQ-RELAY-001 REQ-RELAY-002
enum class Protocol : int {
    CAN    = 1,
    DDS    = 2,
    LIN    = 3,
    MQTT   = 4,
    RCP    = 5,
    SOMEIP = 6,
};

// fusa:req REQ-RELAY-003
std::string to_string(Protocol p);

// fusa:req REQ-RELAY-059
Protocol    parse_protocol(std::string_view s);   // throws std::invalid_argument if unknown
bool        try_parse_protocol(std::string_view s, Protocol& out) noexcept;

// ── Version ───────────────────────────────────────────────────────────────────

// fusa:req REQ-RELAY-004 REQ-RELAY-005
struct Version {
    int major{};
    int minor{};
    int patch{};

    std::string to_string() const;
    bool operator==(const Version& o) const noexcept {
        return major == o.major && minor == o.minor && patch == o.patch;
    }
};

// ── Message ───────────────────────────────────────────────────────────────────

// fusa:req REQ-RELAY-006 REQ-RELAY-007
struct Message {
    Protocol  protocol{};
    Version   version{};
    std::string id;
    std::vector<uint8_t> payload;
    std::chrono::system_clock::time_point timestamp;
    uint64_t  seq{};
    std::unordered_map<std::string, std::string> meta;
};

// ── Error codes ───────────────────────────────────────────────────────────────

// fusa:req REQ-RELAY-008 REQ-RELAY-009 REQ-RELAY-010 REQ-RELAY-011 REQ-RELAY-012
enum class Errc : int {
    closed            = 1,
    not_connected     = 2,
    timeout           = 3,
    payload_too_large = 4,
};

// fusa:req REQ-RELAY-021
const std::error_category& error_category() noexcept;
std::error_code            make_error_code(Errc e) noexcept;

// Convenience error constants — compare with == like Go's errors.Is.
inline std::error_code ErrClosed()          noexcept { return make_error_code(Errc::closed); }
inline std::error_code ErrNotConnected()    noexcept { return make_error_code(Errc::not_connected); }
inline std::error_code ErrTimeout()         noexcept { return make_error_code(Errc::timeout); }
inline std::error_code ErrPayloadTooLarge() noexcept { return make_error_code(Errc::payload_too_large); }

// ── Back-pressure ─────────────────────────────────────────────────────────────

// fusa:req REQ-RELAY-015
enum class BackPressurePolicy {
    DropNewest = 0,  // discard the arriving sample when full
    DropOldest = 1,  // evict the oldest buffered sample
    Block      = 2,  // block sender until space is available
};

// ── Subscriber options ────────────────────────────────────────────────────────

// fusa:req REQ-RELAY-016
struct SubscriberConfig {
    int                chan_depth{};                                    // 0 → use impl default
    BackPressurePolicy back_pressure{BackPressurePolicy::DropNewest};
    uint32_t           event_id{};      // SOME/IP event group
    std::string        topic_name;      // DDS topic

    // fusa:req REQ-RELAY-019
    int effective_depth(int default_depth) const noexcept {
        return chan_depth > 0 ? chan_depth : default_depth;
    }
};

// fusa:req REQ-RELAY-017
using SubscriberOption = std::function<void(SubscriberConfig&)>;

// Option factories — REQ-RELAY-017 REQ-RELAY-051 REQ-RELAY-056
// fusa:req REQ-RELAY-051 REQ-RELAY-056
inline SubscriberOption with_channel_depth(int n) {
    return [n](SubscriberConfig& c){ c.chan_depth = n; };
}
inline SubscriberOption with_back_pressure(BackPressurePolicy p) {
    return [p](SubscriberConfig& c){ c.back_pressure = p; };
}
inline SubscriberOption with_event_id(uint32_t id) {
    return [id](SubscriberConfig& c){ c.event_id = id; };
}
inline SubscriberOption with_topic(std::string name) {
    return [n = std::move(name)](SubscriberConfig& c){ c.topic_name = n; };
}

// fusa:req REQ-RELAY-018
inline SubscriberConfig apply_options(const std::vector<SubscriberOption>& opts) {
    SubscriberConfig c;
    for (auto& o : opts) o(c);
    return c;
}

// ── Context (§18.2 relay::Context) ────────────────────────────────────────────

// fusa:req REQ-RELAY-060
class Context {
public:
    static Context background() noexcept { return Context{}; }

    static Context with_deadline(std::chrono::steady_clock::time_point d) noexcept {
        Context c;
        c.deadline_ = d;
        return c;
    }

    static Context with_timeout(std::chrono::steady_clock::duration d) noexcept {
        return with_deadline(std::chrono::steady_clock::now() + d);
    }

    bool done() const noexcept {
        return deadline_.has_value() && std::chrono::steady_clock::now() >= *deadline_;
    }

    std::optional<std::chrono::steady_clock::time_point> deadline() const noexcept {
        return deadline_;
    }

private:
    std::optional<std::chrono::steady_clock::time_point> deadline_;
};

// ── Channel<T> (§18.2 relay::Channel<T>) ──────────────────────────────────────

// can::Chan<T> already implements the relay::Channel<T> surface (push, recv,
// try_recv, close, is_closed); alias it rather than duplicating the
// implementation, per §18.2's "already implemented — alias it" convention.
// fusa:req REQ-RELAY-061
template<typename T>
using Channel = can::Chan<T>;

// ── SubscriberOptions (§18.2 C++ Node/Caller signature) ───────────────────────

// The plain-struct C++ binding shape used by INode::subscribe(), distinct from
// the Go-mirrored functional-options SubscriberConfig/SubscriberOption above
// (§14.1), which remain the standard helpers for protocols that need routing
// keys (EventID/TopicName) not applicable to CAN.
// fusa:req REQ-RELAY-062
struct SubscriberOptions {
    std::size_t        channel_depth{64};
    BackPressurePolicy back_pressure{BackPressurePolicy::DropNewest};
};

// ── INode ─────────────────────────────────────────────────────────────────────

// fusa:req REQ-RELAY-013 REQ-RELAY-063
class INode {
public:
    virtual ~INode() = default;

    // Returns the protocol this node speaks.
    virtual Protocol protocol() const noexcept = 0;

    // Transmits msg. Returns ErrClosed, ErrNotConnected, ErrTimeout, or
    // ErrPayloadTooLarge on failure.
    virtual std::error_code send(Context ctx, const Message& msg) = 0;

    // Returns a channel of inbound messages. The channel is closed when the
    // node closes (REQ-RELAY-013 §6.3).
    virtual std::pair<std::shared_ptr<Channel<Message>>, std::error_code>
        subscribe(SubscriberOptions opts = {}) = 0;

    // Idempotent close (REQ-RELAY-013 §6.1).
    virtual std::error_code close() noexcept = 0;
};

// fusa:req REQ-RELAY-014 REQ-RELAY-063
class ICaller : public INode {
public:
    // Synchronous request/response. Returns ErrTimeout if ctx expires.
    virtual std::pair<Message, std::error_code>
        call(Context ctx, const Message& req) = 0;
};

// ── Optional capability interfaces ───────────────────────────────────────────

// fusa:req REQ-RELAY-023 REQ-RELAY-024 REQ-RELAY-025
enum class HealthStatus : int { OK = 0, Degraded = 1, Down = 2 };

struct Health {
    HealthStatus status{HealthStatus::OK};
    std::string  details;
};

class IHealthProvider {
public:
    virtual ~IHealthProvider() = default;
    virtual Health health() const = 0;
};

// fusa:req REQ-RELAY-026 REQ-RELAY-029
struct Metrics {
    uint64_t write_count{};
    uint64_t deliver_count{};
    uint64_t drop_count{};
    uint64_t bytes_written{};
    uint64_t bytes_delivered{};
    uint64_t error_count{};
};

// fusa:req REQ-RELAY-027
class IMetricsProvider {
public:
    virtual ~IMetricsProvider() = default;
    virtual Metrics metrics() const = 0;
};

// fusa:req REQ-RELAY-028
class IDrainer {
public:
    virtual ~IDrainer() = default;
    virtual std::error_code close_with_drain(std::chrono::milliseconds timeout) = 0;
};

} // namespace relay

// fusa:req REQ-RELAY-022 — allow implicit construction of std::error_code from relay::Errc.
namespace std {
template<>
struct is_error_code_enum<relay::Errc> : true_type {};
} // namespace std
