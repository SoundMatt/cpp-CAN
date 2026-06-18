# Security Policy

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| main    | :white_check_mark: |
| < v0.1  | :x:                |

## Reporting a Vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Report vulnerabilities privately by emailing **matt@jellybaby.com** with the subject
line `[cpp-CAN SECURITY] <short description>`.

Include:
- A description of the vulnerability and its potential impact
- Steps to reproduce (proof of concept if available)
- Affected versions / configurations
- Any suggested mitigations

We will acknowledge receipt within **2 business days** and aim to provide a fix or
mitigation within **14 calendar days** for critical issues, or **90 days** for lower
severity findings.

## Security Requirements

cpp-CAN targets deployment in safety-critical environments (automotive ASIL-B,
IEC 61508 SIL 2). The security posture reflects these requirements:

- **Memory safety**: no raw `new`/`delete`; RAII throughout; bounds-checked containers
- **Thread safety**: all shared state protected by `std::shared_mutex` or atomics
- **No undefined behaviour**: strict C++17 with `-Wall -Wextra -Wpedantic -Werror`
- **No dynamic code loading**: no `dlopen`, no JIT, no eval
- **Supply-chain**: dependencies pinned by hash in FetchContent; SBOMs generated in CI
- **Secrets**: no credentials, keys, or tokens in source or build artifacts

## Security Hardening

Integrators are advised to:

- Compile with `-D_FORTIFY_SOURCE=2 -fstack-protector-strong`
- Enable ASLR and NX on the target platform
- Restrict CAN socket permissions (CAP_NET_RAW) to the bus daemon only
- Apply allowlist filters on `IBus::subscribe` to prevent message spoofing
- Validate all incoming CAN frames with `can::validate_frame` at the transport boundary

## Known Limitations

- **Availability**: the virtual bus does not enforce rate limiting; a misbehaving
  publisher can saturate subscriber channels (use `BackPressurePolicy::DropOldest`
  or `Block` and add rate-limiting at the application layer).
- **Authentication**: CAN is an unauthenticated bus; use the `can/safety` E2E
  protection layer and/or a hardware security module for message authentication.
