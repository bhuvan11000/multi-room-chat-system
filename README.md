# Multi-Room Secure Chat System

A real-time, asynchronous chat system built in **C++17** using **Boost.Asio** for non-blocking I/O and **OpenSSL/TLS** for end-to-end encryption. Supports multiple chat rooms, private messaging, file transfer, and a live Terminal UI — all over a custom binary protocol.

---

## Table of Contents

1. [Architecture](#architecture)
2. [Project Structure](#project-structure)
3. [Protocol Design](#protocol-design)
4. [SSL/TLS Implementation](#ssltls-implementation)
5. [Server Side](#server-side)
6. [Client Side](#client-side)
7. [Features](#features)
8. [Performance Evaluation](#performance-evaluation)
9. [Known Limitations](#known-limitations)
10. [Build & Run](#build--run)

---

## Architecture

```
┌─────────────┐        TLS/TCP         ┌──────────────────────────────────┐
│  chat_client│ ◄────────────────────► │           chat_server            │
│             │                        │                                  │
│  Main Thread│                        │  Single-threaded io_context      │
│  └─ FTXUI   │                        │  └─ ChatSession (per client)     │
│  Net Thread │                        │  └─ RoomManager (global state)   │
│  └─ ioc.run │                        │                                  │
└─────────────┘                        └──────────────────────────────────┘
```

The server uses the **Proactor pattern** via Boost.Asio — a single-threaded event loop handles all clients through async callbacks. No thread-per-connection; connections are managed as `ChatSession` objects driven by the event loop.

The client runs two threads: a main thread for FTXUI rendering, and a worker thread running `ioc.run()` for all network I/O.

---

## Project Structure

```
multi-room-chat-system
├── CMakeLists.txt            # Build configuration
├── README.md                 # Project documentation
└── src/
    ├── common/
    │   ├── protocol.hpp          # Message types, header struct, constants
    │   └── ssl_manager.hpp       # SSL context factory (wraps OpenSSL + Boost.Asio SSL)
    ├── server/
    │   ├── main.cpp              # ChatServer: acceptor loop, SSL context init
    │   ├── chat_session.hpp/cpp  # Per-client state machine
    │   └── chat_room.hpp/cpp     # ChatRoom + RoomManager
    ├── client/
    │   └── chat_client.cpp       # TUI client (FTXUI + async network)
    └── perf/
        └── perf_test.cpp         # Performance evaluation tool
```

---

## Protocol Design

A **custom binary protocol** is used instead of text framing for efficiency and deterministic parsing.

### Message Format

```
┌──────────┬────────────────┬─────────────────────┐
│  type    │    length      │        body         │
│ (1 byte) │   (4 bytes)    │   (variable bytes)  │
└──────────┴────────────────┴─────────────────────┘
        Total header: 5 bytes (#pragma pack(push,1))
```

### Read Loop (both sides)

```
async_read(5 bytes header)
  → extract body length
  → async_read(N bytes body)
  → handle_message()
  → repeat
```

This solves TCP's stream nature — since TCP has no message boundaries, the fixed 5-byte header tells the reader exactly how many bytes to wait for next.

---

## SSL/TLS Implementation

This is the core security layer. All traffic is encrypted end-to-end using TLS 1.2.

### SSLManager (`ssl_manager.hpp`)

`SSLManager::create_boost_context()` is a static factory that returns a configured `boost::asio::ssl::context`:

```cpp
// Server: loads cert + key
boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_server);
ctx.use_certificate_chain_file("server.crt");
ctx.use_private_key_file("server.key", boost::asio::ssl::context::pem);

// Client: no cert required (verify_none for self-signed)
boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_client);
```

---

## Server Side

### `ChatSession` (`chat_session.hpp/cpp`)

Represents one connected client. Owns:
- `ssl_socket_` — the encrypted socket
- `username_` — set on LOGIN
- `joined_rooms_` — set of all rooms the user has joined.
- `current_room_` — the currently focused active room.

Key behaviour:
- **Active Room Focus**: While a user can join multiple rooms, only one is "active" at a time.
- **Contextual Broadcasting**: `CHAT_MSG` and file transfers are broadcast **only** to the user's current active room.
- **Filtered Delivery**: Users only receive messages from the room they are currently focusing on. This prevents cross-room message noise.
- **Room Switching**: The `/switch` command allows moving focus between joined rooms or swapping the current room for a new one.

### `RoomManager` (`chat_room.hpp/cpp`)

Global registry:
- Tracks all active `ChatRoom` and `ChatSession` objects.
- Pushes `LIST_ROOMS` and `LIST_USERS` updates to all clients when state changes.
- Handles `[Info]:` and `[Server]:` prefixed system messages for consistent client-side TUI coloring.

---

## Client Side

### Terminal UI (FTXUI)

The client features a modern TUI with three main sections:
- **Header**: Displays the current user, the list of joined rooms, and the **Current Room** focus.
- **Main Chat**: A scrollable area for messages, with color-coded tags for `[Info]`, `[Server]`, `[Private]`, and `[Error]`.
- **Sidebar**: A unified column on the right containing the list of available **ROOMS**, online **USERS**, and **AVAILABLE COMMANDS**.

### Commands

| Command | Action |
|---------|--------|
| `/create <room>` | Create and join a new room (sets as active) |
| `/join <room>` | Join an existing room (sets as active) |
| `/switch <room>` | Switch focus to a joined room, or exit current and join the new one |
| `/leave <room>` | Leave a specific room |
| `/private <u1> <m>` | Send a direct message |
| `/sendfile <path>` | Send a file to the active room |
| `/quit` | Exit cleanly |
| *(anything else)* | Broadcast message to the **current active room** |

---

## Features

- **Multi-Room Support**: Join several rooms but focus on one to keep conversations clean.
- **End-to-End Encryption**: TLS 1.2 ensures all data, including files and private messages, is secure.
- **File Transfer**: Non-blocking asynchronous file transfers with progress notifications.
- **Live Redraws**: TUI updates instantly when users join/leave or rooms are created.
- **Private Messaging**: Secure one-on-one communication.

---

## Performance Evaluation

A dedicated tool `perf_test` (at `src/perf/perf_test.cpp`) measures throughput and latency.

### Metrics Observed (Loopback)

| Metric | Observed | What it means |
|--------|----------|---------------|
| Throughput | ~16,000 msg/s | High-frequency processing of encrypted round-trips |
| Avg latency | ~0.07ms | Sub-millisecond response time including TLS overhead |

---

## Build & Run

### Prerequisites

```bash
sudo apt install libboost-all-dev libssl-dev cmake g++
```

### Build

```bash
mkdir build && cd build
cmake .. && make
```

### Generate SSL certificates

```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
```

### Run

```bash
./chat_server 9000
./chat_client 127.0.0.1 9000
```

---

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++17 |
| Networking | Boost.Asio (async) |
| Encryption | OpenSSL (TLS 1.2) |
| Terminal UI | FTXUI |
| Build | CMake 3.15+ |
