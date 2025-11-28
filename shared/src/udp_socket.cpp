#include "udp_socket.h"
#include <cstring>
#include <chrono>
#include <iostream>

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

uint32_t UDPSocket::ip() const {
    if (sock_fd == INVALID_SOCKET_VALUE) return 0;

    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    
    // Tenta ler o endereço bound (será 0.0.0.0 se INADDR_ANY)
    if (getsockname(sock_fd, (struct sockaddr*)&local_addr, &addr_len) < 0) return 0;
    
    // Se bound em INADDR_ANY, descobre o IP de saída usando connect trick
    if (local_addr.sin_addr.s_addr == INADDR_ANY) {
        int tmp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (tmp_fd != INVALID_SOCKET_VALUE) {
            struct sockaddr_in remote{};
            remote.sin_family = AF_INET;
            remote.sin_port = htons(53);
            if (inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr) == 1) {
                connect(tmp_fd, (struct sockaddr*)&remote, sizeof(remote));
                struct sockaddr_in outgoing{};
                socklen_t len = sizeof(outgoing);
                if (getsockname(tmp_fd, (struct sockaddr*)&outgoing, &len) == 0) {
                    local_addr.sin_addr = outgoing.sin_addr;
                }
            }
            close_socket_impl(tmp_fd);
        }
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
    
    if (local_addr.sin_addr.s_addr == INADDR_ANY) {
        int tmp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (tmp_fd != INVALID_SOCKET_VALUE) {
            struct sockaddr_in remote{};
            remote.sin_family = AF_INET;
            remote.sin_port = htons(53);
            if (inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr) == 1) {
                connect(tmp_fd, (struct sockaddr*)&remote, sizeof(remote));
                struct sockaddr_in outgoing{};
                socklen_t len = sizeof(outgoing);
                if (getsockname(tmp_fd, (struct sockaddr*)&outgoing, &len) == 0) {
                    local_addr.sin_addr = outgoing.sin_addr;
                }
            }
            close_socket_impl(tmp_fd);
        }
    }
    
    return SocketAddress(local_addr);
}

bool UDPSocket::initialize(uint16_t port, bool is_broadcast) {
    init_winsock();
    
    socket_id = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() ^ port
    );
    
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd == INVALID_SOCKET_VALUE) return false;

    if (is_broadcast) {
        int broadcast_enable = 1;
        setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast_enable, sizeof(broadcast_enable));
    }

    struct sockaddr_in bind_addr {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    bind_addr.sin_addr.s_addr = INADDR_ANY;  // SEMPRE INADDR_ANY para receber broadcasts

    if (bind(sock_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        close_socket();
        return false;
    }

    return true;
}

// ===== Send data =====

bool UDPSocket::send(const void* data, size_t size, const SocketAddress& dest_addr) {
    if (!data || size == 0 || sock_fd == INVALID_SOCKET_VALUE) return false;

    std::lock_guard<std::mutex> lock(send_mutex);

    // Prepend socket_id to data (4 bytes header + original data)
    uint8_t send_buffer[sizeof(socket_id) + size];
    memcpy(send_buffer, &socket_id, sizeof(socket_id));
    memcpy(send_buffer + sizeof(socket_id), data, size);

    const struct sockaddr_in& native_addr = dest_addr.native();
    ssize_t sent_bytes = sendto(sock_fd, (const char*)send_buffer, sizeof(send_buffer), 0, 
                               (const struct sockaddr*)&native_addr, sizeof(native_addr));
    
    // Return true if all bytes sent (including header)
    return sent_bytes == static_cast<ssize_t>(sizeof(send_buffer));
}

// ===== Receive data =====

int32_t UDPSocket::receive(void* buffer, size_t size, SocketAddress& sender_addr, int32_t timeout_ms) {
    if (!buffer || size == 0 || sock_fd == INVALID_SOCKET_VALUE) return -1;

    std::lock_guard<std::mutex> lock(receive_mutex);
    
    // Track absolute timeout only for timed operations
    auto start_time = (timeout_ms >= 0) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    
    while (true) {  // Loop to skip loopback packets
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock_fd, &read_fds);

        std::cout << "DEBUG: Waiting for data with timeout_ms = " << timeout_ms << " ms" << std::endl;

        struct timeval tv, *tv_ptr = nullptr;
        if (timeout_ms >= 0) {
            // Timed operation: calculate remaining timeout
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            int32_t remaining_ms = timeout_ms - static_cast<int32_t>(elapsed);

            // If timeout already expired, return immediately
            if (remaining_ms <= 0) return 0;
            
            tv.tv_sec = remaining_ms / 1000;
            tv.tv_usec = (remaining_ms % 1000) * 1000;
            tv_ptr = &tv;
        }
        // For timeout_ms < 0: tv_ptr remains nullptr = blocking operation
        
        int select_result;
        #ifdef _WIN32
            select_result = select(0, &read_fds, nullptr, nullptr, tv_ptr);
        #else
            select_result = select(sock_fd + 1, &read_fds, nullptr, nullptr, tv_ptr);
        #endif
        
        // Handle select results properly
        if (select_result < 0) {
            return -1;  // Error occurred
        } else if (select_result == 0) {
            // Timeout occurred - only possible when timeout_ms >= 0
            return 0;
        }
        // select_result > 0: data is available
        
        // Receive into temporary buffer (extract socket_id header)
        uint8_t recv_buffer[sizeof(socket_id) + size];
        struct sockaddr_in native_addr;
        socklen_t addr_len = sizeof(native_addr);
        ssize_t bytes = recvfrom(sock_fd, (char*)recv_buffer, sizeof(recv_buffer), 0, 
                                (struct sockaddr*)&native_addr, &addr_len);
        
        // Packet must contain at least the socket_id header
        if (bytes < static_cast<ssize_t>(sizeof(socket_id))) return -1;
        
        // Extract sender's socket_id from packet header
        uint32_t sender_socket_id;
        memcpy(&sender_socket_id, recv_buffer, sizeof(socket_id));
        
        // Filter loopback: ignore packets from same socket_id (own broadcasts)
        if (sender_socket_id == socket_id) {
            continue;  // Skip and wait for next packet
        }
        
        // Valid packet: copy payload (without header) to user buffer
        size_t payload_size = bytes - sizeof(socket_id);
        size_t copy_size = std::min(payload_size, size); // Prevent buffer overflow
        memcpy(buffer, recv_buffer + sizeof(socket_id), copy_size);
        sender_addr = SocketAddress(native_addr);
        return static_cast<int32_t>(copy_size);
    }
}

// ===== Close address =====

void UDPSocket::close_socket() {
    std::lock_guard<std::mutex> lock(send_mutex);
    
    if (sock_fd != INVALID_SOCKET_VALUE) {
        close_socket_impl(sock_fd);
        sock_fd = INVALID_SOCKET_VALUE;
    }
}
