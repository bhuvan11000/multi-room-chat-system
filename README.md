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

`#pragma pack(push, 1)` removes compiler padding so the header is always exactly 5 bytes regardless of platform, ensuring cross-platform wire compatibility.

### Read Loop (both sides)

```
async_read(5 bytes header)
  → extract body length
  → async_read(N bytes body)
  → handle_message()
  → repeat
```

This solves TCP's stream nature — since TCP has no message boundaries, the fixed 5-byte header tells the reader exactly how many bytes to wait for next. Prevents partial reads and message overlap.

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

### Handshake Flow

```
Client                              Server
  │                                   │
  │──── TCP connect ─────────────────►│
  |                                   |
  │◄─── TCP accept ───────────────────│
  │                                   │
  │──── TLS ClientHello ─────────────►│
  |                                   |
  │◄─── TLS ServerHello + Cert ───────│
  |                                   |
  │──── Key Exchange ────────────────►│
  |                                   |
  │◄─── TLS Finished ─────────────────│
  │                                   │
  │──── LOGIN message ───────────────►│
```

On the server, the handshake is done asynchronously per session:
```cpp
ssl_socket_.async_handshake(boost::asio::ssl::stream_base::server,
    [this, self](const boost::system::error_code& ec) {
        if (!ec) do_read_header();
    });
```

The socket type throughout is `boost::asio::ssl::stream<tcp::socket>` — all reads and writes transparently go through the TLS layer.

### Certificate Generation (self-signed, dev only)

```bash
openssl req -x509 -newkey rsa:2048 \
  -keyout server.key -out server.crt \
  -days 365 -nodes
```

> **Note:** Self-signed certs are fine for on-premise/dev use. For production, use a CA-signed certificate and enable `verify_peer` on the client.

---

## Server Side

### `ChatServer` (`main.cpp`)

- Initialises the TLS context with `SSLManager::create_boost_context(SERVER, "server.crt", "server.key")`
- Sets up `tcp::acceptor` on the given port
- On each accepted connection, creates a `ChatSession` and calls `start()`
- Runs a single `io_context` — all sessions share one event loop

### `ChatSession` (`chat_session.hpp/cpp`)

Represents one connected client. Owns:
- `ssl_socket_` — the encrypted socket
- `username_` — set on LOGIN
- `joined_rooms_` — a `std::set<shared_ptr<ChatRoom>>` (multi-room support)
- `write_msgs_` — a `deque` write queue for ordered async sends

Key behaviour:
- A user can be in **multiple rooms simultaneously**. CHAT_MSG broadcasts to all joined rooms.
- Duplicate usernames are rejected: `register_user()` returns `false` if the name is taken, the session is shut down immediately.
- On disconnect, `handle_error()` removes the session from all joined rooms and unregisters the user.

### `RoomManager` (`chat_room.hpp/cpp`)

Global registry (owned by `ChatServer`):
- `rooms_` — map of room name → `ChatRoom`
- `users_` — map of username → `ChatSession`
- On any user/room change, `notify_user_list_change()` / `notify_room_list_change()` pushes updated lists to **all connected clients** automatically.

---

## Client Side

### `chat_client.cpp`

Two-thread design:

| Thread | Role |
|--------|------|
| Main | FTXUI render loop, user input handling |
| Net | `ioc.run()` — all async reads/writes |

Shared state between threads is protected by `log_mtx` (chat messages) and `state_mtx` (room/user lists). The net thread calls `screen.PostEvent(Event::Custom)` to trigger a UI redraw after updating state.

### Commands

| Command | Action |
|---------|--------|
| `/create <room>` | Create and join a new room |
| `/join <room>` | Join an existing room |
| `/switch <room>` | Switch focus to a joined room, or exit current and join the new one |
| `/leave <room>` | Leave a specific room |
| `/private <user> <msg>` | Send a direct message |
| `/sendfile <path>` | Send a file (4KB chunks) |
| `/quit` | Exit cleanly |
| *(anything else)* | Broadcast to all joined rooms |

### File Transfer

Files are sent as a sequence of protocol messages:
```
FILE_START  → filename
FILE_DATA   → 4096-byte chunks (repeated)
FILE_END    → filename
```
The receiver writes chunks to `received_<filename>` as they arrive. Non-blocking — the UI stays responsive during transfer.

### Error Handling

