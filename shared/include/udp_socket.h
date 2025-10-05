#pragma once
#include <cstdint>
#include <mutex>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    #include <netinet/in.h>
    #include <sys/socket.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
#endif

/**
 * @brief ### Cross-platform UDP socket wrapper with thread-safe operations.
 * 
 * Encapsulates platform-specific socket APIs (Winsock on Windows, BSD sockets on Linux).
 * Configured in **non-blocking mode** for receive operations (allows polling).
 * 
 * Thread safety:
 * - send() and receive() are independently thread-safe (separate mutexes)
 * - Multiple threads can send and receive simultaneously
 * - Multiple sends or receives are serialized (one at a time per operation type)
 * 
 * Lifecycle:
 * 1. Construct UDPSocket (default constructor, no resources allocated)
 * 2. Call initialize() to create and bind socket
 * 3. Use send() and receive() for communication
 * 4. Destructor automatically closes socket
 */
class UDPSocket {
public:
    /**
     * @brief ### Default constructor (does not allocate socket).
     * 
     * Socket is created later by initialize().
     * Safe to construct without network availability.
     */
    UDPSocket() = default;

    /**
     * @brief ### Destructor that ensures socket cleanup.
     * 
     * Automatically closes socket if still open (calls close_socket()).
     * Safe to destroy from any thread.
     */
    ~UDPSocket() { close_socket(); }

    /**
     * @brief ### Creates, configures, and binds the UDP socket.
     * 
     * Configuration applied:
     * - Non-blocking mode (receive() returns immediately if no data)
     * - SO_BROADCAST enabled if is_broadcast=true (allows 255.255.255.255)
     * - Binds to INADDR_ANY (0.0.0.0, accepts packets on all interfaces)
     * - Binds to specified port (0 = OS assigns random available port)
     * 
     * On Windows: Initializes Winsock2 on first call (process-wide).
     * 
     * @param port Port number to bind (host byte order). Use 0 for random port assignment.
     * @param is_broadcast True to enable broadcast (required for client discovery phase).
     * @return True if socket created and bound successfully, false on any failure.
     * 
     * Failure reasons:
     * - Port already in use (bind conflict)
     * - Insufficient permissions (ports < 1024 require root/admin)
     * - Network subsystem unavailable
     */
    bool initialize(uint16_t port, bool is_broadcast = false);

    /**
     * @brief ### Sends UDP datagram to specified destination. Thread-safe.
     * 
     * Serializes sends using send_mutex (only one send at a time).
     * Blocks until OS accepts data into send buffer (usually immediate for UDP).
     * Does NOT wait for delivery confirmation (UDP is unreliable).
     * 
     * @param data Pointer to data buffer (must not be nullptr).
     * @param size Number of bytes to send (must not be 0, recommend <= 512 bytes to avoid fragmentation).
     * @param dest_addr Destination address (IP and port in network byte order).
     * @return True if all bytes written to OS send buffer, false on error.
     * 
     * Failure reasons:
     * - Socket not initialized
     * - Network unreachable
     * - Destination port not listening (no error in UDP, packet silently dropped)
     */
    bool send(const void* data, size_t size, const struct sockaddr_in& dest_addr);

    /**
     * @brief ### Receives UDP datagram from socket (non-blocking). Thread-safe.
     * 
     * Serializes receives using receive_mutex (only one receive at a time).
     * Returns immediately if no data available (non-blocking mode).
     * 
     * @param buffer Pointer to receive buffer (must not be nullptr).
     * @param size Maximum bytes to read (recommend >= 512 bytes for full datagrams).
     * @param sender_addr [OUT] Filled with sender's IP and port (network byte order).
     * @return Number of bytes received (0 = no data available, -1 = error, >0 = success).
     * 
     * Return values:
     * - Positive: Number of bytes received (datagram size)
     * - 0: No data available (EWOULDBLOCK/EAGAIN in non-blocking mode)
     * - -1: Socket error (socket closed, invalid buffer, etc.)
     * 
     * Note: UDP datagrams are atomic (receive gets entire datagram or nothing).
     * Truncation occurs silently if buffer too small (data lost).
     */
    int32_t receive(void* buffer, size_t size, struct sockaddr_in& sender_addr);

    /**
     * @brief ### Closes the socket and releases OS resources.
     * 
     * Thread-safe (acquires send_mutex to prevent concurrent operations).
     * Idempotent (safe to call multiple times).
     * Automatically called by destructor if not called manually.
     * 
     * After closing:
     * - send() and receive() will return errors
     * - Can call initialize() again to reopen socket
     */
    void close_socket();

    /**
     * @brief ### Creates a sockaddr_in from IP string and port.
     * 
     * Converts human-readable IP (dotted-decimal) to binary format.
     * Port is specified in host byte order, converted to network byte order.
     * 
     * @param ip String representation of IPv4 address (e.g. "192.168.1.1").
     * @param port Port number in host byte order.
     * @return sockaddr_in structure with sin_family, sin_addr, and sin_port set.
     *   Note: Does not validate IP format (inet_pton does that).
     */
    static struct sockaddr_in create_address(const std::string& ip, uint16_t port);

    /**
     * @brief ### Creates a sockaddr_in for broadcast address on given port.
     * 
     * Sets IP to 255.255.255.255 (limited broadcast).
     * Port is specified in host byte order, converted to network byte order.
     * 
     * @param port Port number in host byte order.
     * @return sockaddr_in structure with sin_family, sin_addr, and sin_port set.
     */
    static struct sockaddr_in create_broadcast_address(uint16_t port);

    /**
     * @brief ### Converts dotted-decimal IP string to 32-bit network byte order.
     * 
     * Uses inet_pton to parse string and convert to binary format.
     * 
     * @param ip_str String representation of IPv4 address (e.g. "192.168.1.1").
     * @return 32-bit IP address in network byte order (host-to-network).
     */
    static uint32_t string_to_ip(const std::string& ip_str);

    /**
     * @brief ### Converts 32-bit IP address to dotted-decimal string.
     * 
     * Uses inet_ntop to convert binary format to human-readable string.
     * 
     * @param ip_network_byte_order IP address in network byte order.
     * @return String representation in dotted notation (e.g., "192.168.1.1").
     */
    static std::string ip_to_string(uint32_t ip_network_byte_order);

private:
    socket_t sock_fd = INVALID_SOCKET_VALUE;  ///< Socket handle (platform-independent)
    
    mutable std::mutex send_mutex;      ///< Serializes send() calls (allows one send at a time)
    mutable std::mutex receive_mutex;   ///< Serializes receive() calls (allows one receive at a time)
                                        ///< send() and receive() can run concurrently (different mutexes)
};
