# **ZIP (Zero-cost Instant Payment)**

A multi-threaded UDP-based PIX-like transaction system demonstrating concurrent programming concepts: thread synchronization, reader-writer locks, and deadlock prevention.

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
│   │   ├── locked_map.h          # Thread-safe map with per-entry RW locks
│   │   └── server.h              # Server class (multi-threaded request handling)
│   ├── src/
│   │   └── server.cpp            # Server implementation
│   └── main.cpp                  # Server entry point
│
├── shared/
│   ├── include/
│   │   ├── packet.h              # Protocol packet definitions
│   │   ├── print_utils.h         # Formatted console output
│   │   └── udp_socket.h          # Cross-platform UDP wrapper
│   └── src/
│       ├── print_utils.cpp       # Timestamp + formatting
│       └── udp_socket.cpp        # Platform-specific socket code
│
├── tests/
│   ├── include/
│   │   └── subprocess.h          # Subprocess class
│   │   
│   └── src/
│   │   └── subprocess.cpp        # Instantiates and communicates with subprocesses
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

### LockedMap (`server/include/locked_map.h`)

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

### Novo servidor online ou volta de um que tinha caido
1. Ao iniciar, servidor faz um discovery de outros servidores da rede;
2. Apenas o atual lider responde com o id que o servidor novo terá, o número de transação atual e a tabela de saldo dos clientes;
3. Se o servidor (baseado no ip), já existia na tabela e já tinha um id, o lider apenas devolve esse id, sem criar um novo; 
4. O lider atualiza sua tabela de servidores de backup e envia para todos eles para que também se atualizem;
5. Se, ao receber a nova tabela do lider, o id do lider for maior, inicia uma nova eleição.

### Transação requerida
1. Ao receber uma alteração de saldo, altera o banco local e envia para todos os servidores de backup os saldos atualizados de todos os clientes;
2. Lider não precisa de resposta dos backups, na eleição o melhor será eleito igual.
3. Lider responde ao cliente que transação foi concluida.

### Verificação se o lider está online (Ping)
1. A cada segundo, os servidores de backup enviam ao lider um ping para verificar se ele está online;
2. Caso o lider não responda, o servidor de backup inicia uma eleição.

### Servidor Líder caiu (Eleição)
1. Ao iniciar eleição, o servidor envia a todos os outros servidores que ele quer ser lider, enviando seu id e seu número de transação;
2. O critério da eleição será o maior número de transação. Se 2 ou mais processos tiverem o mesmo número de transação, o critério de desempate é o menor id;
3. Caso algum outro servidor responder que na verdade ele é o lider, o servidor aceita, pois se ele respondeu é lider, é pq ele tem vantagem nos criterios;
4. Se ele não receber nenhuma resposta de um servidor acima (com criterio melhores) ele se assume lider;
5. Ao se assumir lider, atualiza sua tabela de servidores e envia para todos os backups;
6. Envia para todos os clientes que ele é o servidor que deverá receber as requisições.
