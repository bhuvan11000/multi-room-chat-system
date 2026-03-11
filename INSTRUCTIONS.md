# Multi-Room Secure Chat System (Server Core)

This is a high-performance, asynchronous chat server built using **C++17** and **Boost.Asio**. It supports multiple rooms, private messaging, and chunked file transfers.

## Current Status (What's Done)
- **Asynchronous Engine**: Handles multiple concurrent clients using a non-blocking event loop.
- **Custom Protocol**: Fixed-size header (5 bytes) + variable body for efficient data parsing.
- **Room Management**: Support for creating and joining specific rooms with error handling.
- **Private Messaging**: Direct user-to-user communication via a global registry.
- **File Transfer Relay**: A "Streaming" relay that handles large files in 4KB chunks.
- **Robustness**: Graceful handling of client joins, leaves, and unexpected crashes.

---

## How to Build and Run

### Prerequisites

#### For Ubuntu:
```bash
sudo apt update
sudo apt install libboost-all-dev cmake g++
```

#### For Fedora:
```bash
sudo dnf install boost-devel cmake gcc-c++
```

### Build Instructions
1. Create a build directory: `mkdir -p build && cd build`
2. Generate Makefiles: `cmake ..`
3. Compile: `make`

### Running the System
1. **Start Server**: `./chat_server 8080`
2. **Start Client**: `./chat_client 127.0.0.1 8080`

---	
## Usage
### Commands (command bar at the bottom)

| Command                        | Description                    |
|--------------------------------|--------------------------------|
| `/create <room>`               | Create and join a new room     |
| `/join <room>`                 | Join an existing room          |
| `/private <user> <message>`    | Send a private message         |
| `/sendfile <path>`             | Send a file to current room    |
| `/quit`                        | Exit                           |

Type a message in the **message input** and press **Enter** to chat.

## Team Tasks

### 1. SSL/Security Implementation
The server currently uses raw TCP sockets. To make it "Secure":
- **Upgrade Sockets**: Change `tcp::socket` to `boost::asio::ssl::stream<tcp::socket>`.
- **Handshake**: Add `async_handshake()` logic in `ChatSession::start()`.
- **Certificates**: Generate a `.pem` certificate and private key.
- **Integration**: Update `ChatServer` to initialize a `boost::asio::ssl::context`.

### 2. Advanced CLI Client
We need a robust Command Line Interface (CLI) that is more user-friendly than the current `test_client`.
- **UI/UX**: Consider using a library or something to make a good-looking cli.
- **Features**: Add support for command history, autocompletion for usernames/rooms, and clear system notifications.
- **Server Modification**: **Note:** You are encouraged to modify the server code (`src/server/`) if you need to add new features (e.g., listing all active rooms, showing user counts, or message timestamps).
- **Background Thread**: Ensure the network logic runs in a background thread so the UI remains responsive while waiting for input.

---
