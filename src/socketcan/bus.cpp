// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <can/socketcan/bus.hpp>

#include <cerrno>
#include <cstring>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace can::socketcan {

namespace {

// How often the reader thread's blocking read() wakes up to re-check
// stop_ — bounds close()'s worst-case join latency without needing a
// shutdown()-based unblock (which is not reliably supported for AF_CAN
// sockets across kernel versions).
constexpr int kReadTimeoutUsec = 200'000; // 200ms

bool matches_any(const std::vector<Filter>& filters, const Frame& f) {
    if (filters.empty()) return true;
    for (const auto& fl : filters)
        if (fl.matches(f)) return true;
    return false;
}

} // anonymous namespace

// fusa:req REQ-SCAN-001 REQ-SCAN-002
std::pair<std::shared_ptr<Bus>, std::error_code> Bus::create(const std::string& iface) {
    int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        return {std::shared_ptr<Bus>{}, relay::ErrNotConnected()};
    }

    // Enable CAN FD frame send/receive — best effort, gracefully ignored on
    // classic-only interfaces (matches go-CAN's New()).
    int fd_enable = 1;
    (void)::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &fd_enable, sizeof(fd_enable));

    // Bound receive timeout so the reader thread can periodically observe
    // stop_ and exit cleanly on close() rather than blocking forever.
    struct timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = kReadTimeoutUsec;
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        ::close(fd);
        return {std::shared_ptr<Bus>{}, relay::ErrNotConnected()};
    }

    struct sockaddr_can addr{};
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return {std::shared_ptr<Bus>{}, relay::ErrNotConnected()};
    }

    auto bus = std::shared_ptr<Bus>(new Bus());
    bus->fd_    = fd;
    bus->iface_ = iface;
    bus->reader_ = std::thread(&Bus::read_loop, bus.get());
    return {bus, std::error_code{}};
}

Bus::~Bus() {
    (void)close();
}

// fusa:req REQ-SCAN-003 REQ-SCAN-004
std::error_code Bus::send(Frame frame) {
    try {
        validate_frame(frame);
    } catch (...) {
        return relay::ErrPayloadTooLarge();
    }
    if (frame.xl) {
        // Not yet supported by this transport — see bus.hpp's module comment.
        return relay::ErrPayloadTooLarge();
    }

    {
        std::shared_lock<std::shared_mutex> lk(mu_);
        if (closed_) return relay::ErrClosed();
    }

    canid_t id = frame.id;
    if (frame.ext) id |= CAN_EFF_FLAG;
    if (frame.rtr) id |= CAN_RTR_FLAG;

    ssize_t n;
    if (frame.fd) {
        struct canfd_frame cf{};
        cf.can_id = id;
        cf.len    = static_cast<uint8_t>(frame.data.size());
        if (frame.brs) cf.flags = static_cast<uint8_t>(cf.flags | CANFD_BRS);
        if (frame.esi) cf.flags = static_cast<uint8_t>(cf.flags | CANFD_ESI);
        if (!frame.data.empty()) {
            std::memcpy(cf.data, frame.data.data(), frame.data.size());
        }
        n = ::write(fd_, &cf, sizeof(cf));
    } else {
        struct can_frame cf{};
        cf.can_id  = id;
        cf.can_dlc = static_cast<uint8_t>(frame.data.size());
        if (!frame.data.empty()) {
            std::memcpy(cf.data, frame.data.data(), frame.data.size());
        }
        n = ::write(fd_, &cf, sizeof(cf));
    }
    if (n < 0) {
        return relay::ErrNotConnected();
    }
    return {};
}

// fusa:req REQ-SCAN-005
std::pair<std::shared_ptr<Chan<Frame>>, std::error_code>
Bus::subscribe(std::vector<Filter> filters, std::vector<relay::SubscriberOption> opts) {
    relay::SubscriberConfig cfg = relay::apply_options(opts);
    int depth = cfg.effective_depth(64);

    std::unique_lock<std::shared_mutex> lk(mu_);
    if (closed_) return {std::shared_ptr<Chan<Frame>>{}, relay::ErrClosed()};

    auto ch = std::make_shared<Chan<Frame>>(static_cast<std::size_t>(depth));
    subs_.push_back({std::move(filters), ch});
    return {ch, std::error_code{}};
}

// fusa:req REQ-SCAN-006
std::error_code Bus::close() {
    {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return {};
        closed_ = true;
    }

    stop_.store(true, std::memory_order_relaxed);
    if (reader_.joinable()) reader_.join();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }

    std::unique_lock<std::shared_mutex> lk(mu_);
    for (auto& s : subs_) s.ch->close();
    subs_.clear();
    return {};
}

// fusa:req REQ-SCAN-005 REQ-SCAN-007 REQ-SCAN-008
void Bus::read_loop() {
    std::vector<uint8_t> buf(sizeof(struct canfd_frame));

    while (!stop_.load(std::memory_order_relaxed)) {
        ssize_t n = ::read(fd_, buf.data(), buf.size());
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            return; // socket closed out from under us or a fatal I/O error
        }

        Frame f{};
        if (static_cast<std::size_t>(n) == sizeof(struct can_frame)) {
            struct can_frame cf{};
            std::memcpy(&cf, buf.data(), sizeof(cf));
            canid_t id = cf.can_id;
            f.ext = (id & CAN_EFF_FLAG) != 0;
            f.rtr = (id & CAN_RTR_FLAG) != 0;
            id &= f.ext ? static_cast<canid_t>(CAN_EFF_MASK) : static_cast<canid_t>(CAN_SFF_MASK);
            f.id = id;
            uint8_t dlc = cf.can_dlc;
            if (dlc > kCANMaxDataLen) dlc = static_cast<uint8_t>(kCANMaxDataLen);
            f.data.assign(cf.data, cf.data + dlc);
        } else if (static_cast<std::size_t>(n) == sizeof(struct canfd_frame)) {
            struct canfd_frame cf{};
            std::memcpy(&cf, buf.data(), sizeof(cf));
            canid_t id = cf.can_id;
            f.ext = (id & CAN_EFF_FLAG) != 0;
            id &= f.ext ? static_cast<canid_t>(CAN_EFF_MASK) : static_cast<canid_t>(CAN_SFF_MASK);
            f.id  = id;
            f.fd  = true;
            f.brs = (cf.flags & CANFD_BRS) != 0;
            f.esi = (cf.flags & CANFD_ESI) != 0;
            uint8_t len = cf.len;
            if (len > kCANFDMaxDataLen) len = static_cast<uint8_t>(kCANFDMaxDataLen);
            f.data.assign(cf.data, cf.data + len);
        } else {
            continue; // unexpected size (e.g. an error frame) — skip
        }

        std::shared_lock<std::shared_mutex> lk(mu_);
        for (auto& s : subs_) {
            if (matches_any(s.filters, f)) s.ch->try_send(f);
        }
    }
}

} // namespace can::socketcan
