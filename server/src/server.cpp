#include "server.h"
#include "print_utils.h"
#include <iostream>
#include <optional>
#include <thread>

// ===== Static member initialization =====

LockedMap<uint32_t, ClientInfo> Server::clients;
uint32_t Server::s_num_transactions = 0;
uint64_t Server::s_total_transferred = 0;
uint64_t Server::s_total_balance = 0;
std::mutex Server::s_stats_mutex;

// ===== Constructor =====

Server::Server(uint16_t port) : port(port) {
    // Bind socket to port (throws if port already in use or permission denied)
    if (!server_socket.initialize(port, true)) {
        throw std::runtime_error("Failed to initialize UDP socket");
    }

    this->is_leader = true;          // Assume leader role at startup
    this->sequence_counter = 0;      // Start sequence counter at 0
    this->server_id = 0;             // Unique server ID
}

// ===== Main execution =====

void Server::run() {
    // Print initial state (empty bank at startup)
    PrintUtils::print_server_state(s_num_transactions, s_total_transferred, s_total_balance);

    // Discover backup servers at startup
    discover_backup_servers();
    
    // Enter infinite listening loop (never returns)
    run_listening_loop();
}

void Server::run_listening_loop() {
    SocketAddress client_addr;
    Packet packet;
    
    while (true) {
        // Blocking receive: waits indefinitely for next packet
        int32_t bytes_received = server_socket.receive(&packet, sizeof(packet), client_addr);
        
        // Validate packet size (prevents processing truncated/malformed packets)
        // Process valid packets in separate detached threads for concurrency
        if (bytes_received == sizeof(packet)) {
            // Spawn worker thread: processes request and terminates automatically
            // Detached: main thread doesn't wait for completion, continues listening immediately
            std::thread(&Server::process_request, this, packet, client_addr).detach();
        }
        // Invalid packets are silently discarded (no response sent)
    }
}

// ===== Request routing =====

void Server::process_request(const Packet& packet, const SocketAddress& client_addr) {
    // Dispatch to appropriate handler based on packet type
    switch (packet.type) {
        case CLIENT_DISCOVERY:
            std::cout << "\nReceived CLIENT_DISCOVERY from " << client_addr.ip_string() << std::endl;
            handle_client_discovery(client_addr);
            break;
        case TRANSACTION_REQUEST:
            std::cout << "\nReceived TRANSACTION_REQUEST from " << client_addr.ip_string() << std::endl;
            handle_transaction(packet, client_addr);
            break;
        case SERVER_DISCOVERY:
            std::cout << "\nReceived SERVER_DISCOVERY from " << client_addr.ip_string() << std::endl;
            handle_server_discovery(client_addr);
            break;
        // Other packet types (ACKs) are ignored (server doesn't expect ACKs from clients)
    }
}

// ===== Discovery handler =====

void Server::handle_client_discovery(const SocketAddress& client_addr) {    
    // Attempt to register new client (insert returns false if already exists)
    if (clients.insert(client_addr.ip(), ClientInfo())) {
        // New client registered: update global balance to reflect new account
        // Lock required because s_total_balance is shared across all worker threads
        std::lock_guard<std::mutex> stats_lock(s_stats_mutex);
        s_total_balance += CLIENT_INITIAL_BALANCE;

        // Send ACK with default initial values (balance = 100, last_request_id = 0)
        ClientInfo default_info;
        Packet reply_packet = Packet::create_reply(CLIENT_DISCOVERY_ACK, default_info.last_processed_request_id, default_info.balance);
        server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
        return;
    }
    
    // Client already exists: read current state (uses LockedMap read lock)
    // Unwrap optional (guaranteed to exist since insert() returned false)
    ClientInfo client_info = *clients.read(client_addr.ip());

    // Send ACK with current client state (idempotent: repeated discoveries get same response)
    Packet reply_packet = Packet::create_reply(CLIENT_DISCOVERY_ACK, client_info.last_processed_request_id, client_info.balance);
    server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
}

// ===== Transaction handler =====

