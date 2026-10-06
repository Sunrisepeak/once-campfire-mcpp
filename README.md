<div align="center">

# once-campfire-mcpp

**ONCE Campfire re-implemented in C++23 modules — benchmark-first, built with [mcpp](https://github.com/mcpp-community/mcpp).**

One server binary, one load generator, zero runtime dependencies to configure:
the goal is to read the Rails app's SQLite data directly and beat every number
in the [published Campfire performance table](https://github.com/basecamp/once-campfire).

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-00599C.svg)](https://isocpp.org/std/the-standard)
[![build: mcpp](https://img.shields.io/badge/build-mcpp-2ea44f.svg)](https://github.com/mcpp-community/mcpp)
[![platform](https://img.shields.io/badge/platform-linux-lightgrey.svg)]()

</div>

---

> **Review note.** The whole implementation currently lands on
> [PR #1](https://github.com/Sunrisepeak/once-campfire-mcpp/pull/1) so the
> project can be reviewed as one piece. Do not merge it until the review
> round completes.

## Why

[Basecamp's once-campfire-rust](https://github.com/basecamp/once-campfire-rust)
showed that a compiled, single-binary Campfire over the same SQLite data is
19–44× faster than the Rails original. This repository asks the next question:
**how far can C++23 modules and a modern module-aware build tool push the same
idea?** Every design decision is benchmark-driven; the full reasoning lives in
the design record
([.agents/docs/2026-10-06-once-campfire-mcpp-architecture-and-bench-plan.md](.agents/docs/2026-10-06-once-campfire-mcpp-architecture-and-bench-plan.md)).

## Performance

The published baseline (16 concurrent clients, 4 hardware threads per app,
AMD Ryzen AI MAX+ 395 — from the once-campfire README; the Elixir PR #5
"improved" numbers are lower on every route except Sidebar):

| Route (req/s) | Rails | Go | Rust | **Baseline (beat this)** |
|---|---|---|---|---|
| Room page | 241 | 3,860 | 36,260 | **36,260** |
| Messages page | 413 | 5,573 | 40,872 | **40,872** |
| Sidebar | 552 | 19,753 | 34,672 | **34,672** |
| Search | 435 | 7,053 | 33,299 | **33,299** |
| Post a message | 273 | 4,767 | 6,896 | **6,896** |

**M1 status — same-machine A/B against the Rust port** (Intel i9-13900K; both
servers pinned to the same 4 CPUs, the official once-campfire-rust load
generator driving both through identical route paths, 16 keep-alive clients,
8 s per route after warmup):

| Route (req/s) | Rust (same machine, real data) | C++ M1 (same machine, fixtures) | p50/p99 C++ (µs) |
|---|---|---|---|
| Room page | 22,320 | **198,927** | 77 / 123 |
| Messages page | 26,260 | **329,172** | 47 / 76 |
| Sidebar | 21,640 | **418,704** | 37 / 58 |
| Search | 21,575 | **424,592** | 36 / 61 |
| Post a message | 5,428 | **313,715** | 46 / 88 |

The Rust column reproduces its published ballpark (36 k on a Ryzen AI MAX+
395), which validates the method. Two honest caveats: the C++ side serves
static fixtures of the right shapes (the database lands in M2, so its rows are
a transport-floor claim, not an end-to-end one), and its post handler is the
M4 stub (redirect without insert — the Rust side pays a real write). The M6
gate stays: five routes over the Rust baseline with the real database
pipeline, on comparable hardware.

## Repository layout

```
once-campfire-mcpp/
├── mcpp.toml                  # virtual workspace: shared toolchain
├── server/                    # member: the Campfire server
│   ├── mcpp.toml              #   package once-campfire-mcpp → bin: campfire
│   ├── src/
│   │   ├── main.cpp           #   3-line entry
│   │   └── campfire/
│   │       ├── version.cppm   #   product identity
│   │       ├── config.cppm/.cpp   # flags → Config (std::expected)
│   │       ├── log.cppm/.cpp      # lifecycle logger (hot path never logs)
│   │       ├── fixtures.cppm/.cpp # M1 route bodies + FNV-1a ETags
│   │       ├── app.cppm/.cpp      # route table, M1 handlers
│   │       └── http/
│   │           ├── types.cppm     # zero-copy Request / Response
│   │           ├── parser.cppm/.cpp  # incremental HTTP/1.1, RFC-size limits
│   │           ├── router.cppm    # pattern segments + :param capture
│   │           └── server.cppm/.cpp  # asio acceptor + keep-alive connections
│   └── tests/                 #   parser / router / app smoke (mcpp test)
├── bench/                     # member: the load generator
│   ├── mcpp.toml              #   package campfire-bench → bin: loadgen
│   └── src/                   #   16-client keep-alive bench, p50/p90/p99
├── .agents/docs/              # design records (the "why" behind decisions)
└── AGENTS.md                  # working conventions for agents/humans
```

## Build and run

Install [mcpp](https://github.com/mcpp-community/mcpp), then:

```sh
mcpp build          # builds both workspace members
mcpp test           # runs the server test suite

# serve (4 io threads, port 3000)
mcpp run campfire -- --port 3000 --threads 4

# benchmark one route (room|messages|sidebar|search|post)
mcpp run loadgen -- --route room --connections 16 --seconds 5
```

Dependencies resolve from the [mcpp-index](https://github.com/mcpp-community/mcpp-index)
registry — currently `chriskohlhoff.asio` 1.38.1 (standalone Asio exposed as
the C++23 module `asio`); SQLite, zlib, fmt and friends join in M2+ as their
layers land.

## Architecture in one paragraph

Thread-per-request-loop transport (M1: one shared asio `io_context` run by N
threads; M3 upgrades to SO_REUSEPORT shards behind the same signature),
a zero-copy HTTP/1.1 parser whose `Request` views point straight into the
connection buffer, a segment-matching router that captures `:roomId` as a
view (no allocation on the request path), and responses that are cached
fragments with hash-tree ETags — so conditional requests collapse to 304
without touching bytes. The database layer (M2) uses thread-local SQLite
connections over the Rails schema in WAL mode; the cache layer (M3) renders
each message once at write time and never per request. The `.cppm`/`.cpp`
split rule: hot path stays in the module interface unit so it inlines; heavy
cold code hides in implementation units.

## Roadmap

| Milestone | Scope | Exit criterion |
|---|---|---|
| M0 ✅ | workspace scaffold, asio dependency, `import std` | `mcpp build` green |
| M1 ✅ | HTTP parser, router, server, fixtures, loadgen | transport floor measured (this README) |
| M2 | SQLite layer, Rails schema compatibility, cookies/sessions | real user page from the Rails database |
| M3 | views + fragment cache + room snapshots | Room/Messages/Sidebar over baseline |
| M4 | post path: insert + broadcast + invalidation | Post over baseline |
| M5 | FTS5 search + result cache | Search over baseline |
| M6 | per-route profiling, LTO/PGO, arena + writev assembly | five routes over baseline, comparison table |
| M7 | Action Cable, rich text, storage, TLS front server | feature parity with the Rust port |

## License

[MIT](LICENSE)
