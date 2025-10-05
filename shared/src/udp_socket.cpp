#include "udp_socket.h"
#include <cstring>

#ifdef _WIN32
    #include <ws2tcpip.h>
    
    /**
     * Windows-specific: Winsock2 requires WSAStartup() before any socket operations.
     * This is called once per process (not per socket).
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
    #define INVALID_FD INVALID_SOCKET
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
    #define INVALID_FD -1
#endif

// ===== Socket initialization =====

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

    // Bind socket to port and all local interfaces (0.0.0.0)
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0 = listen on all interfaces
    addr.sin_port = htons(port);        // Convert to network byte order

    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket();
        return false;  // Port already in use or insufficient permissions
    }

    return true;
}

// ===== Send data =====

bool UDPSocket::send(const void* data, size_t size, const struct sockaddr_in& dest_addr) {
    // Validate input parameters
    if (!data || size == 0 || sock_fd == INVALID_SOCKET_VALUE) {
        return false;
    }

    // Serialize sends (only one thread can send at a time)
    // Note: Doesn't block receives (separate mutex)
    std::lock_guard<std::mutex> lock(send_mutex);

    // Send UDP datagram to destination address
    // Windows expects char* cast, POSIX accepts void* directly
    ssize_t sent_bytes = sendto(sock_fd, (const char*)data, size, 0, 
                               (const struct sockaddr*)&dest_addr, sizeof(dest_addr));
    
    // Verify all bytes were sent (should always be true for UDP, or fails completely)
    // UDP is atomic: either entire datagram is sent, or error occurs
    return sent_bytes == static_cast<ssize_t>(size);
}

// ===== Receive data =====

int32_t UDPSocket::receive(void* buffer, size_t size, struct sockaddr_in& sender_addr) {
    // Validate input parameters
    if (!buffer || size == 0 || sock_fd == INVALID_SOCKET_VALUE) {
        return -1;
    }

    // Serialize receives (only one thread can receive at a time)
    // Note: Doesn't block sends (separate mutex)
    std::lock_guard<std::mutex> lock(receive_mutex);
    
    // Receive UDP datagram from any sender (non-blocking)
    socklen_t addr_len = sizeof(sender_addr);
    ssize_t received_bytes = recvfrom(sock_fd, (char*)buffer, size, 0, 
                                     (struct sockaddr*)&sender_addr, &addr_len);
    
    if (received_bytes < 0) {
        int err = get_socket_error();
        if (is_wouldblock(err)) {
            // No data available (non-blocking mode, not an error)
            return 0;
        }
        // Real error (socket closed, network error, etc.)
        return -1;
    }
    
    // Return number of bytes received (UDP datagram size)
    // Note: If buffer too small, datagram is truncated and excess data is lost
    return static_cast<int32_t>(received_bytes);
}

// ===== Close socket =====

void UDPSocket::close_socket() {
    // Acquire send_mutex to prevent concurrent send operations during close
    // (receive_mutex not needed since closing invalidates socket for both)
    std::lock_guard<std::mutex> lock(send_mutex);
    
    if (sock_fd != INVALID_SOCKET_VALUE) {
        close_socket_impl(sock_fd);         // Platform-specific close function
        sock_fd = INVALID_SOCKET_VALUE;     // Mark as closed (prevents double-close)
    }
}

// ===== Static address helpers =====

struct sockaddr_in UDPSocket::create_address(const std::string& ip, uint16_t port) {
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    return addr;
}

struct sockaddr_in UDPSocket::create_broadcast_address(uint16_t port) {
    return create_address("255.255.255.255", port);
}

uint32_t UDPSocket::string_to_ip(const std::string& ip_str) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str.c_str(), &addr) == 1) {
        return addr.s_addr;
    }
    return 0;  // Invalid IP
}

std::string UDPSocket::ip_to_string(uint32_t ip_network_byte_order) {
    struct in_addr addr;
    addr.s_addr = ip_network_byte_order;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str);
}
