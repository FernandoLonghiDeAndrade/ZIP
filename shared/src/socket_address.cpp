#include "socket_address.h"
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

SocketAddress::SocketAddress(uint32_t ip_host_order, uint16_t port_host_order) {
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_host_order);
    addr.sin_addr.s_addr = htonl(ip_host_order);
}

SocketAddress::SocketAddress(const struct sockaddr_in& addr) : addr(addr) {}

SocketAddress SocketAddress::broadcast(uint16_t port_host_order) {
    return SocketAddress("255.255.255.255", port_host_order);
}

std::string SocketAddress::ip_string() const {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str);
}

uint32_t SocketAddress::ip() const {
    return ntohl(addr.sin_addr.s_addr);
}

uint16_t SocketAddress::port() const {
    return ntohs(addr.sin_port);
}

bool SocketAddress::is_valid() const {
    return addr.sin_addr.s_addr != 0;
}
