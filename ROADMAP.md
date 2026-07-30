# cpp-CAN roadmap

## Interop testing — beyond unit tests against `can::virt::Bus`

Every existing test in `tests/` runs against `can::virt::Bus`, an in-process
broadcast bus with zero OS dependencies. That is the right default for
day-to-day development (fast, deterministic, portable to every OS in the
`build-and-test` CI matrix), but it proves nothing about whether cpp-CAN's
real transport — `can::socketcan::Bus` (Linux SocketCAN, hardware or `vcan`)
— actually decodes and encodes genuine kernel CAN traffic correctly. A bug
in `can_frame`/`canfd_frame` byte layout, ID flag masking, or the
CAN_RAW_FD_FRAMES handshake would be invisible to `can::virt::Bus`-only
tests and only show up against a real interface.

Unlike a DDS/RTPS implementation (which needs a second, independently
maintained protocol stack — e.g. rust-DDS's live CycloneDDS peer — as a
"did we and the peer share the same misreading of the spec" check), CAN has
no equivalent third-party *implementation* to test against: the wire format
is a kernel driver interface (SocketCAN), not a negotiated application
protocol. The real independent oracle here is the Linux kernel's own SocketCAN
subsystem itself, plus `can-utils` (`candump`/`cangen`/`cansend`) — an
entirely separate, upstream-maintained codebase
(github.com/linux-can/can-utils) from cpp-CAN.

**Done — both deliverables implemented and passing where they can run**, landed
as a new `can-interop` CI job, **but currently unverified in hosted CI**: every
`can-interop` run to date, including the run that produced the v0.2.0 release,
has taken the probe-and-skip path, not the live path — GitHub-hosted
`ubuntu-22.04` runners' kernel (`6.8.0-1062-azure` as of this writing) does not
ship the `vcan` module at all (`modprobe: FATAL: Module vcan not found`), so
`sudo modprobe vcan` fails immediately on every run. This is a hosted-runner
kernel limitation, not a permissions or setup-script problem — a container
can't work around it either, since containers share the host kernel. The job
now emits a loud `::warning::` annotation and job-summary line (not a silent
`::notice::`) whenever it takes the skip path, so this is visible without
digging into logs. Until a self-hosted runner (or a hosted image that ships
`vcan`) is available, "Done" means: the live-path code is implemented, builds,
and is exercised locally by any contributor with `vcan` support, and the
skip-path is verified in hosted CI on every run — the live path itself has
**not** been exercised in hosted CI (tracked: #30).
(`.github/workflows/ci.yml`), gated behind `-DCPPCAN_INTEROP_TESTS=ON`
(Linux only — see `interop/CMakeLists.txt`) so it is absent from the
default cross-platform `build-and-test` matrix:

- **Live two-process self-interop** (`interop/test_two_process_interop.cpp`)
  — two real, independent OS processes of `cpp-can-interop-peer`
  (`interop/can_interop_peer.cpp`, a `[[bin]]`-equivalent test-support
  target, not part of the public library API), each driven entirely by the
  real, production `can::socketcan::Bus` machinery (real `CAN_RAW` socket,
  real bind to `vcan0`, real background reader thread — no test-only
  shortcuts). One process sends real classic-CAN and CAN-FD (with BRS)
  frames via the RELAY-conformant `can::IBus` API; the other receives and
  the test asserts field-exact correctness (ID, DLC/length, data bytes, FD
  and BRS flags) between what the writer process actually wrote to its own
  socket and what the reader process actually decoded off of its own real
  vcan0 socket — genuine kernel-level CAN traffic crossing a process
  boundary, not two `can::virt::Bus` handles inside one test binary.
- **Third-party-peer interop against `can-utils`**
  (`interop/test_cangen_candump_interop.cpp`) — Linux's own SocketCAN kernel
  subsystem plus `can-utils` as the independent validator, in both
  directions: `cangen` (fixed `-I`/`-L`/`-D` args, so its output is fully
  deterministic) injects frames onto `vcan0` that `can::socketcan::Bus`'s
  receive path must decode field-exact against exactly what `cangen` was
  told to send; and `can::socketcan::Bus::send()` transmits a known frame
  that `candump -L vcan0` — a wholly separate process and codebase —
  captures, with the test asserting the captured log line's ID/data hex
  matches the expected CAN wire encoding byte-for-byte.

CI posture (`.github/workflows/ci.yml`'s `can-interop` job): loads the
`vcan` kernel module, brings up a real `vcan0` interface, and installs
`can-utils`, each step probed rather than assumed — if any of that setup
fails (e.g. a sandboxed runner lacking the `vcan` module), the job emits an
`::notice::` and exits 0 (skips cleanly) instead of hard-failing, the same
probe-then-skip posture as rust-DDS's `cyclone-interop` job uses for its own
environment-dependent live-peer dependency. Once setup succeeds, the build
and test steps are expected to pass unconditionally (unlike a third-party
Docker image that might simply not be pullable, `can-utils` is an
`apt-get install` away and does not carry that same flakiness risk).

### Not yet in scope

- **CAN XL over SocketCAN** — `can::socketcan::Bus` rejects `Frame{xl=true}`
  at `send()`. `CAN_RAW` support for `CANXL_XLF` is a very recent kernel
  feature not guaranteed present on common CI/production kernels, and
  go-CAN's own `socketcan` package (the reference `can::socketcan::Bus`
  mirrors) does not support it either. Classic CAN and CAN FD are fully
  supported and covered by the interop suite above.
- **Hardware-in-the-loop testing** — the interop suite above runs entirely
  against `vcan` (virtual CAN), matching CI's constraints. A real CAN
  transceiver/hardware run is out of scope for this repo's CI and would need
  a self-hosted runner with attached hardware.
