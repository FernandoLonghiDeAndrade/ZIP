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
}

// ===== Main execution =====

void Server::run() {
    // Print initial state (empty bank at startup)
    PrintUtils::print_server_state(s_num_transactions, s_total_transferred, s_total_balance);
    
    // Enter infinite listening loop (never returns)
    run_listening_loop();
}

void Server::run_listening_loop() {
    struct sockaddr_in client_addr;
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

void Server::process_request(const Packet& packet, struct sockaddr_in client_addr) {
    // Dispatch to appropriate handler based on packet type
    switch (packet.type) {
        case DISCOVERY:
            std::cout << "\nReceived DISCOVERY from " << inet_ntoa(client_addr.sin_addr) << std::endl;
            handle_discovery(client_addr);
            break;
        case TRANSACTION_REQUEST:
            std::cout << "\nReceived TRANSACTION_REQUEST from " << inet_ntoa(client_addr.sin_addr) << std::endl;
            handle_transaction(packet, client_addr);
            break;
        // Other packet types (ACKs) are ignored (server doesn't expect ACKs from clients)
    }
}

// ===== Discovery handler =====

void Server::handle_discovery(const struct sockaddr_in& client_addr) {
    // Extract client IP in host byte order (for use as map key)
    uint32_t client_ip = ntohl(client_addr.sin_addr.s_addr);
    
    // Attempt to register new client (insert returns false if already exists)
    if (clients.insert(client_ip, ClientInfo())) {
        // New client registered: update global balance to reflect new account
        // Lock required because s_total_balance is shared across all worker threads
        std::lock_guard<std::mutex> stats_lock(s_stats_mutex);
        s_total_balance += CLIENT_INITIAL_BALANCE;

        // Send ACK with default initial values (balance = 100, last_request_id = 0)
        ClientInfo default_info;
        Packet reply_packet = Packet::create_reply(DISCOVERY_ACK, default_info.last_processed_request_id, default_info.balance);
        server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
        return;
    }
    
    // Client already exists: read current state (uses LockedMap read lock)
    // Unwrap optional (guaranteed to exist since insert() returned false)
    ClientInfo client_info = *clients.read(client_ip);

    // Send ACK with current client state (idempotent: repeated discoveries get same response)
    Packet reply_packet = Packet::create_reply(DISCOVERY_ACK, client_info.last_processed_request_id, client_info.balance);
    server_socket.send(&reply_packet, sizeof(reply_packet), client_addr);
}

// ===== Transaction handler =====

void Server::handle_transaction(const Packet& packet, const struct sockaddr_in& client_addr) {
    // Extract IPs in host byte order (packet stores network byte order)
    uint32_t src_client_ip = ntohl(client_addr.sin_addr.s_addr);
    uint32_t dest_client_ip = ntohl(packet.payload.request.destination_ip);

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
