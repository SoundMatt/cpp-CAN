# cpp-CAN

A C++17 library for [CAN bus](https://en.wikipedia.org/wiki/CAN_bus) communication.
RELAY-conformant — the `can::IBus` interface is stable; transports are swappable without changing application code.

[![CI](https://github.com/SoundMatt/cpp-CAN/actions/workflows/ci.yml/badge.svg)](https://github.com/SoundMatt/cpp-CAN/actions/workflows/ci.yml)

## Packages

| Header | Description | Dependencies |
|--------|-------------|--------------|
| `can/can.hpp` | Core `IBus` interface, `Frame`, `Filter`, validation, and `adapt(bus)` — wraps `IBus` as a `relay::INode` | `can/relay.hpp` |
| `can/relay.hpp` | Local bundled RELAY core types (§13.7.3): `Protocol`, `Message`, `INode`/`ICaller`, `Context`, `Channel<T>` | Nothing |
| `can/virtual/bus.hpp` | In-process broadcast bus — zero OS deps, default for testing | `can/can.hpp` |
| `can/socketcan/bus.hpp` | Linux SocketCAN transport — hardware CAN or `vcan` interfaces, classic + FD | `can/can.hpp` (Linux only — see [Build](#build)) |
| `can/isotp/transport.hpp` | ISO 15765-2 (ISO-TP) multi-frame transport | `can/can.hpp` |
| `can/j1939/pgn.hpp` | SAE J1939 PGN decode/encode, extended-ID bus | `can/can.hpp` |
| `can/safety/e2e.hpp` | E2E protection header — DataID, SourceID, SeqCounter, CRC-16 | Nothing |
| `can/dbc/parser.hpp` | DBC file parser and signal decoder | Nothing |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Requires CMake ≥ 3.21, a C++17-compliant compiler, and Ninja (CI and the Dockerfile build with `-G Ninja`). Dependencies are fetched automatically via CMake FetchContent (Catch2, nlohmann\_json).

On Linux, `can/socketcan/bus.hpp` (hardware CAN / `vcan`) is always compiled into the library — no extra flag needed, no `vcan`/`can-utils` required just to *build*. The live interop test suite that actually exercises it against a real `vcan0` interface is opt-in — see [Interop testing](#interop-testing).

## Quick start

```cpp
#include <can/can.hpp>
#include <can/virtual/bus.hpp>

auto bus = can::virt::Bus::create();

auto [ch, err] = bus->subscribe({});
bus->send(can::Frame{.id = 0x100, .data = {0xDE, 0xAD, 0xBE, 0xEF}});

if (auto frame = ch->recv()) {
    printf("%03X#%X\n", frame->id, frame->data[0]);
}
bus->close();
```

## Switching transports

```cpp
// Development / testing — zero dependencies:
#include <can/virtual/bus.hpp>
auto bus = can::virt::Bus::create();

// Linux SocketCAN — hardware or vcan0:
#include <can/socketcan/bus.hpp>
auto [bus, err] = can::socketcan::Bus::create("vcan0"); // or "can0" for real hardware
```

## ISO-TP

```cpp
#include <can/isotp/transport.hpp>

auto conn = can::isotp::Conn::create(bus, {.tx_id = 0x7E0, .rx_id = 0x7E8});
conn->send(payload);  // up to 4095 bytes
auto data = conn->recv(std::chrono::milliseconds(500));
```

## J1939

```cpp
#include <can/j1939/pgn.hpp>

auto j_bus = can::j1939::Bus::create(bus, /*src=*/0x00);
auto [ch, err] = j_bus->subscribe(can::j1939::PGN{0x0FECA});
j_bus->send(can::j1939::Frame{.priority = 6, .pgn = 0x0FECA, .data = payload});
```

## Safety E2E

```cpp
#include <can/safety/e2e.hpp>

can::safety::Protector protector{{.data_id = 0x0001, .source_id = 0x0010}};
can::safety::Receiver  receiver {{.data_id = 0x0001, .source_id = 0x0010}};

auto protected_payload = protector.protect(raw);
auto [payload, err]    = receiver.unwrap(protected_payload);
```

## Interop testing

`can::socketcan::Bus` is validated against genuine kernel CAN traffic, not just its own unit tests — see `ROADMAP.md`'s "Interop testing" section and the `can-interop` CI job (`.github/workflows/ci.yml`):

- **Two-process self-interop** (`interop/test_two_process_interop.cpp`) — two real `cpp-can-interop-peer` OS processes (`interop/can_interop_peer.cpp`) bound to the same real `vcan0` interface, one sending real CAN/CAN-FD frames via `can::IBus`, the other receiving and verifying field-exact correctness (ID, DLC, data, FD/BRS flags).
- **Third-party-peer interop** (`interop/test_cangen_candump_interop.cpp`) — Linux's own `can-utils` (`cangen`/`candump`), an entirely independent codebase, as the oracle: `cangen`-injected frames decoded field-exact by `can::socketcan::Bus`, and frames sent via `can::socketcan::Bus` captured byte-exact by `candump -L`.

Both are opt-in (`-DCPPCAN_INTEROP_TESTS=ON`, Linux only) and require a real `vcan0` interface:

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
sudo apt-get install -y can-utils

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCPPCAN_INTEROP_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -L can-interop
```

## Philosophy

- **Interface-first** — one stable `can::IBus`; transports are swappable.
- **RELAY-conformant** — bridges to DDS, MQTT, SOME/IP, LIN, RCP via `adapt()`.
- **Testable by default** — `can::virt::Bus` needs no OS support; tests run everywhere.
- **C++17** — no external runtime dependencies beyond the STL.

## License

Mozilla Public License v2.0. Copyright (c) 2026 Matt Jones.
