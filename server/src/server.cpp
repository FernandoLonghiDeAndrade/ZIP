#include "server.h"
#include "print_utils.h"
#include <iostream>
#include <optional>
#include <thread>
#include <cstring>
#include <atomic>

// ===== Constructor =====

Server::Server(uint16_t port) : port(port) {
    // Bind address to port (throws if port already in use or permission denied)
    if (!server_socket.initialize(port, true)) {
        throw std::runtime_error("Failed to initialize UDP address");
    }

    // Initialize statistics
    num_transactions = 0;
    total_transferred = 0;
    total_balance = 0;

    // Initialize server state
    seq_number = 0;
}

// ===== Main execution =====

void Server::run() {
    // Print initial state (empty bank at startup)
    PrintUtils::print_server_state(num_transactions, total_transferred, total_balance);
    
    // Discover leader server in cluster
    discover_leader_server();

    // Start ping thread
    std::thread([this]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(TIMEOUT_MS));
            
            // Check if this server is leader
            if (this->server_socket.ip() != this->leader_addr.ip()) {
                // Not leader: ping leader
                ping_leader();
            }
        }
    }).detach();

    // Enter infinite listening loop (never returns)
    run_listening_loop();
}

void Server::discover_leader_server() {
    ServerPacket discovery_packet(STATE_SYNC_REQUEST);

    for (int attempt = 0; attempt < 3; attempt++) {
        std::cout << "Broadcasting STATE_SYNC_REQUEST (attempt " << (attempt + 1) << "/3)..." << std::endl;
        server_socket.send(&discovery_packet, discovery_packet.size(), SocketAddress::broadcast(port));

        // Wait for response with timeout
        SocketAddress leader_addr;
        ServerPacket response_packet;
        int32_t bytes_received = server_socket.receive(&response_packet, sizeof(response_packet), leader_addr, TIMEOUT_MS);
        
        if (bytes_received > 0) {
            // Received response: check if it's STATE_SYNC_ACK
            if (response_packet.type == STATE_SYNC_ACK) {
                std::cout << "Discovered leader server at " << leader_addr.ip_string() << std::endl;
                // Set leader address and receive state
                this->leader_addr = leader_addr;
                receive_leader_state(true);

                // Check if the server has a lower ID than the leader to start an election
                size_t server_id, leader_id;
                for (size_t i = 0; i < servers.size(); i++) {
                    if (servers[i].ip() == this->server_socket.ip()) {
                        server_id = i;
                    } else if (servers[i].ip() == leader_addr.ip()) {
                        leader_id = i;
                    }
                }
                if (server_id < leader_id) {
                    start_election();
                }

                return;
            }
        }
        // If no response, loop continues and retransmits
    }

    // If no leader discovered after 3 attempts, elect self as leader
    std::cout << "No leader discovered after 3 attempts. Electing self as leader." << std::endl;
    leader_addr = this->server_socket.address();
    servers.push_back(leader_addr);
}

