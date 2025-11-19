#pragma once
#include <mutex>
#include "socket_address.h"

#ifdef _WIN32
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    #include <sys/socket.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
#endif

/**
 * @brief ### Cross-platform UDP address wrapper with thread-safe operations.
 * 
 * Encapsulates platform-specific address APIs (Winsock on Windows, BSD sockets on Linux).
 * Configured in **non-blocking mode** for receive operations (allows polling).
 * 
 * Thread safety:
 * - send() and receive() are independently thread-safe (separate mutexes)
 * - Multiple threads can send and receive simultaneously
 * - Multiple sends or receives are serialized (one at a time per operation type)
 * 
 * Lifecycle:
 * 1. Construct UDPSocket (default constructor, no resources allocated)
 * 2. Call initialize() to create and bind address
 * 3. Use send() and receive() for communication
 * 4. Destructor automatically closes address
 */
class UDPSocket {
public:
    /**
     * @brief ### Default constructor (does not allocate address).
     * 
     * Socket is created later by initialize().
     * Safe to construct without network availability.
     */
    UDPSocket() = default;

    /**
     * @brief ### Destructor that ensures address cleanup.
     * 
     * Automatically closes address if still open (calls close_socket()).
     * Safe to destroy from any thread.
     */
    ~UDPSocket() { close_socket(); }

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
     * @brief ### Returns SocketAddress representing this address's bound address.
     * 
     * @return SocketAddress with IP and port of this address.
     */
    SocketAddress address() const;

    /**
     * @brief ### Creates, configures, and binds the UDP address.
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
     * @return True if address created and bound successfully, false on any failure.
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
     * @param dest_addr Destination address (IP and port).
     * @return True if all bytes written to OS send buffer, false on error.
     * 
     * Failure reasons:
     * - Socket not initialized
     * - Network unreachable
     * - Destination port not listening (no error in UDP, packet silently dropped)
     */
    bool send(const void* data, size_t size, const SocketAddress& dest_addr);

    /**
     * @brief ### Receives UDP datagram from address (non-blocking). Thread-safe.
     * 
     * Serializes receives using receive_mutex (only one receive at a time).
     * Returns when the timeout expires or data is received.
     * 
     * @param buffer Pointer to receive buffer (must not be nullptr).
     * @param size Maximum bytes to read (recommend >= 512 bytes for full datagrams).
     * @param sender_addr [OUT] Filled with sender's IP and port.
     * @param timeout_ms Maximum time to wait for data (0 = no wait, non-blocking).
     * @return Number of bytes received (0 = no data available, -1 = error, >0 = success).
     * 
     * Return values:
     * - Positive: Number of bytes received (datagram size)
     * - 0: No data available (EWOULDBLOCK/EAGAIN in non-blocking mode)
     * - -1: Socket error (address closed, invalid buffer, etc.)
     * 
     * Note: UDP datagrams are atomic (receive gets entire datagram or nothing).
     * Truncation occurs silently if buffer too small (data lost).
     */
    int32_t receive(void* buffer, size_t size, SocketAddress& sender_addr, uint32_t timeout_ms = 0);

    /**
     * @brief ### Closes the address and releases OS resources.
     * 
     * Thread-safe (acquires send_mutex to prevent concurrent operations).
     * Idempotent (safe to call multiple times).
     * Automatically called by destructor if not called manually.
     * 
     * After closing:
     * - send() and receive() will return errors
     * - Can call initialize() again to reopen address
     */
    void close_socket();

private:
    socket_t sock_fd = INVALID_SOCKET_VALUE;  ///< Socket handle (platform-independent)
    
    mutable std::mutex send_mutex;      ///< Serializes send() calls (allows one send at a time)
    mutable std::mutex receive_mutex;   ///< Serializes receive() calls (allows one receive at a time)
                                        ///< send() and receive() can run concurrently (different mutexes)
};
