#include "udp_socket.h"
#include <cstring>

#ifdef _WIN32
    #include <ws2tcpip.h>
    
    /**
     * Windows-specific: Winsock2 requires WSAStartup() before any address operations.
     * This is called once per process (not per address).
     * Thread-safe via static bool guard (potential race on first call, but benign).
     */
    static bool winsock_initialized = false;
    static void init_winsock() {
        if (!winsock_initialized) {
            WSADATA wsa_data;
            WSAStartup(MAKEWORD(2, 2), &wsa_data);  // Initialize Winsock 2.2
            winsock_initialized = true;
        }
    }
    
    #define close_socket_impl(fd) closesocket(fd)
    #define set_nonblocking(fd) { u_long mode = 1; ioctlsocket(fd, FIONBIO, &mode); }
    #define is_wouldblock(err) (err == WSAEWOULDBLOCK)
    #define get_socket_error() WSAGetLastError()
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <arpa/inet.h>
    #include <errno.h>
    
    #define init_winsock() ((void)0)
    #define close_socket_impl(fd) close(fd)
    #define set_nonblocking(fd) { int flags = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, flags | O_NONBLOCK); }
    #define is_wouldblock(err) (err == EAGAIN || err == EWOULDBLOCK)
    #define get_socket_error() errno
#endif

// ===== SocketAddress implementation =====

SocketAddress::SocketAddress() {
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
}

SocketAddress::SocketAddress(const std::string& ip, uint16_t port) {
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
}

SocketAddress::SocketAddress(uint32_t ip_network_byte_order, uint16_t port) {
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip_network_byte_order;
}

SocketAddress::SocketAddress(const struct sockaddr_in& addr) : addr(addr) {}

SocketAddress SocketAddress::broadcast(uint16_t port) {
    return SocketAddress("255.255.255.255", port);
}

std::string SocketAddress::ip_string() const {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str);
}

uint32_t SocketAddress::ip() const {
    return addr.sin_addr.s_addr;
}

uint16_t SocketAddress::port() const {
    return ntohs(addr.sin_port);
}

bool SocketAddress::is_valid() const {
    return addr.sin_addr.s_addr != 0;
}

// ===== Socket initialization =====

uint32_t UDPSocket::ip() const {
    if (sock_fd == INVALID_SOCKET_VALUE) {
        return 0;
    }

    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock_fd, (struct sockaddr*)&local_addr, &addr_len) < 0) {
        return 0;
    }
    return local_addr.sin_addr.s_addr;
}

uint16_t UDPSocket::port() const {
    if (sock_fd == INVALID_SOCKET_VALUE) {
        return 0;
    }

    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock_fd, (struct sockaddr*)&local_addr, &addr_len) < 0) {
        return 0;
    }
    return ntohs(local_addr.sin_port);
}

SocketAddress UDPSocket::address() const {
    if (sock_fd == INVALID_SOCKET_VALUE) {
        return SocketAddress();
    }

    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock_fd, (struct sockaddr*)&local_addr, &addr_len) < 0) {
        return SocketAddress();
    }
    return SocketAddress(local_addr);
}

bool UDPSocket::initialize(uint16_t port, bool is_broadcast) {
    // Windows: Initialize Winsock library (process-wide, idempotent)
    // Linux: No-op
    init_winsock();
    
    // Create UDP socket (AF_INET = IPv4, SOCK_DGRAM = UDP)
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd == INVALID_SOCKET_VALUE) {
        return false;
    }

    // Set non-blocking mode (receive returns immediately if no data)
    // Windows: ioctlsocket with FIONBIO
    // Linux: fcntl with O_NONBLOCK
    set_nonblocking(sock_fd);

    // Enable broadcast capability if requested (required for 255.255.255.255)
    // Without this, sendto() to broadcast address fails with permission error
    if (is_broadcast) {
        int broadcast_enable = 1;
        if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, 
                      (const char*)&broadcast_enable, sizeof(broadcast_enable)) < 0) {
            close_socket();
            return false;
        }
    }

    // Bind address to port and all local interfaces (0.0.0.0)
    struct sockaddr_in bind_addr {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0 = listen on all interfaces
    bind_addr.sin_port = htons(port);        // Convert to network byte order

    if (bind(sock_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        close_socket();
        return false;  // Port already in use or insufficient permissions
    }

    return true;
}

// ===== Send data =====

bool UDPSocket::send(const void* data, size_t size, const SocketAddress& dest_addr) {
    // Validate input parameters
    if (!data || size == 0 || sock_fd == INVALID_SOCKET_VALUE) {
        return false;
    }

    // Serialize sends (only one thread can send at a time)
    // Note: Doesn't block receives (separate mutex)
    std::lock_guard<std::mutex> lock(send_mutex);

    // Send UDP datagram to destination address
    // Windows expects char* cast, POSIX accepts void* directly
    const struct sockaddr_in& native_addr = dest_addr.native();
    ssize_t sent_bytes = sendto(sock_fd, (const char*)data, size, 0, 
                               (const struct sockaddr*)&native_addr, sizeof(native_addr));
    
    // Verify all bytes were sent (should always be true for UDP, or fails completely)
    // UDP is atomic: either entire datagram is sent, or error occurs
    return sent_bytes == static_cast<ssize_t>(size);
}

// ===== Receive data =====

int32_t UDPSocket::receive(void* buffer, size_t size, SocketAddress& sender_addr, uint32_t timeout_ms) {
    // Validate input parameters
    if (!buffer || size == 0 || sock_fd == INVALID_SOCKET_VALUE) {
        return -1;
    }

    // Serialize receives (only one thread can receive at a time)
    // Note: Doesn't block sends (separate mutex)
    std::lock_guard<std::mutex> lock(receive_mutex);
    
    // Setup select/poll for timeout handling
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock_fd, &read_fds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    // Wait for data with timeout (0 = no timeout, blocks until data arrives)
    int select_result = select(sock_fd + 1, &read_fds, nullptr, nullptr, 
                               timeout_ms > 0 ? &tv : nullptr);
    
    if (select_result < 0) {
        // select() error
        return -1;
    } else if (select_result == 0) {
        // Timeout occurred, no data available
        return 0;
    }
    
    // Receive UDP datagram from any sender (non-blocking)
    struct sockaddr_in native_addr;
    socklen_t addr_len = sizeof(native_addr);
    ssize_t received_bytes = recvfrom(sock_fd, (char*)buffer, size, 0, 
                                     (struct sockaddr*)&native_addr, &addr_len);
    
    if (received_bytes < 0) {
        int err = get_socket_error();
        if (is_wouldblock(err)) {
            // No data available (non-blocking mode, not an error)
            return 0;
        }
        // Real error (address closed, network error, etc.)
        return -1;
    }
    
    // Update sender address with received packet source
    sender_addr = SocketAddress(native_addr);
    
    // Return number of bytes received (UDP datagram size)
    // Note: If buffer too small, datagram is truncated and excess data is lost
    return static_cast<int32_t>(received_bytes);
}

// ===== Close address =====

void UDPSocket::close_socket() {
    // Acquire send_mutex to prevent concurrent send operations during close
    // (receive_mutex not needed since closing invalidates address for both)
    std::lock_guard<std::mutex> lock(send_mutex);
    
    if (sock_fd != INVALID_SOCKET_VALUE) {
        close_socket_impl(sock_fd);         // Platform-specific close function
        sock_fd = INVALID_SOCKET_VALUE;     // Mark as closed (prevents double-close)
    }
}