void Server::run_listening_loop() {
    SocketAddress address;
    uint8_t packet_buffer[sizeof(ServerPacket)];
    
    while (true) {
        // Non-blocking receive
        int32_t bytes_received = server_socket.receive(packet_buffer, sizeof(packet_buffer), address);

        // Validate packet size (prevents processing truncated/malformed packets)
        // Process valid packets in separate detached threads for concurrency
        if (bytes_received > 0) {
            // Determine if packet is from client or server based on first byte (packet type)
            if (packet_buffer[0] >= STATE_SYNC_REQUEST) {
                // Server packet
                process_server_packet(*(ServerPacket*)packet_buffer, address);
            } else {
                // Client packet
                std::thread(&Server::process_client_packet, this, *(ClientPacket*)packet_buffer, address).detach();
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
    if (clients.insert(client_addr.ip(), ClientInfo(client_addr.port()))) {
        // New client registered: update global balance to reflect new account
        // Lock required because total_balance is shared across all worker threads
        std::lock_guard<std::mutex> stats_lock(stats_mutex);
        total_balance += CLIENT_INITIAL_BALANCE;

        // Send ACK with default initial values
        ClientInfo default_info(client_addr.port());
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
        ClientPacket reply_packet(ERROR_ACK);
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
        ClientPacket reply_packet(INVALID_CLIENT_ACK);
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
        ClientPacket reply_packet(INSUFFICIENT_BALANCE_ACK);
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
        case STATE_SYNC_REQUEST:
            std::cout << "\nReceived STATE_SYNC_REQUEST from " << server_addr.ip_string() << std::endl;
            handle_state_sync_request(server_addr);
            break;
        case NEW_SERVER_SYNC:
            std::cout << "\nReceived NEW_SERVER_SYNC from " << server_addr.ip_string() << std::endl;
            handle_new_server_sync(packet);
            break;
        case NEW_TRANSACTION_SYNC:
            std::cout << "\nReceived NEW_TRANSACTION_SYNC from " << server_addr.ip_string() << std::endl;
            handle_new_transaction_sync(packet);
            break;
        case PING_LEADER:
            handle_ping_leader(server_addr);
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

void Server::request_leader_state() {
    ServerPacket request_packet(STATE_SYNC_REQUEST);

    this->server_socket.send(&request_packet, request_packet.size(), this->leader_addr);

    // Wait for response with timeout
    SocketAddress leader_addr;
    ServerPacket response_packet;
    int32_t bytes_received = this->server_socket.receive(&response_packet, sizeof(response_packet), this->leader_addr, TIMEOUT_MS);
    
    if (bytes_received > 0) {
        // Received response: check if it's STATE_SYNC_ACK
        if (response_packet.type == STATE_SYNC_ACK) {
            // Set leader address and receive state
            this->leader_addr = leader_addr;
            receive_leader_state(false);
            return;
        }
    }

    // If no response after timeout, start election
    start_election();
}

void Server::receive_leader_state(bool is_from_discovery) {
    SocketAddress leader_addr;
    ServerPacket response_packet;
    uint32_t sync_seq = 0;

    // Buffers for atomic update
    std::vector<SocketAddress> buffer_servers;
    LockedMap<uint32_t, ClientInfo> buffer_clients;
    uint32_t buffer_seq_number;
    uint32_t buffer_num_transactions;
    uint64_t buffer_total_transferred;
    uint64_t buffer_total_balance;

    // Receive Server infos
    while (true) {
        uint32_t bytes_received;
        do {
            bytes_received = this->server_socket.receive(&response_packet, sizeof(response_packet), this->leader_addr, TIMEOUT_MS);
        } while(bytes_received > 0 and (response_packet.type < SERVER_INFO or SEQ_AND_STATS < response_packet.type));
        
        if (bytes_received > 0 and response_packet.payload.server.seq_number == sync_seq++) {
            if (response_packet.type == SERVER_INFO) {
                // Store server address in buffer
                SocketAddress server_addr = response_packet.payload.server.addr;
                buffer_servers.push_back(server_addr);
            } else {
                // Finished receiving server infos
                break;
            }
        } else {
            // Timeout reached or packet lost, retry state sync
            is_from_discovery ? discover_leader_server() : request_leader_state();
            return;
        }
    }

    // Receive Client infos
    if (response_packet.type == CLIENT_INFO) {
        uint32_t client_ip = response_packet.payload.client.ip;
        ClientInfo client_info = response_packet.payload.client.info;
        buffer_clients.insert(client_ip, client_info);
        
        while (true) {
            uint32_t bytes_received;
            do {
                bytes_received = this->server_socket.receive(&response_packet, sizeof(response_packet), this->leader_addr, TIMEOUT_MS);
            } while(bytes_received > 0 and (response_packet.type < CLIENT_INFO or SEQ_AND_STATS < response_packet.type));
            
            if (bytes_received > 0 and response_packet.payload.server.seq_number == sync_seq++) {
                if (response_packet.type == CLIENT_INFO) {
                    // Store client info in buffer
                    uint32_t client_ip = response_packet.payload.client.ip;
                    ClientInfo client_info = response_packet.payload.client.info;
                    buffer_clients.insert(client_ip, client_info);
                } else {
                    // Finished receiving client infos
                    break;
                }
            } else {
                // Timeout reached or packet lost, retry state sync
                is_from_discovery ? discover_leader_server() : request_leader_state();
                return;
            }
        }
    }

    // Receive sequence number and stats
    buffer_seq_number = response_packet.payload.state.seq_number;
    buffer_num_transactions = response_packet.payload.state.num_transactions;
    buffer_total_transferred = response_packet.payload.state.total_transferred;
    buffer_total_balance = response_packet.payload.state.total_balance;

    // Update server state atomically
    {
        // Update servers list
        this->servers = buffer_servers;

        // Update clients map
        this->clients = buffer_clients;

        // Update sequence number and statistics
        this->seq_number = buffer_seq_number;
        this->num_transactions = buffer_num_transactions;
        this->total_transferred = buffer_total_transferred;
        this->total_balance = buffer_total_balance;
    }
}

void Server::handle_state_sync_request(const SocketAddress& server_addr) {
    if (this->server_socket.ip() != this->leader_addr.ip()) {
        // Not the leader: ignore state sync requests
        return;
    }

    // Shared flag to cancel send_thread if STATE_SYNC_REQUEST received again
    std::atomic<bool> cancel_sync{false};

    // Leader: spawn thread to send state sync to server
    std::thread send_thread = std::thread(&Server::send_state_sync, this, server_addr, std::ref(cancel_sync));

    // While send thread is running, continue listening for other packets
    while (send_thread.joinable()) {
        ServerPacket response_packet;
        SocketAddress addr;
        uint32_t bytes_received = this->server_socket.receive(&response_packet, sizeof(ServerPacket), addr);
        
        if (bytes_received > 0) {
            if (response_packet.type == STATE_SYNC_REQUEST and addr.ip() == server_addr.ip()) {
                cancel_sync.store(true);  // Tells thread to stop
                send_thread.join();       // Waits for thread to finish
                handle_state_sync_request(server_addr); // Starts sync again
                return;
            }
        }
    }
    // Thread finished and sent everything to the server
}

void Server::send_state_sync(const SocketAddress& server_addr, std::atomic<bool>& cancel) {
    if (cancel.load()) return; // Check if it should stop
    
    // Send STATE_SYNC_ACK response
    ServerPacket reply_packet(STATE_SYNC_ACK);
    this->server_socket.send(&reply_packet, reply_packet.size(), server_addr);

    if (cancel.load()) return; // Check if it should stop

    // Check if other server is new
    bool new_server = true;
    for (auto server : servers) {
        if (server.ip() == server_addr.ip()) {
            new_server = false;
        }
    }
    if (new_server) {
        // Server is new, add to the list and send new server sync to the other servers
        this->servers.push_back(server_addr);
        ServerPacket new_server_sync_packet = ServerPacket::create_new_server_sync(this->seq_number++, server_addr);
        for (auto server : servers) {
            this->server_socket.send(&new_server_sync_packet, new_server_sync_packet.size(), server);
        }
    }

    if (cancel.load()) return; // Check if it should stop

    uint32_t sync_seq = 0;

    // Send each server's info to the other server
    for (auto server : servers) {
        ServerPacket server_info_packet = ServerPacket::create_server_info(sync_seq++, server);
        this->server_socket.send(&server_info_packet, server_info_packet.size(), server_addr);
        if (cancel.load()) return; // Check if it should stop
    }
    
    // Send each client's info to the otherserver
    for (auto client : clients) {
        ServerPacket client_info_packet = ServerPacket::create_client_info(sync_seq++, client.first, client.second.get()->value);
        this->server_socket.send(&client_info_packet, client_info_packet.size(), server_addr);
        if (cancel.load()) return; // Check if it should stop
    }

    // Send sequence number and stats to the other server
    ServerPacket seq_and_stats_packet = ServerPacket::create_seq_and_stats(sync_seq, seq_number, num_transactions, total_transferred, total_balance);
    this->server_socket.send(&seq_and_stats_packet, seq_and_stats_packet.size(), server_addr);
}

void Server::handle_new_server_sync(const ServerPacket& packet) {
    uint32_t seq_number = packet.payload.server.seq_number;
    
    if (seq_number != ++this->seq_number) {
        // Lost packets: request full state sync from leader
        request_leader_state();
        return;
    }

    SocketAddress new_server_addr = packet.payload.server.addr;

    this->servers.push_back(new_server_addr);
}

void Server::handle_new_client_sync(const ServerPacket& packet) {
    uint32_t seq_number = packet.payload.client.seq_number;
    
    if (seq_number != ++this->seq_number) {
        // Lost packets: request full state sync from leader
        request_leader_state();
        return;
    }

    uint32_t client_ip = packet.payload.client.ip;
    ClientInfo client_info = packet.payload.client.info;

    this->clients.insert(client_ip, client_info);
    this->total_balance += CLIENT_INITIAL_BALANCE;
}

void Server::handle_new_transaction_sync(const ServerPacket& packet) {
    uint32_t seq_number = packet.payload.new_transaction.seq_number;
    
    if (seq_number != ++this->seq_number) {
        // Lost packets: request full state sync from leader
        request_leader_state();
        return;
    }

    uint32_t src_ip = packet.payload.new_transaction.src_ip;
    uint32_t dest_ip = packet.payload.new_transaction.dest_ip;
    uint32_t value = packet.payload.new_transaction.value;

    // Execute transfer
    clients.atomic_pair_operation(src_ip, dest_ip, [&](ClientInfo& src, ClientInfo& dest) {
        // Debit sender
        src.balance -= value;
        // Credit receiver
        dest.balance += value;
    });

    this->num_transactions++;
    this->total_transferred += value;
}

// ===== Leader Election =====

void Server::ping_leader() {
    ServerPacket ping_packet(PING_LEADER);
    this->server_socket.send(&ping_packet, ping_packet.size(), this->leader_addr);

    uint32_t bytes_received = this->server_socket.receive(&ping_packet, ping_packet.size(), this->leader_addr, TIMEOUT_MS);
    
    if (bytes_received == 0) {
        // No response: start election
        start_election();
    }
}

void Server::handle_ping_leader(const SocketAddress& server_addr) {
    ServerPacket ping_ack_packet(PING_LEADER_ACK);
    this->server_socket.send(&ping_ack_packet, ping_ack_packet.size(), server_addr);
}

void Server::start_election() {
    // Send ELECTION_REQUEST to all servers with lower IDs (older servers)
    for (auto server : servers) {
        if (server.ip() == this->server_socket.ip()) {
            // Reached self: stop sending requests
            break;
        } else {
            ServerPacket election_request_packet(ELECTION_REQUEST);
            this->server_socket.send(&election_request_packet, election_request_packet.size(), server);
        }
    }

    // Wait for ELECTION_REQUEST_ACK responses with timeout
    SocketAddress addr;
    ServerPacket response_packet;
    int32_t bytes_received;

    do {
        bytes_received = this->server_socket.receive(&response_packet, sizeof(response_packet), addr, TIMEOUT_MS);
        if (bytes_received > 0 and response_packet.type == ELECTION_REQUEST) {
            // Send ACK back to requester
            ServerPacket ack_packet(ELECTION_REQUEST_ACK);
            this->server_socket.send(&ack_packet, ack_packet.size(), addr);
        }
    } while (bytes_received > 0 and response_packet.type != ELECTION_REQUEST_ACK);

    if (bytes_received > 0) {
        // Received ACK: another server will take over as leader

        // Wait for COORDINATOR message
        do {
            bytes_received = this->server_socket.receive(&response_packet, sizeof(response_packet), addr, TIMEOUT_MS);
            if (bytes_received > 0 and response_packet.type == ELECTION_REQUEST) {
                // Send ACK back to requester
                ServerPacket ack_packet(ELECTION_REQUEST_ACK);
                this->server_socket.send(&ack_packet, ack_packet.size(), addr);
            }
        } while (bytes_received > 0 and response_packet.type != COORDINATOR);
        
        if (bytes_received > 0) {
            // Received COORDINATOR: Update leader address
            this->leader_addr = addr;
        } else {
            // No COORDINATOR received: start election again
            start_election();
        }
        return;
    }

    // No ACKs received: elect self as leader
    this->leader_addr = this->server_socket.address();
    
    // Send coordinator message to all other servers
    ServerPacket coordinator_packet(COORDINATOR);
    for (auto server : servers) {
        if (server.ip() != this->server_socket.ip()) {
            this->server_socket.send(&coordinator_packet, coordinator_packet.size(), server);
        }
    }

    // Send coordinator message to all clients
    ClientPacket new_leader_packet = ClientPacket::create_new_leader(this->leader_addr);
    for (auto client : clients) {
        uint32_t ip = client.first;
        uint16_t port = client.second.get()->value.port;
        this->server_socket.send(&new_leader_packet, new_leader_packet.size(), SocketAddress(ip, port));
    }
}

void Server::handle_election_request(const SocketAddress& server_addr) {
    // Send ELECTION_REQUEST_ACK back to requester and start election
    ServerPacket ack_packet(ELECTION_REQUEST_ACK);
    this->server_socket.send(&ack_packet, ack_packet.size(), server_addr);
    start_election();
}

void Server::handle_coordinator(const SocketAddress& server_addr) {
    // Check if my ID is smaller than the new leader's ID
    for (auto server : servers) {
        if (server.ip() == server_addr.ip()) {
            // New leader's ID is smaller
            this->leader_addr = server_addr;
            return;
        } else if (server.ip() == this->server_socket.ip()) {
            // My ID is smaller
            start_election();
            return;
        }
    }
}
