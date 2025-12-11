#pragma once
#include "udp_socket.h"
#include "locked_map.h"
#include "client_info.h"
#include "client_packet.h"
#include "server_packet.h"
#include <mutex>

/// Timeout duration for ACK reception before retransmitting a request (milliseconds)
constexpr int32_t TIMEOUT_MS = 100;

/**
 * @brief ### Multi-threaded distributed UDP server implementing the ZIP transaction protocol.
 * 
 * Threading Architecture:
 * - Main thread: listens for incoming packets and spawns worker threads
 * - Worker threads: process requests concurrently (one thread per request)
 * - Background thread: periodic leader health checks (backups only)
 * - Shared state: LockedMap (clients) with fine-grained locking per client
 * 
 * Distributed Architecture:
 * - Leader-Backup model: One leader handles client requests, backups replicate state
 * - Leader election: Bully algorithm using IP address as priority (lower IP = higher priority)
 * - State replication: Leader broadcasts all state changes to backup servers
 * - Failure detection: Backups ping leader periodically, trigger election on timeout
 * - Automatic failover: New leader elected when current leader fails
 * - Client redirection: Leader notifies clients (NEW_LEADER packet) after election
 * 
 * Concurrency Guarantees:
 * - Multiple transactions can execute in parallel if they involve different clients
 * - Transactions involving the same client(s) are serialized via LockedMap locks
 * - Bank statistics (num_transactions, total_transferred, total_balance) protected by stats_mutex
 * - State replication synchronized via sequence numbers (seq_number)
 * 
 * Protocol Phases:
 * 1. Cluster Discovery: New server broadcasts to find leader, syncs full state
 * 2. Client Discovery: Client broadcasts CLIENT_DISCOVERY, leader responds and replicates to backups
 * 3. Transactions: Client sends TRANSACTION_REQUEST to leader, leader processes and replicates
 * 4. Leader Monitoring: Backups ping leader every TIMEOUT_MS, initiate election on failure
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

    /**
     * @brief ### Dispatches server packet to appropriate handler based on packet type.
     * 
     * Handles server-to-server communication for cluster coordination:
     * - STATE_SYNC_REQUEST: New server joining cluster
     * - NEW_SERVER_SYNC: Leader notifies about new server in cluster
     * - NEW_CLIENT_SYNC: Leader notifies about new client registration
     * - NEW_TRANSACTION_SYNC: Leader replicates transaction to backups
     * - PING_LEADER: Backup checks if leader is alive
     * - ELECTION_REQUEST: Bully algorithm election initiation
     * - COORDINATOR: New leader announcement
     * 
     * @param packet The server packet received.
     * @param server_addr Address of the sending server.
     */
    void process_server_packet(const ServerPacket& packet, const SocketAddress& server_addr);

    /**
     * @brief ### [Backup Server] Requests full state synchronization from leader.
     * 
     * Sends STATE_SYNC_REQUEST to leader and waits for STATE_SYNC_ACK response.
     * If no response within timeout, retries discovery process.
     * 
     * Called when:
     * - State sync fails or times out
     * - Gap detected in sequence numbers (missing updates)
     */
    void request_leader_state();

    /**
     * @brief ### [Backup Server] Receives and applies full state from leader server.
     * 
     * Receives state components in sequence:
     * 1. Server list (SERVER_INFO packets)
     * 2. Client data (CLIENT_INFO packets)
     * 3. Statistics (SEQ_AND_STATS packet)
     * 
     * Buffers all state until complete, then atomically updates local state.
     * This prevents serving stale/partial state during synchronization.
     * 
     * @param is_from_discovery True if called during initial discovery, false if called for resync.
     */
    void receive_leader_state(bool is_from_discovery);
    
    /**
     * @brief ### [Leader Server] Handles state sync request from new/recovering server.
     * 
     * Spawns background thread to send full state while main thread listens for
     * retransmissions (STATE_SYNC_REQUEST from same server indicates packet loss).
     * 
     * If retransmission detected:
     * - Cancels current sync thread
     * - Restarts sync from beginning
     * 
     * Thread sends:
     * 1. STATE_SYNC_ACK (acknowledgment)
     * 2. SERVER_INFO for each known server
     * 3. CLIENT_INFO for each registered client
     * 4. SEQ_AND_STATS (statistics and sequence number)
     * 
     * @param server_addr Address of the requesting server.
     */
    void handle_state_sync_request(const SocketAddress& server_addr);
    
    /**
     * @brief ### [Leader Server] Background thread that sends full state to requesting server.
     * 
     * Periodically checks cancel flag to abort if retransmission detected.
     * Sets finished flag when complete to signal main thread.
     * 
     * @param server_addr Destination server address.
     * @param cancel Shared flag to signal thread cancellation (set by main thread).
     * @param finished Shared flag to signal completion (set by this thread).
     */
    void send_state_sync(const SocketAddress& server_addr, std::atomic<bool>& cancel, std::atomic<bool>& finished);

    /**
     * @brief ### [Backup Server] Handles NEW_SERVER_SYNC: adds newly joined server to cluster list.
     * 
     * Leader broadcasts this when new server joins cluster.
     * Updates local servers vector with new server address.
     * 
     * @param packet Packet containing new server's address.
     */
    void handle_new_server_sync(const ServerPacket& packet);

    /**
     * @brief ### [Backup Server] Handles NEW_CLIENT_SYNC: replicates new client registration.
     * 
     * Leader broadcasts this when new client performs CLIENT_DISCOVERY.
     * Adds client to local clients map with initial balance.
     * Updates total_balance statistic.
     * 
     * @param packet Packet containing client IP and initial state.
     */
    void handle_new_client_sync(const ServerPacket& packet);

    /**
     * @brief ### [Backup Server] Handles NEW_TRANSACTION_SYNC: replicates transaction from leader.
     * 
     * Leader broadcasts this after successfully processing each transaction.
     * Applies transaction atomically to local state (updates balances).
     * Updates statistics (num_transactions, total_transferred).
     * 
     * Maintains eventual consistency: all backups replay same transactions in same order.
     * 
     * @param packet Packet containing transaction details (sender, receiver, amount).
     */
    void handle_new_transaction_sync(const ServerPacket& packet);

    // ===== Leader Election =====

    /**
     * @brief ### [Backup Server] Sends periodic heartbeat to check if leader is alive.
     * 
     * Called periodically by background ping thread (every TIMEOUT_MS).
     * 
     * Behavior:
     * 1. Sends PING_LEADER to current leader
     * 2. Waits for PING_LEADER_ACK response with timeout
     * 3. If timeout (no response): assumes leader failed, initiates election
     * 
     * Implements failure detection for automatic leader failover.
     */
    void ping_leader();

    /**
     * @brief ### [Leader Server] Responds to heartbeat ping from backup server.
     * 
     * Simply sends PING_LEADER_ACK back to sender to confirm leader is alive.
     * 
     * @param server_addr Address of the backup server sending the ping.
     */
    void handle_ping_leader(const SocketAddress& server_addr);

    /**
     * @brief ### [Backup Server] Initiates Bully algorithm leader election.
     * 
     * Called when:
     * - Leader ping timeout (leader presumed dead)
     * - This server has lower ID than current leader (during discovery)
     * 
     * Bully algorithm steps:
     * 1. Send ELECTION_REQUEST to all servers with higher ID
     * 2. If any server responds ELECTION_REQUEST_ACK: wait for COORDINATOR
     * 3. If no responses (all higher-ID servers down): become leader
     * 4. Broadcast COORDINATOR message announcing new leadership
     * 
     * Uses server IP as ID: lower IP = higher priority (older servers win).
     */
    void start_election();

    /**
     * @brief ### Handles ELECTION_REQUEST from another server in Bully algorithm.
     * 
     * If my ID > requester's ID:
     * - Send ELECTION_REQUEST_ACK (taking over election)
     * - Start my own election (I have higher priority)
     * 
     * If my ID < requester's ID:
     * - Ignore (requester has higher priority, let them handle it)
     * 
     * @param server_addr Address of the server requesting election.
     */
    void handle_election_request(const SocketAddress& server_addr);

    /**
     * @brief ### Handles COORDINATOR announcement: updates local leader reference.
     * 
     * Receives COORDINATOR broadcast from newly elected leader.
     * Updates leader_addr to point to new leader.
     * 
     * If this server was waiting for election result, this message
     * tells it who won and became the new leader.
     * 
     * @param server_addr Address of the new leader server.
     */
    void handle_coordinator(const SocketAddress& server_addr);

    // ===== Utility Functions =====

    /**
     * @brief ### Receives server packet with timeout, filtering by expected types.
     * 
     * Loops until receiving packet of expected type or timeout occurs.
     * 
     * Behavior:
     * - Blocks up to timeout_ms waiting for packet
     * - If received packet type matches expected_types: returns true
     * - If received packet type is unexpected: calls handler and continues waiting
     * - If timeout expires before receiving expected type: returns false
     * 
     * Useful for request-response patterns where only specific packet types
     * should be processed, but other packets may arrive (e.g., pings, broadcasts).
     * 
     * @param packet [out] The received packet (only valid if return value is true).
     * @param server_addr [out] Address of the server that sent the packet.
     * @param expected_types List of acceptable packet types to return.
     * @param timeout_ms Maximum time to wait in milliseconds.
     * @param unexpected_types_handler Callback for handling unexpected packets (default: ignore).
     * @return true if packet of expected type received, false on timeout.
     */
    bool receive_server_packet(
        ServerPacket& packet,
        SocketAddress& server_addr,
        std::vector<ServerPacketType> expected_types,
        int32_t timeout_ms,
        std::function<void(const ServerPacket&, const SocketAddress&)> unexpected_types_handler = [](const ServerPacket&, const SocketAddress&){}
    );

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
