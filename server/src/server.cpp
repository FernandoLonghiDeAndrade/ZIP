#include "server.h"
#include "print_utils.h"
#include <iostream>
#include <optional>
#include <thread>
#include <cstring>

// ===== Constructor =====

Server::Server(uint16_t port) : port(port) {
    // Bind socket to port (throws if port already in use or permission denied)
    if (!server_socket.initialize(port, true)) {
        throw std::runtime_error("Failed to initialize UDP socket");
    }

    // Initialize statistics
    num_transactions = 0;
    total_transferred = 0;
    total_balance = 0;
}

// ===== Main execution =====

void Server::run() {
    // Print initial state (empty bank at startup)
    PrintUtils::print_server_state(num_transactions, total_transferred, total_balance);
    
    // Discover leader server in cluster
    discover_leader_server();

    // Enter infinite listening loop (never returns)
    run_listening_loop();
}

void Server::discover_leader_server() {
    ServerPacket discovery_packet(SERVER_DISCOVERY);

    for (int attempt = 0; attempt < 3; attempt++) {
        std::cout << "Broadcasting SERVER_DISCOVERY (attempt " << (attempt + 1) << "/3)..." << std::endl;
        server_socket.send(&discovery_packet, discovery_packet.size(), SocketAddress::broadcast(port));

        // Wait for response with timeout
        SocketAddress sender_addr;
        uint8_t packet_buffer[sizeof(ServerPacket)];
        auto start_time = std::chrono::steady_clock::now();

        while (true) {
            int32_t bytes_received = server_socket.receive(packet_buffer, sizeof(packet_buffer), sender_addr);
            if (bytes_received > 0) {
                // Received response: check if it's SERVER_DISCOVERY_ACK
                ServerPacket* response_packet = (ServerPacket*)packet_buffer;
                if (response_packet->type == SERVER_DISCOVERY_ACK) {
                    std::cout << "Discovered leader server at " << sender_addr.ip_string() << std::endl;
                    handle_server_discovery_ack();
                    return; // Leader found
                }
            }

            // Check for timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= TIMEOUT_MS) {
                break; // Timeout reached, retry discovery
            }
        }
    }

    // If no leader discovered after 3 attempts, elect self as leader
    std::cout << "No leader discovered after 3 attempts. Electing self as leader." << std::endl;
    server_id = 0;
    leader_id = 0;
}

void Server::run_listening_loop() {
    SocketAddress address;
    uint8_t packet_buffer[sizeof(ServerPacket)];
    
    while (true) {
        // Blocking receive: waits indefinitely for next packet
        int32_t bytes_received = server_socket.receive(packet_buffer, sizeof(packet_buffer), address);
        
        // Validate packet size (prevents processing truncated/malformed packets)
        // Process valid packets in separate detached threads for concurrency
        if (bytes_received > 0) {
            // Determine if packet is from client or server based on first byte (packet type)
            if (packet_buffer[0] >= SERVER_DISCOVERY) {
                // Server packet
                std::thread(&Server::process_server_packet, this, (ServerPacket*)packet_buffer, address).detach();
            } else {
                // Client packet
                std::thread(&Server::process_client_packet, this, (ClientPacket*)packet_buffer, address).detach();
            }
        }
    }
}

// ===== Client Packet Handlers =====

void Server::process_client_packet(const ClientPacket& packet, const SocketAddress& client_addr) {
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
        // Other packet types (ACKs) are ignored (server doesn't expect ACKs from clients)
    }
}

