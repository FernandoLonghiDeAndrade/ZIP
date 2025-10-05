#include "udp_socket.h"
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
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
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <arpa/inet.h>
#endif

// ===== Socket initialization =====

bool UDPSocket::initialize(uint16_t port, bool is_broadcast) {
#ifdef _WIN32
    // Windows: Initialize Winsock library (process-wide, idempotent)
    init_winsock();
    
    // Create UDP socket (AF_INET = IPv4, SOCK_DGRAM = UDP)
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd == INVALID_SOCKET) {
        return false;
    }

    // Set non-blocking mode (receive returns immediately if no data)
    u_long mode = 1;  // 1 = non-blocking, 0 = blocking
    if (ioctlsocket(sock_fd, FIONBIO, &mode) != 0) {
        close_socket();
        return false;
    }
#else
    // Linux: Create UDP socket
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        return false;
    }

    // Set non-blocking mode using fcntl (POSIX approach)
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags < 0) {
        close_socket();
        return false;
    }
    if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close_socket();
        return false;
    }
#endif

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
    if (!data || size == 0) {
        return false;
    }

    // Serialize sends (only one thread can send at a time)
    // Note: Doesn't block receives (separate mutex)
    std::lock_guard<std::mutex> lock(send_mutex);

#ifdef _WIN32
    if (sock_fd == INVALID_SOCKET) {
        return false;
    }
    
    // Windows: sendto() expects char* and int size
    int sent_bytes = sendto(sock_fd, (const char*)data, (int)size, 0, 
                           (const struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (sent_bytes == SOCKET_ERROR) {
        return false;
    }
#else
    if (sock_fd < 0) {
        return false;
    }
    
    // POSIX: sendto() expects void* and size_t
    ssize_t sent_bytes = sendto(sock_fd, data, size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (sent_bytes < 0) {
        return false;
    }
#endif

    // Verify all bytes were sent (should always be true for UDP, or fails completely)
    return sent_bytes == static_cast<ssize_t>(size);
}

// ===== Receive data =====

int32_t UDPSocket::receive(void* buffer, size_t size, struct sockaddr_in& sender_addr) {
    // Validate input parameters
    if (!buffer || size == 0) {
        return -1;
    }

    // Serialize receives (only one thread can receive at a time)
    // Note: Doesn't block sends (separate mutex)
    std::lock_guard<std::mutex> lock(receive_mutex);

#ifdef _WIN32
    if (sock_fd == INVALID_SOCKET) {
        return -1;
    }
    
    socklen_t addr_len = sizeof(sender_addr);
    int received_bytes = recvfrom(sock_fd, (char*)buffer, (int)size, 0, 
                                 (struct sockaddr*)&sender_addr, &addr_len);
    
    if (received_bytes == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            // No data available (non-blocking mode, not an error)
            return 0;
        }
        // Real error (socket closed, network error, etc.)
        return -1;
    }
#else
    if (sock_fd < 0) {
        return -1;
    }
    
    socklen_t addr_len = sizeof(sender_addr);
    ssize_t received_bytes = recvfrom(sock_fd, buffer, size, 0, (struct sockaddr*)&sender_addr, &addr_len);
    
    if (received_bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available (non-blocking mode, not an error)
            return 0;
        }
        // Real error (socket closed, network error, etc.)
        return -1;
    }
#endif
    
    // Return number of bytes received (UDP datagram size)
    // Note: If buffer too small, datagram is truncated and excess data is lost
    return static_cast<int32_t>(received_bytes);
}

// ===== Close socket =====

void UDPSocket::close_socket() {
    // Acquire send_mutex to prevent concurrent send operations during close
    // (receive_mutex not needed since closing invalidates socket for both)
    std::lock_guard<std::mutex> lock(send_mutex);
    
#ifdef _WIN32
    if (sock_fd != INVALID_SOCKET) {
        closesocket(sock_fd);               // Windows-specific close function
        sock_fd = INVALID_SOCKET;           // Mark as closed (prevents double-close)
    }
#else
    if (sock_fd >= 0) {
        close(sock_fd);                     // POSIX close function
        sock_fd = -1;                       // Mark as closed (prevents double-close)
    }
#endif
}
