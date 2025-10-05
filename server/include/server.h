#pragma once
#include "udp_socket.h"
#include "locked_map.h"
#include "packet.h"
#include <mutex>

/// Initial balance assigned to newly discovered clients (prevents negative balances on first transaction)
constexpr uint32_t CLIENT_INITIAL_BALANCE = 100;

/**
 * @brief ### Per-client state maintained by the server.
 * 
 * Tracks request ID for idempotency (duplicate detection) and current balance.
 * Stored in LockedMap with per-entry reader-writer locks for concurrent access.
 */
struct ClientInfo {
    uint32_t last_processed_request_id = 0;		///< Last processed request ID (for duplicate detection)
                                                ///< 0 = no requests processed yet
    uint32_t balance = CLIENT_INITIAL_BALANCE;  ///< Current balance (decremented on send, incremented on receive)
};

/**
 * @brief ### Multi-threaded UDP server implementing the ZIP transaction protocol.
 * 
 * Architecture:
 * - Main thread: listens for incoming packets and spawns worker threads
 * - Worker threads: process requests concurrently (one thread per request)
 * - Shared state: LockedMap (clients) with fine-grained locking per client
 * 
 * Concurrency guarantees:
 * - Multiple transactions can execute in parallel if they involve different clients
 * - Transactions involving the same client(s) are serialized via LockedMap locks
 * - Bank statistics (s_num_transactions, s_total_transferred, s_total_balance) protected by s_stats_mutex
 * 
 * Protocol phases:
 * 1. Discovery: Client broadcasts DISCOVERY, server responds with DISCOVERY_ACK
 * 2. Transactions: Client sends TRANSACTION_REQUEST, server validates and responds with appropriate ACK
 */
class Server {
public:
    /**
     * @brief ### Constructs the Server instance and binds to the specified port.
     * @param port UDP port to listen on (same port for discovery and transactions).
     */
    Server(uint16_t port);

    /**
     * @brief ### Starts the server's main execution loop (blocks indefinitely).
     * 
     * Initializes socket and enters listening loop.
     * Never returns unless socket initialization fails.
     */
    void run();

private:
    // ===== Main Execution =====
    
    /**
     * @brief ### [Main thread] Infinite loop that receives packets and spawns worker threads.
     * 
     * For each incoming packet:
     * 1. Blocks on socket.receive() waiting for next packet
     * 2. Spawns detached thread running process_request()
     * 3. Immediately returns to listening (doesn't wait for thread to finish)
     * 
     * Worker threads handle request processing asynchronously.
     */
    void run_listening_loop();
    
    /**
     * @brief ### [Worker thread] Dispatches request to appropriate handler based on packet type.
     * 
     * Thread lifecycle:
     * 1. Spawned by run_listening_loop() for each incoming packet
     * 2. Detached immediately (no join() required)
     * 3. Processes request and terminates automatically
     * 
     * @param packet The request packet received from client.
     * @param client_addr Client's address (used for sending ACK response).
     */
    void process_request(const Packet& packet, struct sockaddr_in client_addr);

    // ===== Request Handlers =====
    
    /**
     * @brief ### Handles DISCOVERY packet: registers client and sends DISCOVERY_ACK.
     * 
     * Behavior:
     * - If client doesn't exist: inserts into clients map with initial balance
     * - If client exists: does nothing (idempotent)
     * - Always sends DISCOVERY_ACK response (even if client already registered)
     * 
     * @param client_addr Client's IP address (used as key in clients map).
     */
    void handle_discovery(const struct sockaddr_in& client_addr);

    /**
     * @brief ### Handles TRANSACTION_REQUEST: validates, executes, and sends appropriate ACK.
     * 
     * Validation steps:
     * 1. Check if destination client exists -> INVALID_CLIENT_ACK if not
     * 2. Check for duplicate request (request_id <= last_processed_request_id) -> send cached response
     * 3. Check sender has sufficient balance -> INSUFFICIENT_BALANCE_ACK if not
     * 4. Execute transaction atomically (debit sender, credit receiver)
     * 5. Update bank statistics under s_stats_mutex
     * 6. Send TRANSACTION_ACK with new sender balance
     * 
     * Concurrency:
     * - Uses LockedMap::atomic_pair_operation() to lock both sender and receiver
     * - Prevents deadlocks via fixed locking order (lower IP locked first)
     * - Self-transactions (sender == receiver) acquire single lock
     * 
     * @param packet Transaction packet containing destination IP and value.
     * @param client_addr Sender's address (source of funds).
     */
    void handle_transaction(const Packet& packet, const struct sockaddr_in& client_addr);

    // ===== Server State =====
    
    uint16_t port;				///< UDP port for listening (shared for discovery and transactions)
    UDPSocket server_socket;	///< Blocking UDP socket (receive() blocks until packet arrives)

    // ===== Shared State (accessed by multiple worker threads) =====
    
    /// Map of all registered clients, keyed by IP address (network byte order)
    /// Uses fine-grained per-entry locks for concurrent transaction processing
    static LockedMap<uint32_t, ClientInfo> clients;
    
    /// Global bank statistics (protected by s_stats_mutex)
    static uint32_t s_num_transactions;		///< Total transactions processed successfully (excludes duplicates and failures)
    static uint64_t s_total_transferred;  	///< Sum of all transaction values (cumulative, never decreases)
    static uint64_t s_total_balance;      	///< Sum of all client balances (should remain constant = num_clients * INITIAL_BALANCE)

    // ===== Synchronization =====
    
    /// Protects global statistics (s_num_transactions, s_total_transferred, s_total_balance)
    /// Not needed for clients map (LockedMap has internal locking)
    static std::mutex s_stats_mutex;
};
