# **ZIP (Zero-latency Instant Payment)**

A multi-threaded UDP-based transaction system demonstrating concurrent programming concepts: thread synchronization, reader-writer locks, and deadlock prevention.

## Features

- **Cross-platform UDP** (Windows/Linux/macOS) with non-blocking sockets
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
│   │   └── server.h              # Server class (multi-threaded request handling)
│   ├── src/
│   │   └── server.cpp            # Server implementation
│   └── main.cpp                  # Server entry point
│
├── shared/
│   ├── include/
│   │   ├── locked_map.h          # Thread-safe map with per-entry RW locks
│   │   ├── packet.h              # Protocol packet definitions
│   │   ├── print_utils.h         # Formatted console output
│   │   └── udp_socket.h          # Cross-platform UDP wrapper
│   └── src/
│       ├── print_utils.cpp       # Timestamp + formatting
│       └── udp_socket.cpp        # Platform-specific socket code
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

### LockedMap (`shared/include/locked_map.h`)

Thread-safe map with **per-entry reader-writer locks**. Enables concurrent reads and exclusive writes per entry, preventing contention between different clients.

### UDPSocket (`shared/include/udp_socket.h`)

Cross-platform UDP wrapper with **thread-safe send/receive**. Handles platform differences (Winsock on Windows, BSD sockets on Unix).

### Stop-and-Wait Protocol

Client retransmits requests every **200ms** until receiving ACK. Server uses request IDs for **duplicate detection** (idempotency).

## Concurrency Design

- **Server**: Main thread listens, spawns detached worker threads per request
- **Client**: Main thread sends requests, network thread handles responses
- **Synchronization**: Mutex + condition variable for stop-and-wait
- **Deadlock Prevention**: Atomic pair operations lock in fixed order (lower IP first)

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
