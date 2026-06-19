// Copyright (c) 2026 Matt Jones. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// cpp-can-cli — RELAY-conformant CLI for cpp-CAN.
//
// Subcommands (RELAY spec §11.1 / §11.2):
//   version        — print spec + implementation version as JSON
//   capabilities   — print supported protocols and commands as JSON
//   status         — print health status as JSON
//   convert        — read can.Frame JSON from stdin, write relay.Message JSON to stdout
//
// REQ-CLI-001 through REQ-CLI-006

#include "json.hpp"
#include <can/can.hpp>
#include <iostream>
#include <string>

static int cmd_version()      { std::cout << cli::version_json()      << "\n"; return 0; }
static int cmd_capabilities() { std::cout << cli::capabilities_json() << "\n"; return 0; }
static int cmd_status()       { std::cout << cli::status_json()        << "\n"; return 0; }

// fusa:req REQ-CLI-006
static int cmd_convert(const std::string& protocol) {
    if (protocol != "CAN") {
        std::cerr << "ErrUnsupportedProtocol\n";
        return 1;
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
    msg.version   = {0, 2, 0};
    msg.timestamp = {};
    msg.seq       = 0;

    std::cout << cli::message_to_json(msg) << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: cpp-can-cli <command> [options]\n";
        std::cerr << "commands: version capabilities status convert\n";
        return 2;
    }

    std::string cmd = argv[1];

    if (cmd == "version")      return cmd_version();
    if (cmd == "capabilities") return cmd_capabilities();
    if (cmd == "status")       return cmd_status();

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
