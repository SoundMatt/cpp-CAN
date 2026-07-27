// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// cpp-can-cli — RELAY-conformant CLI for cpp-CAN.
//
// Subcommands (RELAY spec §11.1 / §11.2):
//   version        [--format text|json] — print spec + implementation version
//   capabilities   — print supported protocols and commands as JSON
//   status         [--format text|json] — print health status
//   convert        — read can.Frame JSON from stdin, write relay.Message JSON to stdout
//   send --format json — read relay.Message NDJSON from stdin, publish each (§11.2)
//
// REQ-CLI-001 through REQ-CLI-008

#include "json.hpp"
#include <can/can.hpp>
#include <can/virtual/bus.hpp>
#include <iostream>
#include <string>
#include <vector>

// fusa:req REQ-CLI-007
static int cmd_version(const std::string& format) {
    if (format == "text") {
        std::cout << cli::version_text() << "\n";
    } else {
        std::cout << cli::version_json() << "\n";
    }
    return 0;
}

static int cmd_capabilities() { std::cout << cli::capabilities_json() << "\n"; return 0; }

// fusa:req REQ-CLI-007
static int cmd_status(const std::string& format) {
    if (format == "text") {
        std::cout << cli::status_text() << "\n";
    } else {
        std::cout << cli::status_json() << "\n";
    }
    return 0;
}

// Parses a leading --format <value> flag (defaulting to "json") from the
// remaining argv for a command that accepts it. Returns false and prints an
// error if any other flag is given or the value is not text/json.
// fusa:req REQ-CLI-007
static bool parse_output_format(int argc, char* argv[], int start,
                                 const std::string& cmd, std::string& format) {
    format = "json";
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--format" && i + 1 < argc) {
            format = argv[++i];
        } else {
            std::cerr << "ErrUnknownFlag: " << arg << " (usage: " << cmd << " [--format text|json])\n";
            return false;
        }
    }
    if (!cli::is_valid_output_format(format)) {
        std::cerr << "ErrUnsupportedFormat: " << format << "\n";
        return false;
    }
    return true;
}

// fusa:req REQ-CLI-008
static int cmd_send(const std::vector<std::string>& args) {
    bool json_stream = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--format" && i + 1 < args.size()) {
            const std::string& fmt = args[++i];
            if (fmt != "json") {
                std::cerr << "ErrUnsupportedFormat: " << fmt << "\n";
                return 2;
            }
            json_stream = true;
            continue;
        }
        std::cerr << "ErrUnsupportedArg: " << args[i]
                   << " (send currently only supports the streaming NDJSON "
                      "sink '--format json', RELAY spec §11.2; protocol-flag "
                      "sending is not yet implemented)\n";
        return 2;
    }
    if (!json_stream) {
        std::cerr << "ErrMissingArg: send requires --format json\n";
        return 2;
    }

    // No live transport (e.g. SocketCAN) is wired into the CLI yet — publish
    // onto a fresh in-process virtual bus so the parse/convert/validate/
    // publish pipeline is exercised end-to-end via the same IBus interface a
    // real transport will use once available.
    auto bus = can::virt::Bus::create();
    auto result = cli::send_json_stream(std::cin, std::cerr, bus);
    bus->close();
    return result.exit_code;
}

// fusa:req REQ-CLI-006
static int cmd_convert(const std::string& protocol) {
    if (protocol != "CAN") {
        std::cerr << "ErrUnsupportedProtocol\n";
        return 2;
    }

    std::string input;
    std::string line;
    while (std::getline(std::cin, line)) input += line;

    can::Frame f;
    try {
        f = cli::parse_frame_json(input);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    try {
        can::validate_frame(f);
    } catch (const can::ErrInvalidFrame& e) {
        std::cerr << "ErrInvalidInput: " << e.what() << "\n";
        return 1;
    }

    auto msg      = can::to_message(f);
    msg.timestamp = {};

    std::cout << cli::message_to_json(msg) << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: cpp-can-cli <command> [options]\n";
        std::cerr << "commands: version capabilities status convert send\n";
        return 2;
    }

    std::string cmd = argv[1];

    if (cmd == "version") {
        std::string format;
        if (!parse_output_format(argc, argv, 2, "version", format)) return 2;
        return cmd_version(format);
    }
    if (cmd == "capabilities") return cmd_capabilities();
    if (cmd == "status") {
        std::string format;
        if (!parse_output_format(argc, argv, 2, "status", format)) return 2;
        return cmd_status(format);
    }

    if (cmd == "send") {
        std::vector<std::string> args(argv + 2, argv + argc);
        return cmd_send(args);
    }

    if (cmd == "convert") {
        std::string protocol;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--protocol" && i + 1 < argc) {
                protocol = argv[++i];
            } else if (arg == "--format" && i + 1 < argc) {
                std::string fmt = argv[++i];
                if (fmt != "json") {
                    std::cerr << "ErrUnsupportedFormat: " << fmt << "\n";
                    return 2;
                }
            }
        }
        if (protocol.empty()) {
            std::cerr << "ErrMissingArg: --protocol required\n";
            return 2;
        }
        return cmd_convert(protocol);
    }

    std::cerr << "ErrUnknownCommand: " << cmd << "\n";
    return 2;
}
