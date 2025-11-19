#pragma once
#include <cstdint>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <netinet/in.h>
#endif

/**
 * @brief ### Represents a network address (IP + port) in a cross-platform way.
 * 
 * Encapsulates sockaddr_in to hide platform-specific address address structure.
 * Immutable after construction (thread-safe by design).
 * 
 * Usage:
 * - Create from IP string and port: SocketAddress("192.168.1.1", 8080)
 * - Create broadcast address: SocketAddress::broadcast(8080)
 * - Extract IP as string: addr.ip_string()
 * - Extract port: addr.port()
 * - Get underlying sockaddr_in: addr.native() (for internal UDPSocket use only)
 */
class SocketAddress {
public:
    /**
     * @brief ### Creates address from IP string and port.
     * 
     * @param ip IPv4 address in dotted-decimal notation (e.g., "192.168.1.1").
     * @param port Port number in host byte order.
     */
    SocketAddress(const std::string& ip, uint16_t port = 0);
    
    /**
     * @brief ### Creates address from raw IP (network byte order) and port.
     * 
     * @param ip_network_byte_order 32-bit IP in network byte order.
     * @param port Port number in host byte order.
     */
    SocketAddress(uint32_t ip_network_byte_order, uint16_t port = 0);
    
    /**
     * @brief ### Creates address from sockaddr_in (internal use by UDPSocket).
     * 
     * @param addr Platform-specific address address structure.
     */
    explicit SocketAddress(const struct sockaddr_in& addr);
    
    /**
     * @brief ### Default constructor (creates invalid address 0.0.0.0:0).
     */
    SocketAddress();

    /**
     * @brief ### Creates broadcast address (255.255.255.255) on given port.
     * 
     * @param port Port number in host byte order.
     * @return SocketAddress configured for broadcast.
     */
    static SocketAddress broadcast(uint16_t port);
    
    /**
     * @brief ### Returns IP address as human-readable string.
     * 
     * @return String in dotted-decimal format (e.g., "192.168.1.1").
     */
    std::string ip_string() const;
    
    /**
     * @brief ### Returns IP address as 32-bit integer (network byte order).
     * 
     * @return IP in network byte order (use htonl/ntohl for conversion).
     */
    uint32_t ip() const;
    
    /**
     * @brief ### Returns port number (host byte order).
     * 
     * @return Port number as seen by application (already converted from network order).
     */
    uint16_t port() const;
    
    /**
     * @brief ### Returns underlying sockaddr_in (internal use by UDPSocket only).
     * 
     * @return Reference to platform-specific address address structure.
     */
    const struct sockaddr_in& native() const { return addr; }
    
    /**
     * @brief ### Checks if address is valid (not 0.0.0.0:0).
     * 
     * @return True if IP is non-zero, false otherwise.
     */
    bool is_valid() const;

private:
    struct sockaddr_in addr;  ///< Underlying platform-specific address structure
};