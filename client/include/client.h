#pragma once
#include "udp_socket.h"
#include "packet.h"
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

/// Timeout duration for ACK reception before retransmitting a request (milliseconds)
constexpr uint32_t ACK_TIMEOUT_MS = 200;

/**
 * @brief ### UDP client implementing stop-and-wait ARQ protocol for reliable communication.
 * 
 * The client operates with two threads:
 * - Main thread: handles user input and sends requests
 * - Network thread: listens for and processes server responses
 * 
 * Discovery phase: broadcasts UDP packets until a server responds with DISCOVERY_ACK.
 * Transaction phase: sends requests with automatic retransmission until ACK is received.
 */
class Client {
public:
    /**
     * @brief ### Constructs a Client instance.
     * @param server_port Port number where the server listens (same for discovery and transactions).
     * @param server_ip Optional server IP address. If empty, client performs broadcast discovery.
     */
    Client(uint16_t server_port, const std::string& server_ip = "");

    /**
     * @brief ### Starts client execution: discovers server, spawns network thread, handles user input.
     * 
     * This method blocks until the user exits. It coordinates:
     * 1. Server discovery (broadcast or direct connection)
     * 2. Network thread spawn (handles responses)
     * 3. User input loop (main thread sends requests)
     */
    void run();

private:
    // ===== Server Discovery =====
    
    /**
     * @brief ### Broadcasts DISCOVERY packets until a server responds.
     * 
     * Sends to broadcast address (255.255.255.255) with exponential backoff.
     * Blocks until DISCOVERY_ACK is received, then stores server address.
     */
    void discover_server();

    /**
     * @brief ### Connects to a known server IP without broadcast discovery.
     * 
     * Sends DISCOVERY packets directly to the specified IP address.
     * Blocks until DISCOVERY_ACK is received from that server.
     */
    void connect_to_known_server();

    // ===== Main Execution Loops =====
    
    /**
     * @brief ### [Main thread] Reads user input and sends transaction requests.
     * 
     * Format: <destination_ip> <value>
     * Validates input, creates TRANSACTION_REQUEST packets, calls send_request().
     * Runs indefinitely until program termination.
     */
    void run_user_input_loop();

    /**
     * @brief ### [Network thread] Listens for server responses and processes ACKs.
     * 
     * Runs in infinite loop:
     * 1. Blocks on socket.receive() waiting for packets
     * 2. Checks if response matches pending_ack_request_id
     * 3. If match: stops retransmission, prints result, notifies main thread
     * 4. If no match: ignores packet (duplicate or out-of-order)
     */
    void handle_server_responses();

    // ===== Request Transmission =====
    
    /**
     * @brief ### Sends a request with stop-and-wait retransmission until ACK is received.
     * 
     * Stop-and-wait protocol:
     * 1. Sets pending_ack_request_id to packet's ID  
     * 2. Sends packet to server  
     * 3. Waits ACK_TIMEOUT_MS for ACK (blocking with condition variable)
     * 4. If timeout: retransmits (goto step 2)
     * 5. If ACK received: network thread clears pending_ack_request_id and notifies
     * 6. Exits when pending_ack_request_id == 0
     * 
     * @param packet The request packet to send (TRANSACTION_REQUEST).
     */
    void send_request(const Packet& packet);

    // ===== Server Connection State =====
    
    UDPSocket client_socket;                ///< UDP socket with broadcast capability enabled
    SocketAddress server_addr;              ///< Server's address (populated during discovery phase)
    bool has_server_address;                ///< True after DISCOVERY_ACK received, false otherwise
    uint32_t next_request_id;               ///< Monotonically increasing ID for outgoing requests (starts at 1)

    // ===== Threading =====
    
    std::thread network_thread;             ///< Background thread running handle_server_responses()

    // ===== Stop-and-Wait Synchronization =====
    // Coordinates between main thread (sender) and network thread (ACK receiver)
    
    std::mutex pending_request_mutex;				///< Protects shared state accessed by both threads
    std::condition_variable ack_received_cv;		///< Signals when ACK arrives (wakes up send_request())
    std::atomic<uint32_t> pending_ack_request_id;	///< Request ID waiting for ACK (0 = none pending)
													///< Atomic allows lock-free reads in network thread hot path
    Packet pending_request_packet;                  ///< Copy of current request (used for retransmission and printing results)
};