- If the username is already taken, the server shuts down the connection and the client prints a clear error to stderr and exits with code 1.
- If the server disconnects mid-session, `screen.Exit()` is called and a `[disconnected]` message is shown.

---

## Performance Evaluation

A dedicated tool `perf_test` (at `src/perf/perf_test.cpp`) connects to a running server as a synthetic client and runs two benchmarks over a real TLS connection.

### How to run

```bash
# Terminal 1: start the server
./chat_server 9000

# Terminal 2: run the perf test
./perf_test 127.0.0.1 9000
```

### What it measures

**Test 1 — Throughput**

Sends 1000 `CHAT_MSG` messages back-to-back, then drains all 1000 echoes. Measures total time for the full send+receive cycle.

```
[Test 1] 1000 msgs in 0.061s  →  16434 msg/s
```

**Test 2 — Round-trip latency**

Sends one message, waits for the echo, records the time. Repeated 100 times. Reports avg/min/max.

```
[Test 2] avg=0.070ms  min=0.046ms  max=0.374ms
```

**Test 3 — Concurrent clients**

Runs 5 clients on different threads. Each thread independently connects with TLS handshakes, logs in, creates its own room, and runs its own send/recv burst simultaneously.
Reports per-client thorughput and aggregate throughput.

```
[Test 3] Concurrent clients: 5 clients × 200 msgs each...
  client 0: 11390 msg/s (0.018s)
  client 1: 11377 msg/s (0.018s)
  client 2: 11671 msg/s (0.017s)
  client 3: 11502 msg/s (0.017s)
  client 4: 3461 msg/s (0.058s)
[Test 3] aggregate throughput: 49401 msg/s  |  wall time: 0.272s
         (scalability ratio vs Test 1: 2.78×)
```
**Test 4 — Large payload throughput**

Sends 200 message worth 4KB   

```
[Test 4] Large payload: 200 × 4096-byte messages...
[Test 4] 200 msgs in 0.011s  →  17413 msg/s  |  71.32 MB/s
```

### Setup drain

Before measuring, the test logs in and creates a room. The server sends a variable number of broadcast messages in response (LIST_USERS, LIST_ROOMS, etc.). Rather than hardcoding a count (which would hang if the number differs), `timed_drain()` reads and discards messages until the socket is quiet for 80ms, then starts the clock.

### Interpreting results

| Metric | Observed | What it means |
|--------|----------|---------------|
| Throughput | ~16,000 msg/s | Server processes 16K complete encrypted round-trips/sec on loopback |
| Avg latency | ~0.07ms | 70 microseconds per message including TLS overhead |
| Max latency | ~0.37ms | Occasional OS scheduler or TLS flush spike — not a bug |

> Results are loopback-only. Real-world numbers are dominated by network RTT. Use this tool to compare your server against itself — e.g. before/after a code change.

---

## Known Limitations

| Limitation | Detail |
|------------|--------|
| No persistence | All messages and rooms are in-memory, lost on restart |
| No authentication | Username only — no password or session token |
| Self-signed TLS | Client does not verify server certificate (`verify_none`) |
| No mutex on RoomManager | Safe under single-threaded `ioc.run()`; will race if you add a thread pool |
| No message history | New joiners see no prior messages |

---

## Build & Run

### Prerequisites

```bash
sudo apt update
sudo apt install libboost-all-dev libssl-dev cmake g++
```

### Build

```bash
mkdir -p build && cd build
cmake ..
make                  # builds chat_server, chat_client, perf_test
```

### Generate SSL certificates

```bash
# Run from the build directory
openssl req -x509 -newkey rsa:2048 \
  -keyout server.key -out server.crt \
  -days 365 -nodes
```

### Run

```bash
# Terminal 1
./chat_server 9000

# Terminal 2+
./chat_client 127.0.0.1 9000

# Performance test
./perf_test 127.0.0.1 9000
```

> If the server port says "Address already in use" after a restart, either wait ~60 seconds for `TIME_WAIT` to clear, or add `acceptor_.set_option(boost::asio::socket_base::reuse_address(true))` in `ChatServer`'s constructor (recommended).

---

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++17 |
| Networking | Boost.Asio (async, Proactor pattern) |
| Encryption | OpenSSL + boost::asio::ssl (TLS 1.2) |
| Terminal UI | FTXUI |
| Build | CMake 3.15+ |
