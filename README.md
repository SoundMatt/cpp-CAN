# cpp-CAN

A C++17 library for [CAN bus](https://en.wikipedia.org/wiki/CAN_bus) communication.
RELAY-conformant — the `can::IBus` interface is stable; transports are swappable without changing application code.

[![CI](https://github.com/SoundMatt/cpp-CAN/actions/workflows/ci.yml/badge.svg)](https://github.com/SoundMatt/cpp-CAN/actions/workflows/ci.yml)

## Packages

| Header | Description | Dependencies |
|--------|-------------|--------------|
| `can/can.hpp` | Core `IBus` interface, `Frame`, `Filter`, validation | Nothing |
| `can/virtual/bus.hpp` | In-process broadcast bus — zero OS deps, default for testing | `can/can.hpp` |
| `can/relay_adapter.hpp` | `adapt(bus)` — wraps `IBus` as a `relay::INode` | `can/relay.hpp` |
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

Requires CMake ≥ 3.21 and a C++17-compliant compiler. Dependencies are fetched automatically via CMake FetchContent (Catch2, nlohmann\_json).

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
// (future: can/socketcan/bus.hpp)
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

## Philosophy

- **Interface-first** — one stable `can::IBus`; transports are swappable.
- **RELAY-conformant** — bridges to DDS, MQTT, SOME/IP, LIN, RCP via `adapt()`.
- **Testable by default** — `can::virt::Bus` needs no OS support; tests run everywhere.
- **C++17** — no external runtime dependencies beyond the STL.

## License

Mozilla Public License v2.0. Copyright (c) 2026 Matt Jones.
