# Third-party software

The **default build has no third-party dependencies**: Winsock2 and the C++ standard library only. Everything below applies only to the optional `MORDOR_WITH_GNS=ON` build (decision D-012). No third-party binaries are committed: vcpkg builds everything from source, outside the repository.

| Component | Version | License | Why | How it is obtained |
|---|---|---|---|---|
| vcpkg (tool) | commit `65a2c2391af39b773f83f91e0ab6c8ced8f9c727` | MIT | Builds the dependencies from source | `git clone https://github.com/microsoft/vcpkg` into `%USERPROFILE%\vcpkg`, bootstrapped with `-disableMetrics`. Installed with the user's approval on 2026-10-23. |
| GameNetworkingSockets (Valve) | 1.6.1 (vcpkg port), `default-features: false` (no ICE/P2P) | BSD-3-Clause | Reliable and unreliable messages, fragmentation, congestion control, encryption | `vcpkg.json` manifest, pinned by `builtin-baseline` |
| OpenSSL | 3.6.5 | Apache-2.0 | Crypto backend for GNS; since 2026-10-24 also used **directly** by the D-013 DTLS proof test (owner-approved, review #5 FB-5-05) | `vcpkg.json` (direct entry, same version as before); GNS build only |
| protobuf (+ utf8-range), abseil | 7.36.2 (protoc 36.2.0), abseil 20260107.1 | BSD-3-Clause (protobuf), Apache-2.0 (abseil) | GNS's internal wire format | transitive |

Resolved versions are confirmed in EXP-013 (2026-10-23). Building the dependencies from source took about 12 minutes on the dev machine; vcpkg's binary cache makes later configures fast.

## Rules

- **Pinning.** Pin through `builtin-baseline`. Never float versions.
- **Updates.** A dependency update is a decision with its own test run.
- **Redistribution.** Before distributing any binary that links GNS, include the BSD-3 notices of GNS and protobuf and the Apache-2.0 notice of OpenSSL.
- **Valve services.** Use only the standalone open-source API. Never use Steamworks, Steam Datagram Relay or a Valve/WB AppID. Relaying through a Steam AppID is out of scope and would need its own terms review (F-COOP-006).
