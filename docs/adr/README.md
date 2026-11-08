<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Architecture decision records

Every decision follows the same outline: context, decision, rejected alternatives and consequences. The architecture and requirements document (written in Spanish) is the reference; these files travel with the code.

| Decision | Title | Status |
|---|---|---|
| [ADR-01](0001.md) | Shared C++20 rules and protocol core | Accepted |
| [ADR-02](0002.md) | Native user interfaces on top of the C++ core | Accepted |
| [ADR-03](0003.md) | Server as a modular C++20 monolith with Drogon and PostgreSQL | Accepted |
| [ADR-04](0004.md) | Server-authoritative match over WebSocket | Accepted |
| [ADR-05](0005.md) | Account with a verified email address; card catalog without an account | Accepted |
| [ADR-06](0006.md) | Rules and cards as data packs signed with Ed25519 | Accepted |
| [ADR-07](0007.md) | Hosting and providers only in the EEA | Proposed |
| [ADR-08](0008.md) | Apache 2.0 license and allowed dependencies | Accepted |
| [ADR-09](0009.md) | Develop with the official content and visual identity | Accepted |
| [ADR-10](0010.md) | CMake and vcpkg in manifest mode with pinned versions | Accepted |
| [ADR-11](0011.md) | A single C boundary between the core and the apps | Accepted |
| [ADR-12](0012.md) | Official content in a private pack, separate from the code | Accepted |
