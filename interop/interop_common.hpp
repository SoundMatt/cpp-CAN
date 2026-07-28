// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// interop/interop_common.hpp — shared types/helpers for the live SocketCAN
// interop test infrastructure (ROADMAP.md "Interop testing"):
//
//   - FrameRecord: a field-exact, comparable snapshot of a can::Frame (id,
//     ext, rtr, fd, brs, esi, data) independent of any particular Bus.
//   - to_json_line() / parse_frame_json_line(): a tiny NDJSON encoding for
//     FrameRecord, reused by both `can_interop_peer` (which prints one line
//     per frame it sent/received, plus a final summary line) and the Catch2
//     interop tests (which parse the peer's stdout). Consistent with this
//     crate's own `send --format json` NDJSON convention (see
//     cli/json.hpp's module comment, RELAY spec Sec11.2).
//   - hex_encode()/hex_decode(): plain uppercase hex for frame payloads —
//     deliberately not base64 (unlike cli/json.hpp's relay.Message
//     payloads) so a data_hex field can be eyeballed and diffed directly
//     against candump's own hex-dump wire format in test failure output.
//
// Not part of the public library API — build-and-test-only support code,
// analogous to rust-DDS's src/bin/rtps_interop_peer.rs.

#pragma once

#include <can/can.hpp>
#include "json.hpp" // cli/json.hpp — reuses its detail:: field extractors

#include <array>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace caninterop {

// Joins `t` in the destructor if it is still joinable — a guard against
// std::thread's own destructor calling std::terminate() when a REQUIRE/
// FAIL exception unwinds past a joinable-but-not-yet-joined background
// thread (e.g. a reader/candump/cangen helper spawned earlier in the same
// test case). Cheap belt-and-suspenders: the happy path already joins
// explicitly before any assertion that could throw; this only matters on
// the failure path.
struct ThreadJoinGuard {
    std::thread& t;
    explicit ThreadJoinGuard(std::thread& thread) : t(thread) {}
    ~ThreadJoinGuard() { if (t.joinable()) t.join(); }
    ThreadJoinGuard(const ThreadJoinGuard&)            = delete;
    ThreadJoinGuard& operator=(const ThreadJoinGuard&) = delete;
};

// ── exec_capture ─────────────────────────────────────────────────────────

// Result of exec_capture(): combined stdout+stderr, the child's exit code
// (only meaningful if !timed_out), and whether it was killed for exceeding
// the timeout.
struct ExecResult {
    std::string output;
    int         exit_code{-1};
    bool        timed_out{false};
};

// Runs argv[0] with argv[1:] as arguments and captures combined
// stdout+stderr. Unlike popen()/system(), this NEVER invokes a shell —
// argv[] is passed directly to execvp(), so shell metacharacters in any
// argument have no special meaning and command injection (CWE-78) is
// structurally impossible regardless of what the arguments contain. Used
// by the interop tests to run cangen/candump/can_interop_peer.
//
// If `timeout` is non-zero and the child hasn't exited by then, it is
// SIGKILLed and `timed_out` is set — a self-contained replacement for
// shelling out to the external `timeout` coreutils binary.
inline ExecResult exec_capture(const std::vector<std::string>& argv,
                                std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
    if (argv.empty()) throw std::invalid_argument("exec_capture: argv must not be empty");

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        throw std::runtime_error(std::string("exec_capture: pipe() failed: ") + std::strerror(errno));
    }

    pid_t pid = fork();
    if (pid < 0) {
        int err = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        throw std::runtime_error(std::string("exec_capture: fork() failed: ") + std::strerror(err));
    }

    if (pid == 0) {
        // Child: fold stdout+stderr into the pipe's write end, then exec —
        // no shell is ever invoked.
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);

        execvp(cargv[0], cargv.data());
        // execvp() only returns on failure.
        std::fprintf(stderr, "exec_capture: execvp failed for '%s': %s\n", cargv[0], std::strerror(errno));
        _exit(127);
    }

    // Parent: read the pipe non-blockingly so we can also watch the clock
    // and poll the child's exit status without a second thread.
    close(pipefd[1]);
    if (fcntl(pipefd[0], F_SETFL, O_NONBLOCK) != 0) {
        int err = errno;
        close(pipefd[0]);
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        throw std::runtime_error(std::string("exec_capture: fcntl() failed: ") + std::strerror(err));
    }

    ExecResult result;
    std::array<char, 4096> buf{};
    const auto deadline = timeout.count() > 0
                               ? std::chrono::steady_clock::now() + timeout
                               : std::chrono::steady_clock::time_point::max();
    bool  child_reaped = false;
    int   status        = 0;

    for (;;) {
        ssize_t n = read(pipefd[0], buf.data(), buf.size());
        if (n > 0) {
            result.output.append(buf.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) break; // EOF: child closed the pipe (it has exited).
        if (errno != EAGAIN && errno != EWOULDBLOCK) break; // Real read error — stop.

        // No data available right now: see if the child is already gone.
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            child_reaped = true;
            // Drain whatever's left in the pipe without blocking, then stop.
            while ((n = read(pipefd[0], buf.data(), buf.size())) > 0) {
                result.output.append(buf.data(), static_cast<std::size_t>(n));
            }
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            child_reaped     = true;
            result.timed_out = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    close(pipefd[0]);
    if (!child_reaped) waitpid(pid, &status, 0);

    if (!result.timed_out) {
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return result;
}

// ── hex ───────────────────────────────────────────────────────────────────

inline std::string hex_encode(const std::vector<uint8_t>& data) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out += kDigits[(b >> 4) & 0xF];
        out += kDigits[b & 0xF];
    }
    return out;
}