void Server::handle_transaction(const Packet& packet, const SocketAddress& client_addr) {
    // Extract IPs in host byte order (packet stores network byte order)
    uint32_t src_client_ip = client_addr.ip();
    uint32_t dest_client_ip = packet.payload.request.destination_ip;

    // ===== Validation Step 1: Source client must exist =====
    ClientInfo src_client;
    auto src_opt = clients.read(src_client_ip);
    if (!src_opt) {
        // Source not registered: should never happen if client followed discovery protocol
        Packet reply_packet = Packet::create_reply(ERROR_ACK, packet.request_id, 0);
        server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
        return;
    }
    src_client = *src_opt;

    // ===== Validation Step 2: Check for duplicate request (idempotency) =====
    // If request_id <= last_processed, this is a retransmission of a request we already handled
    if (packet.request_id <= src_client.last_processed_request_id) {
        // Send cached response (same ACK as original, prevents double-spending)
        PrintUtils::print_request(src_client_ip, packet, true, s_num_transactions, s_total_transferred, s_total_balance);
        Packet reply_packet = Packet::create_reply(TRANSACTION_ACK, src_client.last_processed_request_id, src_client.balance);
        server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
        return;
    }

    // ===== Update last_processed_request_id BEFORE validation =====
    // This prevents race condition where same request_id could be processed twice
    // Example: Two threads process same packet simultaneously, both pass duplicate check
    // By updating here, second thread will see request as duplicate when it reads
    src_client.last_processed_request_id = packet.request_id;
    if (!clients.write(src_client_ip, src_client)) {
        // Write failed (shouldn't happen unless client was deleted)
        return;
    }

    // ===== Edge Case: Zero-value transaction (no-op) =====
    if (packet.payload.request.value == 0) {
        // Valid request, but no balance change needed
        Packet reply_packet = Packet::create_reply(TRANSACTION_ACK, packet.request_id, src_client.balance);
        server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
        return;
    }

    // ===== Validation Step 3: Destination client must exist =====
    ClientInfo dest_client;
    auto dest_opt = clients.read(dest_client_ip);
    if (!dest_opt) {
        // Destination not registered: client tried to send to non-existent account
        Packet reply_packet = Packet::create_reply(INVALID_CLIENT_ACK, src_client.last_processed_request_id, src_client.balance);
        server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
        return;
    } else {
        dest_client = *dest_opt;
    }

    // ===== Edge Case: Self-transfer (no-op) =====
    if (src_client_ip == dest_client_ip) {
        // Sending money to yourself: valid but no balance change
        Packet reply_packet = Packet::create_reply(TRANSACTION_ACK, src_client.last_processed_request_id, src_client.balance);
        server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
        return;
    }

    // ===== Validation Step 4: Sufficient balance check =====
    if (src_client.balance < packet.payload.request.value) {
        // Insufficient funds: transaction rejected
        Packet reply_packet = Packet::create_reply(INSUFFICIENT_BALANCE_ACK, src_client.last_processed_request_id, src_client.balance);
        server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
        return;
    }

    // ===== Execute atomic transfer between accounts =====
    // atomic_pair_operation acquires write locks on BOTH accounts simultaneously
    // Prevents deadlock via fixed locking order (lower IP address locked first)
    // Lambda executes with exclusive access to both ClientInfo structs
    uint32_t client_new_balance;
    if (!clients.atomic_pair_operation(src_client_ip, dest_client_ip, [&](ClientInfo& src, ClientInfo& dest) {
        // Debit sender
        src.balance -= packet.payload.request.value;
        // Credit receiver
        dest.balance += packet.payload.request.value;
        // Capture new balance for ACK response (needed outside lambda scope)
        client_new_balance = src.balance;
    })) {
        // Operation failed (one of the clients was deleted mid-transaction, rare race condition)
        return;
    }

    // ===== Update global bank statistics =====
    // Lock required: s_num_transactions, s_total_transferred, s_total_balance are shared
    // Note: s_total_balance doesn't change (money just moved between accounts)
    {
        std::lock_guard<std::mutex> stats_lock(s_stats_mutex);
        s_num_transactions++;                           // Increment successful transaction count
        s_total_transferred += packet.payload.request.value; // Accumulate total money moved
    }

    // ===== Send success ACK with new balance =====
    Packet reply_packet = Packet::create_reply(TRANSACTION_ACK, src_client.last_processed_request_id, client_new_balance);
    server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);

    // Print transaction summary (uses updated stats from above)
    PrintUtils::print_request(src_client_ip, packet, false, s_num_transactions, s_total_transferred, s_total_balance);
}

