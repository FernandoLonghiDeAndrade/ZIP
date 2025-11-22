#pragma once
#include "udp_socket.h"
#include "locked_map.h"
#include "client_info.h"
#include "client_packet.h"
#include "server_packet.h"
#include <mutex>

/// Timeout duration for ACK reception before retransmitting a request (milliseconds)
constexpr uint32_t TIMEOUT_MS = 1000;

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
 * - Bank statistics (num_transactions, total_transferred, total_balance) protected by stats_mutex
 * 
 * Protocol phases:
 * 1. Discovery: Client broadcasts CLIENT_DISCOVERY, server responds with CLIENT_DISCOVERY_ACK
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
     * Initializes address and enters listening loop.
     * Never returns unless address initialization fails.
     */
    void run();

private:
    // ===== Main Execution =====

    /**
     * @brief ### Discovers the leader server in the cluster.
     * 
     * Broadcasts discovery packet.
     * Waits for leader's response.
     * Tries 3 times. If no response is received, elects itself as the leader.
     */
    void discover_leader_server();
    
    /**
     * @brief ### [Main thread] Infinite loop that receives packets and spawns worker threads.
     * 
     * For each incoming packet:
     * 1. Blocks on address.receive() waiting for next packet
     * 2. Spawns detached thread running process_client_packet()
     * 3. Immediately returns to listening (doesn't wait for thread to finish)
     * 
     * Worker threads handle request processing asynchronously.
     */
    void run_listening_loop();
    
    // ===== Client Packet Handlers =====

    /**
     * @brief ### [Worker thread] Dispatches packet to appropriate handler based on packet type.
     * 
     * Thread lifecycle:
     * 1. Spawned by run_listening_loop() for each incoming packet
     * 2. Detached immediately (no join() required)
     * 3. Processes packet and terminates automatically
     * 
     * @param packet The packet received.
     * @param client_addr Address of the sender (used for sending ACK response).
     */
    void process_client_packet(const ClientPacket& packet, const SocketAddress& client_addr);
    
    /**
     * @brief ### Handles client's CLIENT_DISCOVERY packet: registers client and sends CLIENT_DISCOVERY_ACK.
     * 
     * Behavior:
     * - If client doesn't exist: inserts into clients map with initial balance
     * - If client exists: does nothing (idempotent)
     * - Always sends CLIENT_DISCOVERY_ACK response (even if client already registered)
     * 
     * @param client_addr Client's IP address (used as key in clients map).
     */
    void handle_client_discovery(const SocketAddress& client_addr);

    /**
     * @brief ### Handles TRANSACTION_REQUEST: validates, executes, and sends appropriate ACK.
     * 
     * Validation steps:
     * 1. Check if destination client exists -> INVALID_CLIENT_ACK if not
     * 2. Check for duplicate request (request_id <= last_processed_request_id) -> send cached response
     * 3. Check sender has sufficient balance -> INSUFFICIENT_BALANCE_ACK if not
     * 4. Execute transaction atomically (debit sender, credit receiver)
     * 5. Update bank statistics under stats_mutex
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
    void handle_transaction(const ClientPacket& packet, const SocketAddress& client_addr);

    // ===== Server Packet Handlers =====

    // Leader Server
    void process_server_packet(const ServerPacket& packet, const SocketAddress& server_addr);

    // Backup Server
    void request_leader_state();

    // Backup Server
    void receive_leader_state(bool is_from_discovery);
    
    // Leader Server
    void handle_state_sync_request(const SocketAddress& server_addr);
    
    // Leader Server
    void send_state_sync(const SocketAddress& server_addr, std::atomic<bool>& cancel, std::atomic<bool>& finished);

    // Backup Server
    void handle_new_server_sync(const ServerPacket& packet);

    // Backup Server
    void handle_new_client_sync(const ServerPacket& packet);

    // Backup Server
    void handle_new_transaction_sync(const ServerPacket& packet);

    // ===== Leader Election =====

    // Backup Server
    void ping_leader();

    // Leader Server
    void handle_ping_leader(const SocketAddress& server_addr);

    // Backup Server
    void start_election();

    void handle_election_request(const SocketAddress& server_addr);

    void handle_coordinator(const SocketAddress& server_addr);

    // ===== Server State =====
    
    uint16_t port;				///< UDP port for listening (shared for discovery and transactions)
    UDPSocket server_socket;	///< Blocking UDP address (receive() blocks until packet arrives)

    // ===== Shared State (accessed by multiple worker threads) =====
    
    /// Map of all registered clients, keyed by IP address (network byte order)
    /// Uses fine-grained per-entry locks for concurrent transaction processing
    LockedMap<uint32_t, ClientInfo> clients;
    
    /// Global bank statistics (protected by stats_mutex)
    uint32_t num_transactions;	///< Total transactions processed successfully (excludes duplicates and failures)
    uint64_t total_transferred; ///< Sum of all transaction values (cumulative, never decreases)
    uint64_t total_balance;     ///< Sum of all client balances (should remain constant = num_clients * INITIAL_BALANCE)

    // ===== Synchronization =====
    
    /// Protects global statistics (num_transactions, total_transferred, total_balance)
    /// Not needed for clients map (LockedMap has internal locking)
    std::mutex stats_mutex;

    // ===== Backup Servers =====

    uint32_t seq_number;                ///< Server's current sequence number (increments on state changes)
    std::vector<SocketAddress> servers; ///< List of addresses of known servers in the cluster
    SocketAddress leader_addr;          ///< Identifies the address of the leader server
};