inline std::vector<uint8_t> hex_decode(const std::string& s) {
    std::vector<uint8_t> out;
    out.reserve(s.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
        int hi = nibble(s[i]);
        int lo = nibble(s[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

// ── FrameRecord ───────────────────────────────────────────────────────────

// A field-exact, Bus-independent snapshot of a can::Frame.
struct FrameRecord {
    uint32_t             id{};
    bool                  ext{};
    bool                  rtr{};
    bool                  fd{};
    bool                  brs{};
    bool                  esi{};
    std::vector<uint8_t>  data;

    friend bool operator==(const FrameRecord& a, const FrameRecord& b) {
        return a.id == b.id && a.ext == b.ext && a.rtr == b.rtr &&
               a.fd == b.fd && a.brs == b.brs && a.esi == b.esi &&
               a.data == b.data;
    }
    friend bool operator!=(const FrameRecord& a, const FrameRecord& b) { return !(a == b); }

    static FrameRecord from_frame(const can::Frame& f) {
        return FrameRecord{f.id, f.ext, f.rtr, f.fd, f.brs, f.esi, f.data};
    }

    can::Frame to_frame() const {
        can::Frame f{};
        f.id = id; f.ext = ext; f.rtr = rtr; f.fd = fd; f.brs = brs; f.esi = esi;
        f.data = data;
        return f;
    }
};

// One NDJSON line: {"kind":"frame","id":291,"ext":false,"rtr":false,"fd":false,"brs":false,"esi":false,"data_hex":"0102"}
inline std::string to_json_line(const FrameRecord& f) {
    std::ostringstream o;
    o << "{\"kind\":\"frame\""
      << ",\"id\":" << f.id
      << ",\"ext\":" << (f.ext ? "true" : "false")
      << ",\"rtr\":" << (f.rtr ? "true" : "false")
      << ",\"fd\":"  << (f.fd  ? "true" : "false")
      << ",\"brs\":" << (f.brs ? "true" : "false")
      << ",\"esi\":" << (f.esi ? "true" : "false")
      << ",\"data_hex\":\"" << hex_encode(f.data) << "\""
      << "}";
    return o.str();
}

// Parses one `to_json_line()`-shaped line. Returns false (leaving `out`
// untouched) if `line` does not look like a "kind":"frame" record.
inline bool parse_frame_json_line(const std::string& line, FrameRecord& out) {
    std::string kind;
    if (!cli::detail::extract_string(line, "kind", kind) || kind != "frame") return false;

    FrameRecord f{};
    if (!cli::detail::extract_u32(line, "id", f.id)) return false;
    cli::detail::extract_bool(line, "ext", f.ext);
    cli::detail::extract_bool(line, "rtr", f.rtr);
    cli::detail::extract_bool(line, "fd",  f.fd);
    cli::detail::extract_bool(line, "brs", f.brs);
    cli::detail::extract_bool(line, "esi", f.esi);
    std::string data_hex;
    if (cli::detail::extract_string(line, "data_hex", data_hex)) {
        f.data = hex_decode(data_hex);
    }
    out = f;
    return true;
}

// The final summary line `can_interop_peer` prints before exiting:
// {"kind":"summary","role":"writer"|"reader","ok":bool,"iface":"...",
//  "count":N,"error":"..."|null}
struct Summary {
    std::string role;
    bool        ok{false};
    std::string iface;
    uint32_t    count{0};
    std::string error; // empty when absent
};

inline std::string to_json_line(const Summary& s) {
    std::ostringstream o;
    o << "{\"kind\":\"summary\""
      << ",\"role\":\"" << s.role << "\""
      << ",\"ok\":" << (s.ok ? "true" : "false")
      << ",\"iface\":\"" << s.iface << "\""
      << ",\"count\":" << s.count
      << ",\"error\":" << (s.error.empty() ? std::string("null") : ("\"" + s.error + "\""))
      << "}";
    return o.str();
}

inline bool parse_summary_json_line(const std::string& line, Summary& out) {
    std::string kind;
    if (!cli::detail::extract_string(line, "kind", kind) || kind != "summary") return false;
    Summary s{};
    cli::detail::extract_string(line, "role", s.role);
    cli::detail::extract_bool(line, "ok", s.ok);
    cli::detail::extract_string(line, "iface", s.iface);
    cli::detail::extract_u32(line, "count", s.count);
    cli::detail::extract_string(line, "error", s.error);
    out = s;
    return true;
}

} // namespace caninterop