uint32_t Server::get_server_id(const SocketAddress& server_addr) {
    for (const auto& server : backup_servers) {
        if (server.address.ip() == server_addr.ip()) {
            return server.id;
        }
    }
    
    return backup_servers.size();
}

void Server::discover_backup_servers() {

    // Broadcast SERVER_DISCOVERY packet to find current leader
    Packet discovery_packet;
    discovery_packet.type = SERVER_DISCOVERY;
    discovery_packet.request_id = 0;

    server_socket.send(&discovery_packet, sizeof(Packet), SocketAddress::broadcast(this->port));
    
    // Wait for SERVER_DISCOVERY_ACK responses with timeout
    auto start_time = std::chrono::steady_clock::now(); // Start timer
    while (std::chrono::steady_clock::now() - start_time < std::chrono::milliseconds(SERVER_TIMEOUT_DISCOVERY_ACK)) {
        Packet response_packet;
        SocketAddress received_from_addr;
        if (server_socket.receive(&response_packet, sizeof(Packet), received_from_addr) > 0) {
            if (response_packet.type == SERVER_DISCOVERY_ACK) {
                // Received response from current leader
                this->server_id = response_packet.payload.server_discovery.server_id;
                this->sequence_counter = response_packet.payload.server_discovery.sequence_counter;
                this->is_leader = false; // This server is a backup
                this->backup_servers.clear();
                // Populate backup_servers list from response
                uint8_t server_count = response_packet.payload.server_discovery.server_count;
                for (size_t i = 0; i < server_count; ++i) {
                    ServerEntry entry = response_packet.payload.server_discovery.servers[i];
                    SocketAddress server_address(entry.ip, ntohs(entry.port));
                    this->backup_servers.push_back(ServerInfo{
                        .address = server_address,
                        .is_leader = (server_address.ip() == received_from_addr.ip()),
                        .sequence_counter = response_packet.payload.server_discovery.sequence_counter,
                        .id = response_packet.payload.server_discovery.server_id
                    });
                }
                break;
            }
        }
    }
    
    if (this->is_leader) {
        // No response received: assume leader role
        // Add itself to backup_servers list
        this->backup_servers.push_back(ServerInfo{
                    .address = SocketAddress("0.0.0.0", this->port),
                    .is_leader = true,
                    .sequence_counter = this->sequence_counter,
                    .id = this->server_id
                });
    }
}

void Server::handle_server_discovery(const SocketAddress& server_addr) {
    // Respond to SERVER_DISCOVERY with SERVER_DISCOVERY_ACK
    if(!this->is_leader) {
        // Not the leader: ignore discovery requests
        return;
    }

    ServerInfo server_info;
    server_info.address = server_addr;
    server_info.is_leader = false;
    server_info.sequence_counter = this->sequence_counter;
    server_info.id = get_server_id(server_addr);

    backup_servers.push_back(server_info);

    std::vector<ServerEntry> server_entries;
    for (const auto& server : backup_servers) {
        server_entries.push_back(ServerEntry{
            .ip = server.address.ip(),
            .port = server.address.port()
        });
    }

    Packet reply_packet = Packet::create_server_discovery_reply(
        SERVER_DISCOVERY_ACK, server_info.sequence_counter, server_info.id, backup_servers.size(), server_entries.data());
    
    server_socket.send(&reply_packet, sizeof(reply_packet), server_addr);
}