void Server::handle_client_discovery(const SocketAddress& client_addr) {    
    // Attempt to register new client (insert returns false if already exists)
    if (clients.insert(client_addr.ip(), ClientInfo())) {
        // New client registered: update global balance to reflect new account
        // Lock required because total_balance is shared across all worker threads
        std::lock_guard<std::mutex> stats_lock(stats_mutex);
        total_balance += CLIENT_INITIAL_BALANCE;

        // Send ACK with default initial values (balance = 100, last_request_id = 0)
        ClientInfo default_info;
        ClientPacket reply_packet = ClientPacket::create_reply(CLIENT_DISCOVERY_ACK, default_info.last_processed_request_id, default_info.balance);
        server_socket.send(&reply_packet, reply_packet.size(), client_addr);
        return;
    }
    
    // Client already exists: read current state (uses LockedMap read lock)
    // Unwrap optional (guaranteed to exist since insert() returned false)
    ClientInfo client_info = *clients.read(client_addr.ip());

    // Send ACK with current client state (idempotent: repeated discoveries get same response)
    ClientPacket reply_packet = ClientPacket::create_reply(CLIENT_DISCOVERY_ACK, client_info.last_processed_request_id, client_info.balance);
    server_socket.send(&reply_packet, reply_packet.size(), client_addr);
}

void Server::handle_transaction(const ClientPacket& packet, const SocketAddress& client_addr) {
    // Extract IPs in host byte order (packet stores network byte order)
    uint32_t src_client_ip = client_addr.ip();
    uint32_t dest_client_ip = packet.payload.request.destination_ip;

    // ===== Validation Step 1: Source client must exist =====
    ClientInfo src_client;
    auto src_opt = clients.read(src_client_ip);
    if (!src_opt) {
        // Source not registered: should never happen if client followed discovery protocol
        ClientPacket reply_packet = ClientPacket::create_reply(ERROR_ACK, packet.payload.request.id, 0);
        server_socket.send(&reply_packet, reply_packet.size(), client_addr);
        return;
    }
    src_client = *src_opt;

    // ===== Validation Step 2: Check for duplicate request (idempotency) =====
    // If request_id <= last_processed, this is a retransmission of a request we already handled
    if (packet.payload.request.id <= src_client.last_processed_request_id) {
        // Send cached response (same ACK as original, prevents double-spending)
        PrintUtils::print_request(src_client_ip, packet, true, num_transactions, total_transferred, total_balance);
        ClientPacket reply_packet = ClientPacket::create_reply(TRANSACTION_ACK, src_client.last_processed_request_id, src_client.balance);
        server_socket.send(&reply_packet, reply_packet.size(), client_addr);
        return;
    }

    // ===== Update last_processed_request_id BEFORE validation =====
    // This prevents race condition where same request_id could be processed twice
    // Example: Two threads process same packet simultaneously, both pass duplicate check
    // By updating here, second thread will see request as duplicate when it reads
    src_client.last_processed_request_id = packet.payload.request.id;
    if (!clients.write(src_client_ip, src_client)) {
        // Write failed (shouldn't happen unless client was deleted)
        return;
    }

    // ===== Edge Case: Zero-value transaction (no-op) =====
    if (packet.payload.request.value == 0) {
        // Valid request, but no balance change needed
        ClientPacket reply_packet = ClientPacket::create_reply(TRANSACTION_ACK, packet.payload.request.id, src_client.balance);
        server_socket.send(&reply_packet, reply_packet.size(), client_addr);
        return;
    }

    // ===== Validation Step 3: Destination client must exist =====
    ClientInfo dest_client;
    auto dest_opt = clients.read(dest_client_ip);
    if (!dest_opt) {
        // Destination not registered: client tried to send to non-existent account
        ClientPacket reply_packet = ClientPacket::create_reply(INVALID_CLIENT_ACK, src_client.last_processed_request_id, src_client.balance);
        server_socket.send(&reply_packet, reply_packet.size(), client_addr);
        return;
    } else {
        dest_client = *dest_opt;
    }

    // ===== Edge Case: Self-transfer (no-op) =====
    if (src_client_ip == dest_client_ip) {
        // Sending money to yourself: valid but no balance change
        ClientPacket reply_packet = ClientPacket::create_reply(TRANSACTION_ACK, src_client.last_processed_request_id, src_client.balance);
        server_socket.send(&reply_packet, reply_packet.size(), client_addr);
        return;
    }

    // ===== Validation Step 4: Sufficient balance check =====
    if (src_client.balance < packet.payload.request.value) {
        // Insufficient funds: transaction rejected
        ClientPacket reply_packet = ClientPacket::create_reply(INSUFFICIENT_BALANCE_ACK, src_client.last_processed_request_id, src_client.balance);
        server_socket.send(&reply_packet, reply_packet.size(), client_addr);
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
    // Lock required: num_transactions, total_transferred, total_balance are shared
    // Note: total_balance doesn't change (money just moved between accounts)
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex);
        num_transactions++;                           // Increment successful transaction count
        total_transferred += packet.payload.request.value; // Accumulate total money moved
    }

    // ===== Send success ACK with new balance =====
    ClientPacket reply_packet = ClientPacket::create_reply(TRANSACTION_ACK, src_client.last_processed_request_id, client_new_balance);
    server_socket.send(&reply_packet, reply_packet.size(), client_addr);

    // Print transaction summary (uses updated stats from above)
    PrintUtils::print_request(src_client_ip, packet, false, num_transactions, total_transferred, total_balance);
}

