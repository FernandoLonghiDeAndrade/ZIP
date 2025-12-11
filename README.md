# **ZIP (Zero-cost Instant Payment)**

A distributed multi-threaded UDP-based PIX-like transaction system demonstrating concurrent and distributed programming concepts: leader election, state replication, fault tolerance, thread synchronization, reader-writer locks, and deadlock prevention.

## Features

- **Distributed architecture** with leader-backup model and automatic failover
- **Leader election** using Bully algorithm (age-based priority)
- **State replication** across backup servers for fault tolerance
- **Cross-platform UDP** (Windows/Linux/macOS) with timeout support
- **Multi-threaded server** (one thread per request)
- **Fine-grained locking** (per-client reader-writer locks)
- **Stop-and-wait protocol** with automatic retransmission
- **Deadlock prevention** via ordered locking
- **Server discovery** (broadcast or direct connection)

## Project Structure

```md
ZIP/
├── client/
│   ├── include/
│   │   └── client.h              # Client class (discovery + stop-and-wait)
│   ├── src/
│   │   └── client.cpp            # Client implementation
│   └── main.cpp                  # Client entry point
│
├── server/
│   ├── include/
│   │   ├── client_info.h         # Client state structure (balance, request tracking)
│   │   ├── locked_map.h          # Thread-safe map with per-entry RW locks
│   │   ├── server_packet.h       # Server-to-server packet definitions
│   │   └── server.h              # Distributed server (leader election + state replication)
│   ├── src/
│   │   └── server.cpp            # Server implementation
│   └── main.cpp                  # Server entry point
│
├── shared/
│   ├── include/
│   │   ├── client_packet.h       # Client-to-server packet definitions
│   │   ├── print_utils.h         # Formatted console output
│   │   ├── socket_address.h      # IP address wrapper (IPv4)
│   │   └── udp_socket.h          # Cross-platform UDP wrapper with timeout
│   └── src/
│       ├── print_utils.cpp       # Timestamp + formatting
│       ├── socket_address.cpp    # Address parsing and validation
│       └── udp_socket.cpp        # Platform-specific socket code
│
├── tests/
│   ├── include/
│   │   └── subprocess.h          # Subprocess management class
│   ├── src/
│   │   └── subprocess.cpp        # Process instantiation and communication
│   └── main.cpp                  # Test entry point
│
├── CMakeLists.txt                # Build configuration
├── .gitignore
└── README.md
```

## Prerequisites

- **CMake** 3.10+  
- **C++17 compiler**:
  - Windows: MSVC (Visual Studio 2017+) or MinGW-w64
  - Linux: GCC 7+ or Clang 5+
  - macOS: Xcode Command Line Tools
- **Recommended**: VS Code with extensions:
  - [C/C++](vscode:extension/ms-vscode.cpptools)
  - [CMake Tools](vscode:extension/ms-vscode.cmake-tools)

## Setup and Build

### Initial Setup (run once)

```bash
mkdir build
cd build
cmake ..
```

### Build

```bash
# Inside build/ directory
cmake --build .

# Or with parallel jobs (faster)
cmake --build . -j4
```

## Run

### Server

```bash
# Windows
.\server.exe 8080

# Linux/macOS
./server 8080
```

### Client

```bash
# Broadcast discovery (finds server automatically)
.\client.exe 8080          # Windows
./client 8080              # Linux/macOS

# Direct connection (skip discovery)
.\client.exe 8080 192.168.1.100   # Windows
./client 8080 192.168.1.100       # Linux/macOS
```

### Test

```bash
.\test.exe  # Windows
./test      # Linux/macOS

# Passing test args, number of tests and clients ips
.\test.exe [TEST_COUNT] [client_ip1 client_ip2 ...] # Windows
./test [TEST_COUNT] [client_ip1 client_ip2 ...]     # Linux/macOS
```

## Usage

After connecting, enter transactions in the format:

```md
<destination_ip> <value>
```

Example:

```md
192.168.1.100 50
```

## VS Code Integration

### Configure (first time only)

- Open Command Palette (`Ctrl+Shift+P`)
- Run: `CMake: Configure`

### Build (VS Code)

- Click **Build** (⚙️) in status bar
- Or: `Ctrl+Shift+P` → `CMake: Build`

### Run (VS Code)

- Click **Launch** (▶️) in status bar
- Or: `F5` → Select "Run Server" or "Run Client"
- Switch targets: `CMake: Set Launch/Debug Target`

## Key Components

### Distributed Server Architecture

**Leader-Backup Model**: One leader processes client requests, multiple backups replicate state for fault tolerance.

**Leader Election**: Bully algorithm with age-based priority (older servers = higher priority). Server age determined by position in cluster's server list. Automatic failover when leader fails.

**State Replication**: Leader broadcasts all state changes (new clients, transactions) to backups. Backups maintain eventual consistency.

**Failure Detection**: Backups ping leader every 100ms. Election triggered on timeout.

### LockedMap (`server/include/locked_map.h`)

Thread-safe map with **per-entry reader-writer locks**. Enables concurrent reads and exclusive writes per entry, preventing contention between different clients.

### UDPSocket (`shared/include/udp_socket.h`)

Cross-platform UDP wrapper with **timeout support using select()**. Handles platform differences (Winsock on Windows, BSD sockets on Unix). Includes broadcast loopback filtering via socket_id headers.

### Stop-and-Wait Protocol

Client retransmits requests every **100ms** until receiving ACK. Server uses request IDs for **duplicate detection** (idempotency).

## Concurrency Design

- **Server**: Main thread listens, spawns detached worker threads per request, background thread for leader monitoring
- **Client**: Main thread sends requests, network thread handles responses (ACKs + leader updates)
- **Synchronization**: Mutex + condition variable for stop-and-wait, atomic flags for thread cancellation
- **Deadlock Prevention**: Atomic pair operations lock in fixed order (lower IP first)
- **State Replication**: Leader broadcasts changes, backups apply updates atomically with sequence numbers

## Troubleshooting

### Build Issues

**Problem**: CMake configuration fails

```bash
# Solution: Delete build directory and reconfigure
rm -rf build
mkdir build && cd build
cmake ..
```

### Runtime Issues

**Problem**: Port already in use

```bash
# Linux: Find process using port
sudo lsof -i :8080
sudo kill -9 <PID>

# Windows (PowerShell)
netstat -ano | findstr :8080
taskkill /PID <PID> /F

# Or just use a different port
./server 8081
```

**Problem**: Client can't discover server

```bash
# Solution 1: Verify same subnet
ip addr show           # Linux
ipconfig              # Windows

# Solution 2: Provide server IP explicitly
./client 8080 192.168.1.50
```

**Problem**: Windows Firewall blocks packets

```md
1. Windows Defender Firewall → Allow an app
2. Add server.exe and client.exe
3. Enable "Private networks" checkbox
```

## License

Educational project for Operating Systems course.
