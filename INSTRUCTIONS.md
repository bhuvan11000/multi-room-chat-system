# Multi-Room Secure Chat System

This is a high-performance, asynchronous chat system built using **C++17**, **Boost.Asio**, and **OpenSSL**. It features a modern Terminal User Interface (TUI) and secure, encrypted communication.

## Current Status (What's Done)
- **SSL/TLS Encryption**: All communication is secured using `boost::asio::ssl`.
- **Advanced TUI Client**: A responsive, interactive terminal interface built with **FTXUI**.
- **Asynchronous Engine**: Handles multiple concurrent clients using a non-blocking event loop.
- **Custom Protocol**: Fixed-size header (5 bytes) + variable body for efficient data parsing.
- **Room Management**: Support for creating and joining specific rooms with error handling.
- **Private Messaging**: Direct user-to-user communication via a global registry.
- **File Transfer Relay**: A "Streaming" relay that handles large files in 4KB chunks.
- **Robustness**: Graceful handling of client joins, leaves, and unexpected crashes.

---

## How to Build and Run

### 1. Prerequisites

#### For Ubuntu:
```bash
sudo apt update
sudo apt install libboost-all-dev libssl-dev cmake g++
```

#### For Fedora:
```bash
sudo dnf install boost-devel openssl-devel cmake gcc-c++
```

### 2. Build Instructions
```bash
mkdir -p build && cd build
cmake ..
make
```

### 3. Setup and Run (Local)

#### Step A: Generate SSL Certificates (Mandatory)
The server requires a certificate and private key. Run this inside the `build/` directory:
```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
```

#### Step B: Start the Server
```bash
./chat_server 8080
```

#### Step C: Start the Client
Open a new terminal and run:
```bash
./chat_client 127.0.0.1 8080
```

---

## 4. Connecting from Other Devices (Local Network)

To connect a second device on the same network to your server:

### Step A: Find Server's Local IP
On the **server machine**, run:
```bash
hostname -I | awk '{print $1}'
```
*(e.g., `192.168.1.15`)*

### Step B: Open Firewall Port
Ensure the port (e.g., `8080`) is open on the **server machine**:
- **Ubuntu (UFW):** `sudo ufw allow 8080/tcp`
- **Fedora:** `sudo firewall-cmd --add-port=8080/tcp --permanent && sudo firewall-cmd --reload`

### Step C: Run Client on Remote Device
Install prerequisites and build the client on the second device, then run:
```bash
./chat_client <SERVER_IP> 8080
```

---	
## Usage
### TUI Commands (command bar at the bottom)

| Command                        | Description                    |
|--------------------------------|--------------------------------|
| `/create <room>`               | Create and join a new room     |
| `/join <room>`                 | Join an existing room          |
| `/private <user> <message>`    | Send a private message         |
| `/sendfile <path>`             | Send a file to current room    |
| `/quit`                        | Exit the application           |

Type a message in the **message input** and press **Enter** to chat.

## Next Steps / Future Enhancements
- **Persistent Storage**: Integrate a database (like SQLite) to store chat history and user accounts.
- **User Authentication**: Implement a proper login system with password hashing.
- **File Transfer UI**: Add progress bars and file selection dialogs to the TUI for the `/sendfile` command.
- **Enhanced Notifications**: Add system sounds or terminal bells for new messages.
---