// ===== Server Packet Handlers =====

void Server::process_server_packet(const ServerPacket& packet, const SocketAddress& server_addr) {
    // Dispatch to appropriate handler based on packet type
    switch (packet.type) {
        case SERVER_DISCOVERY:
            std::cout << "\nReceived SERVER_DISCOVERY from " << server_addr.ip_string() << std::endl;
            handle_server_discovery(server_addr);
            break;
        case STATE_SYNC_REQUEST:
            std::cout << "\nReceived STATE_SYNC_REQUEST from " << server_addr.ip_string() << std::endl;
            handle_state_sync_request(server_addr);
            break;
        case NEW_SERVER_SYNC:
            std::cout << "\nReceived NEW_SERVER_SYNC from " << server_addr.ip_string() << std::endl;
            handle_new_server_sync(server_addr);
            break;
        case NEW_TRANSACTION_SYNC:
            std::cout << "\nReceived NEW_TRANSACTION_SYNC from " << server_addr.ip_string() << std::endl;
            handle_new_transaction_sync(server_addr);
            break;
        case ELECTION_REQUEST:
            std::cout << "\nReceived ELECTION_REQUEST from " << server_addr.ip_string() << std::endl;
            handle_election_request(server_addr);
            break;
        case COORDINATOR:
            std::cout << "\nReceived COORDINATOR from " << server_addr.ip_string() << std::endl;
            handle_coordinator(server_addr);
            break;
    }
}

void Server::handle_server_discovery(const SocketAddress& server_addr) {
    
}

void Server::handle_server_discovery_ack() {
    // Wait for response with timeout
    SocketAddress sender_addr;
    uint8_t packet_buffer[sizeof(ServerPacket)];
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        int32_t bytes_received = server_socket.receive(packet_buffer, sizeof(packet_buffer), sender_addr);
        if (bytes_received > 0) {
            // Received response: check if it's SERVER_DISCOVERY_ACK
            ServerPacket* response_packet = (ServerPacket*)packet_buffer;
            if (response_packet->type == SERVER_DISCOVERY_ACK) {
                std::cout << "Discovered leader server at " << sender_addr.ip_string() << std::endl;
                handle_server_discovery_ack();
                return; // Leader found
            }
        }

        // Check for timeout
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= TIMEOUT_MS) {
            // Timeout reached, retry discovery
            std::cout << "Timeout reached while waiting for SERVER_DISCOVERY_ACK. Retrying discovery..." << std::endl;
            discover_leader_server();
            return;
        }
    }
}

void Server::handle_state_sync_request(const SocketAddress& server_addr) {}

void Server::handle_new_server_sync(const SocketAddress& server_addr) {}

void Server::handle_new_transaction_sync(const SocketAddress& server_addr) {}

// ===== Leader Election =====

void Server::request_election() {}

void Server::handle_election_request(const SocketAddress& server_addr) {}

void Server::handle_coordinator(const SocketAddress& server_addr) {}
